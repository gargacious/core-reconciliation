#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <fintech/reconciliation/fmi_reconciliation.hpp>

namespace fintech::reconciliation {

template <std::size_t ScopeCapacity, std::size_t PolicyCapacity,
          std::size_t EvidenceCapacity, std::size_t CaseCapacity,
          std::size_t SourceCapacity = 32U,
          std::size_t CommandCapacity = 64U,
          std::size_t AssessmentCapacity = 32U>
class BoundedFmiReconciliationOwner final {
   public:
    static_assert(ScopeCapacity > 0U && PolicyCapacity > 0U &&
                  EvidenceCapacity > 0U && CaseCapacity > 0U &&
                  SourceCapacity > 0U && CommandCapacity > 0U &&
                  AssessmentCapacity > 0U);

    [[nodiscard]] FmiReconciliationResult register_scope(
        const FmiScopeRegistration& registration) noexcept {
        if (!valid(registration)) return invalid();
        if (const auto index = find_scope(registration.scope);
            index != not_found) {
            if (scopes_[index].registration == registration) {
                return result(FmiReconciliationDisposition::ScopeRegistered,
                              registration.scope, {},
                              scopes_[index].version);
            }
            return failure(FmiReconciliationDisposition::Invalid,
                           core::ErrorCode::InvalidState,
                           registration.scope);
        }
        if (scope_count_ == ScopeCapacity) return capacity(registration.scope);
        scopes_[scope_count_].registration = registration;
        scopes_[scope_count_].version = 1U;
        scopes_[scope_count_].evidence.fill(not_found);
        ++scope_count_;
        return result(FmiReconciliationDisposition::ScopeRegistered,
                      registration.scope, {}, 1U);
    }

    [[nodiscard]] FmiReconciliationResult register_policy(
        const FmiTolerancePolicy& policy) noexcept {
        if (!valid(policy)) return invalid();
        for (std::size_t index = 0U; index < policy_count_; ++index) {
            if (policies_[index].policy != policy.policy) continue;
            if (policies_[index] == policy) {
                return result(FmiReconciliationDisposition::PolicyRegistered);
            }
            return failure(FmiReconciliationDisposition::Invalid,
                           core::ErrorCode::InvalidState);
        }
        if (policy_count_ == PolicyCapacity) return capacity({});
        policies_[policy_count_++] = policy;
        return result(FmiReconciliationDisposition::PolicyRegistered);
    }

    [[nodiscard]] FmiReconciliationResult observe(
        const FmiEvidenceObservation& observation) noexcept {
        if (!valid(observation)) return invalid();
        const auto scope_index = find_scope(observation.scope);
        if (scope_index == not_found) {
            return failure(FmiReconciliationDisposition::ScopeUnavailable,
                           core::ErrorCode::Unavailable, observation.scope);
        }
        ScopeState& scope = scopes_[scope_index];
        if (!matches_scope(scope.registration, observation)) return invalid();

        if (const auto retained = find_delivery(observation);
            retained != not_found) {
            if (same_evidence(evidence_[retained], observation)) {
                return result(FmiReconciliationDisposition::DuplicateEvidence,
                              observation.scope, {}, scope.version);
            }
            return opened_failure(scope, FmiBreakReason::ConflictingRetry,
                                  observation.role, {}, observation,
                                  core::ErrorCode::InvalidState);
        }

        const auto source_index = find_source(observation.owner,
                                              observation.sequence_domain);
        if (source_index != not_found) {
            const SourceCursor& source = sources_[source_index];
            if (source.owner_instance != observation.owner_instance ||
                source.owner_generation != observation.owner_generation ||
                source.configuration != observation.configuration) {
                return opened_failure(
                    scope, FmiBreakReason::OwnerGenerationChanged,
                    observation.role, current(scope, observation.role),
                    observation, core::ErrorCode::InvalidState);
            }
            if (observation.sequence != source.last_sequence + 1U) {
                return opened_failure(
                    scope, FmiBreakReason::SourceSequenceGap,
                    observation.role, current(scope, observation.role),
                    observation, core::ErrorCode::SequenceGap);
            }
        } else if (source_count_ == SourceCapacity) {
            return capacity(observation.scope);
        }

        const auto policy_index = find_policy(scope.registration, observation);
        if (policy_index == not_found) {
            return opened_failure(
                scope, FmiBreakReason::TolerancePolicyUnavailable,
                observation.role, current(scope, observation.role), observation,
                core::ErrorCode::Unavailable);
        }
        if (evidence_count_ == EvidenceCapacity) {
            return capacity(observation.scope);
        }

        if (source_index == not_found) {
            sources_[source_count_++] = {
                observation.owner,
                observation.owner_instance,
                observation.owner_generation,
                observation.sequence_domain,
                observation.configuration,
                observation.sequence,
            };
        } else {
            sources_[source_index].last_sequence = observation.sequence;
        }

        evidence_[evidence_count_] = observation;
        scope.evidence[role_index(observation.role)] = evidence_count_;
        ++evidence_count_;
        ++scope.version;

        std::uint32_t opened{};
        FmiExceptionCaseId first_case{};
        if (observation.role == scope.registration.quantity_anchor ||
            observation.role == scope.registration.cash_anchor) {
            for (std::size_t role = 0U; role < role_count; ++role) {
                if (scope.evidence[role] == not_found) continue;
                compare_role(scope, static_cast<FmiEvidenceRole>(role), opened,
                             first_case);
            }
        } else {
            compare_role(scope, observation.role, opened, first_case);
        }
        if (opened != 0U) {
            return result(FmiReconciliationDisposition::CaseOpened,
                          observation.scope, first_case, scope.version, opened);
        }
        return result(all_required_present(scope)
                          ? FmiReconciliationDisposition::Reconciled
                          : FmiReconciliationDisposition::EvidenceRecorded,
                      observation.scope, {}, scope.version);
    }

