// bHaptics: the WebSocket client (bhaptics_link.hpp).

#include "vkcore/bhaptics_link.hpp"

#include <windows.h>

#include <winhttp.h>

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace evr::vkcore {

namespace {

// WinHTTP, from System32 on first use; never unloaded.
struct WinHttp {
    decltype(&WinHttpOpen) open = nullptr;
    decltype(&WinHttpConnect) connect = nullptr;
    decltype(&WinHttpOpenRequest) openRequest = nullptr;
    decltype(&WinHttpSetOption) setOption = nullptr;
    decltype(&WinHttpSetTimeouts) setTimeouts = nullptr;
    decltype(&WinHttpSendRequest) sendRequest = nullptr;
    decltype(&WinHttpReceiveResponse) receiveResponse = nullptr;
    decltype(&WinHttpQueryHeaders) queryHeaders = nullptr;
    decltype(&WinHttpWebSocketCompleteUpgrade) completeUpgrade = nullptr;
    decltype(&WinHttpWebSocketSend) wsSend = nullptr;
    decltype(&WinHttpWebSocketReceive) wsReceive = nullptr;
    decltype(&WinHttpWebSocketShutdown) wsShutdown = nullptr;
    decltype(&WinHttpCloseHandle) closeHandle = nullptr;
    bool ok = false;
};

// Milliseconds for the name, the connection, a send and the upgrade's answer (all on this PC).
constexpr int kResolveMs = 1000;
constexpr int kConnectMs = 1000;
constexpr int kSendMs = 2000;
constexpr int kReceiveMs = 2000;
// How long close() waits for the reader to end.
constexpr DWORD kReaderJoinMs = 2000;
constexpr std::size_t kReplyLogChars = 300;

template <typename T>
void load(HMODULE module, const char* name, T& out, bool& ok) {
    out = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(module, name)));
    ok = ok && out != nullptr;
}

const WinHttp& winHttp() {
    static const WinHttp api = [] {
        WinHttp w;
        const HMODULE module = LoadLibraryExW(L"winhttp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) {
            return w;
        }
        bool ok = true;
        load(module, "WinHttpOpen", w.open, ok);
        load(module, "WinHttpConnect", w.connect, ok);
        load(module, "WinHttpOpenRequest", w.openRequest, ok);
        load(module, "WinHttpSetOption", w.setOption, ok);
        load(module, "WinHttpSetTimeouts", w.setTimeouts, ok);
        load(module, "WinHttpSendRequest", w.sendRequest, ok);
        load(module, "WinHttpReceiveResponse", w.receiveResponse, ok);
        load(module, "WinHttpQueryHeaders", w.queryHeaders, ok);
        load(module, "WinHttpWebSocketCompleteUpgrade", w.completeUpgrade, ok);
        load(module, "WinHttpWebSocketSend", w.wsSend, ok);
        load(module, "WinHttpWebSocketReceive", w.wsReceive, ok);
        load(module, "WinHttpWebSocketShutdown", w.wsShutdown, ok);
        load(module, "WinHttpCloseHandle", w.closeHandle, ok);
        w.ok = ok;
        return w;
    }();
    return api;
}

std::wstring wide(const std::string& text) {
    return std::wstring(text.begin(), text.end()); // ASCII paths only
}

std::string lastError(const char* what) {
    return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")";
}

} // namespace

struct WebSocketLink::Impl {
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET socket = nullptr;
    HANDLE reader = nullptr;
    std::atomic<bool> open{false};
    mutable std::mutex replyMutex;
    std::string firstReply;

    static DWORD WINAPI readerMain(void* self) {
        static_cast<Impl*>(self)->read();
        return 0;
    }

    void read() {
        const WinHttp& w = winHttp();
        std::vector<char> buffer(4096);
        std::string message;
        bool kept = false;
        while (open.load()) {
            DWORD got = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            const DWORD r =
                w.wsReceive(socket, buffer.data(), static_cast<DWORD>(buffer.size()), &got, &type);
            if (r == ERROR_WINHTTP_TIMEOUT) {
                continue; // a quiet Player is not a lost one
            }
            if (r != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                break;
            }
            if (!kept && message.size() < kReplyLogChars) {
                message.append(buffer.data(), got);
            }
            if (!kept && (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                          type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)) {
                kept = true;
                std::lock_guard lock(replyMutex);
                firstReply = message.substr(0, kReplyLogChars);
            }
        }
        open.store(false);
    }

