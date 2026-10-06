#include <array>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include <fintech/reconciliation/fmi_partner_operations_owner.hpp>

namespace {

using namespace fintech;
using namespace fintech::reconciliation;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

core::Timestamp time(std::uint64_t nanoseconds) {
    return {nanoseconds, core::ClockKind::Simulation,
            core::ClockQuality::Synchronized};
}

constexpr auto usd = core::CurrencyCode::from_iso('U', 'S', 'D');

FmiScopeRegistration scope() {
    const auto venue = evidence_role_mask(FmiEvidenceRole::VenueTrade);
    const auto bank = evidence_role_mask(FmiEvidenceRole::SettlementBankCash);
    return {
        FmiReconciliationScopeId{1U},
        {domain::CanonicalTradeId{101U}, domain::CanonicalObligationId{201U},
         core::AccountId{301U}, core::InstrumentId{401U}, 20261006U},
        domain::FmiAssetClass::Equity,
        domain::ProductId{1U},
        domain::JurisdictionId{91U},
        usd,
        core::QuantityUnit::Shares,
        FmiEvidenceRole::VenueTrade,
        FmiEvidenceRole::VenueTrade,
        static_cast<FmiEvidenceRoleMask>(venue | bank),
        venue,
        static_cast<FmiEvidenceRoleMask>(venue | bank),
    };
}

FmiTolerancePolicy policy(std::uint64_t provider) {
    return {
        FmiTolerancePolicyId{provider},
        domain::OrganizationId{provider},
        domain::FmiAssetClass::Equity,
        domain::ProductId{1U},
        domain::JurisdictionId{91U},
        usd,
        core::QuantityUnit::Shares,
        0U,
        0U,
        0U,
        0U,
        core::ComponentId{900U},
        core::RuntimeInstanceId{901U},
        3U,
        core::ConfigFingerprint{902U},
        core::EventId{provider + 900U},
        time(1U),
        time(10'000U),
        provider + 1000U,
    };
}

FmiEvidenceObservation observation(FmiEvidenceRole role,
                                   std::uint64_t provider,
                                   std::int64_t cash,
                                   std::uint64_t sequence = 1U) {
    return {
        FmiReconciliationScopeId{1U},
        role,
        domain::OrganizationId{provider},
        domain::ServiceAuthorizationId{provider + 10U},
        4U,
        {100U, 0U, core::QuantityUnit::Shares},
        {cash, usd, 0U},
        core::ComponentId{static_cast<std::uint32_t>(provider + 100U)},
        core::RuntimeInstanceId{provider + 200U},
        7U,
        core::SequenceDomainId{provider + 300U},
        sequence,
        core::JournalOffset{provider + sequence + 400U},
        core::ConfigFingerprint{provider + 500U},
        core::EventId{provider * 100U + sequence},
        sequence,
        time(100U + provider + sequence),
        provider * 1000U + sequence,
    };
}

domain::WorkspaceScope workspace_scope() {
    return {
        .tenant = domain::TenantId{10U},
        .legal_entity = domain::LegalEntityId{11U},
        .desk = domain::DeskId{12U},
        .account = core::AccountId{301U},
        .environment = domain::WorkspaceEnvironment::Simulation,
        .fields = domain::WorkspaceScopeField::Desk |
                  domain::WorkspaceScopeField::Account,
    };
}

domain::WorkspaceContext workspace_context(
    core::PrincipalId actor = core::PrincipalId{701U},
    domain::AuthorizationDecisionId decision =
        domain::AuthorizationDecisionId{801U},
    std::uint64_t generation = 5U,
    std::uint64_t evaluated = 200U) {
    return {
        .workspace = domain::WorkspaceId{20U},
        .access_session = domain::AccessSessionId{21U},
        .scope = workspace_scope(),
        .authorization = {
            .actor = actor,
            .decision = decision,
            .authorization_generation = generation,
            .capability_digest = 802U,
            .policy = core::ConfigFingerprint{803U},
            .evaluated_at = time(evaluated),
            .expires_at = time(1'000U),
        },
        .context_version = generation,
        .actionable_selection_digest = 804U,
        .mode = domain::WorkspaceMode::Live,
        .data_authority = domain::WorkspaceDataAuthority::Authoritative,
        .read_only = false,
    };
}

domain::PartnerOperationsScope operations_scope() {
    return {
        domain::WorkspaceId{20U},
        domain::ServiceAuthorizationId{30U},
        domain::ProviderCapabilityId{31U},
        domain::ProtocolProfileId{32U},
    };
}

FmiPartnerPageRequest page_request(FmiPartnerPageKind kind,
                                   std::uint32_t maximum = 1U) {
    return {
        .query = {
            .context = workspace_context(),
            .query = domain::QueryId{40U +
                                    static_cast<std::uint64_t>(kind)},
            .requested_schema = core::SchemaId{50U},
            .requested_schema_revision = 1U,
            .filter_digest = 60U,
            .sort_digest = 61U,
            .maximum_items = maximum,
            .surface = domain::WorkspaceQuerySurface::Read,
            .mode = domain::WorkspaceQueryMode::Snapshot,
        },
        .operations = operations_scope(),
        .reconciliation_scope = FmiReconciliationScopeId{1U},
        .kind = kind,
    };
}

domain::WorkspaceCommandEnvelope command_envelope(
    core::CommandId command, domain::IdempotencyKey idempotency,
    core::PrincipalId actor, domain::ApprovalReferenceId approval,
    std::uint64_t expected_version, std::uint64_t payload,
    std::uint64_t created_at = 300U,
    domain::AuthorizationDecisionId decision =
        domain::AuthorizationDecisionId{801U},
    std::uint64_t generation = 5U) {
    return {
        .header = {
            .schema = core::SchemaId{70U},
            .version = 1U,
            .correlation = core::CorrelationId{71U},
            .causation = core::CausationId{72U},
            .created_at = time(created_at),
            .deadline = time(created_at + 100U),
            .config = core::ConfigFingerprint{73U},
            .command = command,
            .owner = core::ComponentId{74U},
            .source = core::ComponentId{75U},
            .source_instance = core::RuntimeInstanceId{76U},
        },
        .context = workspace_context(actor, decision, generation, created_at),
        .idempotency = idempotency,
        .target = {804U, 805U, 1U},
        .payload_digest = payload,
        .reason_digest = 806U,
        .expected_state_version = expected_version,
        .approval = approval,
        .fields = domain::WorkspaceCommandField::ExpectedStateVersion |
                  domain::WorkspaceCommandField::ApprovalReference,
    };
}

FmiPartnerWorkflowRequest workflow_request(
    domain::PartnerCaseWorkflowAction action, core::CommandId command,
    domain::IdempotencyKey idempotency, core::PrincipalId actor,
    domain::ApprovalReferenceId approval, std::uint64_t version,
    std::uint64_t payload, std::uint64_t evidence,
    std::uint64_t created_at = 300U,
    std::uint64_t case_id = 1U,
    std::uint64_t proposal_id = 901U) {
    return {
        .command = {
            .command = command_envelope(command, idempotency, actor, approval,
                                        version, payload, created_at),
            .scope = operations_scope(),
            .case_id = domain::PartnerOperationsCaseId{case_id},
            .evidence_digest = evidence,
            .action = action,
        },
        .proposal = FmiResolutionProposalId{proposal_id},
        .resolution_action =
            action == domain::PartnerCaseWorkflowAction::Escalate
                ? FmiResolutionAction::EscalateToEconomicOwner
                : FmiResolutionAction::CloseAsAuthorizedTolerance,
    };
}

FmiPartnerApprovalBinding approval_binding(
    const FmiPartnerWorkflowRequest& request,
    core::PrincipalId proposer = core::PrincipalId{701U},
    core::PrincipalId approver = core::PrincipalId{702U},
    std::uint64_t expires_at = 900U) {
    return {
        .approval = request.command.command.approval,
        .operations = operations_scope(),
        .case_id = request.command.case_id,
        .expected_case_version =
            request.command.command.expected_state_version,
        .proposer = proposer,
        .approver = approver,
        .authorization_decision = domain::AuthorizationDecisionId{801U},
        .authorization_generation = 5U,
        .payload_digest = request.command.command.payload_digest,
        .evidence_digest = request.command.evidence_digest,
        .approval_event = core::EventId{
            1'000U + request.command.command.approval.value()},
        .expires_at = time(expires_at),
        .action = request.command.action,
        .active = true,
    };
}

template <typename T>
concept HasSnapshot = requires(T value) { value.snapshot(); };

}  // namespace

int main() {
    using Owner = BoundedFmiPartnerOperationsOwner<2U, 4U, 8U, 8U>;
    static_assert(fmi_partner_operations_contract_revision == 1U);
    static_assert(domain::fmi_partner_operations_contract_revision == 1U);
    static_assert(!HasSnapshot<Owner>);

    const FmiPartnerProjectionAuthority projection{
        core::ComponentId{1U}, core::RuntimeInstanceId{2U}, 3U,
        core::ConfigFingerprint{4U}, core::SchemaId{50U}, 1U,
        domain::ProjectionEntityId{100U}, domain::ViewStreamId{200U},
        core::SequenceDomainId{300U}};
    Owner owner{projection};
    require(owner.register_scope(scope()).status.ok(), "scope registration");
    require(owner.register_policy(policy(1U)).status.ok(), "venue policy");
    require(owner.register_policy(policy(2U)).status.ok(), "bank policy");
    require(owner.register_operations_scope(
                {operations_scope(), FmiReconciliationScopeId{1U},
                 workspace_scope(), 802U})
                .ok(),
            "operations scope binding");
    require(owner.synchronize_authorization(
                {operations_scope(), domain::AuthorizationDecisionId{801U},
                 5U, 802U, time(1'000U), core::EventId{803U}, true})
                .ok(),
            "current authorization binding");

    const auto venue = observation(FmiEvidenceRole::VenueTrade, 1U, 10'000);
    const auto bank =
        observation(FmiEvidenceRole::SettlementBankCash, 2U, 9'000);
    require(owner.observe(venue).status.ok(), "venue evidence");
    const auto bank_result = owner.observe(bank);
    require(bank_result.status.ok() && owner.case_count() == 1U,
            "cash mismatch opens retained case");
    const auto evidence_before = *owner.evidence_at(1U);

    const auto scopes = owner.scope_page(
        page_request(FmiPartnerPageKind::Scope));
    require(scopes.status.ok() && scopes.page.cursor.item_count == 1U &&
                scopes.page.items[0].present_roles ==
                    scope().required_roles &&
                scopes.page.items[0].authorization_generation == 5U,
            "scope page binds completeness and authorization provenance");

    const auto evidence = owner.evidence_page(
        page_request(FmiPartnerPageKind::Evidence));
    require(evidence.status.ok() && evidence.page.cursor.item_count == 1U &&
                !evidence.page.cursor.terminal &&
                evidence.page.cursor.next_cursor_digest != 0U,
            "evidence page publishes bounded stable continuation");
    auto evidence_resume_request = page_request(FmiPartnerPageKind::Evidence);
    evidence_resume_request.start_index = 1U;
    evidence_resume_request.query.mode = domain::WorkspaceQueryMode::Resume;
    evidence_resume_request.query.requested_snapshot =
        evidence.page.cursor.snapshot;
    evidence_resume_request.query.resume_stream = evidence.page.cursor.stream;
    evidence_resume_request.query.resume_domain =
        evidence.page.cursor.sequence_domain;
    evidence_resume_request.query.resume_watermark =
        evidence.page.cursor.source_watermark;
    evidence_resume_request.query.cursor_digest =
        evidence.page.cursor.next_cursor_digest;
    const auto evidence_resume = owner.evidence_page(evidence_resume_request);
    require(evidence_resume.status.ok() &&
                evidence_resume.page.cursor.item_count == 1U &&
                evidence_resume.page.cursor.terminal,
            "stable cursor resumes exact next evidence reference");

    const auto sources = owner.source_completeness_page(
        page_request(FmiPartnerPageKind::SourceCompleteness, 2U));
    require(sources.status.ok() && sources.page.cursor.item_count == 2U &&
                sources.page.items[0].completeness ==
                    FmiPartnerSourceCompleteness::Current &&
                sources.page.items[1].completeness ==
                    FmiPartnerSourceCompleteness::Current,
            "source completeness page retains independent watermarks");
    const auto breaks = owner.break_page(
        page_request(FmiPartnerPageKind::Break));
    require(breaks.status.ok() && breaks.page.cursor.item_count == 1U &&
                breaks.page.items[0].evidence_digest == bank.payload_digest,
            "break page retains exact opening evidence digest");

    const auto wrong_evidence = workflow_request(
        domain::PartnerCaseWorkflowAction::Propose, core::CommandId{500U},
        {50U, 50U}, core::PrincipalId{701U},
        domain::ApprovalReferenceId{600U}, 1U, 7'000U,
        bank.payload_digest + 1U);
    require(owner.register_approval(approval_binding(wrong_evidence)).ok(),
            "wrong evidence approval fixture retained");
    require(!owner.submit_workflow(wrong_evidence).status &&
                owner.audit_count() == 0U,
            "command evidence digest must match retained opening evidence");

    const auto propose = workflow_request(
        domain::PartnerCaseWorkflowAction::Propose, core::CommandId{501U},
        {1U, 1U}, core::PrincipalId{701U},
        domain::ApprovalReferenceId{601U}, 1U, 7'001U,
        bank.payload_digest);
    require(owner.register_approval(approval_binding(propose)).ok(),
            "payload-bound proposal approval");
    const auto proposed = owner.submit_workflow(propose);
    require(proposed.status.ok() &&
                proposed.disposition ==
                    FmiPartnerWorkflowDisposition::Accepted &&
                proposed.reconciliation.scope_version == 2U,
            "authorized proposal enters pending approval");
    const auto proposed_retry = owner.submit_workflow(propose);
    require(proposed_retry.status.ok() &&
                proposed_retry.disposition ==
                    FmiPartnerWorkflowDisposition::Duplicate &&
                owner.audit_count() == 1U,
            "exact proposal retry is idempotent");

    const auto idempotency_conflict = workflow_request(
        domain::PartnerCaseWorkflowAction::Propose, core::CommandId{511U},
        {1U, 1U}, core::PrincipalId{701U},
        domain::ApprovalReferenceId{611U}, 1U, 7'011U,
        bank.payload_digest);
    require(owner.register_approval(approval_binding(idempotency_conflict)).ok(),
            "conflicting idempotency approval retained");
    const auto conflict_result = owner.submit_workflow(idempotency_conflict);
    require(!conflict_result.status &&
                conflict_result.status.code == core::ErrorCode::InvalidState &&
                owner.audit_count() == 1U,
            "conflicting idempotency fails closed without another audit");

    const auto stale_resume = owner.evidence_page(evidence_resume_request);
    require(!stale_resume.status,
            "state change invalidates old snapshot continuation");

    const auto stale = workflow_request(
        domain::PartnerCaseWorkflowAction::Approve, core::CommandId{502U},
        {2U, 2U}, core::PrincipalId{702U},
        domain::ApprovalReferenceId{602U}, 1U, 7'002U,
        bank.payload_digest);
    require(owner.register_approval(approval_binding(stale)).ok(),
            "stale approval provenance retained");
    require(!owner.submit_workflow(stale).status,
            "stale case version fails closed");

    const auto approve = workflow_request(
        domain::PartnerCaseWorkflowAction::Approve, core::CommandId{503U},
        {3U, 3U}, core::PrincipalId{702U},
        domain::ApprovalReferenceId{603U}, 2U, 7'003U,
        bank.payload_digest);
    require(owner.register_approval(approval_binding(approve)).ok(),
            "payload-bound checker approval");
    const auto approved = owner.submit_workflow(approve);
    require(approved.status.ok() &&
                owner.exception_case(FmiExceptionCaseId{1U})->state ==
                    FmiCaseState::Resolved &&
                same_evidence(*owner.evidence_at(1U), evidence_before),
            "checker resolves case without economic evidence mutation");

    auto stale_generation = workflow_request(
        domain::PartnerCaseWorkflowAction::Propose, core::CommandId{512U},
        {12U, 12U}, core::PrincipalId{701U},
        domain::ApprovalReferenceId{612U}, 3U, 7'012U,
        bank.payload_digest);
    stale_generation.command.command.context.authorization
        .authorization_generation = 4U;
    require(owner.register_approval(approval_binding(stale_generation)).ok(),
            "stale-generation approval reference retained");
    const auto stale_generation_result =
        owner.submit_workflow(stale_generation);
    require(!stale_generation_result.status &&
                stale_generation_result.status.code ==
                    core::ErrorCode::Unavailable,
            "stale authorization generation fails closed");

    const auto cases = owner.case_page(
        page_request(FmiPartnerPageKind::Case));
    require(cases.status.ok() && cases.page.cursor.item_count == 1U &&
                cases.page.items[0].approval ==
                    domain::ApprovalReferenceId{603U} &&
                cases.page.items[0].authorization_decision ==
                    domain::AuthorizationDecisionId{801U} &&
                cases.page.items[0].authorization_generation == 5U &&
                cases.page.items[0].payload_digest == 7'003U,
            "case page exposes decision generation and approval provenance");
    const auto audits = owner.resolution_audit_page(
        page_request(FmiPartnerPageKind::ResolutionAudit, 2U));
    require(audits.status.ok() && audits.page.cursor.item_count == 2U &&
                domain::valid(audits.page.items[0]) &&
                domain::valid(audits.page.items[1]),
            "resolution audit pages retain valid shared contract views");

    auto conflicting_bank = bank;
    conflicting_bank.cash.mantissa = 8'000;
    ++conflicting_bank.payload_digest;
    require(!owner.observe(conflicting_bank).status &&
                owner.case_count() == 2U,
            "conflicting retry creates a second immutable case");
    const auto escalate = workflow_request(
        domain::PartnerCaseWorkflowAction::Escalate,
        core::CommandId{513U}, {13U, 13U}, core::PrincipalId{701U},
        domain::ApprovalReferenceId{613U}, 1U, 7'013U,
        conflicting_bank.payload_digest, 330U, 2U, 902U);
    require(owner.register_approval(approval_binding(escalate)).ok(),
            "payload-bound escalation proposal approval");
    require(owner.submit_workflow(escalate).status.ok(),
            "governed escalation proposal enters approval state");
    const auto approve_escalation = workflow_request(
        domain::PartnerCaseWorkflowAction::Approve,
        core::CommandId{514U}, {14U, 14U}, core::PrincipalId{702U},
        domain::ApprovalReferenceId{614U}, 2U, 7'014U,
        conflicting_bank.payload_digest, 340U, 2U, 902U);
    require(owner.register_approval(approval_binding(approve_escalation)).ok(),
            "payload-bound escalation checker approval");
    require(owner.submit_workflow(approve_escalation).status.ok() &&
                owner.exception_case(FmiExceptionCaseId{2U})->state ==
                    FmiCaseState::Escalated &&
                same_evidence(*owner.evidence_at(1U), evidence_before),
            "approved escalation leaves source evidence immutable");

    const auto expired = workflow_request(
        domain::PartnerCaseWorkflowAction::Propose, core::CommandId{504U},
        {4U, 4U}, core::PrincipalId{701U},
        domain::ApprovalReferenceId{604U}, 3U, 7'004U,
        bank.payload_digest, 1'001U);
    require(owner.register_approval(
                approval_binding(expired, core::PrincipalId{701U},
                                 core::PrincipalId{702U}, 1'100U))
                .ok(),
            "expired command approval fixture retained");
    const auto case_before_denials =
        *owner.exception_case(FmiExceptionCaseId{1U});
    require(!owner.submit_workflow(expired).status,
            "expired authorization denies workflow");

    require(owner.synchronize_authorization(
                {operations_scope(), domain::AuthorizationDecisionId{801U},
                 5U, 802U, time(1'000U), core::EventId{803U}, false})
                .ok(),
            "authorization revocation generation retained");
    require(!owner.submit_workflow(approve).status,
            "revocation is re-evaluated before exact retry");
    require(owner.exception_case(FmiExceptionCaseId{1U})->version ==
                case_before_denials.version &&
                same_evidence(*owner.evidence_at(1U), evidence_before),
            "denials leave case and economic evidence unchanged");

    auto same_actor = approval_binding(propose, core::PrincipalId{701U},
                                       core::PrincipalId{701U});
    same_actor.approval = domain::ApprovalReferenceId{605U};
    require(!owner.register_approval(same_actor),
            "same maker and checker approval fails closed");

    std::cout << "PASS: FMI-OPS-01S-P2 reconciliation partner operations "
                 "pages=stable authorization=current approval=payload-bound "
                 "retry=idempotent economic-mutation=0 restart-qualified=0\n";
    return EXIT_SUCCESS;
}
