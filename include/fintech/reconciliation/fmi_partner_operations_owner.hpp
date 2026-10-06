#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <fintech/reconciliation/fmi_partner_operations.hpp>
#include <fintech/reconciliation/fmi_reconciliation_owner.hpp>

namespace fintech::reconciliation {

template <std::size_t ScopeCapacity, std::size_t PolicyCapacity,
          std::size_t EvidenceCapacity, std::size_t CaseCapacity,
          std::size_t SourceCapacity = 32U,
          std::size_t CommandCapacity = 64U,
          std::size_t AssessmentCapacity = 32U,
          std::size_t BindingCapacity = ScopeCapacity,
          std::size_t ApprovalCapacity = CommandCapacity,
          std::size_t AuditCapacity = CommandCapacity>
class BoundedFmiPartnerOperationsOwner final {
   public:
    using ReconciliationOwner = BoundedFmiReconciliationOwner<
        ScopeCapacity, PolicyCapacity, EvidenceCapacity, CaseCapacity,
        SourceCapacity, CommandCapacity, AssessmentCapacity>;

    explicit constexpr BoundedFmiPartnerOperationsOwner(
        FmiPartnerProjectionAuthority authority) noexcept
        : authority_{authority} {}

    [[nodiscard]] FmiReconciliationResult register_scope(
        const FmiScopeRegistration& registration) noexcept {
        const auto existing = find_registration(registration.scope);
        const auto before = owner_.scope_count();
        const auto response = owner_.register_scope(registration);
        if (!response.status) return response;
        if (existing == not_found) {
            if (registration_count_ == ScopeCapacity) {
                return local_failure(core::ErrorCode::CapacityExceeded,
                                     registration.scope);
            }
            registrations_[registration_count_++] = registration;
        }
        if (owner_.scope_count() != before) advance_projection();
        return response;
    }

    [[nodiscard]] FmiReconciliationResult register_policy(
        const FmiTolerancePolicy& policy) noexcept {
        return owner_.register_policy(policy);
    }

    [[nodiscard]] FmiReconciliationResult observe(
        const FmiEvidenceObservation& observation) noexcept {
        const auto evidence_before = owner_.evidence_count();
        const auto cases_before = owner_.case_count();
        const auto response = owner_.observe(observation);
        retain_case_digests(cases_before, observation.payload_digest);
        if (owner_.evidence_count() != evidence_before ||
            owner_.case_count() != cases_before) {
            advance_projection();
        }
        return response;
    }

    [[nodiscard]] FmiReconciliationResult assess(
        const FmiAssessmentRequest& request) noexcept {
        const auto cases_before = owner_.case_count();
        const auto response = owner_.assess(request);
        retain_case_digests(cases_before, request.evidence_digest);
        if (owner_.case_count() != cases_before) advance_projection();
        return response;
    }

    [[nodiscard]] core::Status register_operations_scope(
        const FmiPartnerScopeBinding& binding) noexcept {
        if (!valid(binding) ||
            find_registration(binding.reconciliation_scope) == not_found) {
            return core::Status::failure(core::ErrorCode::InvalidArgument);
        }
        const auto retained = find_binding(binding.operations,
                                           binding.reconciliation_scope);
        if (retained != not_found) {
            return same_binding(bindings_[retained], binding)
                       ? core::Status::success()
                       : core::Status::failure(core::ErrorCode::InvalidState);
        }
        if (binding_count_ == BindingCapacity) {
            return core::Status::failure(core::ErrorCode::CapacityExceeded);
        }
        bindings_[binding_count_++] = binding;
        advance_projection();
        return core::Status::success();
    }

    [[nodiscard]] core::Status synchronize_authorization(
        const FmiPartnerAuthorizationState& state) noexcept {
        if (!valid(state) || find_binding(state.operations) == not_found) {
            return core::Status::failure(core::ErrorCode::InvalidArgument);
        }
        const auto retained = find_authorization(state.operations);
        if (retained == not_found) {
            if (authorization_count_ == BindingCapacity) {
                return core::Status::failure(
                    core::ErrorCode::CapacityExceeded);
            }
            authorizations_[authorization_count_++] = state;
            advance_projection();
            return core::Status::success();
        }
        const auto& current = authorizations_[retained];
        if (state.generation < current.generation ||
            (state.generation == current.generation &&
             !same_authorization_identity(current, state)) ||
            (!current.active && state.active &&
             state.generation == current.generation)) {
            return core::Status::failure(core::ErrorCode::InvalidState);
        }
        if (same_authorization(current, state)) {
            return core::Status::success();
        }
        authorizations_[retained] = state;
        advance_projection();
        return core::Status::success();
    }