    [[nodiscard]] FmiReconciliationResult assess(
        const FmiAssessmentRequest& request) noexcept {
        if (!valid(request)) return invalid();
        const auto scope_index = find_scope(request.scope);
        if (scope_index == not_found) {
            return failure(FmiReconciliationDisposition::ScopeUnavailable,
                           core::ErrorCode::Unavailable, request.scope);
        }
        for (std::size_t index = 0U; index < assessment_count_; ++index) {
            if (assessments_[index].assessment != request.assessment) continue;
            if (assessments_[index] == request) {
                return result(
                    FmiReconciliationDisposition::AssessmentCompleted,
                    request.scope, {}, scopes_[scope_index].version);
            }
            return failure(FmiReconciliationDisposition::ConflictingCommand,
                           core::ErrorCode::InvalidState, request.scope);
        }
        if (assessment_count_ == AssessmentCapacity) {
            return capacity(request.scope);
        }
        ScopeState& scope = scopes_[scope_index];
        std::uint32_t opened{};
        FmiExceptionCaseId first_case{};
        for (std::size_t role = 0U; role < role_count; ++role) {
            const auto role_value = static_cast<FmiEvidenceRole>(role);
            if ((scope.registration.required_roles &
                 evidence_role_mask(role_value)) == 0U ||
                scope.evidence[role] != not_found ||
                has_open_missing_case(scope.registration.scope, role_value)) {
                continue;
            }
            const auto case_id = open_case(
                scope, FmiBreakReason::MissingRequiredEvidence,
                anchor_for(scope.registration, role_value), role_value, nullptr,
                nullptr, {}, request.assessed_at);
            if (!case_id.valid()) return capacity(request.scope);
            if (!first_case.valid()) first_case = case_id;
            ++opened;
        }
        assessments_[assessment_count_++] = request;
        ++scope.version;
        return result(FmiReconciliationDisposition::AssessmentCompleted,
                      request.scope, first_case, scope.version, opened);
    }

