#include "webdav.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#ifdef ERROR
#undef ERROR
#endif
#include "../utils/logger.hpp"

namespace {
struct Response { long status = 0; std::string body; std::string etag; std::string error; };
size_t writeBody(char* ptr, size_t size, size_t count, void* data) {
    static_cast<std::string*>(data)->append(ptr, size * count); return size * count;
}
size_t writeHeader(char* ptr, size_t size, size_t count, void* data) {
    auto* response = static_cast<Response*>(data);
    std::string line(ptr, size * count);
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (key == "etag") {
            response->etag = line.substr(colon + 1);
            while (!response->etag.empty() && std::isspace(static_cast<unsigned char>(response->etag.front()))) response->etag.erase(response->etag.begin());
            while (!response->etag.empty() && std::isspace(static_cast<unsigned char>(response->etag.back()))) response->etag.pop_back();
            if (!response->etag.empty() && response->etag.back() == '\n') response->etag.pop_back();
            if (!response->etag.empty() && response->etag.back() == '\r') response->etag.pop_back();
        }
    }
    return size * count;
}
Response request(const std::string& method, const std::string& url, const AppConfig& config,
                 const std::string& body, const std::string& etag, const std::string& extraHeader = {}) {
    Response result;
    CURL* curl = curl_easy_init();
    if (!curl) { result.error = "curl_easy_init failed"; return result; }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &result);
    if (!config.username.empty()) {
        std::string userpass = config.username + ":" + config.password;
        curl_easy_setopt(curl, CURLOPT_USERPWD, userpass.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    }
    curl_slist* headers = nullptr;
    if (!body.empty()) headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!etag.empty()) headers = curl_slist_append(headers, ("If-None-Match: " + etag).c_str());
    if (!extraHeader.empty()) headers = curl_slist_append(headers, extraHeader.c_str());
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (method == "PUT") curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    auto code = curl_easy_perform(curl);
    if (code != CURLE_OK) result.error = curl_easy_strerror(code);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}
std::string trimSlashes(std::string value) {
    while (!value.empty() && value.front() == '/') value.erase(value.begin());
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}
}

std::string textHash(const std::string& text) {
    std::ostringstream out;
    out << std::hex << std::hash<std::string>{}(text);
    return out.str();
}

WebDavClient::WebDavClient(AppConfig config) : config_(std::move(config)) { curl_global_init(CURL_GLOBAL_DEFAULT); }

std::string WebDavClient::fileUrl() const {
    auto base = config_.davUrl;
    while (!base.empty() && base.back() == '/') base.pop_back();
    auto path = trimSlashes(config_.remotePath);
    if (!path.empty()) base += "/" + path;
    return base + "/clipboard/current.json";
}
std::string WebDavClient::directoryUrl() const { return fileUrl().substr(0, fileUrl().find_last_of('/')); }

bool WebDavClient::makeDirectories() {
    auto base = config_.davUrl;
    while (!base.empty() && base.back() == '/') base.pop_back();
    auto path = trimSlashes(config_.remotePath);
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '/')) if (!part.empty()) parts.push_back(part);
    parts.push_back("clipboard");
    auto current = base;
    for (const auto& item : parts) {
        current += "/" + item;
        auto response = request("MKCOL", current, config_, "", "");
        if (!(response.status >= 200 && response.status < 300) && response.status != 405 && response.status != 301 && response.status != 302 && response.status != 307 && response.status != 308) {
            Logger::error("MKCOL failed: HTTP " + std::to_string(response.status) + " " + response.error);
            return false;
        }
    }
    return true;
}

bool WebDavClient::push(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json profile{{"type", "text"}, {"hash", textHash(text)}, {"text", text}, {"has_data", false}, {"data_name", nullptr}, {"size", text.size()}, {"source", nullptr}};
    auto response = request("PUT", fileUrl(), config_, profile.dump(), "");
    if (response.status == 409 && makeDirectories()) response = request("PUT", fileUrl(), config_, profile.dump(), "");
    if (response.status >= 200 && response.status < 300) { etag_ = response.etag; return true; }
    Logger::error("PUT failed: HTTP " + std::to_string(response.status) + " " + response.error);
    return false;
}

std::optional<Profile> WebDavClient::pull() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto response = request("GET", fileUrl(), config_, "", etag_);
    if (response.status == 304 || response.status == 404) return std::nullopt;
    if (response.status < 200 || response.status >= 300) { Logger::error("GET failed: HTTP " + std::to_string(response.status) + " " + response.error); return std::nullopt; }
    etag_ = response.etag;
    if (response.body.empty()) return std::nullopt;
    Profile profile;
    try {
        auto json = nlohmann::json::parse(response.body);
        if (json.is_object() && json.contains("text")) profile.text = json.value("text", "");
        else profile.text = response.body;
    } catch (...) { profile.text = response.body; }
    profile.hash = textHash(profile.text); profile.etag = etag_;
    return profile;
}

bool WebDavClient::testConnection(std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto response = request("PROPFIND", directoryUrl(), config_, "", "", "Depth: 0");
    if (response.status == 401 || response.status == 403) { error = "authentication failed (HTTP " + std::to_string(response.status) + ")"; return false; }
    if ((response.status >= 200 && response.status < 300) || response.status == 404 || response.status == 405) return true;
    error = response.error.empty() ? "connection failed (HTTP " + std::to_string(response.status) + ")" : response.error;
    return false;
}
