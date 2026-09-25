#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <fintech/reconciliation/contracts.hpp>
#include <limits>

namespace fintech::reconciliation {

template <
    std::size_t SourceCapacity = 16U, std::size_t EconomicCapacity = 1024U,
    std::size_t DeliveryCapacity = 4096U, std::size_t ConflictCapacity = 256U>
class BoundedReconciliationOwner final {
    static_assert(SourceCapacity > 0U && SourceCapacity <= 64U);
    static_assert(EconomicCapacity > 0U);
    static_assert(DeliveryCapacity > 0U);
    static_assert(ConflictCapacity > 0U);

   public:
    [[nodiscard]] ReconciliationResult register_source(
        const SourceRegistration& registration) noexcept {
        if (!valid(registration)) return invalid();
        if (const auto* existing = source(registration.identity.source)) {
            if (existing->identity == registration.identity &&
                existing->first_sequence == registration.first_sequence &&
                existing->required == registration.required) {
                return result(ReconciliationDisposition::DuplicateDelivery,
                              registration.identity.source);
            }
            return conflict(ConflictReason::OwnerGenerationChanged,
                            registration.identity.source, {},
                            existing->last_sequence + 1U,
                            registration.first_sequence, 0U, 0U, {}, {});
        }
        if (source_count_ >= SourceCapacity)
            return capacity(registration.identity.source);
        sources_[source_count_] = {
            registration.identity,
            registration.first_sequence,
            registration.first_sequence - 1U,
            registration.first_sequence - 1U,
            0U,
            0U,
            {},
            {},
            SourceHealth::Registering,
            registration.required,
        };
        const auto mask = source_mask(source_count_);
        if (registration.required) required_sources_ |= mask;
        ++source_count_;
        ++epoch_;
        return result(ReconciliationDisposition::Registered,
                      registration.identity.source);
    }

    [[nodiscard]] ReconciliationResult observe(
        const EconomicObservation& observation) noexcept {
        if (!valid(observation)) return invalid();
        const std::size_t source_index = find_source(observation.source);
        if (source_index == not_found) return unavailable(observation.source);
        SourceCursor& cursor = sources_[source_index];
        const std::uint64_t expected = cursor.last_sequence + 1U;

        if (const DeliveryRecord* duplicate =
                delivery(observation.source, observation.source_sequence)) {
            if (duplicate->economic == identity(observation.fact) &&
                duplicate->payload_hash == observation.source_payload_hash) {
                return result(ReconciliationDisposition::DuplicateDelivery,
                              observation.source, duplicate->economic, expected,
                              observation.source_sequence);
            }
            cursor.health = SourceHealth::Gap;
            return conflict(
                ConflictReason::SequenceReuse, observation.source,
                identity(observation.fact), expected,
                observation.source_sequence, duplicate->payload_hash,
                observation.source_payload_hash, observation.observed_event,
                observation.observed_at);
        }

        if (observation.source_sequence < expected) {
            cursor.health = SourceHealth::Gap;
            return conflict(ConflictReason::SequenceReuse, observation.source,
                            identity(observation.fact), expected,
                            observation.source_sequence, 0U,
                            observation.source_payload_hash,
                            observation.observed_event,
                            observation.observed_at);
        }
        if (observation.source_sequence > expected) {
            if (cursor.gap_begin == 0U) cursor.gap_begin = expected;
            if (observation.source_sequence - 1U > cursor.gap_end)
                cursor.gap_end = observation.source_sequence - 1U;
            cursor.health = SourceHealth::Gap;
            ++epoch_;
            return result(ReconciliationDisposition::GapDetected,
                          observation.source, identity(observation.fact),
                          expected, observation.source_sequence,
                          core::Status::failure(core::ErrorCode::SequenceGap));
        }

        if (delivery_count_ >= DeliveryCapacity)
            return capacity(observation.source, identity(observation.fact));
        const std::size_t economic_index =
            find_economic(identity(observation.fact));
        if (economic_index == not_found && economic_count_ >= EconomicCapacity)
            return capacity(observation.source, identity(observation.fact));

        EconomicState state = EconomicState::Uncertain;
        ReconciliationDisposition disposition =
            ReconciliationDisposition::Recorded;
        if (economic_index == not_found) {
            economics_[economic_count_] = {identity(observation.fact),
                                           observation.fact,
                                           source_mask(source_index),
                                           EconomicState::Uncertain,
                                           observation.observed_event,
                                           observation.observed_event,
                                           observation.observed_at,
                                           observation.observed_at,
                                           1U};
            state = refresh_economic_state(economics_[economic_count_]);
            ++economic_count_;
        } else {
            EconomicRecord& existing = economics_[economic_index];
            if (!same_economics(existing.fact, observation.fact)) {
                existing.state = EconomicState::Conflict;
                advance_delivery(cursor, observation, source_index);
                return conflict(
                    ConflictReason::EconomicMismatch, observation.source,
                    existing.identity, expected, observation.source_sequence,
                    economic_hash(existing.fact),
                    observation.source_payload_hash, observation.observed_event,
                    observation.observed_at);
            }
            const SourceMask mask = source_mask(source_index);
            if ((existing.observed_sources & mask) != 0U) {
                disposition = ReconciliationDisposition::DuplicateEconomicFact;
            } else {
                existing.observed_sources |= mask;
                disposition = ReconciliationDisposition::Corroborated;
            }
            existing.last_event = observation.observed_event;
            existing.last_observed_at = observation.observed_at;
            ++existing.version;
            state = refresh_economic_state(existing);
            if (state == EconomicState::Reconciled)
                disposition = ReconciliationDisposition::Reconciled;
        }

        advance_delivery(cursor, observation, source_index);
        return result(disposition, observation.source,
                      identity(observation.fact), expected,
                      observation.source_sequence, core::Status::success(),
                      state);
    }

