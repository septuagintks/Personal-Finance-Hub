// Personal Finance Hub - Bootstrap Entry Point

#include "pfh/infrastructure/json_config_loader.h"
#include "pfh/infrastructure/logger.h"

#ifdef PFH_HAS_POSTGRESQL
#include "pfh/bootstrap/production_composition_root.h"
#endif

#include <filesystem>
#include <iostream>

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifndef PFH_HAS_POSTGRESQL
    std::cerr
        << "pfh_server was built without production PostgreSQL/Drogon support. "
        << "Configure with -DPFH_BUILD_POSTGRESQL=ON to run the service.\n";
    return 2;
#else
    std::filesystem::path config_path = "config/config.local.json";
    if (!std::filesystem::exists(config_path)) {
        config_path = "config/config.example.json";
    }

    pfh::infrastructure::JsonConfigLoader config_loader(config_path);
    auto config = config_loader.load();
    if (!config) {
        std::cerr << "Service configuration failed: "
                  << config.error().message << '\n';
        return 1;
    }

    pfh::infrastructure::Logger::initialize(config->logging);
    spdlog::info(
        "Starting Personal Finance Hub environment={} listener={}:{}",
        config->environment,
        config->server.host,
        config->server.port);

    pfh::bootstrap::ProductionCompositionRoot composition(std::move(*config));
    auto initialized = composition.initialize();
    if (!initialized) {
        spdlog::critical(
            "Production bootstrap failed: {}",
            initialized.error().message);
        return 1;
    }
    composition.run();
    return 0;
#endif
}
