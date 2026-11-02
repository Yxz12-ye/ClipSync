#include "config.hpp"
#include "net/sync_clipboard.hpp"
#include "platform/clipboard.hpp"
#include "sync/sync_engine.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <nlohmann/json.hpp>

// A memory clipboard keeps protocol tests independent of the user's desktop.
namespace {
std::mutex clipboardMutex;
std::string clipboard = "local text";
bool failNextWrite = false;
}
std::string ClipboardMonitor::getText() {
    std::lock_guard<std::mutex> lock(clipboardMutex);
    return clipboard;
}
bool ClipboardMonitor::setText(const std::string& text) {
    std::lock_guard<std::mutex> lock(clipboardMutex);
    if (failNextWrite) { failNextWrite = false; return false; }
    clipboard = text;
    return true;
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    nlohmann::json result;
    try {
        auto config = AppConfig::load(argv[1]);
        const std::string action = argv[2];
        if (action == "startup" || action == "poll" || action == "retry") {
            failNextWrite = action == "retry";
            SyncEngine engine(config);
            result["ok"] = engine.startupSync();
            if (result["ok"].get<bool>()) engine.onClipboardChanged(ClipboardMonitor::getText());
            if (action == "poll" || action == "retry") {
                engine.startPolling();
                std::this_thread::sleep_for(std::chrono::milliseconds(900));
                engine.stop();
            }
            result["text"] = ClipboardMonitor::getText();
        } else {
            SyncClipboardClient client(config);
            if (action == "push") {
                std::ifstream input(argv[1]);
                auto fixture = nlohmann::json::parse(input);
                result["ok"] = client.push(fixture.at("text").get<std::string>());
            } else if (action == "pull") {
                auto first = client.pull();
                result["text"] = first ? nlohmann::json(first->text) : nlohmann::json(nullptr);
                result["hash"] = first ? first->hash : "";
                result["secondEmpty"] = !client.pull().has_value();
            } else if (action == "config") {
                result["interval"] = config.pollIntervalMs;
            } else return 2;
        }
    } catch (const std::exception& error) {
        result["error"] = error.what();
    }
    std::cout << result.dump() << std::endl;
}

