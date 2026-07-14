// Personal Finance Hub - JSON Config Loader Unit Tests
// Version: 1.0
// Test naming: ClassName_WhenCondition_ExpectedBehavior

#include "pfh/infrastructure/json_config_loader.h"
#include "pfh/application/error.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>

using namespace pfh::infrastructure;
using pfh::application::ErrorCode;

namespace pfh::test {

namespace {

// RAII temp file holding JSON content; removed on destruction.
class TempConfig {
public:
    explicit TempConfig(const std::string& content) {
        path_ = std::filesystem::temp_directory_path() /
                ("pfh_cfg_" + std::to_string(counter_++) + ".json");
        std::ofstream out(path_);
        out << content;
    }
    ~TempConfig() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
    static inline int counter_ = 0;
};

constexpr const char* kValidConfig = R"({
  "environment": "test",
  "server": { "host": "127.0.0.1", "port": 9090, "threads": 8 },
  "database": { "host": "db", "port": 6543, "name": "pfh_test",
                "user": "tester", "password": "pw", "pool_size": 5,
                "connection_timeout": 15 },
  "jwt": { "secret": "0123456789abcdef0123456789abcdef", "access_token_expiry_seconds": 600,
           "refresh_token_expiry_seconds": 120000 },
  "logging": { "level": "warn", "output": "both", "file": "x.log" },
  "scheduler": { "enabled": true, "worker_threads": 3, "queue_capacity": 40,
                 "outbox_publish_interval_seconds": 4, "outbox_batch_size": 25,
                 "outbox_processing_timeout_seconds": 120,
                 "exchange_rate_refresh_interval_minutes": 30,
                 "session_cleanup_interval_minutes": 720,
                 "session_cleanup_batch_size": 250,
                 "job_execution_timeout_seconds": 20,
                 "job_lease_duration_seconds": 90 },
  "exchange_rate": { "provider": "ecb", "api_key": "k",
                     "request_timeout_seconds": 8 }
})";

} // namespace

// ---- Valid load ----

TEST(JsonConfigLoader, WhenValidConfig_LoadsAllFields) {
    TempConfig cfg(kValidConfig);
    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_TRUE(r.has_value()) << (r ? "" : r.error().message);

    EXPECT_EQ(r->environment, "test");
    EXPECT_EQ(r->server.host, "127.0.0.1");
    EXPECT_EQ(r->server.port, 9090);
    EXPECT_EQ(r->server.threads, 8u);
    EXPECT_EQ(r->database.port, 6543);
    EXPECT_EQ(r->database.name, "pfh_test");
    EXPECT_EQ(r->database.pool_size, 5u);
    EXPECT_EQ(r->database.connection_timeout, std::chrono::seconds(15));
    EXPECT_EQ(r->jwt.secret, "0123456789abcdef0123456789abcdef");
    EXPECT_EQ(r->jwt.access_token_expiry, std::chrono::seconds(600));
    EXPECT_EQ(r->logging.level, LogLevel::Warning);
    EXPECT_EQ(r->logging.output, LogOutput::Both);
    EXPECT_TRUE(r->scheduler.enabled);
    EXPECT_EQ(r->scheduler.worker_threads, 3U);
    EXPECT_EQ(r->scheduler.queue_capacity, 40U);
    EXPECT_EQ(r->scheduler.outbox_publish_interval, std::chrono::seconds(4));
    EXPECT_EQ(r->scheduler.outbox_batch_size, 25U);
    EXPECT_EQ(r->scheduler.outbox_processing_timeout, std::chrono::seconds(120));
    EXPECT_EQ(r->scheduler.exchange_rate_refresh_interval, std::chrono::minutes(30));
    EXPECT_EQ(r->scheduler.session_cleanup_interval, std::chrono::minutes(720));
    EXPECT_EQ(r->scheduler.session_cleanup_batch_size, 250U);
    EXPECT_EQ(r->scheduler.job_execution_timeout, std::chrono::seconds(20));
    EXPECT_EQ(r->scheduler.job_lease_duration, std::chrono::seconds(90));
    EXPECT_EQ(r->exchange_rate.provider, "ecb");
    EXPECT_EQ(r->exchange_rate.request_timeout, std::chrono::seconds(8));
}

TEST(JsonConfigLoader, WhenOptionalSectionsMissing_UsesDefaults) {
    // Only jwt.secret provided; everything else should default.
    TempConfig cfg(R"({ "jwt": { "secret": "0123456789abcdef0123456789abcdef" } })");
    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->server.host, "0.0.0.0");
    EXPECT_EQ(r->server.port, 8080);
    EXPECT_EQ(r->database.name, "pfh_dev");
    EXPECT_EQ(r->logging.level, LogLevel::Info);
    EXPECT_EQ(r->logging.output, LogOutput::Console);
}

// ---- Error paths ----

TEST(JsonConfigLoader, WhenFileMissing_ReturnsConfigurationError) {
    JsonConfigLoader loader("does/not/exist_pfh.json");
    auto r = loader.load();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::ConfigurationError);
}