    [[nodiscard]] FmiReconciliationResult propose_resolution(
        const FmiResolutionProposal& proposal) noexcept {
        if (!valid(proposal)) return invalid();
        if (const auto retry = command_retry(proposal.command,
                                             proposal.command_digest);
            retry != not_found) {
            return commands_[retry].result;
        }
        if (command_conflict(proposal.command)) {
            return failure(FmiReconciliationDisposition::ConflictingCommand,
                           core::ErrorCode::InvalidState);
        }
        const auto case_index = find_case(proposal.exception_case);
        if (case_index == not_found) {
            return failure(FmiReconciliationDisposition::ScopeUnavailable,
                           core::ErrorCode::Unavailable);
        }
        FmiExceptionCase& exception = cases_[case_index];
        if (exception.version != proposal.expected_case_version ||
            (exception.state != FmiCaseState::Open &&
             exception.state != FmiCaseState::Escalated)) {
            return failure(FmiReconciliationDisposition::StaleCaseVersion,
                           core::ErrorCode::InvalidState, exception.scope,
                           exception.exception_case, exception.version);
        }
        if (command_count_ == CommandCapacity) return capacity(exception.scope);
        exception.state = FmiCaseState::PendingApproval;
        exception.proposal = proposal.proposal;
        exception.proposed_action = proposal.action;
        exception.proposer = proposal.proposer;
        exception.approver = {};
        exception.resolution_evidence_digest = proposal.evidence_digest;
        exception.updated_at = proposal.proposed_at;
        ++exception.version;
        const auto response = result(
            FmiReconciliationDisposition::ResolutionProposed, exception.scope,
            exception.exception_case, exception.version);
        commands_[command_count_++] = {
            proposal.command, proposal.command_digest, response};
        return response;
    }

    [[nodiscard]] FmiReconciliationResult approve_resolution(
        const FmiResolutionApproval& approval) noexcept {
        if (!valid(approval)) return invalid();
        if (const auto retry = command_retry(approval.command,
                                             approval.command_digest);
            retry != not_found) {
            return commands_[retry].result;
        }
        if (command_conflict(approval.command)) {
            return failure(FmiReconciliationDisposition::ConflictingCommand,
                           core::ErrorCode::InvalidState);
        }
        const auto case_index = find_case(approval.exception_case);
        if (case_index == not_found) {
            return failure(FmiReconciliationDisposition::ScopeUnavailable,
                           core::ErrorCode::Unavailable);
        }
        FmiExceptionCase& exception = cases_[case_index];
        if (exception.state != FmiCaseState::PendingApproval ||
            exception.proposal != approval.proposal ||
            exception.version != approval.expected_case_version ||
            exception.proposer == approval.approver) {
            return failure(FmiReconciliationDisposition::StaleCaseVersion,
                           core::ErrorCode::InvalidState, exception.scope,
                           exception.exception_case, exception.version);
        }
        if (command_count_ == CommandCapacity) return capacity(exception.scope);
        exception.approver = approval.approver;
        exception.updated_at = approval.approved_at;
        exception.resolution_evidence_digest = approval.evidence_digest;
        exception.state =
            exception.proposed_action ==
                    FmiResolutionAction::EscalateToEconomicOwner
                ? FmiCaseState::Escalated
                : FmiCaseState::Resolved;
        ++exception.version;
        const auto response = result(
            FmiReconciliationDisposition::ResolutionApproved, exception.scope,
            exception.exception_case, exception.version);
        commands_[command_count_++] = {
            approval.command, approval.command_digest, response};
        return response;
    }

    [[nodiscard]] const FmiExceptionCase* exception_case(
        FmiExceptionCaseId id) const noexcept {
        const auto index = find_case(id);
        return index == not_found ? nullptr : &cases_[index];
    }

    [[nodiscard]] const FmiEvidenceObservation* evidence_at(
        std::size_t index) const noexcept {
        return index < evidence_count_ ? &evidence_[index] : nullptr;
    }

    [[nodiscard]] constexpr std::size_t scope_count() const noexcept {
        return scope_count_;
    }
    [[nodiscard]] constexpr std::size_t policy_count() const noexcept {
        return policy_count_;
    }
    [[nodiscard]] constexpr std::size_t evidence_count() const noexcept {
        return evidence_count_;
    }
    [[nodiscard]] constexpr std::size_t case_count() const noexcept {
        return case_count_;
    }

   private:
    static constexpr std::size_t not_found =
        std::numeric_limits<std::size_t>::max();
    static constexpr std::size_t role_count =
        static_cast<std::size_t>(FmiEvidenceRole::Count);

    struct ScopeState final {
        FmiScopeRegistration registration{};
        std::array<std::size_t, role_count> evidence{};
        std::uint64_t version{};
    };

    struct SourceCursor final {
        core::ComponentId owner{};
        core::RuntimeInstanceId owner_instance{};
        std::uint64_t owner_generation{};
        core::SequenceDomainId sequence_domain{};
        core::ConfigFingerprint configuration{};
        std::uint64_t last_sequence{};
    };

