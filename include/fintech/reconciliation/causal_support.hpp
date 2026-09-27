#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <fintech/core/bounded_diagnostic_store.hpp>
#include <fintech/domain/causal_diagnostics.hpp>
#include <fintech/reconciliation/contracts.hpp>

namespace fintech::reconciliation {

// A retained pointer to source-owned evidence. The payload is deliberately a
// descriptor: this projection cannot create or replace the referenced order,
// execution, trade, position, obligation or settlement state.
struct RetainedCausalSupport final {
    domain::CausalDiagnosticRecordId record{};
    SourceId source{};
    std::uint64_t source_sequence{};
    domain::DiagnosticPrivacyClass privacy{
        domain::DiagnosticPrivacyClass::Unknown};
    domain::VisibilityClassification visibility{};
    domain::CausalContext causal{};
    core::CorrelationId correlation{};
    domain::CausalDiagnosticEvidence evidence{};
    domain::DiagnosticPayloadDescriptor payload{};
    domain::CausalDiagnosticRecordField fields{
        domain::CausalDiagnosticRecordField::None};
    domain::DiagnosticTraceId trace{};
};

enum class CausalSupportDisposition : std::uint8_t {
    SourceRegistered,
    DuplicateSource,
    Recorded,
    CursorAdvanced,
    Invalid,
    SourceUnavailable,
    OwnerChanged,
    SequenceGap,
    CursorRegression,
    EvidenceConflict,
    CapacityExceeded,
};

struct CausalSupportResult final {
    core::Status status{};
    CausalSupportDisposition disposition{CausalSupportDisposition::Invalid};
    SourceId source{};
    std::uint64_t expected_sequence{};
    std::uint64_t observed_sequence{};
    std::uint64_t projection_epoch{};
};

template <std::size_t PageCapacity>
struct CausalSupportQueryResult final {
    core::Status status{};
    domain::CausalDiagnosticResultPage<PageCapacity> page{};
    std::uint64_t projection_epoch{};
};

[[nodiscard]] constexpr domain::CausalDiagnosticRecord diagnostic_record(
    const RetainedCausalSupport& retained,
    domain::CausalDiagnosticQueryId query) noexcept {
    return {
        .record = retained.record,
        .query = query,
        .privacy = retained.privacy,
        .visibility = retained.visibility,
        .causal = retained.causal,
        .correlation = retained.correlation,
        .evidence = retained.evidence,
        .payload = retained.payload,
        .fields = retained.fields,
        .trace = retained.trace,
    };
}

[[nodiscard]] constexpr bool valid(
    const RetainedCausalSupport& retained) noexcept {
    return retained.source.valid() && retained.source_sequence != 0U &&
           retained.source_sequence == retained.evidence.owner_cursor &&
           domain::valid(diagnostic_record(
               retained, domain::CausalDiagnosticQueryId{1U}));
}

struct CausalSupportTraits final {
    [[nodiscard]] static constexpr bool valid(
        const RetainedCausalSupport& retained) noexcept {
        return reconciliation::valid(retained);
    }

    [[nodiscard]] static constexpr core::DiagnosticEvidenceKey key(
        const RetainedCausalSupport& retained) noexcept {
        return {
            retained.record.value(),
            retained.evidence.owner.component.value(),
            retained.evidence.sequence_domain.value(),
            retained.evidence.owner.epoch,
            retained.evidence.owner_cursor,
            retained.evidence.durable_offset.value(),
        };
    }

    [[nodiscard]] static constexpr bool valid_query(
        const domain::CausalDiagnosticQuery& query) noexcept {
        return domain::valid(query);
    }

    [[nodiscard]] static constexpr std::size_t maximum_results(
        const domain::CausalDiagnosticQuery& query) noexcept {
        return query.maximum_results;
    }

