#include "config.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

AppConfig AppConfig::load(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open config: " + path);
    nlohmann::json json;
    input >> json;
    AppConfig config;
    config.davUrl = json.value("davUrl", "");
    config.remotePath = json.value("remotePath", "");
    config.username = json.value("username", "");
    config.password = json.value("password", "");
    config.pollIntervalMs = json.value("pollIntervalMs", 3000L);
    if (config.pollIntervalMs < 250) config.pollIntervalMs = 250;
    if (config.davUrl.empty()) throw std::runtime_error("davUrl is required");
    return config;
}
