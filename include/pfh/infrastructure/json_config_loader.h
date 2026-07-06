// Personal Finance Hub - JSON Configuration Loader Implementation
// Version: 1.0
// C++23
// This file implements configuration loading from JSON files

#pragma once

#include "pfh/infrastructure/config.h"
#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

namespace pfh::infrastructure {

/// @brief JSON-based configuration loader
class JsonConfigLoader : public IConfigLoader {
public:
    /// @brief Constructor with config file path
    /// @param config_path Path to JSON config file (e.g., "config/config.local.json")
    explicit JsonConfigLoader(std::filesystem::path config_path)
        : config_path_(std::move(config_path)) {}

    /// @brief Load configuration from JSON file
    [[nodiscard]] std::expected<AppConfig, std::string> load() override {
        // Check if file exists
        if (!std::filesystem::exists(config_path_)) {
            return std::unexpected("Config file not found: " + config_path_.string());
        }

        // Read file content
        std::ifstream file(config_path_);
        if (!file.is_open()) {
            return std::unexpected("Failed to open config file: " + config_path_.string());
        }

        // Parse JSON
        nlohmann::json json;
        try {
            file >> json;
        } catch (const nlohmann::json::exception& e) {
            return std::unexpected("Failed to parse JSON: " + std::string(e.what()));
        }

        // Convert to AppConfig
        try {
            AppConfig config;

            config.environment = json.value("environment", "development");

            // Server config
            if (json.contains("server")) {
                const auto& server = json["server"];
                config.server.host = server.value("host", "0.0.0.0");
                config.server.port = server.value("port", 8080);
                config.server.threads = server.value("threads", 4);
            }

            // Database config
            if (json.contains("database")) {
                const auto& db = json["database"];
                config.database.host = db.value("host", "localhost");
                config.database.port = db.value("port", 5432);
                config.database.name = db.value("name", "pfh_dev");
                config.database.user = db.value("user", "pfh_user");
                config.database.password = db.value("password", "");
                config.database.pool_size = db.value("pool_size", 10);
                config.database.connection_timeout = std::chrono::seconds(
                    db.value("connection_timeout", 30)
                );
            }

            // JWT config
            if (json.contains("jwt")) {
                const auto& jwt = json["jwt"];
                config.jwt.secret = jwt.value("secret", "");
                config.jwt.access_token_expiry = std::chrono::seconds(
                    jwt.value("access_token_expiry_seconds", 900)
                );
                config.jwt.refresh_token_expiry = std::chrono::seconds(
                    jwt.value("refresh_token_expiry_seconds", 2592000)
                );
            }

            // Logging config
            if (json.contains("logging")) {
                const auto& logging = json["logging"];
                config.logging.level = parse_log_level(logging.value("level", "info"));
                config.logging.output = parse_log_output(logging.value("output", "console"));
                config.logging.file = logging.value("file", "logs/pfh.log");
            }

            // Scheduler config
            if (json.contains("scheduler")) {
                const auto& scheduler = json["scheduler"];
                config.scheduler.exchange_rate_refresh_interval = std::chrono::minutes(
                    scheduler.value("exchange_rate_refresh_interval_minutes", 60)
                );
            }

            // Exchange rate config
            if (json.contains("exchange_rate")) {
                const auto& exchange_rate = json["exchange_rate"];
                config.exchange_rate.provider = exchange_rate.value("provider", "mock");
                config.exchange_rate.api_key = exchange_rate.value("api_key", "");
            }

            // Validate required fields
            if (config.jwt.secret.empty()) {
                return std::unexpected("JWT secret is required but not provided");
            }

            return config;

        } catch (const std::exception& e) {
            return std::unexpected("Failed to parse config: " + std::string(e.what()));
        }
    }

private:
    std::filesystem::path config_path_;

    [[nodiscard]] static LogLevel parse_log_level(const std::string& level) {
        if (level == "trace") return LogLevel::Trace;
        if (level == "debug") return LogLevel::Debug;
        if (level == "info") return LogLevel::Info;
        if (level == "warn" || level == "warning") return LogLevel::Warning;
        if (level == "error") return LogLevel::Error;
        if (level == "critical") return LogLevel::Critical;
        return LogLevel::Info;
    }

    [[nodiscard]] static LogOutput parse_log_output(const std::string& output) {
        if (output == "console") return LogOutput::Console;
        if (output == "file") return LogOutput::File;
        if (output == "both") return LogOutput::Both;
        return LogOutput::Console;
    }
};

} // namespace pfh::infrastructure
