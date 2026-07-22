// Personal Finance Hub - HTTP Boundary Tests

#include "pfh/presentation/http/http_response_mapper.h"
#include "pfh/presentation/http/auth_rate_limiter.h"
#include "pfh/presentation/http/concurrency.h"
#include "pfh/presentation/http/http_admission_metrics.h"
#include "pfh/presentation/http/json_request_parser.h"
#include "pfh/presentation/http/report_resource_metrics.h"
#include "pfh/presentation/http/time_codec.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <limits>

namespace pfh::test {

using namespace application;
using namespace presentation;

TEST(HttpBoundaryTest, Rfc3339OffsetNormalizesToUtc) {
    auto parsed = TimeCodec::parse_rfc3339("2026-07-14T08:30:45+08:00");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(TimeCodec::format_rfc3339(*parsed), "2026-07-14T00:30:45Z");

    auto fractional =
        TimeCodec::parse_rfc3339("2026-07-14T08:30:45.120300+08:00");
    ASSERT_TRUE(fractional.has_value());
    const auto formatted = TimeCodec::format_rfc3339(*fractional);
    EXPECT_EQ(formatted, "2026-07-14T00:30:45.1203Z");
    auto round_trip = TimeCodec::parse_rfc3339(formatted);
    ASSERT_TRUE(round_trip.has_value());
    EXPECT_EQ(*round_trip, *fractional);

    auto nanosecond_input = TimeCodec::parse_rfc3339(
        "2026-07-14T08:30:45.123456789+08:00");
    ASSERT_TRUE(nanosecond_input.has_value());
    EXPECT_EQ(
        TimeCodec::format_rfc3339(*nanosecond_input),
        "2026-07-14T00:30:45.123456Z");
}

TEST(HttpBoundaryTest, Rfc3339RejectsInvalidCalendarDateAndMissingZone) {
    EXPECT_FALSE(TimeCodec::parse_rfc3339("2026-02-30T00:00:00Z").has_value());
    EXPECT_FALSE(TimeCodec::parse_rfc3339("2026-07-14T00:00:00").has_value());
    EXPECT_FALSE(TimeCodec::parse_rfc3339("0001-01-01T00:00:00Z").has_value());
}

TEST(HttpBoundaryTest, DecimalBoundaryRejectsJsonNumberBeforeUseCase) {
    HttpRequest request;
    request.body = R"({"amount":45.12})";
    auto object = JsonRequestParser::parse_object(request);
    ASSERT_TRUE(object.has_value());
    auto amount = JsonRequestParser::required_string(*object, "amount");
    ASSERT_FALSE(amount.has_value());
    EXPECT_EQ(amount.error().code, ErrorCode::InvalidFormat);
}

TEST(HttpBoundaryTest, InfrastructureErrorResponseDoesNotLeakDetails) {
    const Error error(
        ErrorCode::InfrastructureFailure,
        "database exploded",
        "SELECT secret FROM users at C:/private/source.cpp");
    const auto response = HttpResponseMapper::error(error, "trace-safe");

    ASSERT_EQ(response.status, 500);
    EXPECT_EQ(response.body.find("SELECT"), std::string::npos);
    EXPECT_EQ(response.body.find("private/source"), std::string::npos);
    const auto body = nlohmann::json::parse(response.body);
    EXPECT_EQ(body["message"], "An unexpected error occurred");
    EXPECT_EQ(body["trace_id"], "trace-safe");
    EXPECT_FALSE(body["retryable"].get<bool>());
    EXPECT_TRUE(body["field_errors"].empty());
}

TEST(HttpBoundaryTest, RequiredEmptyStringDistinguishesMissingFromEmpty) {
    HttpRequest request;
    request.body = R"({"description":""})";
    auto object = JsonRequestParser::parse_object(request);
    ASSERT_TRUE(object.has_value());

    auto empty = JsonRequestParser::required_string_allow_empty(
        *object, "description", 16);
    ASSERT_TRUE(empty.has_value());
    EXPECT_TRUE(empty->empty());

    auto missing = JsonRequestParser::required_string_allow_empty(
        *object, "note", 16);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, ErrorCode::MissingRequiredField);
}

TEST(HttpBoundaryTest, StructuredFieldErrorsRemainControlledAndMachineReadable) {
    const auto response = HttpResponseMapper::error(
        Error::field_validation(
            "amount", "invalid_decimal", "amount must be a decimal string"),
        "trace-field");
    ASSERT_EQ(response.status, 400);
    const auto body = nlohmann::json::parse(response.body);
    ASSERT_EQ(body["field_errors"].size(), 1U);
    EXPECT_EQ(body["field_errors"][0]["field"], "amount");
    EXPECT_EQ(body["field_errors"][0]["code"], "invalid_decimal");
    EXPECT_FALSE(body["retryable"].get<bool>());
}

TEST(HttpBoundaryTest, OverloadResponseIsRetryableAndTraceable) {
    const auto response = HttpResponseMapper::overloaded("trace-overload");
    ASSERT_EQ(response.status, 503);
    ASSERT_TRUE(response.headers.contains("Retry-After"));
    EXPECT_EQ(response.headers.at("Retry-After"), "1");
    const auto body = nlohmann::json::parse(response.body);
    EXPECT_EQ(body["error_code"], "SERVICE_UNAVAILABLE");
    EXPECT_EQ(body["trace_id"], "trace-overload");
    EXPECT_TRUE(body["retryable"].get<bool>());
    EXPECT_TRUE(body["field_errors"].empty());
}