    [[nodiscard]] ReconciliationResult advance(
        const SourceCursorUpdate& update) noexcept {
        if (!valid(update)) return invalid();
        const std::size_t source_index = find_source(update.source);
        if (source_index == not_found) return unavailable(update.source);
        SourceCursor& cursor = sources_[source_index];
        if (update.owner_instance != cursor.identity.owner_instance ||
            update.owner_generation != cursor.identity.owner_generation ||
            update.configuration != cursor.identity.configuration) {
            cursor.health = SourceHealth::OwnerChanged;
            return conflict(ConflictReason::OwnerGenerationChanged,
                            update.source, {}, cursor.sequence_watermark,
                            update.sequence_watermark, 0U, 0U, {},
                            update.observed_at);
        }
        const bool time_regressed =
            cursor.covered_through.quality != core::ClockQuality::Unknown &&
            (!core::comparable_clock_domain(cursor.covered_through,
                                            update.covered_through) ||
             update.covered_through.nanoseconds <
                 cursor.covered_through.nanoseconds);
        if (update.sequence_watermark < cursor.last_sequence ||
            update.sequence_watermark < cursor.sequence_watermark ||
            time_regressed) {
            cursor.health = SourceHealth::Gap;
            return conflict(ConflictReason::CursorRegression, update.source, {},
                            cursor.sequence_watermark,
                            update.sequence_watermark, 0U, 0U, {},
                            update.observed_at);
        }
        cursor.sequence_watermark = update.sequence_watermark;
        cursor.covered_through = update.covered_through;
        cursor.observed_at = update.observed_at;
        if (cursor.gap_begin != 0U) {
            cursor.health = SourceHealth::Gap;
        } else if (!update.healthy) {
            cursor.health = SourceHealth::Unhealthy;
        } else if (!update.fresh) {
            cursor.health = SourceHealth::Stale;
        } else {
            cursor.health = SourceHealth::Current;
        }
        ++epoch_;
        return result(ReconciliationDisposition::CursorAdvanced, update.source,
                      {}, cursor.last_sequence + 1U, update.sequence_watermark);
    }