    [[nodiscard]] static constexpr bool matches(
        const domain::CausalDiagnosticQuery& query,
        const RetainedCausalSupport& retained) noexcept {
        if (!domain::valid(query) || !valid(retained) ||
            (query.requested_privacy &
             domain::diagnostic_privacy_mask(retained.privacy)) == 0U) {
            return false;
        }
        return (!domain::has_field(
                    query.fields,
                    domain::CausalDiagnosticQueryField::Workflow) ||
                query.workflow == retained.causal.workflow) &&
               (!domain::has_field(
                    query.fields,
                    domain::CausalDiagnosticQueryField::Command) ||
                query.command == retained.causal.command) &&
               (!domain::has_field(
                    query.fields,
                    domain::CausalDiagnosticQueryField::Correlation) ||
                query.correlation == retained.correlation) &&
               (!domain::has_field(
                    query.fields,
                    domain::CausalDiagnosticQueryField::Trace) ||
                (domain::has_field(
                     retained.fields,
                     domain::CausalDiagnosticRecordField::Trace) &&
                 query.trace == retained.trace));
    }
};

template <std::size_t SourceCapacity = 16U,
          std::size_t EvidenceCapacity = 1024U>
class BoundedCausalSupportIndex final {
    static_assert(SourceCapacity > 0U && SourceCapacity <= 64U);
    static_assert(EvidenceCapacity > 0U);

   public:
    [[nodiscard]] CausalSupportResult register_source(
        const SourceRegistration& registration) noexcept {
        if (!reconciliation::valid(registration)) {
            return result(CausalSupportDisposition::Invalid, {}, 0U, 0U,
                          core::Status::failure(
                              core::ErrorCode::InvalidArgument));
        }
        if (const auto* existing = source(registration.identity.source)) {
            if (existing->registration.identity == registration.identity &&
                existing->registration.first_sequence ==
                    registration.first_sequence &&
                existing->registration.required == registration.required) {
                return result(CausalSupportDisposition::DuplicateSource,
                              registration.identity.source);
            }
            return result(CausalSupportDisposition::OwnerChanged,
                          registration.identity.source,
                          existing->last_sequence + 1U,
                          registration.first_sequence,
                          core::Status::failure(
                              core::ErrorCode::InvalidState));
        }
        if (source_count_ == SourceCapacity) {
            return result(CausalSupportDisposition::CapacityExceeded,
                          registration.identity.source, 0U, 0U,
                          core::Status::failure(
                              core::ErrorCode::CapacityExceeded));
        }
        sources_[source_count_] = {
            .registration = registration,
            .last_sequence = registration.first_sequence - 1U,
            .watermark = registration.first_sequence - 1U,
            .health = SourceHealth::Registering,
        };
        ++source_count_;
        ++epoch_;
        return result(CausalSupportDisposition::SourceRegistered,
                      registration.identity.source);
    }