    [[nodiscard]] core::Status register_approval(
        const FmiPartnerApprovalBinding& approval) noexcept {
        if (!valid(approval) || find_binding(approval.operations) == not_found) {
            return core::Status::failure(core::ErrorCode::InvalidArgument);
        }
        const auto retained = find_approval(approval.approval);
        if (retained != not_found) {
            return same_approval(approvals_[retained], approval)
                       ? core::Status::success()
                       : core::Status::failure(core::ErrorCode::InvalidState);
        }
        if (approval_count_ == ApprovalCapacity) {
            return core::Status::failure(core::ErrorCode::CapacityExceeded);
        }
        approvals_[approval_count_++] = approval;
        return core::Status::success();
    }

    [[nodiscard]] FmiPartnerWorkflowResult submit_workflow(
        const FmiPartnerWorkflowRequest& request) noexcept {
        if (!valid(request)) return rejected(core::ErrorCode::InvalidArgument);
        const auto binding_index = find_binding(request.command.scope);
        const auto authorization_index =
            find_authorization(request.command.scope);
        const auto approval_index =
            find_approval(request.command.command.approval);
        if (binding_index == not_found ||
            authorization_index == not_found || approval_index == not_found) {
            return rejected(core::ErrorCode::Unavailable);
        }
        const auto& binding = bindings_[binding_index];
        const auto& authorization = authorizations_[authorization_index];
        const auto& approval = approvals_[approval_index];
        if (!authorized_command(request, binding, authorization, approval)) {
            return rejected(core::ErrorCode::Unavailable);
        }

        if (const auto retry = find_command_retry(request); retry != not_found) {
            auto retained = command_records_[retry].result;
            retained.disposition = FmiPartnerWorkflowDisposition::Duplicate;
            return retained;
        }
        if (command_conflict(request)) {
            return rejected(core::ErrorCode::InvalidState);
        }
        if (command_count_ == CommandCapacity || audit_count_ == AuditCapacity) {
            return rejected(core::ErrorCode::CapacityExceeded);
        }

        const FmiExceptionCaseId case_id{request.command.case_id.value()};
        const auto* exception = owner_.exception_case(case_id);
        const auto case_index =
            static_cast<std::size_t>(case_id.value() - 1U);
        if (exception == nullptr ||
            exception->scope != binding.reconciliation_scope ||
            case_index >= CaseCapacity ||
            case_evidence_digests_[case_index] == 0U ||
            request.command.evidence_digest !=
                case_evidence_digests_[case_index] ||
            exception->version !=
                request.command.command.expected_state_version) {
            return rejected(core::ErrorCode::InvalidState);
        }

        const auto evidence_before = owner_.evidence_count();
        FmiReconciliationResult reconciliation{};
        if (request.command.action ==
                domain::PartnerCaseWorkflowAction::Propose ||
            request.command.action ==
                domain::PartnerCaseWorkflowAction::Escalate) {
            reconciliation = owner_.propose_resolution({
                request.command.command.header.command,
                request.proposal,
                case_id,
                request.command.command.expected_state_version,
                request.resolution_action,
                request.command.command.context.authorization.actor,
                request.command.command.header.created_at,
                request.command.evidence_digest,
                request.command.command.payload_digest,
            });
        } else {
            if (exception->proposal != request.proposal) {
                return rejected(core::ErrorCode::InvalidState);
            }
            reconciliation = owner_.approve_resolution({
                request.command.command.header.command,
                request.proposal,
                case_id,
                request.command.command.expected_state_version,
                request.command.command.context.authorization.actor,
                request.command.command.header.created_at,
                request.command.evidence_digest,
                request.command.command.payload_digest,
            });
        }
        if (!reconciliation.status ||
            owner_.evidence_count() != evidence_before) {
            return {reconciliation.status,
                    FmiPartnerWorkflowDisposition::Rejected,
                    reconciliation, {}};
        }

        const auto audit_id = retain_audit(request, reconciliation,
                                           approval.approval_event);
        retain_case_provenance(case_id, request, reconciliation);
        FmiPartnerWorkflowResult response{
            core::Status::success(), FmiPartnerWorkflowDisposition::Accepted,
            reconciliation, audit_id};
        command_records_[command_count_++] = {request, response};
        advance_projection();
        return response;
    }

    [[nodiscard]] FmiPartnerPageResult<FmiPartnerScopePage> scope_page(
        const FmiPartnerPageRequest& request) const noexcept {
        FmiPartnerPageResult<FmiPartnerScopePage> result{};
        if (request.kind != FmiPartnerPageKind::Scope ||
            !prepare_page(request, 1U, result.page)) {
            result.status = core::Status::failure(core::ErrorCode::InvalidState);
            return result;
        }
        if (request.start_index == 0U) {
            result.page.items[0] = make_scope_view(request);
            finish_page(request, 1U, 1U, result.page);
        } else {
            finish_page(request, 0U, 1U, result.page);
        }
        result.status = core::Status::success();
        return result;
    }

