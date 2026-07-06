// Personal Finance Hub - Configuration Management
// Version: 1.0
// C++23
// This file defines configuration structure and loading interface

#pragma once

#include "pfh/application/error.h"
#include <chrono>
#include <cstdint>
#include <string>

namespace pfh::infrastructure {

/// @brief Server configuration
struct ServerConfig {
    std::string host = "0.0.0.0";
    std::uint16_t port = 8080;
    std::uint32_t threads = 4;
};

/// @brief Database configuration
struct DatabaseConfig {
    std::string host = "localhost";
    std::uint16_t port = 5432;
    std::string name = "pfh_dev";
    std::string user = "pfh_user";
    std::string password;
    std::uint32_t pool_size = 10;
    std::chrono::seconds connection_timeout{30};
};

/// @brief JWT configuration
struct JwtConfig {
    std::string secret;
    std::chrono::seconds access_token_expiry{900};   // 15 minutes
    std::chrono::seconds refresh_token_expiry{2592000}; // 30 days
};

/// @brief Logging configuration
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

enum class LogOutput {
    Console,
    File,
    Both
};

struct LoggingConfig {
    LogLevel level = LogLevel::Info;
    LogOutput output = LogOutput::Console;
    std::string file = "logs/pfh.log";
};

/// @brief Scheduler configuration
struct SchedulerConfig {
    std::chrono::minutes exchange_rate_refresh_interval{60};
};

/// @brief Exchange rate provider configuration
struct ExchangeRateConfig {
    std::string provider = "mock";
    std::string api_key;
};

/// @brief Application configuration root
struct AppConfig {
    std::string environment = "development";
    ServerConfig server;
    DatabaseConfig database;
    JwtConfig jwt;
    LoggingConfig logging;
    SchedulerConfig scheduler;
    ExchangeRateConfig exchange_rate;
};

/// @brief Configuration loader interface
class IConfigLoader {
public:
    virtual ~IConfigLoader() = default;

    /// @brief Load configuration from file or environment.
    /// @return AppConfig on success; application::Error (ConfigurationError) on failure,
    ///         so the presentation layer can map it uniformly.
    [[nodiscard]] virtual application::Result<AppConfig> load() = 0;
};

} // namespace pfh::infrastructure