    [[nodiscard]] CompletenessWatermark completeness() const noexcept {
        CompletenessWatermark value;
        value.required_sources = required_sources_;
        value.epoch = epoch_;
        value.economic_event_count =
            static_cast<std::uint32_t>(economic_count_);
        bool have_watermark = false;
        for (std::size_t index = 0U; index < source_count_; ++index) {
            const SourceCursor& cursor = sources_[index];
            const SourceMask mask = source_mask(index);
            if (cursor.health == SourceHealth::Current)
                value.current_sources |= mask;
            if (cursor.health == SourceHealth::Gap) value.gap_sources |= mask;
            if (cursor.health == SourceHealth::Stale ||
                cursor.health == SourceHealth::Unhealthy ||
                cursor.health == SourceHealth::OwnerChanged)
                value.degraded_sources |= mask;
            if (cursor.required && cursor.health == SourceHealth::Current) {
                if (!have_watermark || cursor.covered_through.nanoseconds <
                                           value.covered_through.nanoseconds) {
                    value.covered_through = cursor.covered_through;
                    have_watermark = true;
                }
            }
        }
        value.conflict_count = static_cast<std::uint32_t>(conflict_count_);
        const bool all_current =
            required_sources_ != 0U &&
            (value.current_sources & required_sources_) == required_sources_;
        if (all_current && have_watermark) {
            for (std::size_t index = 0U; index < economic_count_; ++index) {
                const auto& record = economics_[index];
                if (record.state == EconomicState::Conflict) continue;
                const auto time = event_time(record.fact);
                if (core::comparable_clock_domain(time,
                                                  value.covered_through) &&
                    time.nanoseconds <= value.covered_through.nanoseconds &&
                    (record.observed_sources & required_sources_) !=
                        required_sources_) {
                    ++value.uncertain_event_count;
                }
            }
        } else {
            value.uncertain_event_count =
                static_cast<std::uint32_t>(economic_count_);
        }

        if (conflict_count_ != 0U) {
            value.state = ProjectionState::Conflict;
        } else if (value.degraded_sources != 0U) {
            value.state = ProjectionState::Degraded;
        } else if (!all_current || value.gap_sources != 0U ||
                   value.uncertain_event_count != 0U) {
            value.state = ProjectionState::Incomplete;
        } else {
            value.state = ProjectionState::Current;
        }
        return value;
    }

    [[nodiscard]] const SourceCursor* source(SourceId id) const noexcept {
        const std::size_t index = find_source(id);
        return index == not_found ? nullptr : &sources_[index];
    }

    [[nodiscard]] const EconomicRecord* economic(
        EconomicIdentity id) const noexcept {
        const std::size_t index = find_economic(id);
        return index == not_found ? nullptr : &economics_[index];
    }

    [[nodiscard]] const ConflictRecord* conflict_at(
        std::size_t index) const noexcept {
        return index < conflict_count_ ? &conflicts_[index] : nullptr;
    }

    [[nodiscard]] constexpr std::size_t source_count() const noexcept {
        return source_count_;
    }
    [[nodiscard]] constexpr std::size_t economic_count() const noexcept {
        return economic_count_;
    }
    [[nodiscard]] constexpr std::size_t delivery_count() const noexcept {
        return delivery_count_;
    }
    [[nodiscard]] constexpr std::size_t conflict_count() const noexcept {
        return conflict_count_;
    }

   private:
    static constexpr std::size_t not_found =
        std::numeric_limits<std::size_t>::max();

    struct DeliveryRecord final {
        SourceId source{};
        std::uint64_t sequence{};
        EconomicIdentity economic{};
        std::uint64_t payload_hash{};
        core::EventId event{};
    };

    [[nodiscard]] static constexpr SourceMask source_mask(
        std::size_t index) noexcept {
        return SourceMask{1U} << index;
    }

    [[nodiscard]] std::size_t find_source(SourceId id) const noexcept {
        for (std::size_t index = 0U; index < source_count_; ++index)
            if (sources_[index].identity.source == id) return index;
        return not_found;
    }

    [[nodiscard]] std::size_t find_economic(
        EconomicIdentity id) const noexcept {
        for (std::size_t index = 0U; index < economic_count_; ++index)
            if (economics_[index].identity == id) return index;
        return not_found;
    }

    [[nodiscard]] const DeliveryRecord* delivery(
        SourceId source_id, std::uint64_t sequence) const noexcept {
        for (std::size_t index = 0U; index < delivery_count_; ++index)
            if (deliveries_[index].source == source_id &&
                deliveries_[index].sequence == sequence)
                return &deliveries_[index];
        return nullptr;
    }

    [[nodiscard]] EconomicState refresh_economic_state(
        EconomicRecord& record) const noexcept {
        if (record.state == EconomicState::Conflict) return record.state;
        record.state = required_sources_ != 0U &&
                               (record.observed_sources & required_sources_) ==
                                   required_sources_
                           ? EconomicState::Reconciled
                           : EconomicState::Uncertain;
        return record.state;
    }

    void advance_delivery(SourceCursor& cursor,
                          const EconomicObservation& observation,
                          std::size_t source_index) noexcept {
        deliveries_[delivery_count_++] = {
            observation.source, observation.source_sequence,
            identity(observation.fact), observation.source_payload_hash,
            observation.observed_event};
        cursor.last_sequence = observation.source_sequence;
        if (cursor.gap_begin != 0U &&
            observation.source_sequence == cursor.gap_begin) {
            ++cursor.gap_begin;
            if (cursor.gap_begin > cursor.gap_end) {
                cursor.gap_begin = 0U;
                cursor.gap_end = 0U;
                cursor.health = SourceHealth::Registering;
            }
        }
        (void)source_index;
        ++epoch_;
    }