    struct CommandRecord final {
        core::CommandId command{};
        std::uint64_t command_digest{};
        FmiReconciliationResult result{};
    };

    [[nodiscard]] static constexpr std::size_t role_index(
        FmiEvidenceRole role) noexcept {
        return static_cast<std::size_t>(role);
    }

    [[nodiscard]] std::size_t find_scope(
        FmiReconciliationScopeId id) const noexcept {
        for (std::size_t index = 0U; index < scope_count_; ++index)
            if (scopes_[index].registration.scope == id) return index;
        return not_found;
    }

    [[nodiscard]] std::size_t find_case(
        FmiExceptionCaseId id) const noexcept {
        for (std::size_t index = 0U; index < case_count_; ++index)
            if (cases_[index].exception_case == id) return index;
        return not_found;
    }

    [[nodiscard]] std::size_t find_source(
        core::ComponentId owner,
        core::SequenceDomainId sequence_domain) const noexcept {
        for (std::size_t index = 0U; index < source_count_; ++index)
            if (sources_[index].owner == owner &&
                sources_[index].sequence_domain == sequence_domain)
                return index;
        return not_found;
    }

    [[nodiscard]] std::size_t find_delivery(
        const FmiEvidenceObservation& observation) const noexcept {
        for (std::size_t index = 0U; index < evidence_count_; ++index) {
            if (evidence_[index].owner == observation.owner &&
                evidence_[index].sequence_domain ==
                    observation.sequence_domain &&
                evidence_[index].sequence == observation.sequence)
                return index;
        }
        return not_found;
    }

    [[nodiscard]] std::size_t find_policy(
        const FmiScopeRegistration& scope,
        const FmiEvidenceObservation& observation) const noexcept {
        for (std::size_t index = 0U; index < policy_count_; ++index) {
            const auto& policy = policies_[index];
            if (policy.provider == observation.provider &&
                policy.asset_class == scope.asset_class &&
                policy.product == scope.product &&
                policy.jurisdiction == scope.jurisdiction &&
                policy.currency == scope.currency &&
                policy.quantity_unit == scope.quantity_unit &&
                policy.quantity_scale == observation.quantity.scale &&
                policy.cash_scale == observation.cash.scale &&
                core::comparable_clock_domain(policy.effective_from,
                                              observation.observed_at) &&
                core::comparable_clock_domain(policy.effective_until,
                                              observation.observed_at) &&
                policy.effective_from.nanoseconds <=
                    observation.observed_at.nanoseconds &&
                observation.observed_at.nanoseconds <
                    policy.effective_until.nanoseconds)
                return index;
        }
        return not_found;
    }

    [[nodiscard]] const FmiEvidenceObservation* current(
        const ScopeState& scope, FmiEvidenceRole role) const noexcept {
        const auto index = scope.evidence[role_index(role)];
        return index == not_found ? nullptr : &evidence_[index];
    }

    [[nodiscard]] static constexpr bool matches_scope(
        const FmiScopeRegistration& scope,
        const FmiEvidenceObservation& observation) noexcept {
        const auto mask = evidence_role_mask(observation.role);
        return (scope.required_roles & mask) != 0U &&
               observation.quantity.unit == scope.quantity_unit &&
               observation.cash.currency == scope.currency;
    }

    [[nodiscard]] static constexpr FmiEvidenceRole anchor_for(
        const FmiScopeRegistration& scope, FmiEvidenceRole role) noexcept {
        const auto mask = evidence_role_mask(role);
        if ((scope.quantity_roles & mask) != 0U) return scope.quantity_anchor;
        if ((scope.cash_roles & mask) != 0U) return scope.cash_anchor;
        return FmiEvidenceRole::Count;
    }

    [[nodiscard]] bool all_required_present(
        const ScopeState& scope) const noexcept {
        for (std::size_t role = 0U; role < role_count; ++role) {
            const auto role_value = static_cast<FmiEvidenceRole>(role);
            if ((scope.registration.required_roles &
                 evidence_role_mask(role_value)) != 0U &&
                scope.evidence[role] == not_found)
                return false;
        }
        return true;
    }