TEST(HttpBoundaryTest, ResourceRejectionsUseStableHttpContracts) {
    const auto oversized = HttpResponseMapper::payload_too_large(
        "trace-payload");
    ASSERT_EQ(oversized.status, 413);
    auto oversized_body = nlohmann::json::parse(oversized.body);
    EXPECT_EQ(oversized_body["error_code"], "PAYLOAD_TOO_LARGE");
    EXPECT_FALSE(oversized_body["retryable"].get<bool>());

    const auto limited = HttpResponseMapper::rate_limited("trace-rate");
    ASSERT_EQ(limited.status, 429);
    ASSERT_TRUE(limited.headers.contains("Retry-After"));
    auto limited_body = nlohmann::json::parse(limited.body);
    EXPECT_EQ(limited_body["error_code"], "RATE_LIMITED");
    EXPECT_TRUE(limited_body["retryable"].get<bool>());
}

TEST(HttpBoundaryTest, AdmissionMetricsExposeFixedCapacityAndMonotonicRejections) {
    HttpAdmissionMetrics metrics(HttpAdmissionCapacity{
        1024U, 8U, 32U, 4096U, 2U, 4U, 512U, 20U, 60U, 100U});
    metrics.record_oversized_body_rejection();
    metrics.record_auth_rate_limit_rejection();
    metrics.record_request_queue_rejection();
    metrics.record_request_queue_rejection();
    metrics.record_auth_queue_rejection();

    const auto snapshot = metrics.snapshot();
    EXPECT_EQ(snapshot.capacity.request_queue_byte_capacity, 4096U);
    EXPECT_EQ(snapshot.capacity.auth_worker_threads, 2U);
    EXPECT_EQ(snapshot.oversized_body_rejections, 1U);
    EXPECT_EQ(snapshot.auth_rate_limit_rejections, 1U);
    EXPECT_EQ(snapshot.request_queue_rejections, 2U);
    EXPECT_EQ(snapshot.auth_queue_rejections, 1U);
}

TEST(HttpBoundaryTest, ReportResourceMetricsExposeCapacityAndRejections) {
    ReportResourceMetrics metrics(ReportResourceCapacity{
        100'000U, 10'000U, 64U * 1024U * 1024U,
        32U * 1024U * 1024U, 10'000U, 100'000U, 1024U, 120U, 366U});
    metrics.record_report_query_rejection();
    metrics.record_report_query_rejection();
    metrics.record_csv_export_rejection();

    const auto snapshot = metrics.snapshot();
    EXPECT_EQ(snapshot.capacity.historical_rate_points, 1024U);
    EXPECT_EQ(snapshot.capacity.csv_range_days, 366U);
    EXPECT_EQ(snapshot.report_query_rejections, 2U);
    EXPECT_EQ(snapshot.csv_export_rejections, 1U);
}

TEST(AuthRateLimiterTest, BoundsSourcesAndResetsExpiredWindows) {
    using Clock = AuthRateLimiter::Clock;
    const auto start = Clock::time_point{};
    AuthRateLimiter limiter(2, std::chrono::seconds(60), 2);

    EXPECT_TRUE(limiter.allow("source-a", start));
    EXPECT_TRUE(limiter.allow(
        "source-a", start + std::chrono::seconds(1)));
    EXPECT_FALSE(limiter.allow(
        "source-a", start + std::chrono::seconds(2)));
    EXPECT_TRUE(limiter.allow(
        "source-b", start + std::chrono::seconds(2)));
    EXPECT_FALSE(limiter.allow(
        "source-c", start + std::chrono::seconds(2)));
    EXPECT_EQ(limiter.tracked_sources(), 2U);

    EXPECT_TRUE(limiter.allow(
        "source-c", start + std::chrono::seconds(63)));
    EXPECT_EQ(limiter.tracked_sources(), 1U);
}

TEST(HttpBoundaryTest, IfMatchAcceptsOneStrongVersionAndRejectsAmbiguity) {
    const auto version = parse_if_match_version("\"42\"");
    ASSERT_TRUE(version.has_value());
    EXPECT_EQ(*version, 42);
    EXPECT_EQ(version_etag(*version), "\"42\"");

    for (const auto invalid : {
             "", "42", "W/\"42\"", "\"0\"", "\"01\"",
             "\"42\", \"43\"", "*"}) {
        const auto result = parse_if_match_version(invalid);
        ASSERT_FALSE(result.has_value()) << invalid;
        EXPECT_EQ(result.error().code, ErrorCode::ValidationError);
        ASSERT_EQ(result.error().field_errors.size(), 1U);
        EXPECT_EQ(result.error().field_errors.front().field, "If-Match");
    }
}

TEST(HttpBoundaryTest, OversizedUnsignedIdReturns400ClassErrorInsteadOfThrowing) {
    HttpRequest request;
    request.body = nlohmann::json{
        {"accountId", std::numeric_limits<std::uint64_t>::max()}}.dump();
    auto object = JsonRequestParser::parse_object(request);
    ASSERT_TRUE(object.has_value());
    auto id = JsonRequestParser::required_id<domain::AccountId>(
        *object, "accountId");
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidFormat);
}

} // namespace pfh::test
