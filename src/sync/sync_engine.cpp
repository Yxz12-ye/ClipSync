#include "sync_engine.hpp"

#include <chrono>

#include "../platform/clipboard.hpp"
#include "../utils/logger.hpp"

SyncEngine::SyncEngine(AppConfig config) : client_(config), pollIntervalMs_(config.pollIntervalMs) {}
SyncEngine::~SyncEngine() { stop(); }

bool SyncEngine::startupSync() {
    auto remote = client_.pull();
    if (remote) {
        lastTextHash_ = remote->hash; suppressedHash_ = remote->hash;
        return ClipboardMonitor::setText(remote->text);
    }
    auto local = ClipboardMonitor::getText();
    lastTextHash_ = textHash(local);
    return client_.push(local);
}

void SyncEngine::onClipboardChanged(const std::string& text) {
    auto hash = textHash(text);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (hash == suppressedHash_) { suppressedHash_.clear(); return; }
        if (hash == lastTextHash_) return;
    }
    if (client_.push(text)) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastTextHash_ = hash;
    }
}

void SyncEngine::startPolling() {
    poller_ = std::thread([this] {
        while (!stopping_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs_));
            if (stopping_) break;
            auto remote = client_.pull();
            bool changed = false;
            if (remote) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                changed = remote->hash != lastTextHash_;
            }
            if (remote && changed) {
                if (ClipboardMonitor::setText(remote->text)) {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    lastTextHash_ = remote->hash; suppressedHash_ = remote->hash;
                }
            }
        }
    });
}
void SyncEngine::stop() { stopping_ = true; if (poller_.joinable()) poller_.join(); }
