#include "sync_clipboard.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <limits>
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

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
                 const std::string& body, const std::string& etag) {
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
    {
        std::string userpass = config.username + ":" + config.password;
        curl_easy_setopt(curl, CURLOPT_USERPWD, userpass.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    }
    curl_slist* headers = nullptr;
    if (!body.empty()) headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!etag.empty()) headers = curl_slist_append(headers, ("If-None-Match: " + etag).c_str());
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    }
    auto code = curl_easy_perform(curl);
    if (code != CURLE_OK) result.error = curl_easy_strerror(code);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}
} // namespace

std::string textHash(const std::string& text) {
    if (text.size() > std::numeric_limits<ULONG>::max()) throw std::runtime_error("clipboard text is too large");
    unsigned char digest[32];
    const auto status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())), static_cast<ULONG>(text.size()),
        digest, sizeof(digest));
    if (status < 0) throw std::runtime_error("SHA-256 failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (auto value : digest) { result += hex[value >> 4]; result += hex[value & 15]; }
    return result;
}

SyncClipboardClient::SyncClipboardClient(AppConfig config) : config_(std::move(config)) { curl_global_init(CURL_GLOBAL_DEFAULT); }

std::string SyncClipboardClient::fileUrl() const {
    auto url = config_.serverUrl;
    while (!url.empty() && url.back() == '/') url.pop_back();
    auto lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    constexpr char name[] = "syncclipboard.json";
    if (lower.size() >= sizeof(name) - 1 && lower.compare(lower.size() - (sizeof(name) - 1), sizeof(name) - 1, name) == 0)
        return url;
    return url + "/SyncClipboard.json";
}

bool SyncClipboardClient::push(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json profile{{"Type", "Text"}, {"Hash", textHash(text)}, {"Text", text}, {"HasData", false}, {"DataName", ""}, {"Size", text.size()}};
    auto response = request("PUT", fileUrl(), config_, profile.dump(), "");
    if (response.error.empty() && response.status >= 200 && response.status < 300) { etag_.clear(); return true; }
    Logger::error("PUT failed: HTTP " + std::to_string(response.status) + " " + response.error);
    return false;
}

std::optional<Profile> SyncClipboardClient::pull() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto response = request("GET", fileUrl(), config_, "", etag_);
    if (response.error.empty() && (response.status == 304 || response.status == 404)) return std::nullopt;
    if (!response.error.empty() || response.status < 200 || response.status >= 300)
        throw std::runtime_error("GET failed: HTTP " + std::to_string(response.status) + " " + response.error);
    auto json = nlohmann::json::parse(response.body);
    if (!json.is_object()) throw std::runtime_error("invalid SyncClipboard profile: expected an object");
    const auto type = json.value("Type", json.value("type", std::string("Text")));
    auto lowerType = type;
    std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lowerType != "text" || json.value("HasData", json.value("hasData", false)))
        throw std::runtime_error("remote clipboard contains unsupported non-text data");
    const auto field = json.contains("Text") ? "Text" : json.contains("text") ? "text" : json.contains("Clipboard") ? "Clipboard" : "value";
    if (!json.contains(field) || !json.at(field).is_string())
        throw std::runtime_error("invalid SyncClipboard profile: missing string Text");
    Profile profile;
    profile.text = json.at(field).get<std::string>();
    profile.hash = textHash(profile.text);
    etag_ = response.etag;
    profile.etag = etag_;
    return profile;
}

void SyncClipboardClient::resetRevision() {
    std::lock_guard<std::mutex> lock(mutex_);
    etag_.clear();
}