    [[nodiscard]] FmiPartnerPageResult<FmiPartnerEvidencePage> evidence_page(
        const FmiPartnerPageRequest& request) const noexcept {
        const auto total = evidence_total(request.reconciliation_scope);
        FmiPartnerPageResult<FmiPartnerEvidencePage> result{};
        if (request.kind != FmiPartnerPageKind::Evidence ||
            !prepare_page(request, total, result.page)) {
            result.status = core::Status::failure(core::ErrorCode::InvalidState);
            return result;
        }
        const auto count = fill_evidence(request, result.page);
        finish_page(request, count, total, result.page);
        result.status = core::Status::success();
        return result;
    }

    [[nodiscard]] FmiPartnerPageResult<FmiPartnerSourceCompletenessPage>
    source_completeness_page(
        const FmiPartnerPageRequest& request) const noexcept {
        const auto* registration = registration_for(
            request.reconciliation_scope);
        const auto total = registration == nullptr
                               ? 0U
                               : required_role_total(*registration);
        FmiPartnerPageResult<FmiPartnerSourceCompletenessPage> result{};
        if (request.kind != FmiPartnerPageKind::SourceCompleteness ||
            !prepare_page(request, total, result.page)) {
            result.status = core::Status::failure(core::ErrorCode::InvalidState);
            return result;
        }
        const auto count = fill_source_completeness(
            request, *registration, result.page);
        finish_page(request, count, total, result.page);
        result.status = core::Status::success();
        return result;
    }

    [[nodiscard]] FmiPartnerPageResult<FmiPartnerBreakPage> break_page(
        const FmiPartnerPageRequest& request) const noexcept {
        const auto total = case_total(request.reconciliation_scope);
        FmiPartnerPageResult<FmiPartnerBreakPage> result{};
        if (request.kind != FmiPartnerPageKind::Break ||
            !prepare_page(request, total, result.page)) {
            result.status = core::Status::failure(core::ErrorCode::InvalidState);
            return result;
        }
        const auto count = fill_breaks(request, result.page);
        finish_page(request, count, total, result.page);
        result.status = core::Status::success();
        return result;
    }

    [[nodiscard]] FmiPartnerPageResult<FmiPartnerCasePage> case_page(
        const FmiPartnerPageRequest& request) const noexcept {
        const auto total = case_total(request.reconciliation_scope);
        FmiPartnerPageResult<FmiPartnerCasePage> result{};
        if (request.kind != FmiPartnerPageKind::Case ||
            !prepare_page(request, total, result.page)) {
            result.status = core::Status::failure(core::ErrorCode::InvalidState);
            return result;
        }
        const auto count = fill_cases(request, result.page);
        finish_page(request, count, total, result.page);
        result.status = core::Status::success();
        return result;
    }

    [[nodiscard]] FmiPartnerPageResult<FmiPartnerResolutionAuditPage>
    resolution_audit_page(
        const FmiPartnerPageRequest& request) const noexcept {
        const auto total = audit_total(request.operations);
        FmiPartnerPageResult<FmiPartnerResolutionAuditPage> result{};
        if (request.kind != FmiPartnerPageKind::ResolutionAudit ||
            !prepare_page(request, total, result.page)) {
            result.status = core::Status::failure(core::ErrorCode::InvalidState);
            return result;
        }
        const auto count = fill_audits(request, result.page);
        finish_page(request, count, total, result.page);
        result.status = core::Status::success();
        return result;
    }

    [[nodiscard]] constexpr std::size_t evidence_count() const noexcept {
        return owner_.evidence_count();
    }
    [[nodiscard]] constexpr std::size_t case_count() const noexcept {
        return owner_.case_count();
    }
    [[nodiscard]] constexpr std::size_t audit_count() const noexcept {
        return audit_count_;
    }
    [[nodiscard]] const FmiEvidenceObservation* evidence_at(
        std::size_t index) const noexcept {
        return owner_.evidence_at(index);
    }
    [[nodiscard]] const FmiExceptionCase* exception_case(
        FmiExceptionCaseId id) const noexcept {
        return owner_.exception_case(id);
    }

   private:
    static constexpr std::size_t not_found =
        std::numeric_limits<std::size_t>::max();

    struct CaseProvenance final {
        domain::ApprovalReferenceId approval{};
        domain::AuthorizationDecisionId authorization_decision{};
        std::uint64_t authorization_generation{};
        std::uint64_t payload_digest{};
    };

    struct CommandRecord final {
        FmiPartnerWorkflowRequest request{};
        FmiPartnerWorkflowResult result{};
    };

    [[nodiscard]] static constexpr bool same_binding(
        const FmiPartnerScopeBinding& left,
        const FmiPartnerScopeBinding& right) noexcept {
        return same_operations_scope(left.operations, right.operations) &&
               left.reconciliation_scope == right.reconciliation_scope &&
               domain::same_workspace_scope(left.workspace_scope,
                                            right.workspace_scope) &&
               left.capability_digest == right.capability_digest;
    }

