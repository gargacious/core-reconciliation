#include <array>
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

SourceRegistration source(std::uint8_t id, SourceKind kind,
                          std::uint64_t delivery_session,
                          std::uint64_t observed_session,
                          std::uint64_t domain) {
    return {
        {SourceId{id}, kind, core::VenueId{7U},
         core::SessionId{delivery_session}, core::SessionId{observed_session},
         core::AccountId{11U}, core::SequenceDomainId{domain},
         core::ComponentId{20U + id}, core::RuntimeInstanceId{200U + id}, 1U,
         core::SchemaId{9U}, 1U, core::ConfigFingerprint{66U}},
        1U,
        true};
}

EconomicFact fill(std::uint64_t quantity = 20U) {
    return {EconomicEventKind::Execution,
            {core::ExecutionId{7001U},
             {core::ChildOrderId{41U}, core::ParentOrderId{31U},
              core::InternalOrderId{21U}},
             core::AccountId{11U},
             core::InstrumentId{101U},
             domain::Side::Buy,
             {123450, 4U},
             {quantity, 0U, core::QuantityUnit::Shares},
             time(100U)},
            {}};
}

EconomicObservation observation(SourceId source, std::uint64_t sequence,
                                std::uint64_t event,
                                EconomicFact fact = fill()) {
    return {source, sequence,       core::EventId{event},
            fact,   10000U + event, time(110U + event)};
}

SourceCursorUpdate checkpoint(const SourceRegistration& registration,
                              std::uint64_t watermark) {
    return {registration.identity.source,
            watermark,
            time(200U),
            time(210U),
            registration.identity.owner_instance,
            registration.identity.owner_generation,
            registration.identity.configuration,
            true,
            true};
}

}  // namespace

int main() {
    BoundedReconciliationOwner<8U, 16U, 32U, 8U> owner;
    const std::array sources{
        source(1U, SourceKind::Primary, 10U, 0U, 101U),
        source(2U, SourceKind::OrderSession, 100U, 100U, 102U),
        source(3U, SourceKind::DropCopy, 900U, 100U, 103U),
        source(4U, SourceKind::Clearing, 800U, 0U, 104U),
    };
    for (const auto& item : sources)
        check(owner.register_source(item).disposition ==
              ReconciliationDisposition::Registered);

    // Drop Copy can arrive first. Numeric sequence 1 in four independent
    // domains never acts as the business dedupe key.
    auto result = owner.observe(observation(SourceId{3U}, 1U, 31U));
    check(result.disposition == ReconciliationDisposition::Recorded);
    check(result.economic_state == EconomicState::Uncertain);
    check(owner.economic_count() == 1U);

    result = owner.observe(observation(SourceId{2U}, 1U, 21U));
    check(result.disposition == ReconciliationDisposition::Corroborated);
    result = owner.observe(observation(SourceId{1U}, 1U, 11U));
    check(result.disposition == ReconciliationDisposition::Corroborated);
    result = owner.observe(observation(SourceId{4U}, 1U, 41U));
    check(result.disposition == ReconciliationDisposition::Reconciled);
    check(result.economic_state == EconomicState::Reconciled);
    check(owner.economic_count() == 1U);
    check(owner.delivery_count() == 4U);

    for (const auto& item : sources)
        check(owner.advance(checkpoint(item, 1U)).status.ok());
    const auto complete = owner.completeness();
    check(complete.state == ProjectionState::Current);
    check(complete.uncertain_event_count == 0U);
    check(complete.covered_through.nanoseconds == 200U);

    // Exact delivery retry is idempotent and does not move any cursor.
    result = owner.observe(observation(SourceId{3U}, 1U, 31U));
    check(result.disposition == ReconciliationDisposition::DuplicateDelivery);
    check(owner.delivery_count() == 4U);

    // The same stable execution identity with different economics is retained
    // as an explicit conflict, never silently treated as another fill.
    result = owner.observe(observation(SourceId{4U}, 2U, 42U, fill(21U)));
    check(!result.status);
    check(result.disposition == ReconciliationDisposition::EconomicConflict);
    check(owner.conflict_count() == 1U);
    check(owner.completeness().state == ProjectionState::Conflict);
    check(owner.conflict_at(0U)->reason == ConflictReason::EconomicMismatch);
}