    // True when the reader has ended (or never started), so the Impl may go.
    bool closeAll() {
        const WinHttp& w = winHttp();
        const bool wasOpen = open.exchange(false);
        if (socket && wasOpen) {
            // Sends the close frame without waiting for the answer.
            w.wsShutdown(socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        }
        // Closing the handle also ends a receive the reader is blocked in.
        for (HINTERNET* h : {&socket, &connection, &session}) {
            if (*h) {
                w.closeHandle(*h);
                *h = nullptr;
            }
        }
        bool ended = true;
        if (reader) {
            ended = WaitForSingleObject(reader, kReaderJoinMs) == WAIT_OBJECT_0;
            CloseHandle(reader);
            reader = nullptr;
        }
        return ended;
    }
};

WebSocketLink::WebSocketLink() = default;

WebSocketLink::~WebSocketLink() {
    close();
}

bool WebSocketLink::available(std::string& error) {
    if (!winHttp().ok) {
        error = "WinHTTP with WebSocket support (Windows 8 or later) could not be loaded";
        return false;
    }
    return true;
}

bool WebSocketLink::open(int port, const std::string& path, std::string& error) {
    close();
    if (!available(error)) {
        return false;
    }
    const WinHttp& w = winHttp();
    impl_ = std::make_unique<Impl>();
    Impl& i = *impl_;
    i.session =
        w.open(L"EternalVR", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!i.session) {
        error = lastError("WinHttpOpen");
        close();
        return false;
    }
    w.setTimeouts(i.session, kResolveMs, kConnectMs, kSendMs, kReceiveMs);
    i.connection = w.connect(i.session, L"127.0.0.1", static_cast<INTERNET_PORT>(port), 0);
    if (!i.connection) {
        error = lastError("WinHttpConnect");
        close();
        return false;
    }
    const std::wstring widePath = wide(path);
    HINTERNET request = w.openRequest(i.connection, L"GET", widePath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) {
        error = lastError("WinHttpOpenRequest");
        close();
        return false;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    const bool upgraded =
        w.setOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) &&
        w.sendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        w.receiveResponse(request, nullptr) &&
        w.queryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                       WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (!upgraded) {
        error = lastError("connecting");
        w.closeHandle(request);
        close();
        return false;
    }
    if (status != 101) {
        error = "the server answered HTTP " + std::to_string(status) + " instead of a WebSocket upgrade";
        w.closeHandle(request);
        close();
        return false;
    }
    i.socket = w.completeUpgrade(request, 0);
    w.closeHandle(request);
    if (!i.socket) {
        error = lastError("WinHttpWebSocketCompleteUpgrade");
        close();
        return false;
    }
    i.open.store(true);
    i.reader = CreateThread(nullptr, 0, &Impl::readerMain, &i, 0, nullptr);
    if (!i.reader) {
        error = lastError("starting the reader");
        close();
        return false;
    }
    return true;
}

bool WebSocketLink::sendText(const std::string& message, std::string& error) {
    if (!isOpen()) {
        error = "not connected";
        return false;
    }
    const DWORD r = winHttp().wsSend(impl_->socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                     const_cast<char*>(message.data()), static_cast<DWORD>(message.size()));
    if (r != NO_ERROR) {
        error = "WinHttpWebSocketSend failed (error " + std::to_string(r) + ")";
        close();
        return false;
    }
    return true;
}

bool WebSocketLink::isOpen() const {
    return impl_ && impl_->open.load();
}

std::string WebSocketLink::firstReply() const {
    if (!impl_) {
        return {};
    }
    std::lock_guard lock(impl_->replyMutex);
    return impl_->firstReply;
}

void WebSocketLink::close() {
    if (impl_ && !impl_->closeAll()) {
        // A reader that did not end still uses its Impl: left to it rather than freed under it.
        static_cast<void>(impl_.release());
    }
    impl_.reset();
}

} // namespace evr::vkcore