TEST(JsonConfigLoader, WhenMalformedJson_ReturnsConfigurationError) {
    TempConfig cfg("{ not valid json ");
    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::ConfigurationError);
}

TEST(JsonConfigLoader, WhenJwtSecretMissing_ReturnsConfigurationError) {
    TempConfig cfg(R"({ "environment": "test" })");
    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::ConfigurationError);
}

TEST(JsonConfigLoader, WhenSecretIsPlaceholder_ReturnsConfigurationError) {
    {
        // Placeholder from config.example.json must be rejected.
        TempConfig cfg(R"({ "jwt": { "secret": "REPLACE_WITH_ACTUAL_SECRET" } })");
        JsonConfigLoader loader(cfg.path());
        auto r = loader.load();
        ASSERT_FALSE(r.has_value());
        EXPECT_EQ(r.error().code, ErrorCode::ConfigurationError);
    }

#ifdef _WIN32
    _putenv_s("PFH_PASSWORD_PEPPER", "REPLACE_WITH_PEPPER_OR_LEAVE_EMPTY");
#else
    setenv("PFH_PASSWORD_PEPPER", "REPLACE_WITH_PEPPER_OR_LEAVE_EMPTY", 1);
#endif

    TempConfig cfg(kValidConfig);
    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    EXPECT_FALSE(r.has_value());
    if (!r) {
        EXPECT_EQ(r.error().code, ErrorCode::ConfigurationError);
    }

#ifdef _WIN32
    _putenv_s("PFH_PASSWORD_PEPPER", "");
#else
    unsetenv("PFH_PASSWORD_PEPPER");
#endif
}

TEST(JsonConfigLoader, WhenJwtSecretIsShort_ReturnsConfigurationError) {
    TempConfig cfg(R"({ "jwt": { "secret": "too-short" } })");
    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::ConfigurationError);
}

TEST(JsonConfigLoader, WhenSchedulerDurationsConflict_ReturnsError) {
    const auto expect_invalid = [](const std::string& scheduler_fields,
                                   const std::string& exchange_fields = "") {
        TempConfig cfg(
            "{\"jwt\":{\"secret\":\"0123456789abcdef0123456789abcdef\"},"
            "\"scheduler\":{" + scheduler_fields + "}" + exchange_fields +
            "}");
        JsonConfigLoader loader(cfg.path());
        auto result = loader.load();
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, ErrorCode::ConfigurationError);
    };

    expect_invalid(
        "\"job_execution_timeout_seconds\":60,"
        "\"job_lease_duration_seconds\":60");
    expect_invalid(
        "\"job_execution_timeout_seconds\":60,"
        "\"outbox_processing_timeout_seconds\":60");
    expect_invalid(
        "\"job_execution_timeout_seconds\":10,"
        "\"job_lease_duration_seconds\":60",
        ",\"exchange_rate\":{\"provider\":\"mock\","
        "\"request_timeout_seconds\":11}");
}

// ---- Log level / output parsing ----

TEST(JsonConfigLoader, WhenLogLevelVariants_ParseCorrectly) {
    struct Case { const char* level; LogLevel expected; };
    const Case cases[] = {
        {"trace", LogLevel::Trace}, {"debug", LogLevel::Debug},
        {"info", LogLevel::Info},   {"warn", LogLevel::Warning},
        {"warning", LogLevel::Warning}, {"error", LogLevel::Error},
        {"critical", LogLevel::Critical},
        {"nonsense", LogLevel::Info}, // unknown falls back to Info
    };
    for (const auto& c : cases) {
        std::string json = std::string(R"({ "jwt": { "secret": "0123456789abcdef0123456789abcdef" }, "logging": { "level": ")")
                         + c.level + R"(" } })";
        TempConfig cfg(json);
        JsonConfigLoader loader(cfg.path());
        auto r = loader.load();
        ASSERT_TRUE(r.has_value()) << "level=" << c.level;
        EXPECT_EQ(r->logging.level, c.expected) << "level=" << c.level;
    }
}

// ---- Environment variable overrides ----

TEST(JsonConfigLoader, WhenPfhPrefixedEnvSet_OverridesJsonValue) {
    TempConfig cfg(kValidConfig);

#ifdef _WIN32
    _putenv_s("PFH_JWT_SECRET", "pfh-prefixed-secret-0123456789abcd");
    _putenv_s("JWT_SECRET", "");
#else
    setenv("PFH_JWT_SECRET", "pfh-prefixed-secret-0123456789abcd", 1);
    unsetenv("JWT_SECRET");
#endif

    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->jwt.secret, "pfh-prefixed-secret-0123456789abcd");

#ifdef _WIN32
    _putenv_s("PFH_JWT_SECRET", "");
#else
    unsetenv("PFH_JWT_SECRET");
#endif
}