    [[nodiscard]] bool has_open_missing_case(
        FmiReconciliationScopeId scope, FmiEvidenceRole role) const noexcept {
        for (std::size_t index = 0U; index < case_count_; ++index) {
            if (cases_[index].scope == scope &&
                cases_[index].reason ==
                    FmiBreakReason::MissingRequiredEvidence &&
                cases_[index].observed_role == role &&
                cases_[index].state != FmiCaseState::Resolved)
                return true;
        }
        return false;
    }

    [[nodiscard]] static constexpr std::uint64_t quantity_delta(
        const core::ScaledQuantity& left,
        const core::ScaledQuantity& right) noexcept {
        return left.mantissa >= right.mantissa
                   ? left.mantissa - right.mantissa
                   : right.mantissa - left.mantissa;
    }

    [[nodiscard]] static constexpr std::uint64_t magnitude(
        std::int64_t value) noexcept {
        return value >= 0
                   ? static_cast<std::uint64_t>(value)
                   : static_cast<std::uint64_t>(-(value + 1)) + 1U;
    }

    [[nodiscard]] static constexpr std::uint64_t cash_delta(
        const core::Money& left, const core::Money& right) noexcept {
        const auto left_magnitude = magnitude(left.mantissa);
        const auto right_magnitude = magnitude(right.mantissa);
        if ((left.mantissa < 0) == (right.mantissa < 0)) {
            return left_magnitude >= right_magnitude
                       ? left_magnitude - right_magnitude
                       : right_magnitude - left_magnitude;
        }
        if (std::numeric_limits<std::uint64_t>::max() - left_magnitude <
            right_magnitude)
            return std::numeric_limits<std::uint64_t>::max();
        return left_magnitude + right_magnitude;
    }

    void compare_role(ScopeState& scope, FmiEvidenceRole role,
                      std::uint32_t& opened,
                      FmiExceptionCaseId& first_case) noexcept {
        const auto role_mask = evidence_role_mask(role);
        const auto* observed = current(scope, role);
        if (observed == nullptr) return;
        const auto policy_index = find_policy(scope.registration, *observed);
        if (policy_index == not_found) return;
        const auto& policy = policies_[policy_index];

        if ((scope.registration.quantity_roles & role_mask) != 0U &&
            role != scope.registration.quantity_anchor) {
            const auto* anchor = current(scope, scope.registration.quantity_anchor);
            if (anchor != nullptr) {
                const auto delta = quantity_delta(anchor->quantity,
                                                  observed->quantity);
                if (delta > policy.maximum_quantity_delta) {
                    const auto id = open_case(
                        scope, FmiBreakReason::QuantityMismatch,
                        scope.registration.quantity_anchor, role, anchor,
                        observed, policy.policy, observed->observed_at, delta,
                        0U);
                    if (id.valid()) {
                        if (!first_case.valid()) first_case = id;
                        ++opened;
                    }
                }
            }
        }
        if ((scope.registration.cash_roles & role_mask) != 0U &&
            role != scope.registration.cash_anchor) {
            const auto* anchor = current(scope, scope.registration.cash_anchor);
            if (anchor != nullptr) {
                const auto delta = cash_delta(anchor->cash, observed->cash);
                if (delta > policy.maximum_cash_delta) {
                    const auto id = open_case(
                        scope, FmiBreakReason::CashMismatch,
                        scope.registration.cash_anchor, role, anchor, observed,
                        policy.policy, observed->observed_at, 0U, delta);
                    if (id.valid()) {
                        if (!first_case.valid()) first_case = id;
                        ++opened;
                    }
                }
            }
        }
    }

