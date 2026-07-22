// Personal Finance Hub - Health and Operator Controller

#include "pfh/presentation/controllers/operations_controller.h"

#include "pfh/presentation/http/http_response_mapper.h"
#include "pfh/presentation/http/time_codec.h"

#include <charconv>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>

namespace pfh::presentation {

namespace {

using Json = nlohmann::json;

[[nodiscard]] application::Result<application::AccessTokenClaims>
require_identity(const HttpRequest& request) {
    if (!request.identity.has_value()) {
        return application::err(application::Error::unauthorized());
    }
    return request.identity->access_claims;
}

[[nodiscard]] const char* job_result_text(
    application::JobLastResult result) noexcept {
    using application::JobLastResult;
    switch (result) {
    case JobLastResult::NeverRun: return "neverRun";
    case JobLastResult::Succeeded: return "succeeded";
    case JobLastResult::Failed: return "failed";
    case JobLastResult::Skipped: return "skipped";
    }
    return "failed";
}

[[nodiscard]] Json overview_json(
    const application::OperationalOverview& overview) {
    const auto bounded_count_json = [](
        const application::OperationalBoundedCount& value) {
        return Json{{"count", value.count}, {"saturated", value.saturated}};
    };
    Json jobs = Json::array();
    for (const auto& job : overview.jobs) {
        jobs.push_back(Json{
            {"name", job.name},
            {"schedulerStarted", job.scheduler_started},
            {"running", job.running},
            {"executionSequence", job.execution_sequence},
            {"lastResult", job_result_text(job.last_result)},
            {"lastStartedAt", job.last_started_at.has_value()
                ? Json(TimeCodec::format_rfc3339(*job.last_started_at))
                : Json(nullptr)},
            {"lastFinishedAt", job.last_finished_at.has_value()
                ? Json(TimeCodec::format_rfc3339(*job.last_finished_at))
                : Json(nullptr)},
            {"lastDurationMs", job.last_duration_milliseconds}});
    }
    Json leases = Json::array();
    for (const auto& lease : overview.data.leases) {
        leases.push_back(Json{
            {"jobName", lease.job_name},
            {"active", lease.active},
            {"leaseUntil", TimeCodec::format_rfc3339(lease.lease_until)}});
    }
    Json outbox = Json::object();
    for (const auto& [status, count] : overview.data.outbox_counts) {
        outbox[status] = bounded_count_json(count);
    }
    return Json{
        {"generatedAt", TimeCodec::format_rfc3339(overview.generated_at)},
        {"windowStart", TimeCodec::format_rfc3339(overview.data.window_start)},
        {"outbox", std::move(outbox)},
        {"handlerReceipts", Json{
            {"count", overview.data.handler_receipt_count.count},
            {"saturated", overview.data.handler_receipt_count.saturated},
            {"latestAt", overview.data.latest_receipt_at.has_value()
                ? Json(TimeCodec::format_rfc3339(
                      *overview.data.latest_receipt_at))
                : Json(nullptr)}}},
        {"expiredIdempotency", bounded_count_json(
            overview.data.expired_idempotency_count)},
        {"leases", std::move(leases)},
        {"jobs", std::move(jobs)}};
}

[[nodiscard]] Json http_admission_json(
    const HttpAdmissionSnapshot& snapshot) {
    const auto& capacity = snapshot.capacity;
    return Json{
        {"capacity", Json{
            {"maximumRequestBodyBytes", capacity.maximum_request_body_bytes},
            {"requestWorkerThreads", capacity.request_worker_threads},
            {"requestQueueTasks", capacity.request_queue_capacity},
            {"requestQueueBytes", capacity.request_queue_byte_capacity},
            {"authWorkerThreads", capacity.auth_worker_threads},
            {"authQueueTasks", capacity.auth_queue_capacity},
            {"authQueueBytes", capacity.auth_queue_byte_capacity},
            {"authRateLimitAttempts", capacity.auth_rate_limit_attempts},
            {"authRateLimitWindowSeconds",
             capacity.auth_rate_limit_window_seconds},
            {"authRateLimitSources", capacity.auth_rate_limit_sources}}},
        {"rejections", Json{
            {"oversizedBody", snapshot.oversized_body_rejections},
            {"authRateLimit", snapshot.auth_rate_limit_rejections},
            {"requestQueue", snapshot.request_queue_rejections},
            {"authQueue", snapshot.auth_queue_rejections}}}};
}

void append_http_admission_metrics(
    std::ostringstream& output,
    const HttpAdmissionSnapshot& snapshot) {
    const auto& capacity = snapshot.capacity;
    output << "# TYPE pfh_http_admission_capacity gauge\n"
           << "pfh_http_admission_capacity{resource=\"maximum_request_body_bytes\"} "
           << capacity.maximum_request_body_bytes << '\n'
           << "pfh_http_admission_capacity{resource=\"request_worker_threads\"} "
           << capacity.request_worker_threads << '\n'
           << "pfh_http_admission_capacity{resource=\"request_queue_tasks\"} "
           << capacity.request_queue_capacity << '\n'
           << "pfh_http_admission_capacity{resource=\"request_queue_bytes\"} "
           << capacity.request_queue_byte_capacity << '\n'
           << "pfh_http_admission_capacity{resource=\"auth_worker_threads\"} "
           << capacity.auth_worker_threads << '\n'
           << "pfh_http_admission_capacity{resource=\"auth_queue_tasks\"} "
           << capacity.auth_queue_capacity << '\n'
           << "pfh_http_admission_capacity{resource=\"auth_queue_bytes\"} "
           << capacity.auth_queue_byte_capacity << '\n'
           << "pfh_http_admission_capacity{resource=\"auth_rate_limit_attempts\"} "
           << capacity.auth_rate_limit_attempts << '\n'
           << "pfh_http_admission_capacity{resource=\"auth_rate_limit_window_seconds\"} "
           << capacity.auth_rate_limit_window_seconds << '\n'
           << "pfh_http_admission_capacity{resource=\"auth_rate_limit_sources\"} "
           << capacity.auth_rate_limit_sources << '\n'
           << "# TYPE pfh_http_admission_rejections_total counter\n"
           << "pfh_http_admission_rejections_total{reason=\"oversized_body\"} "
           << snapshot.oversized_body_rejections << '\n'
           << "pfh_http_admission_rejections_total{reason=\"auth_rate_limit\"} "
           << snapshot.auth_rate_limit_rejections << '\n'
           << "pfh_http_admission_rejections_total{reason=\"request_queue\"} "
           << snapshot.request_queue_rejections << '\n'
           << "pfh_http_admission_rejections_total{reason=\"auth_queue\"} "
           << snapshot.auth_queue_rejections << '\n';
}

[[nodiscard]] Json report_resources_json(
    const ReportResourceSnapshot& snapshot) {
    const auto& capacity = snapshot.capacity;
    return Json{
        {"capacity", Json{
            {"aggregateRows", capacity.aggregate_rows},
            {"detailedRows", capacity.detailed_rows},
            {"inputBytes", capacity.input_bytes},
            {"csvOutputBytes", capacity.csv_output_bytes},
            {"breakdownBuckets", capacity.breakdown_buckets},
            {"breakdownExpansions", capacity.breakdown_expansions},
            {"historicalRatePoints", capacity.historical_rate_points},
            {"cashFlowMonths", capacity.cash_flow_months},
            {"csvRangeDays", capacity.csv_range_days}}},
        {"rejections", Json{
            {"reportQuery", snapshot.report_query_rejections},
            {"csvExport", snapshot.csv_export_rejections}}}};
}

void append_report_resource_metrics(
    std::ostringstream& output,
    const ReportResourceSnapshot& snapshot) {
    const auto& capacity = snapshot.capacity;
    output << "# TYPE pfh_report_resource_capacity gauge\n"
           << "pfh_report_resource_capacity{resource=\"aggregate_rows\"} "
           << capacity.aggregate_rows << '\n'
           << "pfh_report_resource_capacity{resource=\"detailed_rows\"} "
           << capacity.detailed_rows << '\n'
           << "pfh_report_resource_capacity{resource=\"input_bytes\"} "
           << capacity.input_bytes << '\n'
           << "pfh_report_resource_capacity{resource=\"csv_output_bytes\"} "
           << capacity.csv_output_bytes << '\n'
           << "pfh_report_resource_capacity{resource=\"breakdown_buckets\"} "
           << capacity.breakdown_buckets << '\n'
           << "pfh_report_resource_capacity{resource=\"breakdown_expansions\"} "
           << capacity.breakdown_expansions << '\n'
           << "pfh_report_resource_capacity{resource=\"historical_rate_points\"} "
           << capacity.historical_rate_points << '\n'
           << "pfh_report_resource_capacity{resource=\"cash_flow_months\"} "
           << capacity.cash_flow_months << '\n'
           << "pfh_report_resource_capacity{resource=\"csv_range_days\"} "
           << capacity.csv_range_days << '\n'
           << "# TYPE pfh_report_resource_rejections_total counter\n"
           << "pfh_report_resource_rejections_total{surface=\"report_query\"} "
           << snapshot.report_query_rejections << '\n'
           << "pfh_report_resource_rejections_total{surface=\"csv_export\"} "
           << snapshot.csv_export_rejections << '\n';
}

[[nodiscard]] application::Result<std::size_t> page_size(
    const HttpRequest& request) {
    const auto found = request.query.find("pageSize");
    if (found == request.query.end()) return std::size_t{50};
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(
        found->second.data(),
        found->second.data() + found->second.size(),
        parsed);
    if (result.ec != std::errc{} ||
        result.ptr != found->second.data() + found->second.size() ||
        parsed == 0 || parsed > 100) {
        return application::err(application::Error::validation(
            "pageSize must be between 1 and 100"));
    }
    return static_cast<std::size_t>(parsed);
}

} // namespace

HttpResponse OperationsController::liveness(const HttpRequest& /*request*/) const {
    return HttpResponseMapper::json(200, Json{{"status", "alive"}});
}

HttpResponse OperationsController::readiness(const HttpRequest& request) {
    auto ready = service_.readiness();
    if (!ready) {
        HttpResponse response = HttpResponseMapper::json(
            503, Json{{"status", "not_ready"}});
        response.headers.emplace("Retry-After", "5");
        return response;
    }
    if (!*ready) {
        HttpResponse response = HttpResponseMapper::json(
            503, Json{{"status", "not_ready"}});
        response.headers.emplace("Retry-After", "5");
        return response;
    }
    (void)request;
    return HttpResponseMapper::json(200, Json{{"status", "ready"}});
}

HttpResponse OperationsController::summary(const HttpRequest& request) {
    auto identity = require_identity(request);
    if (!identity) {
        return HttpResponseMapper::error(identity.error(), request.trace_id);
    }
    auto result = service_.overview(*identity);
    if (!result) {
        return HttpResponseMapper::error(result.error(), request.trace_id);
    }
    auto body = overview_json(*result);
    body["httpAdmission"] = http_admission_json(
        admission_metrics_.snapshot());
    body["reportResources"] = report_resources_json(
        report_resource_metrics_.snapshot());
    return HttpResponseMapper::json(200, body);
}

HttpResponse OperationsController::metrics(const HttpRequest& request) {
    auto identity = require_identity(request);
    if (!identity) {
        return HttpResponseMapper::error(identity.error(), request.trace_id);
    }
    auto result = service_.overview(*identity);
    if (!result) {
        return HttpResponseMapper::error(result.error(), request.trace_id);
    }
    std::ostringstream output;
    output << "# TYPE pfh_outbox_messages gauge\n"
           << "# TYPE pfh_outbox_messages_saturated gauge\n";
    for (const auto& [status, count] : result->data.outbox_counts) {
        output << "pfh_outbox_messages{status=\"" << status << "\"} "
               << count.count << '\n'
               << "pfh_outbox_messages_saturated{status=\"" << status
               << "\"} " << (count.saturated ? 1 : 0) << '\n';
    }
    output << "# TYPE pfh_outbox_handler_receipts gauge\n"
           << "pfh_outbox_handler_receipts "
           << result->data.handler_receipt_count.count << '\n'
           << "# TYPE pfh_outbox_handler_receipts_saturated gauge\n"
           << "pfh_outbox_handler_receipts_saturated "
           << (result->data.handler_receipt_count.saturated ? 1 : 0) << '\n'
           << "# TYPE pfh_expired_idempotency_records gauge\n"
           << "pfh_expired_idempotency_records "
           << result->data.expired_idempotency_count.count << '\n'
           << "# TYPE pfh_expired_idempotency_records_saturated gauge\n"
           << "pfh_expired_idempotency_records_saturated "
           << (result->data.expired_idempotency_count.saturated ? 1 : 0) << '\n'
           << "# TYPE pfh_background_job_running gauge\n";
    for (const auto& job : result->jobs) {
        output << "pfh_background_job_running{job=\"" << job.name << "\"} "
               << (job.running ? 1 : 0) << '\n';
    }
    append_http_admission_metrics(output, admission_metrics_.snapshot());
    append_report_resource_metrics(output, report_resource_metrics_.snapshot());
    HttpResponse response;
    response.status = 200;
    response.headers.emplace(
        "Content-Type", "text/plain; version=0.0.4; charset=utf-8");
    response.body = output.str();
    return response;
}

HttpResponse OperationsController::list_dead_letters(
    const HttpRequest& request) {
    auto identity = require_identity(request);
    if (!identity) {
        return HttpResponseMapper::error(identity.error(), request.trace_id);
    }
    auto limit = page_size(request);
    if (!limit) return HttpResponseMapper::error(limit.error(), request.trace_id);
    const auto cursor = request.query.find("cursor");
    const std::optional<std::string_view> cursor_value =
        cursor == request.query.end()
            ? std::nullopt
            : std::optional<std::string_view>(cursor->second);
    auto result = service_.list_dead_letters(*identity, cursor_value, *limit);
    if (!result) {
        return HttpResponseMapper::error(result.error(), request.trace_id);
    }
    Json items = Json::array();
    for (const auto& item : result->items) {
        items.push_back(Json{
            {"id", item.id},
            {"eventName", item.event_name},
            {"aggregateType", item.aggregate_type.empty()
                ? Json(nullptr) : Json(item.aggregate_type)},
            {"aggregateId", item.aggregate_id.empty()
                ? Json(nullptr) : Json(item.aggregate_id)},
            {"retryCount", item.retry_count},
            {"maxRetryCount", item.max_retry_count},
            {"lastFailedHandler", item.last_failed_handler.empty()
                ? Json(nullptr) : Json(item.last_failed_handler)},
            {"lastFailedAt", item.last_failed_at ==
                    std::chrono::system_clock::time_point{}
                ? Json(nullptr)
                : Json(TimeCodec::format_rfc3339(item.last_failed_at))},
            {"createdAt", TimeCodec::format_rfc3339(item.created_at)}});
    }
    return HttpResponseMapper::json(200, Json{
        {"items", std::move(items)},
        {"nextCursor", result->next_cursor.has_value()
            ? Json(*result->next_cursor)
            : Json(nullptr)}});
}

HttpResponse OperationsController::retry_dead_letter(
    const HttpRequest& request,
    std::string_view outbox_id) {
    auto identity = require_identity(request);
    if (!identity) {
        return HttpResponseMapper::error(identity.error(), request.trace_id);
    }
    const auto key = request.header("Idempotency-Key");
    if (!key.has_value()) {
        return HttpResponseMapper::error(
            application::Error::validation(
                "Idempotency-Key header is required"),
            request.trace_id);
    }
    auto result = service_.retry_dead_letter(
        *identity,
        std::string(outbox_id),
        *key,
        request.trace_id);
    return result
        ? HttpResponseMapper::json(202, Json{
              {"outboxId", result->outbox_id},
              {"replayed", result->replayed},
              {"status", "retry_scheduled"}})
        : HttpResponseMapper::error(result.error(), request.trace_id);
}

} // namespace pfh::presentation
