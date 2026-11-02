#include "sync_engine.hpp"

#include <chrono>

#include "../platform/clipboard.hpp"
#include "../utils/logger.hpp"

SyncEngine::SyncEngine(AppConfig config) : client_(config), pollIntervalMs_(config.pollIntervalMs) {}
SyncEngine::~SyncEngine() { stop(); }

bool SyncEngine::startupSync() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    try {
        auto remote = client_.pull();
        if (remote) {
            if (!ClipboardMonitor::setText(remote->text)) {
                client_.resetRevision();
                return false;
            }
            lastTextHash_ = remote->hash;
            return true;
        }
        auto local = ClipboardMonitor::getText();
        if (!client_.push(local)) return false;
        lastTextHash_ = textHash(local);
        return true;
    } catch (const std::exception& ex) {
        Logger::error(ex.what());
        return false;
    }
}

void SyncEngine::onClipboardChanged(const std::string& text) {
    // Serialize the whole transfer with polling, including the clipboard write,
    // so a remote update cannot race a local upload or echo back to the server.
    std::lock_guard<std::mutex> lock(stateMutex_);
    try {
        auto hash = textHash(text);
        if (hash == lastTextHash_ || text != ClipboardMonitor::getText()) return;
        if (client_.push(text)) lastTextHash_ = hash;
    } catch (const std::exception& ex) {
        Logger::error(ex.what());
    }
}

void SyncEngine::startPolling() {
    poller_ = std::thread([this] {
        while (!stopping_) {
            {
                std::unique_lock<std::mutex> lock(waitMutex_);
                if (wake_.wait_for(lock, std::chrono::milliseconds(pollIntervalMs_), [this] { return stopping_.load(); })) break;
            }
            std::lock_guard<std::mutex> lock(stateMutex_);
            try {
                auto remote = client_.pull();
                if (remote && remote->hash != lastTextHash_) {
                    if (ClipboardMonitor::setText(remote->text)) {
                        lastTextHash_ = remote->hash;
                    } else {
                        // Retry the current profile even if its ETag has not changed.
                        client_.resetRevision();
                    }
                }
            } catch (const std::exception& ex) {
                Logger::error(ex.what());
            }
        }
    });
}
void SyncEngine::stop() {
    {
        std::lock_guard<std::mutex> lock(waitMutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (poller_.joinable()) poller_.join();
}
