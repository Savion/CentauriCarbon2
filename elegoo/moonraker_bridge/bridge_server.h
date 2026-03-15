/*****************************************************************************
 * @Description : Moonraker-compatible HTTP + WebSocket bridge server.
 *                Listens on port 7125 and translates Moonraker API calls
 *                to CC2 Unix socket protocol, making the CC2 appear as a
 *                standard Klipper printer to OrcaSlicer / Mainsail / Fluidd.
 *****************************************************************************/
#pragma once

#include <memory>
#include <set>
#include <mutex>
#include <string>
#include "json.h"
#include "cc2_client.h"

// Forward declarations for libhv types
#include "hv/WebSocketServer.h"
#include "hv/HttpService.h"

class BridgeServer {
public:
    // webroot: optional path to static web UI files (e.g. Mainsail dist/).
    // If non-empty, GET / and unmatched routes serve files from that directory.
    BridgeServer(std::shared_ptr<CC2Client> client, int port = 7125,
                 const std::string& webroot = "");
    ~BridgeServer();

    // Start HTTP+WS server (non-blocking)
    void start();
    void stop();

private:
    // Route registration
    void setup_http_routes(hv::HttpService& svc);
    void setup_ws_service(hv::WebSocketService& ws);

    // CC2 subscription management
    void start_subscription();

    // WebSocket JSON-RPC dispatcher
    void handle_ws_rpc(const WebSocketChannelPtr& ch, const std::string& raw);

    // Broadcast Moonraker notify_status_update to all WS clients
    void broadcast_update(const json& cc2_update);

    // Helper: call CC2, wrap result in Moonraker HTTP 200 JSON
    // Returns HTTP status code
    int cc2_http(const std::string& method,
                 const json& params,
                 HttpResponse* resp);

    std::shared_ptr<CC2Client> client;
    int port;
    std::string webroot;
    hv::HttpService  http_service;
    hv::WebSocketService ws_service;
    hv::WebSocketServer server;

    std::mutex       ws_clients_mutex;
    std::set<WebSocketChannelPtr> ws_clients;
};