    [[nodiscard]] static constexpr std::uint64_t economic_hash(
        const EconomicFact& fact) noexcept {
        std::uint64_t hash = 1469598103934665603ULL;
        const auto mix = [&hash](std::uint64_t value) constexpr noexcept {
            hash ^= value;
            hash *= 1099511628211ULL;
        };
        mix(static_cast<std::uint64_t>(fact.kind));
        mix(fact.execution.execution.value());
        mix(fact.execution.account.value());
        mix(fact.execution.instrument.value());
        mix(static_cast<std::uint64_t>(fact.execution.side));
        mix(static_cast<std::uint64_t>(fact.execution.price.mantissa));
        mix(fact.execution.price.scale);
        mix(fact.execution.quantity.mantissa);
        mix(fact.execution.quantity.scale);
        mix(static_cast<std::uint64_t>(fact.execution.quantity.unit));
        mix(fact.adjustment.adjustment.value());
        mix(static_cast<std::uint64_t>(fact.adjustment.kind));
        mix(static_cast<std::uint64_t>(
            fact.adjustment.replacement_price.mantissa));
        mix(fact.adjustment.replacement_quantity.mantissa);
        return hash;
    }

    [[nodiscard]] ReconciliationResult conflict(
        ConflictReason reason, SourceId source_id, EconomicIdentity economic_id,
        std::uint64_t expected, std::uint64_t observed,
        std::uint64_t expected_hash, std::uint64_t observed_hash,
        core::EventId event, core::Timestamp detected_at) noexcept {
        if (conflict_count_ >= ConflictCapacity)
            return capacity(source_id, economic_id);
        conflicts_[conflict_count_] = {
            conflict_count_ + 1U, reason,        source_id,
            economic_id,          expected,      observed,
            expected_hash,        observed_hash, event,
            detected_at};
        ++conflict_count_;
        ++epoch_;
        const auto disposition =
            reason == ConflictReason::EconomicMismatch
                ? ReconciliationDisposition::EconomicConflict
            : reason == ConflictReason::CursorRegression
                ? ReconciliationDisposition::CursorRegression
            : reason == ConflictReason::OwnerGenerationChanged
                ? ReconciliationDisposition::OwnerChanged
                : ReconciliationDisposition::SequenceConflict;
        return result(disposition, source_id, economic_id, expected, observed,
                      core::Status::failure(core::ErrorCode::InvalidState),
                      EconomicState::Conflict);
    }

    [[nodiscard]] ReconciliationResult result(
        ReconciliationDisposition disposition, SourceId source_id = {},
        EconomicIdentity economic_id = {}, std::uint64_t expected = 0U,
        std::uint64_t observed = 0U,
        core::Status status = core::Status::success(),
        EconomicState state = EconomicState::Uncertain) const noexcept {
        return {status,      disposition, state,    source_id,
                economic_id, expected,    observed, epoch_};
    }

    [[nodiscard]] ReconciliationResult invalid() const noexcept {
        return result(ReconciliationDisposition::Invalid, {}, {}, 0U, 0U,
                      core::Status::failure(core::ErrorCode::InvalidArgument));
    }

    [[nodiscard]] ReconciliationResult unavailable(
        SourceId source_id) const noexcept {
        return result(ReconciliationDisposition::SourceUnavailable, source_id,
                      {}, 0U, 0U,
                      core::Status::failure(core::ErrorCode::Unavailable));
    }

    [[nodiscard]] ReconciliationResult capacity(
        SourceId source_id, EconomicIdentity economic_id = {}) const noexcept {
        return result(ReconciliationDisposition::CapacityExceeded, source_id,
                      economic_id, 0U, 0U,
                      core::Status::failure(core::ErrorCode::CapacityExceeded));
    }

    std::array<SourceCursor, SourceCapacity> sources_{};
    std::array<EconomicRecord, EconomicCapacity> economics_{};
    std::array<DeliveryRecord, DeliveryCapacity> deliveries_{};
    std::array<ConflictRecord, ConflictCapacity> conflicts_{};
    std::size_t source_count_{};
    std::size_t economic_count_{};
    std::size_t delivery_count_{};
    std::size_t conflict_count_{};
    SourceMask required_sources_{};
    std::uint64_t epoch_{};
};

}  // namespace fintech::reconciliation
