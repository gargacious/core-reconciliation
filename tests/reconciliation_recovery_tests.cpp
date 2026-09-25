#include <cstdlib>
#include <fintech/reconciliation/owner.hpp>

namespace {

using namespace fintech;
using namespace fintech::reconciliation;

void check(bool condition) {
    if (!condition) std::abort();
}

core::Timestamp time(std::uint64_t ns) {
    return {ns, core::ClockKind::Simulation, core::ClockQuality::Synchronized};
}

SourceRegistration source(std::uint8_t id, std::uint64_t session,
                          std::uint64_t domain) {
    return {{SourceId{id}, SourceKind::OrderSession, core::VenueId{7U},
             core::SessionId{session}, core::SessionId{session},
             core::AccountId{11U}, core::SequenceDomainId{domain},
             core::ComponentId{30U + id}, core::RuntimeInstanceId{300U + id},
             7U, core::SchemaId{9U}, 1U, core::ConfigFingerprint{66U}},
            10U,
            true};
}

EconomicFact fact(std::uint64_t execution) {
    return {EconomicEventKind::Execution,
            {core::ExecutionId{execution},
             {core::ChildOrderId{execution + 100U},
              core::ParentOrderId{execution + 200U},
              core::InternalOrderId{execution + 300U}},
             core::AccountId{11U},
             core::InstrumentId{101U},
             domain::Side::Sell,
             {9900, 2U},
             {5U, 0U, core::QuantityUnit::Shares},
             time(execution)},
            {}};
}

EconomicObservation observation(SourceId source, std::uint64_t sequence,
                                std::uint64_t execution) {
    return {
        source,          sequence,          core::EventId{1000U + execution},
        fact(execution), 9000U + execution, time(2000U + execution)};
}

}  // namespace

int main() {
    BoundedReconciliationOwner<4U, 16U, 32U, 8U> owner;
    const auto session_a = source(1U, 100U, 501U);
    const auto session_b = source(2U, 200U, 502U);
    check(owner.register_source(session_a).status.ok());
    check(owner.register_source(session_b).status.ok());

    check(owner.observe(observation(SourceId{1U}, 10U, 1U)).status.ok());
    check(owner.observe(observation(SourceId{2U}, 10U, 1U)).status.ok());

    // Sequence 12 exposes only session A's missing 11; session B remains an
    // independent complete domain despite using the same numeric sequences.
    auto result = owner.observe(observation(SourceId{1U}, 12U, 3U));
    check(!result.status);
    check(result.disposition == ReconciliationDisposition::GapDetected);
    check(owner.source(SourceId{1U})->gap_begin == 11U);
    check(owner.source(SourceId{2U})->gap_begin == 0U);

    result = owner.observe(observation(SourceId{1U}, 11U, 2U));
    check(result.status.ok());
    check(owner.source(SourceId{1U})->gap_begin == 0U);
    result = owner.observe(observation(SourceId{1U}, 12U, 3U));
    check(result.status.ok());

    SourceCursorUpdate a_update{SourceId{1U},
                                12U,
                                time(50U),
                                time(60U),
                                session_a.identity.owner_instance,
                                session_a.identity.owner_generation,
                                session_a.identity.configuration,
                                true,
                                true};
    SourceCursorUpdate b_update{SourceId{2U},
                                10U,
                                time(50U),
                                time(60U),
                                session_b.identity.owner_instance,
                                session_b.identity.owner_generation,
                                session_b.identity.configuration,
                                true,
                                true};
    check(owner.advance(a_update).status.ok());
    check(owner.advance(b_update).status.ok());
    check(owner.completeness().state == ProjectionState::Incomplete);

    // A changed owner generation and a cursor regression are explicit
    // conflicts; neither can certify a current completeness watermark.
    auto changed_owner = a_update;
    ++changed_owner.owner_generation;
    result = owner.advance(changed_owner);
    check(!result.status);
    check(result.disposition == ReconciliationDisposition::OwnerChanged);

    auto regressed = b_update;
    regressed.sequence_watermark = 9U;
    result = owner.advance(regressed);
    check(!result.status);
    check(result.disposition == ReconciliationDisposition::CursorRegression);

    // Stable correction identity deduplicates independently from its original
    // execution identity and retains the exact replacement economics.
    EconomicFact correction = fact(1U);
    correction.kind = EconomicEventKind::Correction;
    correction.adjustment = {core::ExecutionAdjustmentId{77U},
                             core::ExecutionId{1U},
                             {},
                             domain::ExecutionAdjustmentKind::Correction,
                             {9800, 2U},
                             {4U, 0U, core::QuantityUnit::Shares},
                             time(70U)};
    EconomicObservation corrected{SourceId{2U}, 11U,   core::EventId{2001U},
                                  correction,   7070U, time(80U)};
    check(owner.observe(corrected).status.ok());
    check(owner.economic(identity(correction)) != nullptr);
}
