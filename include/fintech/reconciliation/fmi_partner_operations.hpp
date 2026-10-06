#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include <fintech/domain/fmi_partner_operations.hpp>
#include <fintech/reconciliation/fmi_reconciliation.hpp>

namespace fintech::reconciliation {

inline constexpr std::string_view fmi_partner_operations_contract_id{
    "core-reconciliation.fmi-partner-operations"};
inline constexpr std::uint16_t fmi_partner_operations_contract_revision = 1U;
inline constexpr std::size_t fmi_partner_page_capacity =
    domain::maximum_partner_operations_page_items;

static_assert(domain::fmi_partner_operations_contract_revision == 1U);

enum class FmiPartnerPageKind : std::uint8_t {
    Scope = 1U,
    Evidence,
    SourceCompleteness,
    Break,
    Case,
    ResolutionAudit,
};

enum class FmiPartnerSourceCompleteness : std::uint8_t {
    Current = 1U,
    Missing,
    Gap,
    OwnerGenerationChanged,
};

enum class FmiPartnerWorkflowDisposition : std::uint8_t {
    Accepted = 1U,
    Duplicate,
    Rejected,
};

struct FmiPartnerProjectionAuthority final {
    core::ComponentId owner{};
    core::RuntimeInstanceId owner_instance{};
    std::uint64_t owner_generation{};
    core::ConfigFingerprint configuration{};
    core::SchemaId schema{};
    std::uint16_t schema_revision{};
    domain::ProjectionEntityId entity_base{};
    domain::ViewStreamId stream_base{};
    core::SequenceDomainId sequence_domain{};
};

struct FmiPartnerScopeBinding final {
    domain::PartnerOperationsScope operations{};
    FmiReconciliationScopeId reconciliation_scope{};
    domain::WorkspaceScope workspace_scope{};
    std::uint64_t capability_digest{};
};

// Identity owners validate grants. This record only retains their current
// decision/generation/expiry so this owner can reject stale or revoked work.
struct FmiPartnerAuthorizationState final {
    domain::PartnerOperationsScope operations{};
    domain::AuthorizationDecisionId decision{};
    std::uint64_t generation{};
    std::uint64_t capability_digest{};
    core::Timestamp expires_at{};
    core::EventId decision_event{};
    bool active{};
};

// The approval owner remains external. This immutable binding is the minimum
// provenance needed to admit a payload-bound maker/checker command.
struct FmiPartnerApprovalBinding final {
    domain::ApprovalReferenceId approval{};
    domain::PartnerOperationsScope operations{};
    domain::PartnerOperationsCaseId case_id{};
    std::uint64_t expected_case_version{};
    core::PrincipalId proposer{};
    core::PrincipalId approver{};
    domain::AuthorizationDecisionId authorization_decision{};
    std::uint64_t authorization_generation{};
    std::uint64_t payload_digest{};
    std::uint64_t evidence_digest{};
    core::EventId approval_event{};
    core::Timestamp expires_at{};
    domain::PartnerCaseWorkflowAction action{
        domain::PartnerCaseWorkflowAction::Propose};
    bool active{};
};

struct FmiPartnerWorkflowRequest final {
    domain::PartnerCaseWorkflowCommand command{};
    FmiResolutionProposalId proposal{};
    FmiResolutionAction resolution_action{
        FmiResolutionAction::EscalateToEconomicOwner};
};

struct FmiPartnerWorkflowResult final {
    core::Status status{core::Status::failure(core::ErrorCode::Unavailable)};
    FmiPartnerWorkflowDisposition disposition{
        FmiPartnerWorkflowDisposition::Rejected};
    FmiReconciliationResult reconciliation{};
    domain::PartnerOperationsAuditId audit{};
};

struct FmiPartnerPageRequest final {
    domain::WorkspaceQueryEnvelope query{};
    domain::PartnerOperationsScope operations{};
    FmiReconciliationScopeId reconciliation_scope{};
    std::uint32_t start_index{};
    FmiPartnerPageKind kind{FmiPartnerPageKind::Scope};
};

struct FmiPartnerScopeView final {
    domain::PartnerOperationsScope operations{};
    FmiReconciliationScopeId reconciliation_scope{};
    FmiReconciliationKey key{};
    FmiEvidenceRoleMask required_roles{};
    FmiEvidenceRoleMask present_roles{};
    std::uint64_t scope_version{};
    std::uint64_t evidence_digest{};
    domain::AuthorizationDecisionId authorization_decision{};
    std::uint64_t authorization_generation{};
    core::Timestamp as_of{};
};

// Evidence views intentionally expose custody/provenance only. Source-owned
// quantity, cash and obligation values are never copied into this API.
struct FmiPartnerEvidenceView final {
    domain::PartnerOperationsScope operations{};
    FmiReconciliationScopeId reconciliation_scope{};
    FmiEvidenceRole role{FmiEvidenceRole::Count};
    domain::OrganizationId provider{};
    domain::ServiceAuthorizationId service_authorization{};
    std::uint64_t service_authorization_generation{};
    core::ComponentId owner{};
    core::RuntimeInstanceId owner_instance{};
    std::uint64_t owner_generation{};
    core::SequenceDomainId sequence_domain{};
    std::uint64_t sequence{};
    core::JournalOffset journal_offset{};
    core::ConfigFingerprint configuration{};
    core::EventId event{};
    std::uint64_t owner_state_version{};
    std::uint64_t payload_digest{};
    core::Timestamp observed_at{};
};

struct FmiPartnerSourceCompletenessView final {
    domain::PartnerOperationsScope operations{};
    FmiReconciliationScopeId reconciliation_scope{};
    FmiEvidenceRole role{FmiEvidenceRole::Count};
    core::ComponentId owner{};
    std::uint64_t owner_generation{};
    core::SequenceDomainId sequence_domain{};
    std::uint64_t watermark{};
    core::ConfigFingerprint configuration{};
    core::EventId evidence_event{};
    std::uint64_t evidence_digest{};
    core::Timestamp observed_at{};
    FmiPartnerSourceCompleteness completeness{
        FmiPartnerSourceCompleteness::Missing};
};

struct FmiPartnerBreakView final {
    domain::PartnerOperationsScope operations{};
    FmiExceptionCaseId exception_case{};
    FmiReconciliationScopeId reconciliation_scope{};
    FmiBreakReason reason{FmiBreakReason::MissingRequiredEvidence};
    FmiEvidenceRole anchor_role{FmiEvidenceRole::Count};
    FmiEvidenceRole observed_role{FmiEvidenceRole::Count};
    core::EventId anchor_event{};
    core::EventId observed_event{};
    std::uint64_t case_version{};
    std::uint64_t evidence_digest{};
    core::Timestamp detected_at{};
};

struct FmiPartnerCaseView final {
    domain::PartnerOperationsScope operations{};
    FmiExceptionCaseId exception_case{};
    FmiReconciliationScopeId reconciliation_scope{};
    FmiCaseState state{FmiCaseState::Open};
    std::uint64_t case_version{};
    std::uint64_t evidence_digest{};
    core::PrincipalId proposer{};
    core::PrincipalId approver{};
    domain::ApprovalReferenceId approval{};
    domain::AuthorizationDecisionId authorization_decision{};
    std::uint64_t authorization_generation{};
    std::uint64_t payload_digest{};
    core::Timestamp updated_at{};
};

template <typename Item>
struct FmiPartnerPage final {
    domain::WorkspaceProjectionEnvelope envelope{};
    domain::PartnerOperationsCursor cursor{};
    std::array<Item, fmi_partner_page_capacity> items{};
};

template <typename Page>
struct FmiPartnerPageResult final {
    core::Status status{core::Status::failure(core::ErrorCode::Unavailable)};
    Page page{};
};

using FmiPartnerScopePage = FmiPartnerPage<FmiPartnerScopeView>;
using FmiPartnerEvidencePage = FmiPartnerPage<FmiPartnerEvidenceView>;
using FmiPartnerSourceCompletenessPage =
    FmiPartnerPage<FmiPartnerSourceCompletenessView>;
using FmiPartnerBreakPage = FmiPartnerPage<FmiPartnerBreakView>;
using FmiPartnerCasePage = FmiPartnerPage<FmiPartnerCaseView>;
using FmiPartnerResolutionAuditPage =
    FmiPartnerPage<domain::PartnerOperationsAuditView>;

[[nodiscard]] constexpr bool same_operations_scope(
    const domain::PartnerOperationsScope& left,
    const domain::PartnerOperationsScope& right) noexcept {
    return left.workspace == right.workspace &&
           left.authorization == right.authorization &&
           left.capability == right.capability &&
           left.protocol_profile == right.protocol_profile;
}

[[nodiscard]] constexpr bool valid(
    const FmiPartnerProjectionAuthority& value) noexcept {
    return value.owner.valid() && value.owner_instance.valid() &&
           value.owner_generation != 0U && value.configuration.value != 0U &&
           value.schema.valid() && value.schema_revision != 0U &&
           value.entity_base.valid() && value.stream_base.valid() &&
           value.sequence_domain.valid();
}

[[nodiscard]] constexpr bool valid(
    const FmiPartnerScopeBinding& value) noexcept {
    return domain::valid(value.operations) &&
           value.reconciliation_scope.valid() &&
           domain::valid(value.workspace_scope) &&
           value.capability_digest != 0U;
}

[[nodiscard]] constexpr bool valid(
    const FmiPartnerAuthorizationState& value) noexcept {
    return domain::valid(value.operations) && value.decision.valid() &&
           value.generation != 0U && value.capability_digest != 0U &&
           domain::valid_workspace_timestamp(value.expires_at) &&
           value.decision_event.valid();
}

[[nodiscard]] constexpr bool valid(
    const FmiPartnerApprovalBinding& value) noexcept {
    return value.approval.valid() && domain::valid(value.operations) &&
           value.case_id.valid() && value.expected_case_version != 0U &&
           value.proposer.valid() && value.approver.valid() &&
           value.proposer != value.approver &&
           value.authorization_decision.valid() &&
           value.authorization_generation != 0U &&
           value.payload_digest != 0U && value.evidence_digest != 0U &&
           value.approval_event.valid() &&
           domain::valid_workspace_timestamp(value.expires_at) &&
           value.action >= domain::PartnerCaseWorkflowAction::Propose &&
           value.action <= domain::PartnerCaseWorkflowAction::Escalate;
}

[[nodiscard]] constexpr bool valid(
    const FmiPartnerWorkflowRequest& value) noexcept {
    if (!domain::valid(value.command) || !value.proposal.valid()) return false;
    if (value.command.action == domain::PartnerCaseWorkflowAction::Escalate) {
        return value.resolution_action ==
               FmiResolutionAction::EscalateToEconomicOwner;
    }
    return value.command.action == domain::PartnerCaseWorkflowAction::Propose ||
           value.command.action == domain::PartnerCaseWorkflowAction::Approve;
}

[[nodiscard]] constexpr bool valid(
    const FmiPartnerPageRequest& value) noexcept {
    return domain::valid(value.query) && domain::valid(value.operations) &&
           value.reconciliation_scope.valid() &&
           value.query.maximum_items <= fmi_partner_page_capacity &&
           value.kind >= FmiPartnerPageKind::Scope &&
           value.kind <= FmiPartnerPageKind::ResolutionAudit &&
           ((value.query.mode == domain::WorkspaceQueryMode::Snapshot &&
             value.start_index == 0U) ||
            (value.query.mode == domain::WorkspaceQueryMode::Resume &&
             value.start_index != 0U));
}

template <typename T>
concept HasEconomicQuantity = requires(T value) { value.quantity; };

template <typename T>
concept HasEconomicCash = requires(T value) { value.cash; };

static_assert(!HasEconomicQuantity<FmiPartnerEvidenceView>);
static_assert(!HasEconomicCash<FmiPartnerEvidenceView>);
static_assert(std::is_trivially_copyable_v<FmiPartnerEvidenceView>);

}  // namespace fintech::reconciliation
