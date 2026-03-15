/*****************************************************************************
 * @Description : Moonraker bridge — HTTP + WebSocket server implementation
 *****************************************************************************/
#include "bridge_server.h"

#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include "hv/HttpMessage.h"
#include "hv/HttpContext.h"

// ── Moonraker → CC2 method translation ────────────────────────────────────
static std::string moonraker_to_cc2(const std::string& method) {
    // Dots to slashes, drop "printer." prefix
    // e.g. printer.gcode.script → gcode/script
    //      printer.objects.query → objects/query
    //      printer.info          → info
    static const std::map<std::string, std::string> table = {
        {"printer.info",              "info"},
        {"printer.gcode.script",      "gcode/script"},
        {"printer.gcode.help",        "gcode/help"},
        {"printer.gcode.restart",     "gcode/restart"},
        {"printer.gcode.firmware_restart", "gcode/firmware_restart"},
        {"printer.objects.list",      "objects/list"},
        {"printer.objects.query",     "objects/query"},
        {"printer.objects.subscribe", "objects/subscribe"},
        {"printer.emergency_stop",    "emergency_stop"},
        {"printer.restart",           "gcode/restart"},
        {"printer.firmware_restart",  "gcode/firmware_restart"},
    };
    auto it = table.find(method);
    return (it != table.end()) ? it->second : "";
}

// ── Constructor / Destructor ───────────────────────────────────────────────
BridgeServer::BridgeServer(std::shared_ptr<CC2Client> client, int port,
                           const std::string& webroot)
    : client(std::move(client))
    , port(port)
    , webroot(webroot)
{
    // WebSocketServer inherits HttpServer; register both services explicitly
    server.registerHttpService(&http_service);
    server.registerWebSocketService(&ws_service);
    setup_http_routes(http_service);
    setup_ws_service(ws_service);
}

BridgeServer::~BridgeServer() {
    stop();
}

void BridgeServer::start() {
    server.setPort(port);
    server.setThreadNum(4);
    server.start();
    std::cerr << "[bridge] Moonraker bridge listening on port " << port << "\n";
    start_subscription();
}

void BridgeServer::stop() {
    server.stop();
}

