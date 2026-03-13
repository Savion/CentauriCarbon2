/*****************************************************************************
 * @Description : CC2 Unix socket client implementation
 *****************************************************************************/
#include "cc2_client.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <iostream>

CC2Client::CC2Client(const std::string& socket_path)
    : socket_path(socket_path)
    , sockfd(-1)
    , running(false)
    , next_id(1)
{}

CC2Client::~CC2Client() {
    stop();
}

void CC2Client::start() {
    running = true;
    bg_thread = std::thread([this]{ reconnect_loop(); });
}

void CC2Client::stop() {
    running = false;
    if (sockfd >= 0) {
        ::shutdown(sockfd, SHUT_RDWR);
        ::close(sockfd);
        sockfd = -1;
    }
    if (bg_thread.joinable())
        bg_thread.join();
    fail_all_pending();
}

void CC2Client::set_update_callback(UpdateCallback cb) {
    std::lock_guard<std::mutex> lock(cb_mutex);
    update_cb = std::move(cb);
}

// ── Reconnect loop ────────────────────────────────────────────────────────
void CC2Client::reconnect_loop() {
    while (running) {
        // Try to connect
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        sockfd = fd;
        recv_buffer.clear();
        std::cerr << "[cc2_client] Connected to " << socket_path << "\n";

        // Synchronous read until socket drops
        read_loop();

        sockfd = -1;
        ::close(fd);
        fail_all_pending();
        std::cerr << "[cc2_client] Disconnected, reconnecting...\n";
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

// ── Read loop ─────────────────────────────────────────────────────────────
void CC2Client::read_loop() {
    char buf[8192];
    while (running && sockfd >= 0) {
        int n = ::recv(sockfd, buf, sizeof(buf), 0);
        if (n <= 0) break;

        recv_buffer.append(buf, n);

        // Messages are delimited by 0x03 (ASCII ETX)
        size_t pos;
        while ((pos = recv_buffer.find('\x03')) != std::string::npos) {
            std::string msg_str = recv_buffer.substr(0, pos);
            recv_buffer = recv_buffer.substr(pos + 1);
            if (msg_str.empty()) continue;
            try {
                json msg = json::parse(msg_str);
                handle_message(msg);
            } catch (...) {
                // Ignore malformed messages
            }
        }
    }
}

// ── Message dispatch ─────────────────────────────────────────────────────
void CC2Client::handle_message(const json& msg) {
    // id > 0  → response to a pending request
    if (msg.contains("id") && msg["id"].is_number() && msg["id"] != 0) {
        int64_t id = msg["id"].get<int64_t>();
        std::lock_guard<std::mutex> lock(pending_mutex);
        auto it = pending.find(id);
        if (it != pending.end()) {
            it->second.set_value(msg);
            pending.erase(it);
        }
        return;
    }

    // id == 0 or no id → unsolicited update (subscription)
    std::lock_guard<std::mutex> lock(cb_mutex);
    if (update_cb) {
        update_cb(msg);
    }
}

// ── Synchronous request ───────────────────────────────────────────────────
json CC2Client::request(const std::string& method, const json& params, int timeout_ms) {
    if (sockfd < 0) {
        return json::object();
    }

    int64_t id = next_id++;

    std::promise<json> p;
    auto future = p.get_future();

    {
        std::lock_guard<std::mutex> lock(pending_mutex);
        pending.emplace(id, std::move(p));
    }

    json req;
    req["id"] = id;
    req["method"] = method;
    req["params"] = params;
    send_raw(req);

    if (future.wait_for(std::chrono::milliseconds(timeout_ms)) == std::future_status::timeout) {
        std::lock_guard<std::mutex> lock(pending_mutex);
        pending.erase(id);
        return json::object();
    }

    try {
        return future.get();
    } catch (...) {
        return json::object();
    }
}

// ── Send ──────────────────────────────────────────────────────────────────
void CC2Client::send_raw(const json& msg) {
    std::string data = msg.dump() + '\x03';
    std::lock_guard<std::mutex> lock(send_mutex);
    int fd = sockfd;
    if (fd < 0) return;
    size_t sent = 0;
    while (sent < data.size()) {
        int n = ::send(fd, data.c_str() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
    }
}

// ── Fail pending on disconnect ─────────────────────────────────────────────
void CC2Client::fail_all_pending() {
    std::lock_guard<std::mutex> lock(pending_mutex);
    for (auto& kv : pending) {
        kv.second.set_value(json::object());
    }
    pending.clear();
}
