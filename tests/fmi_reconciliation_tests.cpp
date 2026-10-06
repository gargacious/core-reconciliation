#include <array>
#include <cstdlib>
#include <fintech/reconciliation/fmi_reconciliation_owner.hpp>

namespace {

using namespace fintech;
using namespace fintech::reconciliation;

void check(bool condition) {
    if (!condition) std::abort();
}

core::Timestamp time(std::uint64_t nanoseconds) {
    return {nanoseconds, core::ClockKind::Simulation,
            core::ClockQuality::Synchronized};
}

constexpr auto usd = core::CurrencyCode::from_iso('U', 'S', 'D');

FmiScopeRegistration scope(std::uint64_t id) {
    constexpr auto venue = evidence_role_mask(FmiEvidenceRole::VenueTrade);
    constexpr auto clearing =
        evidence_role_mask(FmiEvidenceRole::ClearingLedger);
    constexpr auto obligation =
        evidence_role_mask(FmiEvidenceRole::SettlementObligation);
    constexpr auto position =
        evidence_role_mask(FmiEvidenceRole::PositionLedger);
    constexpr auto csd = evidence_role_mask(FmiEvidenceRole::CsdPosition);
    constexpr auto custody =
        evidence_role_mask(FmiEvidenceRole::CustodianSubledger);
    constexpr auto bank =
        evidence_role_mask(FmiEvidenceRole::SettlementBankCash);
    constexpr auto client =
        evidence_role_mask(FmiEvidenceRole::ClientBrokerLedger);
    return {
        FmiReconciliationScopeId{id},
        {domain::CanonicalTradeId{id + 100U},
         domain::CanonicalObligationId{id + 200U},
         core::AccountId{static_cast<std::uint32_t>(id + 300U)},
         core::InstrumentId{static_cast<std::uint32_t>(id + 400U)},
         20261006U},
        domain::AssetClass::Equity,
        domain::ProductId{1U},
        domain::JurisdictionId{91U},
        usd,
        core::QuantityUnit::Shares,
        FmiEvidenceRole::VenueTrade,
        FmiEvidenceRole::SettlementObligation,
        static_cast<FmiEvidenceRoleMask>(venue | clearing | obligation |
                                         position | csd | custody | bank |
                                         client),
        static_cast<FmiEvidenceRoleMask>(venue | clearing | obligation |
                                         position | csd | custody | client),
        static_cast<FmiEvidenceRoleMask>(obligation | bank | client),
    };
}

FmiTolerancePolicy policy(std::uint64_t provider,
                          std::uint64_t cash_tolerance = 0U) {
    return {
        FmiTolerancePolicyId{provider},
        domain::OrganizationId{provider},
        domain::AssetClass::Equity,
        domain::ProductId{1U},
        domain::JurisdictionId{91U},
        usd,
        core::QuantityUnit::Shares,
        0U,
        0U,
        0U,
        cash_tolerance,
        core::ComponentId{900U},
        core::RuntimeInstanceId{901U},
        3U,
        core::ConfigFingerprint{902U},
        core::EventId{provider + 900U},
        time(1U),
        time(100000U),
        provider + 1000U,
    };
}

FmiEvidenceObservation observation(FmiReconciliationScopeId scope_id,
                                   FmiEvidenceRole role,
                                   std::uint64_t provider,
                                   std::uint64_t sequence,
                                   std::uint64_t quantity = 100U,
                                   std::int64_t cash = 10000) {
    const auto role_value = static_cast<std::uint64_t>(role);
    return {
        scope_id,
        role,
        domain::OrganizationId{provider},
        domain::ServiceAuthorizationId{provider + 10U},
        4U,
        {quantity, 0U, core::QuantityUnit::Shares},
        {cash, usd, 0U},
        core::ComponentId{static_cast<std::uint32_t>(provider + 100U)},
        core::RuntimeInstanceId{provider + 200U},
        7U,
        core::SequenceDomainId{provider + 300U},
        sequence,
        core::JournalOffset{provider + sequence + 400U},
        core::ConfigFingerprint{provider + 500U},
        core::EventId{provider * 100U + sequence + role_value},
        sequence,
        time(100U + provider + sequence),
        provider * 1000U + sequence + role_value,
    };
}

}  // namespace

