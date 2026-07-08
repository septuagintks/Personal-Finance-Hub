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
  "jwt": { "secret": "a-real-secret", "access_token_expiry_seconds": 600,
           "refresh_token_expiry_seconds": 120000 },
  "logging": { "level": "warn", "output": "both", "file": "x.log" },
  "scheduler": { "exchange_rate_refresh_interval_minutes": 30 },
  "exchange_rate": { "provider": "ecb", "api_key": "k" }
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
    EXPECT_EQ(r->jwt.secret, "a-real-secret");
    EXPECT_EQ(r->jwt.access_token_expiry, std::chrono::seconds(600));
    EXPECT_EQ(r->logging.level, LogLevel::Warning);
    EXPECT_EQ(r->logging.output, LogOutput::Both);
    EXPECT_EQ(r->scheduler.exchange_rate_refresh_interval, std::chrono::minutes(30));
    EXPECT_EQ(r->exchange_rate.provider, "ecb");
}

TEST(JsonConfigLoader, WhenOptionalSectionsMissing_UsesDefaults) {
    // Only jwt.secret provided; everything else should default.
    TempConfig cfg(R"({ "jwt": { "secret": "s" } })");
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

TEST(JsonConfigLoader, WhenJwtSecretIsPlaceholder_ReturnsConfigurationError) {
    // Placeholder from config.example.json must be rejected.
    TempConfig cfg(R"({ "jwt": { "secret": "REPLACE_WITH_ACTUAL_SECRET" } })");
    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::ConfigurationError);
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
        std::string json = std::string(R"({ "jwt": { "secret": "s" }, "logging": { "level": ")")
                         + c.level + R"(" } })";
        TempConfig cfg(json);
        JsonConfigLoader loader(cfg.path());
        auto r = loader.load();
        ASSERT_TRUE(r.has_value()) << "level=" << c.level;
        EXPECT_EQ(r->logging.level, c.expected) << "level=" << c.level;
    }
}

// ---- Environment variable overrides ----

TEST(JsonConfigLoader, WhenJwtSecretEnvSet_OverridesJsonValue) {
    TempConfig cfg(kValidConfig);

    // Set environment variable
    #ifdef _WIN32
    _putenv_s("JWT_SECRET", "env-secret-123");
    #else
    setenv("JWT_SECRET", "env-secret-123", 1);
    #endif

    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->jwt.secret, "env-secret-123");

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
    EXPECT_EQ(r->jwt.secret, "a-real-secret");  // From JSON
    EXPECT_EQ(r->database.host, "db");          // From JSON
}

TEST(JsonConfigLoader, WhenDbPortEnvInvalid_KeepsJsonValue) {
    TempConfig cfg(kValidConfig);

    // Set invalid port
    #ifdef _WIN32
    _putenv_s("DB_PORT", "not-a-number");
    #else
    setenv("DB_PORT", "not-a-number", 1);
    #endif

    JsonConfigLoader loader(cfg.path());
    auto r = loader.load();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->database.port, 6543);  // From JSON (env ignored)

    // Clean up
    #ifdef _WIN32
    _putenv_s("DB_PORT", "");
    #else
    unsetenv("DB_PORT");
    #endif
}

} // namespace pfh::test