TEST(JsonConfigLoader, WhenJwtSecretEnvSet_OverridesJsonValue) {
    TempConfig cfg(kValidConfig);

    // Set environment variable
    #ifdef _WIN32
    _putenv_s("JWT_SECRET", "env-secret-123-0123456789abcdef01");
    _putenv_s("PFH_JWT_SECRET", "");
    #else
    setenv("JWT_SECRET", "env-secret-123-0123456789abcdef01", 1);
    unsetenv("PFH_JWT_SECRET");
    #endif

    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->jwt.secret, "env-secret-123-0123456789abcdef01");

    // Clean up
    #ifdef _WIN32
    _putenv_s("JWT_SECRET", "");
    #else
    unsetenv("JWT_SECRET");
    #endif
}

TEST(JsonConfigLoader, WhenDbEnvVarsSet_OverrideJsonValues) {
    TempConfig cfg(kValidConfig);

    // Set environment variables
    #ifdef _WIN32
    _putenv_s("DB_HOST", "env-db-host");
    _putenv_s("DB_PORT", "7777");
    _putenv_s("DB_NAME", "env_db");
    _putenv_s("DB_USER", "env_user");
    _putenv_s("DB_PASSWORD", "env_pass");
    #else
    setenv("DB_HOST", "env-db-host", 1);
    setenv("DB_PORT", "7777", 1);
    setenv("DB_NAME", "env_db", 1);
    setenv("DB_USER", "env_user", 1);
    setenv("DB_PASSWORD", "env_pass", 1);
    #endif

    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->database.host, "env-db-host");
    EXPECT_EQ(r->database.port, 7777);
    EXPECT_EQ(r->database.name, "env_db");
    EXPECT_EQ(r->database.user, "env_user");
    EXPECT_EQ(r->database.password, "env_pass");

    // Clean up
    #ifdef _WIN32
    _putenv_s("DB_HOST", "");
    _putenv_s("DB_PORT", "");
    _putenv_s("DB_NAME", "");
    _putenv_s("DB_USER", "");
    _putenv_s("DB_PASSWORD", "");
    #else
    unsetenv("DB_HOST");
    unsetenv("DB_PORT");
    unsetenv("DB_NAME");
    unsetenv("DB_USER");
    unsetenv("DB_PASSWORD");
    #endif
}

TEST(JsonConfigLoader, WhenEnvVarNotSet_UsesJsonValue) {
    TempConfig cfg(kValidConfig);

    // Ensure env vars are not set
    #ifdef _WIN32
    _putenv_s("JWT_SECRET", "");
    _putenv_s("DB_HOST", "");
    #else
    unsetenv("JWT_SECRET");
    unsetenv("DB_HOST");
    #endif

    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->jwt.secret, "0123456789abcdef0123456789abcdef");  // From JSON
    EXPECT_EQ(r->database.host, "db");          // From JSON
}

TEST(JsonConfigLoader, WhenDbPortEnvInvalid_FailsLoudly) {
    TempConfig cfg(kValidConfig);

    // An invalid deployment value must fail loudly rather than silently reverting
    // to the JSON/default (which could point the app at the wrong database).
    #ifdef _WIN32
    _putenv_s("PFH_DB_PORT", "not-a-number");
    #else
    setenv("PFH_DB_PORT", "not-a-number", 1);
    #endif

    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, application::ErrorCode::ConfigurationError);

    // Clean up
    #ifdef _WIN32
    _putenv_s("PFH_DB_PORT", "");
    #else
    unsetenv("PFH_DB_PORT");
    #endif
}

TEST(JsonConfigLoader, WhenDbPortEnvOutOfRange_FailsLoudly) {
    TempConfig cfg(kValidConfig);

    #ifdef _WIN32
    _putenv_s("PFH_DB_PORT", "99999");
    #else
    setenv("PFH_DB_PORT", "99999", 1);
    #endif

    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, application::ErrorCode::ConfigurationError);

    #ifdef _WIN32
    _putenv_s("PFH_DB_PORT", "");
    #else
    unsetenv("PFH_DB_PORT");
    #endif
}

TEST(JsonConfigLoader, WhenEnvironmentAndApiKeyEnvSet_Overrides) {
    TempConfig cfg(kValidConfig);

    #ifdef _WIN32
    _putenv_s("PFH_ENVIRONMENT", "production");
    _putenv_s("PFH_EXCHANGE_RATE_API_KEY", "secret-key-xyz");
    #else
    setenv("PFH_ENVIRONMENT", "production", 1);
    setenv("PFH_EXCHANGE_RATE_API_KEY", "secret-key-xyz", 1);
    #endif

    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->environment, "production");
    EXPECT_EQ(r->exchange_rate.api_key, "secret-key-xyz");

    #ifdef _WIN32
    _putenv_s("PFH_ENVIRONMENT", "");
    _putenv_s("PFH_EXCHANGE_RATE_API_KEY", "");
    #else
    unsetenv("PFH_ENVIRONMENT");
    unsetenv("PFH_EXCHANGE_RATE_API_KEY");
    #endif
}

} // namespace pfh::test
