// Personal Finance Hub - HTTP Boundary Tests

#include "pfh/presentation/http/http_response_mapper.h"
#include "pfh/presentation/http/json_request_parser.h"
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
