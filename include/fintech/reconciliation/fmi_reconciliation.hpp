#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

#include <fintech/domain/fmi_lifecycle.hpp>

namespace fintech::reconciliation {

inline constexpr std::string_view fmi_reconciliation_contract_id{
    "core-reconciliation.fmi-reconciliation"};
inline constexpr std::uint16_t fmi_reconciliation_contract_revision = 1U;

struct FmiReconciliationScopeTag;
struct FmiTolerancePolicyTag;
struct FmiExceptionCaseTag;
struct FmiResolutionProposalTag;
struct FmiAssessmentTag;

using FmiReconciliationScopeId = core::StrongId<FmiReconciliationScopeTag>;
using FmiTolerancePolicyId = core::StrongId<FmiTolerancePolicyTag>;
using FmiExceptionCaseId = core::StrongId<FmiExceptionCaseTag>;
using FmiResolutionProposalId = core::StrongId<FmiResolutionProposalTag>;
using FmiAssessmentId = core::StrongId<FmiAssessmentTag>;

enum class FmiEvidenceRole : std::uint8_t {
    VenueTrade = 0U,
    ClearingLedger,
    SettlementObligation,
    PositionLedger,
    CsdPosition,
    CustodianSubledger,
    SettlementBankCash,
    ClientBrokerLedger,
    Count,
};

using FmiEvidenceRoleMask = std::uint16_t;

[[nodiscard]] constexpr FmiEvidenceRoleMask evidence_role_mask(
    FmiEvidenceRole role) noexcept {
    return role < FmiEvidenceRole::Count
               ? static_cast<FmiEvidenceRoleMask>(
                     FmiEvidenceRoleMask{1U}
                     << static_cast<std::uint8_t>(role))
               : 0U;
}

enum class FmiAssessmentKind : std::uint8_t {
    Intraday = 1U,
    EndOfDay,
    RestartOrFailover,
    PostSettlement,
    Correction,
};

enum class FmiBreakReason : std::uint8_t {
    MissingRequiredEvidence = 1U,
    QuantityMismatch,
    CashMismatch,
    ConflictingRetry,
    SourceSequenceGap,
    OwnerGenerationChanged,
    TolerancePolicyUnavailable,
};

enum class FmiCaseState : std::uint8_t {
    Open = 1U,
    PendingApproval,
    Escalated,
    Resolved,
};

// Resolution records govern the break only. They never authorize this owner to
// rewrite a venue, clearing, position, custody, cash or client-ledger fact.
enum class FmiResolutionAction : std::uint8_t {
    EscalateToEconomicOwner = 1U,
    CloseWithOwnerCorrectionEvidence,
    CloseAsAuthorizedTolerance,
};

enum class FmiReconciliationDisposition : std::uint8_t {
    ScopeRegistered = 1U,
    PolicyRegistered,
    EvidenceRecorded,
    DuplicateEvidence,
    Reconciled,
    CaseOpened,
    AssessmentCompleted,
    ResolutionProposed,
    ResolutionApproved,
    Invalid,
    ScopeUnavailable,
    PolicyUnavailable,
    StaleCaseVersion,
    ConflictingCommand,
    CapacityExceeded,
};

struct FmiReconciliationKey final {
    domain::CanonicalTradeId trade{};
    domain::CanonicalObligationId obligation{};
    core::AccountId account{};
    core::InstrumentId instrument{};
    std::uint32_t business_date{};

    friend constexpr bool operator==(
        const FmiReconciliationKey&, const FmiReconciliationKey&) noexcept =
        default;
};

struct FmiScopeRegistration final {
    FmiReconciliationScopeId scope{};
    FmiReconciliationKey key{};
    domain::AssetClass asset_class{domain::AssetClass::Equity};
    domain::ProductId product{};
    domain::JurisdictionId jurisdiction{};
    core::CurrencyCode currency{};
    core::QuantityUnit quantity_unit{core::QuantityUnit::Shares};
    FmiEvidenceRole quantity_anchor{FmiEvidenceRole::VenueTrade};
    FmiEvidenceRole cash_anchor{FmiEvidenceRole::SettlementObligation};
    FmiEvidenceRoleMask required_roles{};
    FmiEvidenceRoleMask quantity_roles{};
    FmiEvidenceRoleMask cash_roles{};

    friend constexpr bool operator==(
        const FmiScopeRegistration&, const FmiScopeRegistration&) noexcept =
        default;
};

// Policies are exact provider and economic-scope facts. Deltas are compared
// only at the declared scales, avoiding implicit rounding in this owner.
struct FmiTolerancePolicy final {
    FmiTolerancePolicyId policy{};
    domain::OrganizationId provider{};
    domain::AssetClass asset_class{domain::AssetClass::Equity};
    domain::ProductId product{};
    domain::JurisdictionId jurisdiction{};
    core::CurrencyCode currency{};
    core::QuantityUnit quantity_unit{core::QuantityUnit::Shares};
    std::uint8_t quantity_scale{};
    std::uint8_t cash_scale{};
    std::uint64_t maximum_quantity_delta{};
    std::uint64_t maximum_cash_delta{};
    core::ComponentId owner{};
    core::RuntimeInstanceId owner_instance{};
    std::uint64_t owner_generation{};
    core::ConfigFingerprint configuration{};
    core::EventId authorization_event{};
    core::Timestamp effective_from{};
    core::Timestamp effective_until{};
    std::uint64_t payload_digest{};