    [[nodiscard]] CausalSupportResult retain(
        const RetainedCausalSupport& retained) noexcept {
        if (!reconciliation::valid(retained)) {
            return result(CausalSupportDisposition::Invalid, retained.source,
                          0U, retained.source_sequence,
                          core::Status::failure(
                              core::ErrorCode::InvalidArgument));
        }
        CausalSourceCursor* cursor = source_mutable(retained.source);
        if (cursor == nullptr) {
            return result(CausalSupportDisposition::SourceUnavailable,
                          retained.source, 0U, retained.source_sequence,
                          core::Status::failure(core::ErrorCode::Unavailable));
        }
        const auto& identity = cursor->registration.identity;
        if (retained.causal.owner.component != identity.owner ||
            retained.causal.owner.instance != identity.owner_instance ||
            retained.causal.owner.epoch != identity.owner_generation ||
            retained.evidence.configuration != identity.configuration ||
            retained.evidence.sequence_domain != identity.sequence_domain) {
            cursor->health = SourceHealth::OwnerChanged;
            ++epoch_;
            return result(CausalSupportDisposition::OwnerChanged,
                          retained.source, cursor->last_sequence + 1U,
                          retained.source_sequence,
                          core::Status::failure(
                              core::ErrorCode::InvalidState));
        }

        const std::uint64_t expected = cursor->last_sequence + 1U;
        if (retained.source_sequence != expected) {
            cursor->health = SourceHealth::Gap;
            ++epoch_;
            return result(retained.source_sequence < expected
                              ? CausalSupportDisposition::CursorRegression
                              : CausalSupportDisposition::SequenceGap,
                          retained.source, expected,
                          retained.source_sequence,
                          core::Status::failure(
                              retained.source_sequence < expected
                                  ? core::ErrorCode::InvalidState
                                  : core::ErrorCode::SequenceGap));
        }

        const core::Status appended = evidence_.append(retained);
        if (!appended) {
            const auto disposition =
                appended.code == core::ErrorCode::CapacityExceeded
                    ? CausalSupportDisposition::CapacityExceeded
                    : CausalSupportDisposition::EvidenceConflict;
            cursor->health = SourceHealth::Gap;
            ++epoch_;
            return result(disposition, retained.source, expected,
                          retained.source_sequence, appended);
        }
        cursor->last_sequence = retained.source_sequence;
        ++epoch_;
        return result(CausalSupportDisposition::Recorded, retained.source,
                      expected, retained.source_sequence);
    }

    [[nodiscard]] CausalSupportResult advance(
        const SourceCursorUpdate& update) noexcept {
        if (!reconciliation::valid(update)) {
            return result(CausalSupportDisposition::Invalid, update.source,
                          0U, update.sequence_watermark,
                          core::Status::failure(
                              core::ErrorCode::InvalidArgument));
        }
        CausalSourceCursor* cursor = source_mutable(update.source);
        if (cursor == nullptr) {
            return result(CausalSupportDisposition::SourceUnavailable,
                          update.source, 0U, update.sequence_watermark,
                          core::Status::failure(core::ErrorCode::Unavailable));
        }
        const auto& identity = cursor->registration.identity;
        if (update.owner_instance != identity.owner_instance ||
            update.owner_generation != identity.owner_generation ||
            update.configuration != identity.configuration) {
            cursor->health = SourceHealth::OwnerChanged;
            ++epoch_;
            return result(CausalSupportDisposition::OwnerChanged,
                          update.source, cursor->watermark,
                          update.sequence_watermark,
                          core::Status::failure(
                              core::ErrorCode::InvalidState));
        }
        const bool time_regressed =
            cursor->covered_through.quality != core::ClockQuality::Unknown &&
            (!core::comparable_clock_domain(cursor->covered_through,
                                            update.covered_through) ||
             update.covered_through.nanoseconds <
                 cursor->covered_through.nanoseconds);
        if (update.sequence_watermark < cursor->last_sequence ||
            update.sequence_watermark < cursor->watermark || time_regressed) {
            cursor->health = SourceHealth::Gap;
            ++epoch_;
            return result(CausalSupportDisposition::CursorRegression,
                          update.source, cursor->watermark,
                          update.sequence_watermark,
                          core::Status::failure(
                              core::ErrorCode::InvalidState));
        }
        cursor->watermark = update.sequence_watermark;
        cursor->covered_through = update.covered_through;
        cursor->health = cursor->health == SourceHealth::Gap
                             ? SourceHealth::Gap
                         : !update.healthy ? SourceHealth::Unhealthy
                         : !update.fresh   ? SourceHealth::Stale
                                           : SourceHealth::Current;
        ++epoch_;
        return result(CausalSupportDisposition::CursorAdvanced, update.source,
                      cursor->last_sequence + 1U,
                      update.sequence_watermark);
    }

