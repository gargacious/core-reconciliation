#pragma once

#include <cstdint>
#include <fintech/domain/execution.hpp>
#include <type_traits>

namespace fintech::reconciliation {

struct SourceTag;
using SourceId = core::StrongId<SourceTag, std::uint8_t>;

enum class SourceKind : std::uint8_t {
    Primary,
    OrderSession,
    DropCopy,
    Clearing,
    Count,
};

enum class EconomicEventKind : std::uint8_t {
    Execution,
    Correction,
    Bust,
    Count,
};

enum class SourceHealth : std::uint8_t {
    Registering,
    Current,
    Gap,
    Stale,
    Unhealthy,
    OwnerChanged,
};

enum class EconomicState : std::uint8_t {
    Uncertain,
    Reconciled,
    Conflict,
};

enum class ProjectionState : std::uint8_t {
    Incomplete,
    Current,
    Degraded,
    Conflict,
};

enum class ReconciliationDisposition : std::uint8_t {
    Registered,
    Recorded,
    Corroborated,
    Reconciled,
    DuplicateDelivery,
    DuplicateEconomicFact,
    CursorAdvanced,
    GapDetected,
    SequenceConflict,
    EconomicConflict,
    CursorRegression,
    SourceUnavailable,
    OwnerChanged,
    CapacityExceeded,
    Invalid,
};

enum class ConflictReason : std::uint8_t {
    SequenceReuse,
    EconomicMismatch,
    OwnerGenerationChanged,
    CursorRegression,
};

using SourceMask = std::uint64_t;

struct SourceIdentity final {
    SourceId source{};
    SourceKind kind{SourceKind::Count};
    core::VenueId venue{};
    core::SessionId delivery_session{};
    core::SessionId observed_session{};
    core::AccountId account{};
    core::SequenceDomainId sequence_domain{};
    core::ComponentId owner{};
    core::RuntimeInstanceId owner_instance{};
    std::uint64_t owner_generation{};
    core::SchemaId schema{};
    std::uint16_t schema_version{};
    core::ConfigFingerprint configuration{};

    friend constexpr bool operator==(const SourceIdentity&,
                                     const SourceIdentity&) noexcept = default;
};

struct SourceRegistration final {
    SourceIdentity identity{};
    std::uint64_t first_sequence{};
    bool required{};
};

struct EconomicIdentity final {
    EconomicEventKind kind{EconomicEventKind::Count};
    core::ExecutionId execution{};
    core::ExecutionAdjustmentId adjustment{};

