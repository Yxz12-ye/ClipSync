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
    config.serverUrl = json.value("serverUrl", "");
    if (config.serverUrl.empty() && json.contains("davUrl")) {
        throw std::runtime_error("legacy WebDAV config: replace davUrl/remotePath with serverUrl matching Zhongzhou Goose custom server address");
    }
    config.username = json.value("username", "");
    config.password = json.value("password", "");
    config.pollIntervalMs = json.value("pollIntervalMs", 3000L);
    if (config.pollIntervalMs < 250) config.pollIntervalMs = 250;
    if (config.serverUrl.empty()) throw std::runtime_error("serverUrl is required");
    if (config.serverUrl.rfind("http://", 0) != 0 && config.serverUrl.rfind("https://", 0) != 0)
        throw std::runtime_error("serverUrl must start with http:// or https://");
    if (config.serverUrl.find_first_of("?#\r\n") != std::string::npos)
        throw std::runtime_error("serverUrl must not contain a query, fragment or newline");
    return config;
}