    [[nodiscard]] static constexpr bool same_authorization_identity(
        const FmiPartnerAuthorizationState& left,
        const FmiPartnerAuthorizationState& right) noexcept {
        return same_operations_scope(left.operations, right.operations) &&
               left.decision == right.decision &&
               left.generation == right.generation &&
               left.capability_digest == right.capability_digest &&
               left.expires_at == right.expires_at &&
               left.decision_event == right.decision_event;
    }

    [[nodiscard]] static constexpr bool same_authorization(
        const FmiPartnerAuthorizationState& left,
        const FmiPartnerAuthorizationState& right) noexcept {
        return same_authorization_identity(left, right) &&
               left.active == right.active;
    }

    [[nodiscard]] static constexpr bool same_approval(
        const FmiPartnerApprovalBinding& left,
        const FmiPartnerApprovalBinding& right) noexcept {
        return left.approval == right.approval &&
               same_operations_scope(left.operations, right.operations) &&
               left.case_id == right.case_id &&
               left.expected_case_version == right.expected_case_version &&
               left.proposer == right.proposer &&
               left.approver == right.approver &&
               left.authorization_decision == right.authorization_decision &&
               left.authorization_generation ==
                   right.authorization_generation &&
               left.payload_digest == right.payload_digest &&
               left.evidence_digest == right.evidence_digest &&
               left.approval_event == right.approval_event &&
               left.expires_at == right.expires_at &&
               left.action == right.action && left.active == right.active;
    }

    [[nodiscard]] std::size_t find_registration(
        FmiReconciliationScopeId scope) const noexcept {
        for (std::size_t index = 0U; index < registration_count_; ++index) {
            if (registrations_[index].scope == scope) return index;
        }
        return not_found;
    }

    [[nodiscard]] const FmiScopeRegistration* registration_for(
        FmiReconciliationScopeId scope) const noexcept {
        const auto index = find_registration(scope);
        return index == not_found ? nullptr : &registrations_[index];
    }

    [[nodiscard]] std::size_t find_binding(
        const domain::PartnerOperationsScope& operations,
        FmiReconciliationScopeId scope = {}) const noexcept {
        for (std::size_t index = 0U; index < binding_count_; ++index) {
            if (same_operations_scope(bindings_[index].operations,
                                      operations) &&
                (!scope.valid() ||
                 bindings_[index].reconciliation_scope == scope)) {
                return index;
            }
        }
        return not_found;
    }

    [[nodiscard]] std::size_t find_authorization(
        const domain::PartnerOperationsScope& operations) const noexcept {
        for (std::size_t index = 0U; index < authorization_count_; ++index) {
            if (same_operations_scope(authorizations_[index].operations,
                                      operations)) {
                return index;
            }
        }
        return not_found;
    }

    [[nodiscard]] std::size_t find_approval(
        domain::ApprovalReferenceId approval) const noexcept {
        for (std::size_t index = 0U; index < approval_count_; ++index) {
            if (approvals_[index].approval == approval) return index;
        }
        return not_found;
    }

    [[nodiscard]] bool authorized_command(
        const FmiPartnerWorkflowRequest& request,
        const FmiPartnerScopeBinding& binding,
        const FmiPartnerAuthorizationState& authorization,
        const FmiPartnerApprovalBinding& approval) const noexcept {
        const auto& command = request.command.command;
        const auto& context = command.context;
        const auto actor = context.authorization.actor;
        const bool maker =
            request.command.action ==
                domain::PartnerCaseWorkflowAction::Propose ||
            request.command.action ==
                domain::PartnerCaseWorkflowAction::Escalate;
        return authorized_command_case(request, binding, authorization,
                                       approval, actor, maker);
    }

    [[nodiscard]] bool authorized_command_case(
        const FmiPartnerWorkflowRequest& request,
        const FmiPartnerScopeBinding& binding,
        const FmiPartnerAuthorizationState& authorization,
        const FmiPartnerApprovalBinding& approval,
        core::PrincipalId actor, bool maker) const noexcept {
        const auto& command = request.command.command;
        const auto& context = command.context;
        return same_operations_scope(binding.operations,
                                     request.command.scope) &&
               context.workspace == binding.operations.workspace &&
               domain::same_workspace_scope(context.scope,
                                            binding.workspace_scope) &&
               context.authorization.decision == authorization.decision &&
               context.authorization.authorization_generation ==
                   authorization.generation &&
               context.authorization.capability_digest ==
                   authorization.capability_digest &&
               authorization.capability_digest == binding.capability_digest &&
               authorization.active &&
               not_expired(command.header.created_at,
                           authorization.expires_at) &&
               approval.active &&
               same_operations_scope(approval.operations,
                                     request.command.scope) &&
               approval.case_id == request.command.case_id &&
               approval.expected_case_version ==
                   command.expected_state_version &&
               approval.authorization_decision == authorization.decision &&
               approval.authorization_generation == authorization.generation &&
               approval.payload_digest == command.payload_digest &&
               approval.evidence_digest == request.command.evidence_digest &&
               approval.action == request.command.action &&
               not_expired(command.header.created_at, approval.expires_at) &&
               (maker ? actor == approval.proposer
                      : actor == approval.approver);
    }

