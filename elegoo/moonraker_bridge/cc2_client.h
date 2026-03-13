/*****************************************************************************
 * @Description : CC2 Unix socket client — connects to /tmp/elegoo_uds,
 *                sends JSON-RPC style requests (delimited by 0x03) and
 *                receives responses. Subscription updates are forwarded
 *                to a registered callback.
 *****************************************************************************/
#pragma once

#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <map>
#include <future>
#include <atomic>
#include "json.h"

class CC2Client {
public:
    using UpdateCallback = std::function<void(const json&)>;

    explicit CC2Client(const std::string& socket_path = "/tmp/elegoo_uds");
    ~CC2Client();

    // Start background read + reconnect loop
    void start();
    void stop();

    bool is_connected() const { return sockfd >= 0; }

    // Synchronous request — blocks until response or timeout.
    // Returns the full response JSON {"id":N,"result":{...}} or {} on error.
    json request(const std::string& method,
                 const json& params = json::object(),
                 int timeout_ms = 5000);

    // Called for every unsolicited message (subscription updates, id==0)
    void set_update_callback(UpdateCallback cb);

private:
    void reconnect_loop();
    void read_loop();
    void send_raw(const json& msg);
    void handle_message(const json& msg);
    void fail_all_pending();

    std::string socket_path;
    int sockfd;
    std::atomic<bool> running;
    std::atomic<int64_t> next_id;

    std::mutex send_mutex;      // serialises writes to sockfd
    std::mutex pending_mutex;   // protects pending map
    std::map<int64_t, std::promise<json>> pending;

    UpdateCallback update_cb;
    std::mutex cb_mutex;

    std::thread bg_thread;
    std::string recv_buffer;
};
