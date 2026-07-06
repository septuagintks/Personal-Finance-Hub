// Personal Finance Hub - Bootstrap Entry Point
// Version: 1.0
// C++23

#include "pfh/infrastructure/config.h"
#include "pfh/infrastructure/json_config_loader.h"
#include "pfh/infrastructure/logger.h"
#include <iostream>
#include <filesystem>

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "Personal Finance Hub - Backend Service" << std::endl;
    std::cout << "Version: 0.1.0-alpha" << std::endl;
    std::cout << "Initializing..." << std::endl;

    // Determine config file path
    std::filesystem::path config_path = "config/config.local.json";
    if (!std::filesystem::exists(config_path)) {
        std::cerr << "Warning: config.local.json not found, using example config" << std::endl;
        config_path = "config/config.example.json";
    }

    // Load configuration
    pfh::infrastructure::JsonConfigLoader config_loader(config_path);
    auto config_result = config_loader.load();

    if (!config_result) {
        std::cerr << "Failed to load configuration: " << config_result.error() << std::endl;
        return 1;
    }

    const auto& config = config_result.value();
    std::cout << "Configuration loaded successfully" << std::endl;
    std::cout << "Environment: " << config.environment << std::endl;

    // Initialize logger
    pfh::infrastructure::Logger::initialize(config.logging);

    // Log startup information
    spdlog::info("=================================================");
    spdlog::info("Personal Finance Hub - Backend Service");
    spdlog::info("Version: 0.1.0-alpha");
    spdlog::info("Environment: {}", config.environment);
    spdlog::info("Server: {}:{}", config.server.host, config.server.port);
    spdlog::info("=================================================");

    // TODO: Initialize Drogon application
    // TODO: Setup database connection pool
    // TODO: Register controllers and middleware
    // TODO: Start server

    spdlog::info("Initialization complete (application logic pending)");
    std::cout << "Press Ctrl+C to exit" << std::endl;

    return 0;
}