    friend constexpr bool operator==(
        const FmiTolerancePolicy&, const FmiTolerancePolicy&) noexcept =
        default;
};

struct FmiEvidenceObservation final {
    FmiReconciliationScopeId scope{};
    FmiEvidenceRole role{FmiEvidenceRole::Count};
    domain::OrganizationId provider{};
    domain::ServiceAuthorizationId authorization{};
    std::uint64_t authorization_generation{};
    core::ScaledQuantity quantity{};
    core::Money cash{};
    core::ComponentId owner{};
    core::RuntimeInstanceId owner_instance{};
    std::uint64_t owner_generation{};
    core::SequenceDomainId sequence_domain{};
    std::uint64_t sequence{};
    core::JournalOffset journal_offset{};
    core::ConfigFingerprint configuration{};
    core::EventId event{};
    std::uint64_t owner_state_version{};
    core::Timestamp observed_at{};
    std::uint64_t payload_digest{};
};

struct FmiAssessmentRequest final {
    FmiAssessmentId assessment{};
    FmiReconciliationScopeId scope{};
    FmiAssessmentKind kind{FmiAssessmentKind::Intraday};
    core::Timestamp assessed_at{};
    std::uint64_t evidence_digest{};

    friend constexpr bool operator==(
        const FmiAssessmentRequest&, const FmiAssessmentRequest&) noexcept =
        default;
};

struct FmiExceptionCase final {
    FmiExceptionCaseId exception_case{};
    FmiReconciliationScopeId scope{};
    FmiBreakReason reason{FmiBreakReason::MissingRequiredEvidence};
    FmiEvidenceRole anchor_role{FmiEvidenceRole::Count};
    FmiEvidenceRole observed_role{FmiEvidenceRole::Count};
    core::EventId anchor_event{};
    core::EventId observed_event{};
    std::uint64_t quantity_delta{};
    std::uint64_t cash_delta{};
    FmiTolerancePolicyId tolerance_policy{};
    FmiCaseState state{FmiCaseState::Open};
    std::uint64_t version{1U};
    core::Timestamp opened_at{};
    core::Timestamp updated_at{};
    FmiResolutionProposalId proposal{};
    FmiResolutionAction proposed_action{
        FmiResolutionAction::EscalateToEconomicOwner};
    core::PrincipalId proposer{};
    core::PrincipalId approver{};
    std::uint64_t resolution_evidence_digest{};
};

struct FmiResolutionProposal final {
    core::CommandId command{};
    FmiResolutionProposalId proposal{};
    FmiExceptionCaseId exception_case{};
    std::uint64_t expected_case_version{};
    FmiResolutionAction action{FmiResolutionAction::EscalateToEconomicOwner};
    core::PrincipalId proposer{};
    core::Timestamp proposed_at{};
    std::uint64_t evidence_digest{};
    std::uint64_t command_digest{};
};

struct FmiResolutionApproval final {
    core::CommandId command{};
    FmiResolutionProposalId proposal{};
    FmiExceptionCaseId exception_case{};
    std::uint64_t expected_case_version{};
    core::PrincipalId approver{};
    core::Timestamp approved_at{};
    std::uint64_t evidence_digest{};
    std::uint64_t command_digest{};
};

struct FmiReconciliationResult final {
    core::Status status{};
    FmiReconciliationDisposition disposition{
        FmiReconciliationDisposition::Invalid};
    FmiReconciliationScopeId scope{};
    FmiExceptionCaseId exception_case{};
    std::uint64_t scope_version{};
    std::uint32_t cases_opened{};
};

[[nodiscard]] constexpr bool valid(
    const FmiReconciliationKey& value) noexcept {
    return (value.trade.valid() || value.obligation.valid()) &&
           value.account.valid() && value.instrument.valid() &&
           value.business_date != 0U;
}

[[nodiscard]] constexpr bool valid(
    const FmiScopeRegistration& value) noexcept {
    const auto all_roles = static_cast<FmiEvidenceRoleMask>(
        (FmiEvidenceRoleMask{1U}
         << static_cast<std::uint8_t>(FmiEvidenceRole::Count)) -
        1U);
    return value.scope.valid() && valid(value.key) && value.product.valid() &&
           value.jurisdiction.valid() && value.currency.valid() &&
           value.asset_class >= domain::AssetClass::Equity &&
           value.asset_class <= domain::AssetClass::DigitalAsset &&
           value.quantity_unit <= core::QuantityUnit::Fractional &&
           value.required_roles != 0U &&
           (value.required_roles & ~all_roles) == 0U &&
           (value.quantity_roles & ~value.required_roles) == 0U &&
           (value.cash_roles & ~value.required_roles) == 0U &&
           (value.quantity_roles == 0U ||
            (value.quantity_roles & evidence_role_mask(value.quantity_anchor)) !=
                0U) &&
           (value.cash_roles == 0U ||
            (value.cash_roles & evidence_role_mask(value.cash_anchor)) != 0U);
}

[[nodiscard]] constexpr bool valid(
    const FmiTolerancePolicy& value) noexcept {
    return value.policy.valid() && value.provider.valid() &&
           value.product.valid() && value.jurisdiction.valid() &&
           value.currency.valid() &&
           value.asset_class >= domain::AssetClass::Equity &&
           value.asset_class <= domain::AssetClass::DigitalAsset &&
           value.quantity_unit <= core::QuantityUnit::Fractional &&
           value.quantity_scale <= core::maximum_decimal_scale &&
           value.cash_scale <= core::maximum_decimal_scale &&
           value.owner.valid() && value.owner_instance.valid() &&
           value.owner_generation != 0U && value.configuration.value != 0U &&
           value.authorization_event.valid() &&
           core::comparable_clock_domain(value.effective_from,
                                         value.effective_until) &&
           value.effective_from.nanoseconds < value.effective_until.nanoseconds &&
           value.payload_digest != 0U;
}

[[nodiscard]] constexpr bool valid(
    const FmiEvidenceObservation& value) noexcept {
    return value.scope.valid() && value.role < FmiEvidenceRole::Count &&
           value.provider.valid() && value.authorization.valid() &&
           value.authorization_generation != 0U &&
           value.quantity.scale <= core::maximum_decimal_scale &&
           value.quantity.unit <= core::QuantityUnit::Fractional &&
           value.cash.currency.valid() &&
           value.cash.scale <= core::maximum_decimal_scale &&
           value.owner.valid() && value.owner_instance.valid() &&
           value.owner_generation != 0U && value.sequence_domain.valid() &&
           value.sequence != 0U && value.journal_offset.valid() &&
           value.configuration.value != 0U && value.event.valid() &&
           value.owner_state_version != 0U &&
           value.observed_at.quality != core::ClockQuality::Unknown &&
           value.payload_digest != 0U;
}

[[nodiscard]] constexpr bool valid(
    const FmiAssessmentRequest& value) noexcept {
    return value.assessment.valid() && value.scope.valid() &&
           value.kind >= FmiAssessmentKind::Intraday &&
           value.kind <= FmiAssessmentKind::Correction &&
           value.assessed_at.quality != core::ClockQuality::Unknown &&
           value.evidence_digest != 0U;
}

[[nodiscard]] constexpr bool valid(
    const FmiResolutionProposal& value) noexcept {
    return value.command.valid() && value.proposal.valid() &&
           value.exception_case.valid() && value.expected_case_version != 0U &&
           value.action >= FmiResolutionAction::EscalateToEconomicOwner &&
           value.action <= FmiResolutionAction::CloseAsAuthorizedTolerance &&
           value.proposer.valid() &&
           value.proposed_at.quality != core::ClockQuality::Unknown &&
           value.evidence_digest != 0U && value.command_digest != 0U;
}

[[nodiscard]] constexpr bool valid(
    const FmiResolutionApproval& value) noexcept {
    return value.command.valid() && value.proposal.valid() &&
           value.exception_case.valid() && value.expected_case_version != 0U &&
           value.approver.valid() &&
           value.approved_at.quality != core::ClockQuality::Unknown &&
           value.evidence_digest != 0U && value.command_digest != 0U;
}

[[nodiscard]] constexpr bool same_evidence(
    const FmiEvidenceObservation& left,
    const FmiEvidenceObservation& right) noexcept {
    return left.scope == right.scope && left.role == right.role &&
           left.provider == right.provider &&
           left.authorization == right.authorization &&
           left.authorization_generation == right.authorization_generation &&
           left.quantity == right.quantity && left.cash == right.cash &&
           left.owner == right.owner &&
           left.owner_instance == right.owner_instance &&
           left.owner_generation == right.owner_generation &&
           left.sequence_domain == right.sequence_domain &&
           left.sequence == right.sequence &&
           left.journal_offset == right.journal_offset &&
           left.configuration == right.configuration &&
           left.event == right.event &&
           left.owner_state_version == right.owner_state_version &&
           left.observed_at == right.observed_at &&
           left.payload_digest == right.payload_digest;
}

static_assert(std::is_trivially_copyable_v<FmiEvidenceObservation>);
static_assert(std::is_trivially_copyable_v<FmiExceptionCase>);

}  // namespace fintech::reconciliation