int main() {
    static_assert(fmi_reconciliation_contract_revision == 1U);
    check(fmi_reconciliation_contract_id ==
          "core-reconciliation.fmi-reconciliation");

    BoundedFmiReconciliationOwner<4U, 24U, 48U, 24U> owner;
    const auto registration = scope(1U);
    check(owner.register_scope(registration).status.ok());
    for (std::uint64_t provider = 1U; provider <= 8U; ++provider) {
        check(owner.register_policy(
                   policy(provider, provider == 7U ? 5U : 0U))
                  .status.ok());
    }

    const std::array roles{
        FmiEvidenceRole::VenueTrade,
        FmiEvidenceRole::ClearingLedger,
        FmiEvidenceRole::SettlementObligation,
        FmiEvidenceRole::PositionLedger,
        FmiEvidenceRole::CsdPosition,
        FmiEvidenceRole::CustodianSubledger,
        FmiEvidenceRole::SettlementBankCash,
        FmiEvidenceRole::ClientBrokerLedger,
    };

    FmiEvidenceObservation bank{};
    for (std::size_t index = 0U; index < roles.size(); ++index) {
        const auto provider = static_cast<std::uint64_t>(index + 1U);
        auto evidence = observation(registration.scope, roles[index], provider,
                                    1U);
        if (roles[index] == FmiEvidenceRole::SettlementBankCash) {
            evidence.cash.mantissa = 9990;
            bank = evidence;
        }
        const auto result = owner.observe(evidence);
        check(result.status.ok());
        if (roles[index] == FmiEvidenceRole::SettlementBankCash) {
            check(result.disposition ==
                  FmiReconciliationDisposition::CaseOpened);
        }
    }
    check(owner.evidence_count() == 8U);
    check(owner.case_count() == 1U);
    const auto* cash_break = owner.exception_case(FmiExceptionCaseId{1U});
    check(cash_break != nullptr);
    check(cash_break->reason == FmiBreakReason::CashMismatch);
    check(cash_break->cash_delta == 10U);
    check(cash_break->tolerance_policy == FmiTolerancePolicyId{7U});

    // Exact delivery retry is idempotent and does not create another case or
    // replace the source-owned evidence.
    auto result = owner.observe(bank);
    check(result.status.ok());
    check(result.disposition ==
          FmiReconciliationDisposition::DuplicateEvidence);
    check(owner.evidence_count() == 8U);
    check(owner.case_count() == 1U);

    // The same source sequence with changed economics is retained as a new
    // governed break, never applied over the source fact.
    auto conflicting = bank;
    conflicting.cash.mantissa = 9000;
    conflicting.payload_digest += 1U;
    result = owner.observe(conflicting);
    check(!result.status);
    check(result.disposition == FmiReconciliationDisposition::CaseOpened);
    check(owner.evidence_count() == 8U);
    check(owner.case_count() == 2U);
    check(owner.exception_case(FmiExceptionCaseId{2U})->reason ==
          FmiBreakReason::ConflictingRetry);

    // Intraday/EOD/restart-style assessment is explicit. A complete scope
    // opens no missing-source case; an incomplete scope does.
    result = owner.assess({FmiAssessmentId{1U}, registration.scope,
                           FmiAssessmentKind::EndOfDay, time(1000U), 55U});
    check(result.status.ok());
    check(result.cases_opened == 0U);

    const auto incomplete = scope(2U);
    check(owner.register_scope(incomplete).status.ok());
    check(owner.register_policy(policy(21U)).status.ok());
    check(owner.observe(observation(incomplete.scope,
                                    FmiEvidenceRole::VenueTrade, 21U, 1U))
              .status.ok());
    result = owner.assess({FmiAssessmentId{2U}, incomplete.scope,
                           FmiAssessmentKind::RestartOrFailover, time(1100U),
                           56U});
    check(result.status.ok());
    check(result.cases_opened == 7U);

    auto sequence_gap = observation(incomplete.scope,
                                    FmiEvidenceRole::VenueTrade, 21U, 3U);
    result = owner.observe(sequence_gap);
    check(!result.status);
    check(owner.exception_case(result.exception_case)->reason ==
          FmiBreakReason::SourceSequenceGap);
    auto changed_owner = observation(incomplete.scope,
                                     FmiEvidenceRole::VenueTrade, 21U, 2U);
    changed_owner.owner_generation += 1U;
    result = owner.observe(changed_owner);
    check(!result.status);
    check(owner.exception_case(result.exception_case)->reason ==
          FmiBreakReason::OwnerGenerationChanged);

    // Maker/checker resolution changes only the exception case. It leaves all
    // eight retained economic facts byte-for-byte owned by their publishers.
    const auto evidence_before = *owner.evidence_at(6U);
    const FmiResolutionProposal proposal{
        core::CommandId{501U}, FmiResolutionProposalId{601U},
        FmiExceptionCaseId{1U}, cash_break->version,
        FmiResolutionAction::CloseWithOwnerCorrectionEvidence,
        core::PrincipalId{701U}, time(1200U), 8001U, 9001U};
    result = owner.propose_resolution(proposal);
    check(result.status.ok());
    check(result.disposition ==
          FmiReconciliationDisposition::ResolutionProposed);
    const auto proposed_version = result.scope_version;

    // Exact command retry is stable; a different approver is mandatory.
    const auto proposal_retry = owner.propose_resolution(proposal);
    check(proposal_retry.status.ok());
    check(proposal_retry.scope_version == proposed_version);

    result = owner.approve_resolution(
        {core::CommandId{502U}, FmiResolutionProposalId{601U},
         FmiExceptionCaseId{1U}, proposed_version, core::PrincipalId{701U},
         time(1300U), 8002U, 9002U});
    check(!result.status);

    result = owner.approve_resolution(
        {core::CommandId{503U}, FmiResolutionProposalId{601U},
         FmiExceptionCaseId{1U}, proposed_version, core::PrincipalId{702U},
         time(1301U), 8003U, 9003U});
    check(result.status.ok());
    check(owner.exception_case(FmiExceptionCaseId{1U})->state ==
          FmiCaseState::Resolved);
    check(same_evidence(*owner.evidence_at(6U), evidence_before));
    check(owner.evidence_count() == 9U);

    return 0;
}
