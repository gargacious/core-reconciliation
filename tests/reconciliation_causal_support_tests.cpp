#include <cstdio>
#include <cstdlib>

#include <fintech/reconciliation/causal_support.hpp>

namespace {

using namespace fintech;
using namespace fintech::reconciliation;

void checked(bool condition, int line) {
    if (!condition) {
        std::fprintf(stderr, "check failed at line %d\n", line);
        std::abort();
    }
}

#define check(condition) checked((condition), __LINE__)

core::Timestamp time(std::uint64_t nanoseconds) {
    return {nanoseconds, core::ClockKind::Simulation,
            core::ClockQuality::Synchronized};
}

SourceRegistration source(std::uint8_t id, SourceKind kind,
                          std::uint64_t domain, bool required = true) {
    const std::uint64_t session = 100U + id;
    return {{SourceId{id},
             kind,
             core::VenueId{7U},
             core::SessionId{session},
             kind == SourceKind::OrderSession ? core::SessionId{session}
                                               : core::SessionId{},
             core::AccountId{11U},
             core::SequenceDomainId{domain},
             core::ComponentId{20U + id},
             core::RuntimeInstanceId{200U + id},
             5U,
             core::SchemaId{9U},
             1U,
             core::ConfigFingerprint{66U + id}},
            1U,
            required};
}

RetainedCausalSupport support(
    std::uint64_t record, const SourceRegistration& registration,
    std::uint64_t sequence, std::uint64_t workflow, std::uint64_t command,
    domain::DiagnosticPrivacyClass privacy,
    core::CorrelationId correlation = core::CorrelationId{900U},
    domain::DiagnosticTraceId trace = {}) {
    const auto& identity = registration.identity;
    const domain::CausalOwnerEpoch owner{identity.owner,
                                         identity.owner_instance,
                                         identity.owner_generation};
    const bool carries_trace = trace.valid();
    return {
        .record = domain::CausalDiagnosticRecordId{record},
        .source = identity.source,
        .source_sequence = sequence,
        .privacy = privacy,
        .visibility = {.schema = core::SchemaId{41U},
                       .version = domain::visibility_contract_version,
                       .visibility = domain::VisibilityClass::Operator,
                       .resource_owner = identity.owner,
                       .resource_domain = identity.sequence_domain,
                       .scope = {}},
        .causal = {.schema = core::SchemaId{42U},
                   .schema_version = domain::causal_context_schema_version,
                   .fields = domain::CausalContextField::None,
                   .workflow = domain::WorkflowId{workflow},
                   .root_command = core::CommandId{command},
                   .command = core::CommandId{command},
                   .owner = owner},
        .correlation = correlation,
        .evidence = {.owner = owner,
                     .configuration = identity.configuration,
                     .sequence_domain = identity.sequence_domain,
                     .owner_cursor = sequence,
                     .durable_offset = core::JournalOffset{sequence * 10U},
                     .observed_at = time(1000U + record)},
        .payload = {.schema = core::SchemaId{43U},
                    .version = 1U,
                    .size = 64U,
                    .hash = 7000U + record},
        .fields = carries_trace
                      ? domain::CausalDiagnosticRecordField::Trace
                      : domain::CausalDiagnosticRecordField::None,
        .trace = trace,
    };
}

SourceCursorUpdate checkpoint(const SourceRegistration& registration,
                              std::uint64_t watermark, bool fresh = true,
                              bool healthy = true) {
    return {.source = registration.identity.source,
            .sequence_watermark = watermark,
            .covered_through = time(5000U),
            .observed_at = time(5010U),
            .owner_instance = registration.identity.owner_instance,
            .owner_generation = registration.identity.owner_generation,
            .configuration = registration.identity.configuration,
            .fresh = fresh,
            .healthy = healthy};
}

domain::CausalDiagnosticQuery query(
    std::uint64_t id, std::uint64_t workflow,
    domain::DiagnosticPrivacyMask privacy,
    std::uint16_t maximum_results =
        domain::maximum_causal_diagnostic_results) {
    return {.schema = core::SchemaId{51U},
            .version = domain::causal_diagnostic_contract_version,
            .query = domain::CausalDiagnosticQueryId{id},
            .principal = core::PrincipalId{61U},
            .visibility_policy = domain::VisibilityPolicyId{71U},
            .visibility_policy_generation = 3U,
            .fields = domain::CausalDiagnosticQueryField::Workflow,
            .workflow = domain::WorkflowId{workflow},
            .requested_privacy = privacy,
            .maximum_results = maximum_results};
}

constexpr auto operational = domain::diagnostic_privacy_mask(
    domain::DiagnosticPrivacyClass::Operational);
constexpr auto restricted = domain::diagnostic_privacy_mask(
    domain::DiagnosticPrivacyClass::Restricted);

}  // namespace

