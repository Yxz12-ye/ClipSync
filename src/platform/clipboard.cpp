#ifdef _WIN32
#include "clipboard.hpp"

#include <windows.h>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>

#ifdef ERROR
#undef ERROR
#endif
#include "../utils/logger.hpp"

namespace { std::atomic<bool> stopRequested{false}; }
void requestClipboardMonitorStop() { stopRequested = true; }

std::string ClipboardMonitor::getText() {
    if (!OpenClipboard(nullptr)) return {};
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (!handle) { CloseClipboard(); return {}; }
    auto* text = static_cast<wchar_t*>(GlobalLock(handle));
    if (!text) { CloseClipboard(); return {}; }
    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string result(size > 1 ? size : 0, '\0');
    if (size > 1) {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
        result.resize(size - 1);
    }
    GlobalUnlock(handle); CloseClipboard(); return result;
}

bool ClipboardMonitor::setText(const std::string& text) {
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (size + 1) * sizeof(wchar_t));
    if (!memory || !OpenClipboard(nullptr)) { if (memory) GlobalFree(memory); return false; }
    auto* target = static_cast<wchar_t*>(GlobalLock(memory));
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), target, size);
    target[size] = L'\0'; GlobalUnlock(memory); EmptyClipboard(); SetClipboardData(CF_UNICODETEXT, memory); CloseClipboard(); return true;
}

bool ClipboardMonitor::run(const Callback& cb) {
    stopRequested = false;
    auto previous = getText();
    while (!stopRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        auto current = getText();
        if (current != previous) { previous = current; cb(current); }
    }
    return true;
}
#else
#include "clipboard.hpp"
bool ClipboardMonitor::run(const Callback&) { return false; }
bool ClipboardMonitor::setText(const std::string&) { return false; }
std::string ClipboardMonitor::getText() { return {}; }
#endif