// ── CC2 subscription setup ─────────────────────────────────────────────────
void BridgeServer::start_subscription() {
    // Subscribe to the objects OrcaSlicer cares most about.
    // CC2 will push updates every ~0.5s to our Unix socket connection.
    json params;
    params["objects"] = {
        {"toolhead",        nullptr},
        {"extruder",        nullptr},
        {"heater_bed",      nullptr},
        {"heaters",         nullptr},
        {"print_stats",     nullptr},
        {"fan",             nullptr},
        {"gcode_move",      nullptr},
        {"virtual_sdcard",  nullptr},
        {"idle_timeout",    nullptr},
        {"webhooks",        nullptr}
    };
    params["response_template"] = json::object();

    // Register the callback before subscribing
    client->set_update_callback([this](const json& msg) {
        broadcast_update(msg);
    });

    // The subscribe request is fire-and-forget here; response is just the
    // initial status dump which we don't need to surface specially.
    std::thread([this, params]() {
        // Wait until CC2 is connected
        for (int i = 0; i < 30 && !client->is_connected(); ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        if (client->is_connected())
            client->request("objects/subscribe", params, 10000);
    }).detach();
}

// ── Broadcast CC2 update → all WS clients ─────────────────────────────────
void BridgeServer::broadcast_update(const json& cc2_update) {
    // CC2 update format: {"id":0,"params":{"eventtime":N,"status":{...}}}
    // Moonraker expects: {"jsonrpc":"2.0","method":"notify_status_update",
    //                     "params":[{"eventtime":N,"status":{...}}]}
    json notify;
    notify["jsonrpc"] = "2.0";
    notify["method"] = "notify_status_update";

    if (cc2_update.contains("params")) {
        notify["params"] = json::array({cc2_update["params"]});
    } else {
        notify["params"] = json::array({cc2_update});
    }

    std::string payload = notify.dump();

    std::lock_guard<std::mutex> lock(ws_clients_mutex);
    for (auto& ch : ws_clients) {
        if (ch && ch->isConnected()) {
            ch->send(payload);
        }
    }
}

// ── Helper: forward to CC2, wrap in Moonraker HTTP response ───────────────
int BridgeServer::cc2_http(const std::string& method,
                            const json& params,
                            HttpResponse* resp) {
    json cc2_resp = client->request(method, params);

    resp->content_type = APPLICATION_JSON;

    if (!cc2_resp.contains("result")) {
        // Error or timeout
        json err;
        err["error"] = cc2_resp.value("error", json{{"message", "CC2 request failed or timed out"}});
        resp->body = err.dump();
        return 500;
    }

    // Moonraker wraps results in {"result": ...}
    json out;
    out["result"] = cc2_resp["result"];
    resp->body = out.dump();
    return 200;
}

// ── HTTP route setup ───────────────────────────────────────────────────────
void BridgeServer::setup_http_routes(hv::HttpService& svc) {
    svc.AllowCORS();

    // ── Synthesised endpoints ─────────────────────────────────────────────

    // GET /server/info  (Moonraker server metadata — synthesised)
    svc.GET("/server/info", [this](HttpRequest*, HttpResponse* resp) -> int {
        json r;
        r["klippy_connected"] = client->is_connected();
        r["klippy_state"]     = client->is_connected() ? "ready" : "disconnected";
        r["components"]       = json::array({"connection_manager", "database",
                                             "file_manager", "klippy_apis"});
        r["registered_directories"] = json::array();
        r["warnings"]               = json::array();
        r["websocket_count"]        = 0;
        r["moonraker_version"]      = "v0.8.0-cc2bridge";
        json out; out["result"] = r;
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });

    // GET /api/version  (Fluidd / Mainsail compatibility)
    svc.GET("/api/version", [](HttpRequest*, HttpResponse* resp) -> int {
        json r;
        r["api"]    = "0.1.0";
        r["server"] = "1.1.0";
        r["text"]   = "cc2-moonraker-bridge";
        json out; out["result"] = r;
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });

    // GET /access/info  (no-auth)
    svc.GET("/access/info", [](HttpRequest*, HttpResponse* resp) -> int {
        json r;
        r["default_source"] = "moonraker";
        r["api_key_enabled"] = false;
        json out; out["result"] = r;
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });

    // GET /access/oneshot_token  (return empty token so Orca doesn't block)
    svc.GET("/access/oneshot_token", [](HttpRequest*, HttpResponse* resp) -> int {
        json out; out["result"] = "";
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });

    // ── Forwarded to CC2 ──────────────────────────────────────────────────

    // GET /printer/info
    svc.GET("/printer/info", [this](HttpRequest*, HttpResponse* resp) -> int {
        json cc2 = client->request("info");
        resp->content_type = APPLICATION_JSON;
        if (!cc2.contains("result")) {
            json e; e["error"] = {{"message", "CC2 not connected"}};
            resp->body = e.dump(); return 500;
        }
        json r = cc2["result"];
        // Add Klipper-style fields OrcaSlicer expects
        if (!r.contains("klipper_path"))
            r["klipper_path"] = r.value("elegoo_path", "/home/eeb001/elegoo");
        if (!r.contains("components"))
            r["components"] = json::array({"webhooks", "extruder", "heaters", "gcode"});
        json out; out["result"] = r;
        resp->body = out.dump();
        return 200;
    });

    // POST /printer/gcode/script   body: {"script":"G28"}
    svc.POST("/printer/gcode/script", [this](HttpRequest* req, HttpResponse* resp) -> int {
        json body;
        try { body = json::parse(req->body); } catch (...) {}
        json params;
        params["script"] = body.value("script", "");
        return cc2_http("gcode/script", params, resp);
    });

    // GET /printer/gcode/help
    svc.GET("/printer/gcode/help", [this](HttpRequest*, HttpResponse* resp) -> int {
        return cc2_http("gcode/help", json::object(), resp);
    });

    // GET /printer/objects/list
    svc.GET("/printer/objects/list", [this](HttpRequest*, HttpResponse* resp) -> int {
        return cc2_http("objects/list", json::object(), resp);
    });

    // GET/POST /printer/objects/query
    // Accepts ?objects[toolhead]=temperature as query params or JSON body
    svc.Handle("GET", "/printer/objects/query", [this](HttpRequest* req, HttpResponse* resp) -> int {
        // Build objects map from query params  e.g. ?extruder=temperature&heater_bed=
        json objects = json::object();
        for (auto& kv : req->query_params) {
            if (kv.second.empty())
                objects[kv.first] = nullptr;
            else
                objects[kv.first] = json::array({kv.second});
        }
        if (objects.empty()) objects = nullptr; // query all
        json params; params["objects"] = objects;
        return cc2_http("objects/query", params, resp);
    });
    svc.Handle("POST", "/printer/objects/query", [this](HttpRequest* req, HttpResponse* resp) -> int {
        json body;
        try { body = json::parse(req->body); } catch (...) {}
        json params;
        params["objects"] = body.value("objects", json(nullptr));
        return cc2_http("objects/query", params, resp);
    });

    // POST /printer/emergency_stop
    svc.POST("/printer/emergency_stop", [this](HttpRequest*, HttpResponse* resp) -> int {
        return cc2_http("emergency_stop", json::object(), resp);
    });

    // POST /printer/restart
    svc.POST("/printer/restart", [this](HttpRequest*, HttpResponse* resp) -> int {
        return cc2_http("gcode/restart", json::object(), resp);
    });

    // POST /printer/firmware_restart
    svc.POST("/printer/firmware_restart", [this](HttpRequest*, HttpResponse* resp) -> int {
        return cc2_http("gcode/firmware_restart", json::object(), resp);
    });

    // ── Static web UI (Mainsail / Fluidd) ────────────────────────────────
    // Must be registered LAST so all API routes above take priority.
    // Enable with -w /path/to/mainsail on the command line.
    if (!webroot.empty()) {
        svc.Static("/", webroot.c_str());
        std::cerr << "[bridge] Serving web UI from: " << webroot << "\n";
    }
}

// ── WebSocket service setup ────────────────────────────────────────────────
void BridgeServer::setup_ws_service(hv::WebSocketService& ws) {
    ws.setPingInterval(20000); // 20s keepalive ping

    ws.onopen = [this](const WebSocketChannelPtr& ch, const HttpRequestPtr&) {
        std::lock_guard<std::mutex> lock(ws_clients_mutex);
        ws_clients.insert(ch);
        std::cerr << "[bridge] WS client connected\n";
    };

    ws.onclose = [this](const WebSocketChannelPtr& ch) {
        std::lock_guard<std::mutex> lock(ws_clients_mutex);
        ws_clients.erase(ch);
        std::cerr << "[bridge] WS client disconnected\n";
    };

    ws.onmessage = [this](const WebSocketChannelPtr& ch, const std::string& raw) {
        // Run in detached thread so we don't block the IO thread
        std::thread([this, ch, raw]() {
            handle_ws_rpc(ch, raw);
        }).detach();
    };
}

// ── WebSocket JSON-RPC handler ─────────────────────────────────────────────
void BridgeServer::handle_ws_rpc(const WebSocketChannelPtr& ch, const std::string& raw) {
    json req;
    try {
        req = json::parse(raw);
    } catch (...) {
        return;
    }

    // Required fields: method (and optionally id, params)
    if (!req.contains("method")) return;

    std::string method = req["method"];
    json params = req.value("params", json::object());
    int64_t id = req.value("id", (int64_t)-1);

    auto send_response = [&](const json& result) {
        if (id < 0) return; // notification, no response needed
        json resp;
        resp["jsonrpc"] = "2.0";
        resp["id"] = id;
        resp["result"] = result;
        if (ch && ch->isConnected())
            ch->send(resp.dump());
    };

    auto send_error = [&](const std::string& msg) {
        if (id < 0) return;
        json resp;
        resp["jsonrpc"] = "2.0";
        resp["id"] = id;
        resp["error"] = {{"code", -32603}, {"message", msg}};
        if (ch && ch->isConnected())
            ch->send(resp.dump());
    };

    // ── Synthesised methods ───────────────────────────────────────────────
    if (method == "server.info") {
        json r;
        r["klippy_connected"]   = client->is_connected();
        r["klippy_state"]       = client->is_connected() ? "ready" : "disconnected";
        r["moonraker_version"]  = "v0.8.0-cc2bridge";
        r["components"]         = json::array({"klippy_apis"});
        r["registered_directories"] = json::array();
        r["warnings"]           = json::array();
        send_response(r);
        return;
    }

    if (method == "access.oneshot_token") {
        send_response("");
        return;
    }

    // ── Forward to CC2 ────────────────────────────────────────────────────
    std::string cc2_method = moonraker_to_cc2(method);
    if (cc2_method.empty()) {
        send_error("Unknown method: " + method);
        return;
    }

    json cc2_resp = client->request(cc2_method, params);

    if (!cc2_resp.contains("result")) {
        send_error("CC2 request failed or timed out");
        return;
    }

    json result = cc2_resp["result"];

    // Special post-processing for printer.info
    if (method == "printer.info") {
        if (!result.contains("klipper_path"))
            result["klipper_path"] = result.value("elegoo_path", "/home/eeb001/elegoo");
        if (!result.contains("components"))
            result["components"] = json::array({"webhooks", "extruder", "heaters"});
    }

    // Special post-processing for printer.objects.subscribe:
    // Moonraker expects {"eventtime":N,"status":{...}} in result
    if (method == "printer.objects.subscribe" && result.contains("status")) {
        // result is already in the right shape
    }

    send_response(result);
}