int main() {
    const auto primary = source(1U, SourceKind::Primary, 101U);
    const auto order_session =
        source(2U, SourceKind::OrderSession, 202U);
    BoundedCausalSupportIndex<4U, 32U> index;
    check(index.register_source(primary).disposition ==
          CausalSupportDisposition::SourceRegistered);
    check(index.register_source(order_session).status.ok());
    check(index.register_source(primary).disposition ==
          CausalSupportDisposition::DuplicateSource);

    check(index.retain(support(1001U, primary, 1U, 81U, 101U,
                               domain::DiagnosticPrivacyClass::Operational))
              .status.ok());
    check(index.retain(support(1002U, primary, 2U, 81U, 102U,
                               domain::DiagnosticPrivacyClass::Restricted))
              .status.ok());
    check(index.retain(support(1003U, primary, 3U, 82U, 103U,
                               domain::DiagnosticPrivacyClass::Operational))
              .status.ok());
    const domain::DiagnosticTraceId trace{111U, 222U};
    check(index.retain(support(2001U, order_session, 1U, 81U, 201U,
                               domain::DiagnosticPrivacyClass::Operational,
                               core::CorrelationId{901U}, trace))
              .status.ok());

    // Retention alone does not make the projection queryable. Every required
    // source must publish a fresh and healthy watermark first.
    auto request = query(301U, 81U, operational | restricted, 2U);
    auto first_page = index.query<2U>(request);
    check(!first_page.status);
    check(first_page.status.code == core::ErrorCode::Unavailable);
    check(index.advance(checkpoint(primary, 3U)).status.ok());
    check(!index.query<2U>(request).status);
    check(index.advance(checkpoint(order_session, 1U)).status.ok());

    first_page = index.query<2U>(request);
    check(first_page.status.ok());
    check(first_page.page.record_count == 2U);
    check(!first_page.page.complete);
    check(first_page.page.next_after ==
          domain::CausalDiagnosticRecordId{1002U});
    check(first_page.page.records[0].query == request.query);
    check(first_page.page.records[0].record ==
          domain::CausalDiagnosticRecordId{1001U});
    check(first_page.page.records[0].evidence.owner_cursor == 1U);
    check(first_page.page.records[0].payload.hash == 8001U);
    check(domain::valid(first_page.page));

    const auto second_page = index.query<2U>(
        request, domain::CausalDiagnosticRecordId{1002U});
    check(second_page.status.ok());
    check(second_page.page.record_count == 1U);
    check(second_page.page.records[0].record ==
          domain::CausalDiagnosticRecordId{2001U});
    check(second_page.page.complete);
    check(!second_page.page.next_after.valid());
    check(domain::valid(second_page.page));

    const auto operational_only = index.query(
        query(302U, 81U, operational));
    check(operational_only.status.ok());
    check(operational_only.page.record_count == 2U);
    check(operational_only.page.records[0].record ==
          domain::CausalDiagnosticRecordId{1001U});
    check(operational_only.page.records[1].record ==
          domain::CausalDiagnosticRecordId{2001U});

    auto trace_request = query(303U, 81U, operational);
    trace_request.fields =
        trace_request.fields | domain::CausalDiagnosticQueryField::Trace;
    trace_request.trace = trace;
    const auto trace_page = index.query(trace_request);
    check(trace_page.status.ok());
    check(trace_page.page.record_count == 1U);
    check(trace_page.page.records[0].record ==
          domain::CausalDiagnosticRecordId{2001U});

    // A completed empty search says only that no retained diagnostic metadata
    // matched. It does not manufacture an execution or reconciliation fact.
    const auto empty_page = index.query(query(304U, 999U, operational));
    check(empty_page.status.ok());
    check(empty_page.page.complete);
    check(empty_page.page.record_count == 0U);
    check(domain::valid(empty_page.page));

    // A continuation must be a retained record that matched this exact query.
    const auto bad_resume = index.query(
        query(305U, 81U, operational),
        domain::CausalDiagnosticRecordId{1002U});
    check(!bad_resume.status);
    check(bad_resume.status.code == core::ErrorCode::InvalidArgument);

    // Source ownership/configuration mismatch is a hard fence and makes all
    // subsequent queries unavailable until the projection is rebuilt.
    BoundedCausalSupportIndex<2U, 4U> changed_owner;
    check(changed_owner.register_source(primary).status.ok());
    auto alien = support(3001U, primary, 1U, 81U, 301U,
                         domain::DiagnosticPrivacyClass::Operational);
    alien.causal.owner.component = core::ComponentId{999U};
    alien.evidence.owner.component = core::ComponentId{999U};
    alien.visibility.resource_owner = core::ComponentId{999U};
    auto result = changed_owner.retain(alien);
    check(!result.status);
    check(result.disposition == CausalSupportDisposition::OwnerChanged);
    check(changed_owner.evidence_count() == 0U);
    check(changed_owner.advance(checkpoint(primary, 1U)).status.ok());
    check(!changed_owner.query(query(306U, 81U, operational)).status);

    // Sequence gaps and regressions are isolated to their source and never
    // enter the evidence collection.
    BoundedCausalSupportIndex<2U, 4U> gap;
    check(gap.register_source(primary).status.ok());
    result = gap.retain(support(4002U, primary, 2U, 81U, 401U,
                                domain::DiagnosticPrivacyClass::Operational));
    check(!result.status);
    check(result.disposition == CausalSupportDisposition::SequenceGap);
    check(result.expected_sequence == 1U);
    check(gap.evidence_count() == 0U);

    // Stable record IDs cannot be rebound and bounded capacity fails closed.
    BoundedCausalSupportIndex<2U, 2U> conflict;
    check(conflict.register_source(primary).status.ok());
    check(conflict.retain(support(5001U, primary, 1U, 81U, 501U,
                                  domain::DiagnosticPrivacyClass::Operational))
              .status.ok());
    result = conflict.retain(support(
        5001U, primary, 2U, 81U, 502U,
        domain::DiagnosticPrivacyClass::Operational));
    check(!result.status);
    check(result.disposition == CausalSupportDisposition::EvidenceConflict);
    check(conflict.evidence_count() == 1U);

    BoundedCausalSupportIndex<2U, 1U> full;
    check(full.register_source(primary).status.ok());
    check(full.retain(support(6001U, primary, 1U, 81U, 601U,
                              domain::DiagnosticPrivacyClass::Operational))
              .status.ok());
    result = full.retain(support(
        6002U, primary, 2U, 81U, 602U,
        domain::DiagnosticPrivacyClass::Operational));
    check(!result.status);
    check(result.disposition == CausalSupportDisposition::CapacityExceeded);
    check(full.evidence_count() == 1U);
}
