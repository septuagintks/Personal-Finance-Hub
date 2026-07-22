// Personal Finance Hub - Logger Unit Tests
// Version: 1.0
// Test naming: ClassName_WhenCondition_ExpectedBehavior
//
// These tests capture log output via an in-memory ostream sink and assert the
// rendered line, which guards against context/placeholder formatting bugs
// (e.g. a message being dropped due to a placeholder-count mismatch).

#include "pfh/infrastructure/logger.h"
#include <gtest/gtest.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/ostream_sink.h>
#include <filesystem>
#include <memory>
#include <sstream>

using namespace pfh::infrastructure;

namespace pfh::test {

namespace {

// Installs an ostream sink as the default logger for the duration of a test,
// so Logger::* output can be inspected. Restores nothing special afterward;
// each test re-installs its own capture logger.
class LogCapture {
public:
    LogCapture() {
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(stream_);
        sink->set_pattern("%v"); // message only, no timestamp/level noise
        auto logger = std::make_shared<spdlog::logger>("capture", sink);
        logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(logger);
    }
    [[nodiscard]] std::string text() { return stream_.str(); }

private:
    std::ostringstream stream_;
};

} // namespace

TEST(Logger, WhenInfoWithArgs_RendersMessageWithTraceId) {
    LogCapture cap;
    Logger::info("trace-1", "value is {}", 42);
    const std::string out = cap.text();
    EXPECT_NE(out.find("[TraceId:trace-1]"), std::string::npos);
    EXPECT_NE(out.find("value is 42"), std::string::npos);
}

TEST(Logger, WhenDebugWithArgs_DoesNotDropMessage) {
    // Regression: debug previously used a format string with one placeholder
    // but two args, silently dropping the message.
    LogCapture cap;
    Logger::debug("t", "debug msg {}", 7);
    const std::string out = cap.text();
    EXPECT_NE(out.find("debug msg 7"), std::string::npos);
}

TEST(Logger, WhenAllLevelsWithArgs_RenderMessage) {
    LogCapture cap;
    Logger::trace("t", "trace {}", 1);
    Logger::warn("t", "warn {}", 2);
    Logger::error("t", "error {}", 3);
    Logger::critical("t", "critical {}", 4);
    const std::string out = cap.text();
    EXPECT_NE(out.find("trace 1"), std::string::npos);
    EXPECT_NE(out.find("warn 2"), std::string::npos);
    EXPECT_NE(out.find("error 3"), std::string::npos);
    EXPECT_NE(out.find("critical 4"), std::string::npos);
}

TEST(Logger, WhenUserContext_RendersUserId) {
    LogCapture cap;
    Logger::info_user("t", domain::UserId(123), "did {}", "thing");
    const std::string out = cap.text();
    EXPECT_NE(out.find("[UserId:123]"), std::string::npos);
    EXPECT_NE(out.find("did thing"), std::string::npos);
}

TEST(Logger, WhenErrorContext_RendersContextLabel) {
    LogCapture cap;
    Logger::error_with_context("t", "PaymentFlow", "failed code={}", 500);
    const std::string out = cap.text();
    EXPECT_NE(out.find("[Context:PaymentFlow]"), std::string::npos);
    EXPECT_NE(out.find("failed code=500"), std::string::npos);
}

TEST(Logger, WhenMessageContainsBraces_DoesNotReparse) {
    // A pre-rendered message containing '{}' must not be treated as a format
    // string by spdlog (which would throw or mis-render).
    LogCapture cap;
    Logger::info("t", "literal {{}} braces: {}", "ok");
    const std::string out = cap.text();
    EXPECT_NE(out.find("ok"), std::string::npos);
}

TEST(Logger, WhenFileOutputExceedsBudget_RetainsConfiguredFileCount) {
    static std::uint64_t sequence = 0;
    const auto directory = std::filesystem::temp_directory_path() /
        ("pfh_log_rotation_" + std::to_string(sequence++));
    std::filesystem::create_directories(directory);
    const auto path = directory / "pfh.log";
    LoggingConfig config;
    config.output = LogOutput::File;
    config.file = path.string();
    config.maximum_file_size_bytes = 64U * 1024U;
    config.maximum_file_count = 3;
    Logger::initialize(config);

    const std::string payload(8192, 'x');
    for (int index = 0; index < 40; ++index) {
        spdlog::info("{}", payload);
    }
    spdlog::default_logger()->flush();

    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file()) ++files;
    }
    EXPECT_GE(files, 2U);
    EXPECT_LE(files, config.maximum_file_count);
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_TRUE(std::filesystem::exists(directory / "pfh.1.log"));
    EXPECT_FALSE(std::filesystem::exists(directory / "pfh.3.log"));

    auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    spdlog::set_default_logger(
        std::make_shared<spdlog::logger>("post-rotation", sink));
    spdlog::drop("pfh");
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

} // namespace pfh::test