    template <std::size_t PageCapacity =
                  domain::maximum_causal_diagnostic_results>
    [[nodiscard]] CausalSupportQueryResult<PageCapacity> query(
        const domain::CausalDiagnosticQuery& request,
        domain::CausalDiagnosticRecordId after = {}) const noexcept {
        static_assert(PageCapacity > 0U &&
                      PageCapacity <=
                          domain::maximum_causal_diagnostic_results);
        CausalSupportQueryResult<PageCapacity> result{};
        result.page.request = request;
        result.projection_epoch = epoch_;
        if (!domain::valid(request) || request.maximum_results > PageCapacity) {
            result.status =
                core::Status::failure(core::ErrorCode::InvalidArgument);
            return result;
        }
        if (!required_sources_current()) {
            result.status = core::Status::failure(core::ErrorCode::Unavailable);
            return result;
        }

        const auto searched = evidence_.query(
            request, after.value(),
            [&](const RetainedCausalSupport& retained) noexcept {
                if (result.page.record_count == PageCapacity) {
                    return core::Status::failure(
                        core::ErrorCode::CapacityExceeded);
                }
                result.page.records[result.page.record_count++] =
                    diagnostic_record(retained, request.query);
                return core::Status::success();
            });
        result.status = searched.status;
        if (!searched.status) {
            result.page.record_count = 0U;
            return result;
        }
        result.page.complete = searched.complete;
        result.page.next_after =
            searched.complete
                ? domain::CausalDiagnosticRecordId{}
                : domain::CausalDiagnosticRecordId{searched.next_after};
        if (!domain::valid(result.page)) {
            result.status =
                core::Status::failure(core::ErrorCode::InvalidState);
            result.page.record_count = 0U;
            result.page.complete = false;
            result.page.next_after = {};
        }
        return result;
    }

    [[nodiscard]] constexpr std::size_t source_count() const noexcept {
        return source_count_;
    }

    [[nodiscard]] constexpr std::size_t evidence_count() const noexcept {
        return evidence_.size();
    }

    [[nodiscard]] constexpr std::uint64_t epoch() const noexcept {
        return epoch_;
    }

   private:
    struct CausalSourceCursor final {
        SourceRegistration registration{};
        std::uint64_t last_sequence{};
        std::uint64_t watermark{};
        core::Timestamp covered_through{};
        SourceHealth health{SourceHealth::Registering};
    };

    [[nodiscard]] const CausalSourceCursor* source(SourceId id) const noexcept {
        for (std::size_t index = 0U; index < source_count_; ++index) {
            if (sources_[index].registration.identity.source == id) {
                return &sources_[index];
            }
        }
        return nullptr;
    }

    [[nodiscard]] CausalSourceCursor* source_mutable(SourceId id) noexcept {
        for (std::size_t index = 0U; index < source_count_; ++index) {
            if (sources_[index].registration.identity.source == id) {
                return &sources_[index];
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool required_sources_current() const noexcept {
        bool have_required{};
        for (std::size_t index = 0U; index < source_count_; ++index) {
            if (!sources_[index].registration.required) continue;
            have_required = true;
            if (sources_[index].health != SourceHealth::Current) return false;
        }
        return have_required;
    }

    [[nodiscard]] CausalSupportResult result(
        CausalSupportDisposition disposition, SourceId source_id,
        std::uint64_t expected = 0U, std::uint64_t observed = 0U,
        core::Status status = core::Status::success()) const noexcept {
        return {status, disposition, source_id, expected, observed, epoch_};
    }

    std::array<CausalSourceCursor, SourceCapacity> sources_{};
    core::BoundedDiagnosticStore<RetainedCausalSupport,
                                 domain::CausalDiagnosticQuery,
                                 CausalSupportTraits, EvidenceCapacity>
        evidence_{};
    std::size_t source_count_{};
    std::uint64_t epoch_{};
};

static_assert(std::is_trivially_copyable_v<RetainedCausalSupport>);
static_assert(std::is_standard_layout_v<RetainedCausalSupport>);

}  // namespace fintech::reconciliation
