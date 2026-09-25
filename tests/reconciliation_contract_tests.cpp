#include <cstdlib>
#include <fintech/reconciliation/contracts.hpp>
#include <type_traits>

namespace {

using namespace fintech;
using namespace fintech::reconciliation;

void check(bool condition) {
    if (!condition) std::abort();
}

core::Timestamp time(std::uint64_t ns) {
    return {ns, core::ClockKind::Simulation, core::ClockQuality::Synchronized};
}

domain::ExecutionFact execution() {
    return {core::ExecutionId{7001U},
            {core::ChildOrderId{41U}, core::ParentOrderId{31U},
             core::InternalOrderId{21U}},
            core::AccountId{11U},
            core::InstrumentId{101U},
            domain::Side::Buy,
            {123450, 4U},
            {20U, 0U, core::QuantityUnit::Shares},
            time(100U)};
}

}  // namespace

int main() {
    static_assert(std::is_trivially_copyable_v<SourceIdentity>);
    static_assert(std::is_trivially_copyable_v<EconomicObservation>);

    SourceIdentity drop_copy{SourceId{1U},
                             SourceKind::DropCopy,
                             core::VenueId{7U},
                             core::SessionId{900U},
                             core::SessionId{100U},
                             core::AccountId{11U},
                             core::SequenceDomainId{80U},
                             core::ComponentId{4U},
                             core::RuntimeInstanceId{40U},
                             3U,
                             core::SchemaId{9U},
                             1U,
                             core::ConfigFingerprint{66U}};
    check(valid(drop_copy));
    drop_copy.observed_session = {};
    check(!valid(drop_copy));

    EconomicFact original{EconomicEventKind::Execution, execution(), {}};
    check(valid(original));
    check(identity(original).execution == core::ExecutionId{7001U});
    check(!identity(original).adjustment.valid());

    EconomicFact correction{EconomicEventKind::Correction, execution(), {}};
    correction.adjustment = {core::ExecutionAdjustmentId{9001U},
                             core::ExecutionId{7001U},
                             {},
                             domain::ExecutionAdjustmentKind::Correction,
                             {123400, 4U},
                             {19U, 0U, core::QuantityUnit::Shares},
                             time(200U)};
    check(valid(correction));
    check(identity(correction).adjustment ==
          core::ExecutionAdjustmentId{9001U});

    EconomicFact bust = correction;
    bust.kind = EconomicEventKind::Bust;
    bust.adjustment.kind = domain::ExecutionAdjustmentKind::Bust;
    bust.adjustment.adjustment = core::ExecutionAdjustmentId{9002U};
    bust.adjustment.previous_adjustment = core::ExecutionAdjustmentId{9001U};
    bust.adjustment.replacement_quantity.mantissa = 0U;
    check(valid(bust));

    correction.adjustment.execution = core::ExecutionId{999U};
    check(!valid(correction));
}
