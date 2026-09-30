#pragma once

// A WebSocket client to the bHaptics Player on this PC (docs/BHAPTICS.md), for body_haptics.cpp.
//
// WinHTTP's own WebSocket support (Windows 8 and later), loaded from System32 only when bHaptics is on, so
// the layer imports nothing new. Blocking calls, for one owner thread: open() tries once (a refused
// connection on 127.0.0.1 fails at once when the Player is not running), sendText() sends one message. A
// second thread per connection reads and drops what the Player sends (its status), so its messages never
// pile up; the first one is kept for the log. Nothing here runs on a game thread.

#include <memory>
#include <string>

namespace evr::vkcore {

class WebSocketLink {
public:
    WebSocketLink();
    ~WebSocketLink();
    WebSocketLink(const WebSocketLink&) = delete;
    WebSocketLink& operator=(const WebSocketLink&) = delete;

    // False (with `error` set) when WinHTTP or its WebSocket functions cannot be loaded.
    static bool available(std::string& error);

    // Connects to ws://127.0.0.1:<port><path> and completes the upgrade. False with `error` set otherwise.
    bool open(int port, const std::string& path, std::string& error);
    // Sends one text message; false (and the link closed) when it fails.
    bool sendText(const std::string& message, std::string& error);
    // True while connected and the reader has seen no close or error.
    [[nodiscard]] bool isOpen() const;
    // The first message the Player sent on this connection (empty until one came), cut to a log line.
    [[nodiscard]] std::string firstReply() const;
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace evr::vkcore