    friend constexpr bool operator==(
        const EconomicIdentity&, const EconomicIdentity&) noexcept = default;
};

// Adjustment observations carry the original immutable execution fact so
// account/instrument/side and exact quantity domains remain independently
// comparable without querying the OMS projection.
struct EconomicFact final {
    EconomicEventKind kind{EconomicEventKind::Count};
    domain::ExecutionFact execution{};
    domain::ExecutionAdjustmentFact adjustment{};
};

struct EconomicObservation final {
    SourceId source{};
    std::uint64_t source_sequence{};
    core::EventId observed_event{};
    EconomicFact fact{};
    std::uint64_t source_payload_hash{};
    core::Timestamp observed_at{};
};

struct SourceCursorUpdate final {
    SourceId source{};
    std::uint64_t sequence_watermark{};
    core::Timestamp covered_through{};
    core::Timestamp observed_at{};
    core::RuntimeInstanceId owner_instance{};
    std::uint64_t owner_generation{};
    core::ConfigFingerprint configuration{};
    bool fresh{};
    bool healthy{};
};

struct SourceCursor final {
    SourceIdentity identity{};
    std::uint64_t first_sequence{};
    std::uint64_t last_sequence{};
    std::uint64_t sequence_watermark{};
    std::uint64_t gap_begin{};
    std::uint64_t gap_end{};
    core::Timestamp covered_through{};
    core::Timestamp observed_at{};
    SourceHealth health{SourceHealth::Registering};
    bool required{};
};

struct EconomicRecord final {
    EconomicIdentity identity{};
    EconomicFact fact{};
    SourceMask observed_sources{};
    EconomicState state{EconomicState::Uncertain};
    core::EventId first_event{};
    core::EventId last_event{};
    core::Timestamp first_observed_at{};
    core::Timestamp last_observed_at{};
    std::uint64_t version{};
};

struct ConflictRecord final {
    std::uint64_t conflict_id{};
    ConflictReason reason{ConflictReason::EconomicMismatch};
    SourceId source{};
    EconomicIdentity economic{};
    std::uint64_t expected_sequence{};
    std::uint64_t observed_sequence{};
    std::uint64_t expected_payload_hash{};
    std::uint64_t observed_payload_hash{};
    core::EventId event{};
    core::Timestamp detected_at{};
};

struct CompletenessWatermark final {
    ProjectionState state{ProjectionState::Incomplete};
    SourceMask required_sources{};
    SourceMask current_sources{};
    SourceMask gap_sources{};
    SourceMask degraded_sources{};
    core::Timestamp covered_through{};
    std::uint64_t epoch{};
    std::uint32_t economic_event_count{};
    std::uint32_t uncertain_event_count{};
    std::uint32_t conflict_count{};
};

struct ReconciliationResult final {
    core::Status status{};
    ReconciliationDisposition disposition{ReconciliationDisposition::Invalid};
    EconomicState economic_state{EconomicState::Uncertain};
    SourceId source{};
    EconomicIdentity economic{};
    std::uint64_t expected_sequence{};
    std::uint64_t observed_sequence{};
    std::uint64_t completeness_epoch{};
};

[[nodiscard]] constexpr EconomicIdentity identity(
    const EconomicFact& value) noexcept {
    return {value.kind, value.execution.execution,
            value.kind == EconomicEventKind::Execution
                ? core::ExecutionAdjustmentId{}
                : value.adjustment.adjustment};
}

[[nodiscard]] constexpr core::Timestamp event_time(
    const EconomicFact& value) noexcept {
    return value.kind == EconomicEventKind::Execution
               ? value.execution.execution_time
               : value.adjustment.adjustment_time;
}

[[nodiscard]] constexpr bool valid(const SourceIdentity& value) noexcept {
    if (!value.source.valid() || value.kind >= SourceKind::Count ||
        !value.venue.valid() || !value.delivery_session.valid() ||
        !value.account.valid() || !value.sequence_domain.valid() ||
        !value.owner.valid() || !value.owner_instance.valid() ||
        value.owner_generation == 0U || !value.schema.valid() ||
        value.schema_version == 0U || value.configuration.value == 0U) {
        return false;
    }
    if (value.kind == SourceKind::DropCopy)
        return value.observed_session.valid();
    if (value.kind == SourceKind::OrderSession)
        return !value.observed_session.valid() ||
               value.observed_session == value.delivery_session;
    return !value.observed_session.valid();
}

[[nodiscard]] constexpr bool valid(const SourceRegistration& value) noexcept {
    return valid(value.identity) && value.first_sequence != 0U;
}

[[nodiscard]] constexpr bool same_economics(
    const EconomicFact& left, const EconomicFact& right) noexcept {
    if (identity(left) != identity(right) ||
        !domain::same_execution_fact(left.execution, right.execution)) {
        return false;
    }
    return left.kind == EconomicEventKind::Execution ||
           domain::same_execution_adjustment_fact(left.adjustment,
                                                  right.adjustment);
}

[[nodiscard]] constexpr bool valid(const EconomicFact& value) noexcept {
    if (value.kind >= EconomicEventKind::Count ||
        !domain::valid(value.execution)) {
        return false;
    }
    if (value.kind == EconomicEventKind::Execution)
        return !value.adjustment.adjustment.valid();
    return domain::valid(value.adjustment) &&
           value.adjustment.execution == value.execution.execution &&
           ((value.kind == EconomicEventKind::Correction &&
             value.adjustment.kind ==
                 domain::ExecutionAdjustmentKind::Correction) ||
            (value.kind == EconomicEventKind::Bust &&
             value.adjustment.kind == domain::ExecutionAdjustmentKind::Bust));
}

[[nodiscard]] constexpr bool valid(const EconomicObservation& value) noexcept {
    return value.source.valid() && value.source_sequence != 0U &&
           value.observed_event.valid() && valid(value.fact) &&
           value.source_payload_hash != 0U &&
           value.observed_at.quality != core::ClockQuality::Unknown;
}

[[nodiscard]] constexpr bool valid(const SourceCursorUpdate& value) noexcept {
    return value.source.valid() && value.sequence_watermark != 0U &&
           value.covered_through.quality != core::ClockQuality::Unknown &&
           value.observed_at.quality != core::ClockQuality::Unknown &&
           core::comparable_clock_domain(value.covered_through,
                                         value.observed_at) &&
           value.observed_at.nanoseconds >= value.covered_through.nanoseconds &&
           value.owner_instance.valid() && value.owner_generation != 0U &&
           value.configuration.value != 0U;
}

static_assert(std::is_trivially_copyable_v<SourceIdentity>);
static_assert(std::is_standard_layout_v<SourceIdentity>);
static_assert(std::is_trivially_copyable_v<EconomicFact>);
static_assert(std::is_standard_layout_v<EconomicFact>);
static_assert(std::is_trivially_copyable_v<EconomicObservation>);
static_assert(std::is_standard_layout_v<EconomicObservation>);
static_assert(std::is_trivially_copyable_v<CompletenessWatermark>);
static_assert(std::is_standard_layout_v<CompletenessWatermark>);

}  // namespace fintech::reconciliation