    [[nodiscard]] FmiExceptionCaseId open_case(
        ScopeState& scope, FmiBreakReason reason, FmiEvidenceRole anchor_role,
        FmiEvidenceRole observed_role,
        const FmiEvidenceObservation* anchor,
        const FmiEvidenceObservation* observed,
        FmiTolerancePolicyId tolerance_policy, core::Timestamp opened_at,
        std::uint64_t quantity_difference = 0U,
        std::uint64_t cash_difference = 0U) noexcept {
        for (std::size_t index = 0U; index < case_count_; ++index) {
            const auto& retained = cases_[index];
            if (retained.scope == scope.registration.scope &&
                retained.reason == reason &&
                retained.anchor_role == anchor_role &&
                retained.observed_role == observed_role &&
                retained.anchor_event ==
                    (anchor == nullptr ? core::EventId{} : anchor->event) &&
                retained.observed_event ==
                    (observed == nullptr ? core::EventId{} : observed->event) &&
                retained.state != FmiCaseState::Resolved)
                return retained.exception_case;
        }
        if (case_count_ == CaseCapacity) return {};
        const FmiExceptionCaseId id{case_count_ + 1U};
        cases_[case_count_++] = {
            id,
            scope.registration.scope,
            reason,
            anchor_role,
            observed_role,
            anchor == nullptr ? core::EventId{} : anchor->event,
            observed == nullptr ? core::EventId{} : observed->event,
            quantity_difference,
            cash_difference,
            tolerance_policy,
            FmiCaseState::Open,
            1U,
            opened_at,
            opened_at,
            {},
            FmiResolutionAction::EscalateToEconomicOwner,
            {},
            {},
            0U,
        };
        ++scope.version;
        return id;
    }

    [[nodiscard]] FmiReconciliationResult opened_failure(
        ScopeState& scope, FmiBreakReason reason, FmiEvidenceRole role,
        const FmiEvidenceObservation* anchor,
        const FmiEvidenceObservation& observed,
        core::ErrorCode error) noexcept {
        const auto case_id = open_case(scope, reason, role, role, anchor,
                                       &observed, {}, observed.observed_at);
        if (!case_id.valid()) return capacity(observed.scope);
        return failure(FmiReconciliationDisposition::CaseOpened, error,
                       observed.scope, case_id, scope.version, 1U);
    }

    [[nodiscard]] std::size_t command_retry(
        core::CommandId command, std::uint64_t digest) const noexcept {
        for (std::size_t index = 0U; index < command_count_; ++index)
            if (commands_[index].command == command &&
                commands_[index].command_digest == digest)
                return index;
        return not_found;
    }

    [[nodiscard]] bool command_conflict(core::CommandId command) const noexcept {
        for (std::size_t index = 0U; index < command_count_; ++index)
            if (commands_[index].command == command) return true;
        return false;
    }

    [[nodiscard]] static constexpr FmiReconciliationResult result(
        FmiReconciliationDisposition disposition,
        FmiReconciliationScopeId scope = {},
        FmiExceptionCaseId exception_case = {},
        std::uint64_t scope_version = 0U,
        std::uint32_t cases_opened = 0U) noexcept {
        return {core::Status::success(), disposition, scope, exception_case,
                scope_version, cases_opened};
    }

    [[nodiscard]] static constexpr FmiReconciliationResult failure(
        FmiReconciliationDisposition disposition, core::ErrorCode error,
        FmiReconciliationScopeId scope = {},
        FmiExceptionCaseId exception_case = {},
        std::uint64_t scope_version = 0U,
        std::uint32_t cases_opened = 0U) noexcept {
        return {core::Status::failure(error), disposition, scope,
                exception_case, scope_version, cases_opened};
    }

    [[nodiscard]] static constexpr FmiReconciliationResult invalid() noexcept {
        return failure(FmiReconciliationDisposition::Invalid,
                       core::ErrorCode::InvalidArgument);
    }

    [[nodiscard]] static constexpr FmiReconciliationResult capacity(
        FmiReconciliationScopeId scope) noexcept {
        return failure(FmiReconciliationDisposition::CapacityExceeded,
                       core::ErrorCode::CapacityExceeded, scope);
    }

    std::array<ScopeState, ScopeCapacity> scopes_{};
    std::array<FmiTolerancePolicy, PolicyCapacity> policies_{};
    std::array<FmiEvidenceObservation, EvidenceCapacity> evidence_{};
    std::array<FmiExceptionCase, CaseCapacity> cases_{};
    std::array<SourceCursor, SourceCapacity> sources_{};
    std::array<CommandRecord, CommandCapacity> commands_{};
    std::array<FmiAssessmentRequest, AssessmentCapacity> assessments_{};
    std::size_t scope_count_{};
    std::size_t policy_count_{};
    std::size_t evidence_count_{};
    std::size_t case_count_{};
    std::size_t source_count_{};
    std::size_t command_count_{};
    std::size_t assessment_count_{};
};

}  // namespace fintech::reconciliation