    [[nodiscard]] static constexpr bool not_expired(
        core::Timestamp at, core::Timestamp expires_at) noexcept {
        return core::comparable_clock_domain(at, expires_at) &&
               at.nanoseconds < expires_at.nanoseconds;
    }

    [[nodiscard]] std::size_t find_command_retry(
        const FmiPartnerWorkflowRequest& request) const noexcept {
        for (std::size_t index = 0U; index < command_count_; ++index) {
            if (same_command(command_records_[index].request, request)) {
                return index;
            }
        }
        return not_found;
    }

    [[nodiscard]] bool command_conflict(
        const FmiPartnerWorkflowRequest& request) const noexcept {
        for (std::size_t index = 0U; index < command_count_; ++index) {
            const auto& retained = command_records_[index].request.command.command;
            const auto& incoming = request.command.command;
            if (retained.header.command == incoming.header.command ||
                retained.idempotency == incoming.idempotency) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static constexpr bool same_command(
        const FmiPartnerWorkflowRequest& left,
        const FmiPartnerWorkflowRequest& right) noexcept {
        return left.command.command.header.command ==
                   right.command.command.header.command &&
               left.command.command.idempotency ==
                   right.command.command.idempotency &&
               left.command.command.payload_digest ==
                   right.command.command.payload_digest &&
               left.command.command.expected_state_version ==
                   right.command.command.expected_state_version &&
               left.command.command.approval ==
                   right.command.command.approval &&
               left.command.case_id == right.command.case_id &&
               left.command.evidence_digest == right.command.evidence_digest &&
               left.command.action == right.command.action &&
               left.proposal == right.proposal &&
               left.resolution_action == right.resolution_action;
    }

    [[nodiscard]] domain::PartnerOperationsAuditId retain_audit(
        const FmiPartnerWorkflowRequest& request,
        const FmiReconciliationResult& result,
        core::EventId approval_event) noexcept {
        const domain::PartnerOperationsAuditId id{audit_count_ + 1U};
        audits_[audit_count_++] = {
            .scope = request.command.scope,
            .audit = id,
            .case_id = request.command.case_id,
            .command = request.command.command.header.command,
            .event = approval_event,
            .actor = request.command.command.context.authorization.actor,
            .authorization_decision =
                request.command.command.context.authorization.decision,
            .approval = request.command.command.approval,
            .authorization_generation = request.command.command.context
                                                .authorization
                                                .authorization_generation,
            .case_version = result.scope_version,
            .payload_digest = request.command.command.payload_digest,
            .evidence_digest = request.command.evidence_digest,
            .recorded_at = request.command.command.header.created_at,
            .action = audit_action(request.command.action),
        };
        return id;
    }

    [[nodiscard]] static constexpr domain::PartnerAuditAction audit_action(
        domain::PartnerCaseWorkflowAction action) noexcept {
        if (action == domain::PartnerCaseWorkflowAction::Approve) {
            return domain::PartnerAuditAction::ProposalApproved;
        }
        if (action == domain::PartnerCaseWorkflowAction::Escalate) {
            return domain::PartnerAuditAction::CaseEscalated;
        }
        return domain::PartnerAuditAction::ProposalSubmitted;
    }

    void retain_case_provenance(
        FmiExceptionCaseId case_id,
        const FmiPartnerWorkflowRequest& request,
        const FmiReconciliationResult&) noexcept {
        const auto index = static_cast<std::size_t>(case_id.value() - 1U);
        if (index >= CaseCapacity) return;
        case_provenance_[index] = {
            request.command.command.approval,
            request.command.command.context.authorization.decision,
            request.command.command.context.authorization
                .authorization_generation,
            request.command.command.payload_digest,
        };
    }

    void retain_case_digests(std::size_t first,
                             std::uint64_t digest) noexcept {
        for (std::size_t index = first;
             index < owner_.case_count() && index < CaseCapacity; ++index) {
            case_evidence_digests_[index] = digest;
        }
    }

    void advance_projection() noexcept {
        if (projection_revision_ !=
            std::numeric_limits<std::uint64_t>::max()) {
            ++projection_revision_;
        }
    }

    [[nodiscard]] static constexpr std::uint64_t mix(
        std::uint64_t value) noexcept {
        value ^= value >> 30U;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27U;
        value *= 0x94d049bb133111ebULL;
        value ^= value >> 31U;
        return value == 0U ? 1U : value;
    }

    [[nodiscard]] std::uint64_t page_seed(
        const FmiPartnerPageRequest& request) const noexcept {
        return mix(projection_revision_ ^
                   request.operations.workspace.value() ^
                   (request.reconciliation_scope.value() << 1U) ^
                   (request.query.filter_digest << 2U) ^
                   (request.query.sort_digest << 3U) ^
                   (static_cast<std::uint64_t>(request.kind) << 56U));
    }

    [[nodiscard]] std::uint64_t cursor_digest(
        const FmiPartnerPageRequest& request,
        std::uint32_t index) const noexcept {
        return mix(page_seed(request) ^
                   (static_cast<std::uint64_t>(index) + 1U));
    }

    template <typename Page>
    [[nodiscard]] bool prepare_page(const FmiPartnerPageRequest& request,
                                    std::size_t total,
                                    Page& page) const noexcept {
        if (!valid(request) || !valid(authority_)) return false;
        const auto binding_index = find_binding(request.operations,
                                                request.reconciliation_scope);
        const auto authorization_index = find_authorization(request.operations);
        if (binding_index == not_found ||
            authorization_index == not_found ||
            request.start_index > total) {
            return false;
        }
        const auto& binding = bindings_[binding_index];
        const auto& authorization = authorizations_[authorization_index];
        if (!authorized_query(request, binding, authorization)) return false;

        const auto seed = page_seed(request);
        const core::SnapshotId snapshot{seed};
        const domain::ViewStreamId stream{
            authority_.stream_base.value() +
            static_cast<std::uint64_t>(request.kind)};
        if (request.query.mode == domain::WorkspaceQueryMode::Resume &&
            (request.query.requested_snapshot != snapshot ||
             request.query.resume_stream != stream ||
             request.query.resume_domain != authority_.sequence_domain ||
             request.query.resume_watermark != projection_revision_ ||
             request.query.cursor_digest !=
                 cursor_digest(request, request.start_index))) {
            return false;
        }
        const auto at = request.query.context.authorization.evaluated_at;
        page.envelope = {
            .context = request.query.context,
            .query = request.query.query,
            .entity = domain::ProjectionEntityId{
                authority_.entity_base.value() +
                static_cast<std::uint64_t>(request.kind)},
            .event = core::EventId{seed},
            .schema = authority_.schema,
            .schema_revision = authority_.schema_revision,
            .snapshot = snapshot,
            .stream = stream,
            .owner = authority_.owner,
            .sequence_domain = authority_.sequence_domain,
            .stream_sequence = projection_revision_,
            .source_watermark = projection_revision_,
            .entity_version = projection_revision_,
            .event_time = at,
            .receive_time = at,
            .as_of_time = at,
            .publish_time = at,
            .freshness = domain::FreshnessState::Current,
            .source_health = domain::SourceHealth::Healthy,
            .completeness = domain::ProjectionCompleteness::Complete,
        };
        page.cursor = {
            .snapshot = snapshot,
            .stream = stream,
            .sequence_domain = authority_.sequence_domain,
            .source_watermark = projection_revision_,
            .cursor_digest = cursor_digest(request, request.start_index),
        };
        return domain::valid(page.envelope);
    }

    [[nodiscard]] bool authorized_query(
        const FmiPartnerPageRequest& request,
        const FmiPartnerScopeBinding& binding,
        const FmiPartnerAuthorizationState& authorization) const noexcept {
        const auto& context = request.query.context;
        return request.query.requested_schema == authority_.schema &&
               request.query.requested_schema_revision ==
                   authority_.schema_revision &&
               context.workspace == binding.operations.workspace &&
               domain::same_workspace_scope(context.scope,
                                            binding.workspace_scope) &&
               context.authorization.decision == authorization.decision &&
               context.authorization.authorization_generation ==
                   authorization.generation &&
               context.authorization.capability_digest ==
                   authorization.capability_digest &&
               authorization.capability_digest == binding.capability_digest &&
               authorization.active &&
               not_expired(context.authorization.evaluated_at,
                           authorization.expires_at);
    }

    template <typename Page>
    void finish_page(const FmiPartnerPageRequest& request,
                     std::uint32_t count, std::size_t total,
                     Page& page) const noexcept {
        const auto next = static_cast<std::size_t>(request.start_index) + count;
        page.cursor.item_count = count;
        page.cursor.terminal = next >= total;
        page.cursor.next_cursor_digest =
            page.cursor.terminal
                ? 0U
                : cursor_digest(request, static_cast<std::uint32_t>(next));
    }

    [[nodiscard]] FmiPartnerScopeView make_scope_view(
        const FmiPartnerPageRequest& request) const noexcept {
        const auto* registration = registration_for(
            request.reconciliation_scope);
        const auto authorization_index = find_authorization(request.operations);
        const auto& authorization = authorizations_[authorization_index];
        FmiEvidenceRoleMask present{};
        std::uint64_t digest{1U};
        for (std::size_t index = 0U; index < owner_.evidence_count(); ++index) {
            const auto* evidence = owner_.evidence_at(index);
            if (evidence->scope != request.reconciliation_scope) continue;
            present = static_cast<FmiEvidenceRoleMask>(
                present | evidence_role_mask(evidence->role));
            digest = mix(digest ^ evidence->payload_digest);
        }
        return {
            .operations = request.operations,
            .reconciliation_scope = request.reconciliation_scope,
            .key = registration->key,
            .required_roles = registration->required_roles,
            .present_roles = present,
            .scope_version = projection_revision_,
            .evidence_digest = digest,
            .authorization_decision = authorization.decision,
            .authorization_generation = authorization.generation,
            .as_of = request.query.context.authorization.evaluated_at,
        };
    }

    [[nodiscard]] std::size_t evidence_total(
        FmiReconciliationScopeId scope) const noexcept {
        std::size_t total{};
        for (std::size_t index = 0U; index < owner_.evidence_count(); ++index) {
            if (owner_.evidence_at(index)->scope == scope) ++total;
        }
        return total;
    }

    [[nodiscard]] std::uint32_t fill_evidence(
        const FmiPartnerPageRequest& request,
        FmiPartnerEvidencePage& page) const noexcept {
        std::size_t ordinal{};
        std::uint32_t count{};
        for (std::size_t index = 0U; index < owner_.evidence_count(); ++index) {
            const auto* evidence = owner_.evidence_at(index);
            if (evidence->scope != request.reconciliation_scope) continue;
            if (ordinal++ < request.start_index) continue;
            if (count == request.query.maximum_items) break;
            page.items[count++] = evidence_view(request.operations, *evidence);
        }
        return count;
    }

    [[nodiscard]] static constexpr FmiPartnerEvidenceView evidence_view(
        domain::PartnerOperationsScope operations,
        const FmiEvidenceObservation& evidence) noexcept {
        return {
            operations,
            evidence.scope,
            evidence.role,
            evidence.provider,
            evidence.authorization,
            evidence.authorization_generation,
            evidence.owner,
            evidence.owner_instance,
            evidence.owner_generation,
            evidence.sequence_domain,
            evidence.sequence,
            evidence.journal_offset,
            evidence.configuration,
            evidence.event,
            evidence.owner_state_version,
            evidence.payload_digest,
            evidence.observed_at,
        };
    }

    [[nodiscard]] static constexpr std::size_t required_role_total(
        const FmiScopeRegistration& registration) noexcept {
        std::size_t total{};
        for (std::size_t role = 0U;
             role < static_cast<std::size_t>(FmiEvidenceRole::Count); ++role) {
            if ((registration.required_roles &
                 evidence_role_mask(static_cast<FmiEvidenceRole>(role))) != 0U) {
                ++total;
            }
        }
        return total;
    }

    [[nodiscard]] std::uint32_t fill_source_completeness(
        const FmiPartnerPageRequest& request,
        const FmiScopeRegistration& registration,
        FmiPartnerSourceCompletenessPage& page) const noexcept {
        std::size_t ordinal{};
        std::uint32_t count{};
        for (std::size_t role = 0U;
             role < static_cast<std::size_t>(FmiEvidenceRole::Count); ++role) {
            const auto role_value = static_cast<FmiEvidenceRole>(role);
            if ((registration.required_roles & evidence_role_mask(role_value)) ==
                0U) {
                continue;
            }
            if (ordinal++ < request.start_index) continue;
            if (count == request.query.maximum_items) break;
            page.items[count++] = source_completeness_view(
                request.operations, request.reconciliation_scope, role_value);
        }
        return count;
    }

    [[nodiscard]] FmiPartnerSourceCompletenessView source_completeness_view(
        domain::PartnerOperationsScope operations,
        FmiReconciliationScopeId scope, FmiEvidenceRole role) const noexcept {
        FmiPartnerSourceCompletenessView view{
            .operations = operations,
            .reconciliation_scope = scope,
            .role = role,
        };
        const FmiEvidenceObservation* latest{};
        for (std::size_t index = 0U; index < owner_.evidence_count(); ++index) {
            const auto* evidence = owner_.evidence_at(index);
            if (evidence->scope == scope && evidence->role == role) {
                latest = evidence;
            }
        }
        if (latest != nullptr) {
            view.owner = latest->owner;
            view.owner_generation = latest->owner_generation;
            view.sequence_domain = latest->sequence_domain;
            view.watermark = latest->sequence;
            view.configuration = latest->configuration;
            view.evidence_event = latest->event;
            view.evidence_digest = latest->payload_digest;
            view.observed_at = latest->observed_at;
            view.completeness = FmiPartnerSourceCompleteness::Current;
        }
        for (std::size_t index = 0U; index < owner_.case_count(); ++index) {
            const auto* exception = owner_.exception_case(
                FmiExceptionCaseId{index + 1U});
            if (exception == nullptr || exception->scope != scope ||
                exception->observed_role != role ||
                exception->state == FmiCaseState::Resolved) {
                continue;
            }
            if (exception->reason == FmiBreakReason::SourceSequenceGap) {
                view.completeness = FmiPartnerSourceCompleteness::Gap;
            } else if (exception->reason ==
                       FmiBreakReason::OwnerGenerationChanged) {
                view.completeness =
                    FmiPartnerSourceCompleteness::OwnerGenerationChanged;
            }
        }
        return view;
    }

    [[nodiscard]] std::size_t case_total(
        FmiReconciliationScopeId scope) const noexcept {
        std::size_t total{};
        for (std::size_t index = 0U; index < owner_.case_count(); ++index) {
            const auto* exception = owner_.exception_case(
                FmiExceptionCaseId{index + 1U});
            if (exception != nullptr && exception->scope == scope) ++total;
        }
        return total;
    }

    [[nodiscard]] std::uint32_t fill_breaks(
        const FmiPartnerPageRequest& request,
        FmiPartnerBreakPage& page) const noexcept {
        std::size_t ordinal{};
        std::uint32_t count{};
        for (std::size_t index = 0U; index < owner_.case_count(); ++index) {
            const auto* exception = owner_.exception_case(
                FmiExceptionCaseId{index + 1U});
            if (exception == nullptr ||
                exception->scope != request.reconciliation_scope) continue;
            if (ordinal++ < request.start_index) continue;
            if (count == request.query.maximum_items) break;
            page.items[count++] = {
                request.operations,
                exception->exception_case,
                exception->scope,
                exception->reason,
                exception->anchor_role,
                exception->observed_role,
                exception->anchor_event,
                exception->observed_event,
                exception->version,
                case_evidence_digests_[index],
                exception->opened_at,
            };
        }
        return count;
    }

    [[nodiscard]] std::uint32_t fill_cases(
        const FmiPartnerPageRequest& request,
        FmiPartnerCasePage& page) const noexcept {
        std::size_t ordinal{};
        std::uint32_t count{};
        for (std::size_t index = 0U; index < owner_.case_count(); ++index) {
            const auto* exception = owner_.exception_case(
                FmiExceptionCaseId{index + 1U});
            if (exception == nullptr ||
                exception->scope != request.reconciliation_scope) continue;
            if (ordinal++ < request.start_index) continue;
            if (count == request.query.maximum_items) break;
            const auto& provenance = case_provenance_[index];
            page.items[count++] = {
                request.operations,
                exception->exception_case,
                exception->scope,
                exception->state,
                exception->version,
                case_evidence_digests_[index],
                exception->proposer,
                exception->approver,
                provenance.approval,
                provenance.authorization_decision,
                provenance.authorization_generation,
                provenance.payload_digest,
                exception->updated_at,
            };
        }
        return count;
    }

    [[nodiscard]] std::size_t audit_total(
        const domain::PartnerOperationsScope& operations) const noexcept {
        std::size_t total{};
        for (std::size_t index = 0U; index < audit_count_; ++index) {
            if (same_operations_scope(audits_[index].scope, operations)) ++total;
        }
        return total;
    }

    [[nodiscard]] std::uint32_t fill_audits(
        const FmiPartnerPageRequest& request,
        FmiPartnerResolutionAuditPage& page) const noexcept {
        std::size_t ordinal{};
        std::uint32_t count{};
        for (std::size_t index = 0U; index < audit_count_; ++index) {
            if (!same_operations_scope(audits_[index].scope,
                                       request.operations)) continue;
            if (ordinal++ < request.start_index) continue;
            if (count == request.query.maximum_items) break;
            page.items[count++] = audits_[index];
        }
        return count;
    }

    [[nodiscard]] static constexpr FmiPartnerWorkflowResult rejected(
        core::ErrorCode error) noexcept {
        return {core::Status::failure(error),
                FmiPartnerWorkflowDisposition::Rejected, {}, {}};
    }

    [[nodiscard]] static constexpr FmiReconciliationResult local_failure(
        core::ErrorCode error,
        FmiReconciliationScopeId scope) noexcept {
        return {core::Status::failure(error),
                FmiReconciliationDisposition::CapacityExceeded,
                scope, {}, 0U, 0U};
    }

    FmiPartnerProjectionAuthority authority_{};
    ReconciliationOwner owner_{};
    std::array<FmiScopeRegistration, ScopeCapacity> registrations_{};
    std::array<FmiPartnerScopeBinding, BindingCapacity> bindings_{};
    std::array<FmiPartnerAuthorizationState, BindingCapacity>
        authorizations_{};
    std::array<FmiPartnerApprovalBinding, ApprovalCapacity> approvals_{};
    std::array<std::uint64_t, CaseCapacity> case_evidence_digests_{};
    std::array<CaseProvenance, CaseCapacity> case_provenance_{};
    std::array<domain::PartnerOperationsAuditView, AuditCapacity> audits_{};
    std::array<CommandRecord, CommandCapacity> command_records_{};
    std::size_t registration_count_{};
    std::size_t binding_count_{};
    std::size_t authorization_count_{};
    std::size_t approval_count_{};
    std::size_t audit_count_{};
    std::size_t command_count_{};
    std::uint64_t projection_revision_{1U};
};

}  // namespace fintech::reconciliation
