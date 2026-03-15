/*****************************************************************************
 * @Description : Moonraker bridge — HTTP + WebSocket server implementation
 *****************************************************************************/
#include "bridge_server.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include "hv/HttpMessage.h"
#include "hv/HttpContext.h"

// ── Filesystem helpers ─────────────────────────────────────────────────────
static const std::string GCODE_ROOT  = "/opt/usr/gcode";
static const std::string CONFIG_ROOT = "/opt/usr/config";
static const std::string LOGS_ROOT   = "/home/eeb001/printer_data/logs";

static std::string root_to_path(const std::string& root) {
    if (root == "gcodes") return GCODE_ROOT;
    if (root == "config") return CONFIG_ROOT;
    if (root == "logs")   return LOGS_ROOT;
    return "";
}

// Returns a JSON object like {"filename":"foo.gcode","size":N,"modified":T,"permissions":"rw"}
static json file_to_json(const std::string& name, const std::string& full_path,
                          const std::string& permissions = "rw") {
    struct stat st;
    json f;
    f["filename"] = name;
    f["permissions"] = permissions;
    if (stat(full_path.c_str(), &st) == 0) {
        f["size"]     = (int64_t)st.st_size;
        f["modified"] = (double)st.st_mtime;
    } else {
        f["size"]     = 0;
        f["modified"] = 0.0;
    }
    return f;
}

// Read directory; returns JSON object with dirs/files/disk_usage/root_info
static json read_directory(const std::string& dir_path, const std::string& root_name,
                            const std::string& permissions = "rw") {
    json dirs  = json::array();
    json files = json::array();

    DIR* d = opendir(dir_path.c_str());
    if (d) {
        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            std::string name = ent->d_name;
            if (name == "." || name == "..") continue;
            std::string full = dir_path + "/" + name;
            struct stat st;
            if (stat(full.c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                json dj;
                dj["dirname"]     = name;
                dj["size"]        = 0;
                dj["modified"]    = (double)st.st_mtime;
                dj["permissions"] = permissions;
                dirs.push_back(dj);
            } else if (S_ISREG(st.st_mode)) {
                files.push_back(file_to_json(name, full, permissions));
            }
        }
        closedir(d);
    }

    // disk usage
    json disk;
    struct statvfs svfs;
    if (statvfs(dir_path.c_str(), &svfs) == 0) {
        uint64_t bsize = svfs.f_frsize ? svfs.f_frsize : svfs.f_bsize;
        disk["total"] = (int64_t)(svfs.f_blocks * bsize);
        disk["used"]  = (int64_t)((svfs.f_blocks - svfs.f_bfree) * bsize);
        disk["free"]  = (int64_t)(svfs.f_bavail * bsize);
    } else {
        disk["total"] = 6700000000LL;
        disk["used"]  = 1200000000LL;
        disk["free"]  = 5000000000LL;
    }

    json result;
    result["dirs"]       = dirs;
    result["files"]      = files;
    result["disk_usage"] = disk;
    result["root_info"]  = {{"name", root_name}, {"permissions", permissions}};
    return result;
}

// Flat list of files under a root (for /server/files/list)
static json list_files_flat(const std::string& dir_path,
                             const std::string& prefix = "") {
    json arr = json::array();
    DIR* d = opendir(dir_path.c_str());
    if (!d) return arr;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        std::string full = dir_path + "/" + name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        std::string rel = prefix.empty() ? name : prefix + "/" + name;
        if (S_ISDIR(st.st_mode)) {
            // Recurse into subdirectory
            json sub = list_files_flat(full, rel);
            for (auto& item : sub) arr.push_back(item);
        } else if (S_ISREG(st.st_mode)) {
            json f = file_to_json(rel, full);
            f["path"] = rel; // flat listing uses "path" not "filename"
            f.erase("filename");
            arr.push_back(f);
        }
    }
    closedir(d);
    return arr;
}

// Helper: split "gcodes/sub/file.gcode" → {"gcodes", "sub/file.gcode"}
static std::pair<std::string,std::string> split_root_path(const std::string& path) {
    auto slash = path.find('/');
    if (slash == std::string::npos) return {path, ""};
    return {path.substr(0, slash), path.substr(slash + 1)};
}

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
    start_proc_stat_broadcast();
}

// Periodically broadcast notify_proc_stat_update so Mainsail's System Loads panel updates
void BridgeServer::start_proc_stat_broadcast() {
    std::thread([this]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            json notify;
            notify["jsonrpc"] = "2.0";
            notify["method"]  = "notify_proc_stat_update";
            json stat;
            stat["moonraker_stats"]  = json::object();
            stat["throttled_state"]  = {{"bits", 0}, {"flags", json::array()}};
            stat["cpu_temp"]         = 45.0;
            stat["network"]          = json::object();
            stat["system_cpu_usage"] = {{"cpu", 5.0}};
            stat["system_memory"]    = {{"total", 524288}, {"available", 262144}, {"used", 262144}};
            stat["system_uptime"]    = (double)time(nullptr);
            notify["params"]         = json::array({stat});
            std::string payload = notify.dump();
            std::lock_guard<std::mutex> lock(ws_clients_mutex);
            for (auto& ch : ws_clients) {
                if (ch && ch->isConnected()) ch->send(payload);
            }
        }
    }).detach();
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
        {"webhooks",        nullptr},
        {"configfile",      nullptr},
        {"display_status",  nullptr}
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

    // ── /server/database/item  (HTTP GET + POST) ──────────────────────────
    // Mainsail uses both WebSocket RPC and HTTP for database access.
    svc.GET("/server/database/item", [this](HttpRequest* req, HttpResponse* resp) -> int {
        std::string ns  = req->GetParam("namespace");
        std::string key = req->GetParam("key");
        resp->content_type = APPLICATION_JSON;
        std::lock_guard<std::mutex> lock(db_mutex);

        if (key.empty()) {
            // No key — return ALL items in this namespace as a value object.
            // Used by Mainsail's loadBackupableNamespaces feature.
            json values = json::object();
            std::string prefix = ns + "\x1f";
            for (auto& kv : db_store) {
                if (kv.first.size() > prefix.size() &&
                    kv.first.substr(0, prefix.size()) == prefix) {
                    values[kv.first.substr(prefix.size())] = kv.second;
                }
            }
            json r; r["namespace"] = ns; r["key"] = json(nullptr); r["value"] = values;
            json out; out["result"] = r;
            resp->body = out.dump();
            return 200;
        }

        std::string db_key = ns + "\x1f" + key;
        auto it = db_store.find(db_key);
        if (it == db_store.end()) {
            resp->body = "{\"error\":{\"message\":\"Key not found\",\"code\":404}}";
            return 404;
        }
        json r; r["namespace"] = ns; r["key"] = key; r["value"] = it->second;
        json out; out["result"] = r;
        resp->body = out.dump();
        return 200;
    });

    svc.POST("/server/database/item", [this](HttpRequest* req, HttpResponse* resp) -> int {
        json body;
        try { body = json::parse(req->body); } catch (...) {}
        // params can come from JSON body or query string
        std::string ns  = body.value("namespace", req->GetParam("namespace"));
        std::string key = body.value("key",       req->GetParam("key"));
        json value      = body.contains("value") ? body["value"] : json(nullptr);
        std::string db_key = ns + "\x1f" + key;
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            db_store[db_key] = value;
        }
        json r; r["namespace"] = ns; r["key"] = key; r["value"] = value;
        json out; out["result"] = r;
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });

    svc.Handle("DELETE", "/server/database/item", [this](HttpRequest* req, HttpResponse* resp) -> int {
        std::string ns  = req->GetParam("namespace");
        std::string key = req->GetParam("key");
        std::string db_key = ns + "\x1f" + key;
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            db_store.erase(db_key);
        }
        resp->content_type = APPLICATION_JSON;
        resp->body = "{\"result\":{}}";
        return 200;
    });

    // ── /server/database/list  (HTTP GET) ────────────────────────────────
    svc.GET("/server/database/list", [this](HttpRequest*, HttpResponse* resp) -> int {
        std::lock_guard<std::mutex> lock(db_mutex);
        std::set<std::string> ns_set;
        for (auto& kv : db_store) {
            auto sep = kv.first.find('\x1f');
            if (sep != std::string::npos)
                ns_set.insert(kv.first.substr(0, sep));
        }
        json r; r["namespaces"] = json::array();
        for (auto& ns : ns_set) r["namespaces"].push_back(ns);
        json out; out["result"] = r;
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });

    // ── API catch-all: return JSON 404 for unknown /server/ /printer/ /machine/ paths ──
    // Must come BEFORE Static() so API paths don't fall through to index.html.
    auto json_404 = [](HttpRequest* req, HttpResponse* resp) -> int {
        resp->content_type = APPLICATION_JSON;
        resp->body = "{\"error\":{\"message\":\"Not Found\",\"code\":404}}";
        std::cerr << "[http] 404 " << req->path << "\n";
        return 404;
    };
    // /server/files/roots  — Mainsail may call this via HTTP (not just WS).
    // Must come before the /server/files/* wildcard catch-all.
    svc.GET("/server/files/roots", [](HttpRequest*, HttpResponse* resp) -> int {
        json r = json::array();
        json gcodes;
        gcodes["name"]        = "gcodes";
        gcodes["path"]        = "/opt/usr/gcode";
        gcodes["permissions"] = "rw";
        r.push_back(gcodes);
        json config;
        config["name"]        = "config";
        config["path"]        = "/opt/usr/config";
        config["permissions"] = "rw";
        r.push_back(config);
        json out; out["result"] = r;
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        std::cerr << "[http] GET /server/files/roots -> 200\n";
        return 200;
    });

    // /server/files/list?root=gcodes  — list files in root from filesystem
    svc.GET("/server/files/list", [](HttpRequest* req, HttpResponse* resp) -> int {
        std::string root = req->GetParam("root");
        if (root.empty()) root = "gcodes";
        std::string dir = root_to_path(root);
        json arr = dir.empty() ? json::array() : list_files_flat(dir);
        json out; out["result"] = arr;
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });

    // /server/files/get_directory?path=config  — return actual directory listing.
    // Mainsail's Config Files panel calls this via HTTP to list the config root.
    svc.GET("/server/files/get_directory", [](HttpRequest* req, HttpResponse* resp) -> int {
        std::string path = req->GetParam("path");
        std::cerr << "[http] GET /server/files/get_directory path=" << path << "\n";
        // path can be "gcodes", "config", or "gcodes/subdir"
        std::string root_name = path.empty() ? "gcodes" : path.substr(0, path.find('/'));
        std::string root_dir  = root_to_path(root_name);
        std::string sub_path  = (path.find('/') != std::string::npos)
                                    ? path.substr(path.find('/') + 1) : "";
        std::string full_dir  = root_dir.empty() ? "" :
                                (sub_path.empty() ? root_dir : root_dir + "/" + sub_path);
        json r = full_dir.empty() ? json({{"dirs", json::array()}, {"files", json::array()},
                                          {"disk_usage", {{"total",0},{"used",0},{"free",0}}},
                                          {"root_info",  {{"name", root_name},{"permissions","rw"}}}})
                                  : read_directory(full_dir, root_name);
        json out; out["result"] = r;
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });

    // ── File upload: POST /server/files/upload ────────────────────────────
    // multipart/form-data; fields: file (the file), root, path
    svc.POST("/server/files/upload", [this](HttpRequest* req, HttpResponse* resp) -> int {
        // libhv puts multipart files in req->form
        std::string root = req->GetParam("root");
        if (root.empty()) root = "gcodes";
        std::string dest_dir = root_to_path(root);
        std::string path = req->GetParam("path");
        if (!path.empty()) dest_dir += "/" + path;

        resp->content_type = APPLICATION_JSON;
        if (dest_dir.empty()) {
            resp->body = "{\"error\":{\"message\":\"Unknown root\",\"code\":400}}";
            return 400;
        }

        // Check if a file part was submitted
        auto it = req->form.find("file");
        if (it == req->form.end()) {
            resp->body = "{\"error\":{\"message\":\"No file in upload\",\"code\":400}}";
            return 400;
        }

        const auto& fp = it->second;
        std::string filename = fp.filename.empty() ? "upload.gcode" : fp.filename;
        // Sanitize filename (no path traversal)
        auto slash = filename.rfind('/');
        if (slash != std::string::npos) filename = filename.substr(slash + 1);

        std::string out_path = dest_dir + "/" + filename;
        std::ofstream ofs(out_path, std::ios::binary);
        if (!ofs) {
            resp->body = "{\"error\":{\"message\":\"Cannot write file\",\"code\":500}}";
            return 500;
        }
        ofs.write(fp.content.data(), fp.content.size());
        ofs.close();

        std::string rel = path.empty() ? filename : path + "/" + filename;
        json item = file_to_json(rel, out_path);
        item["path"] = rel;
        item.erase("filename");
        json out; out["result"] = {{"item", item}, {"print_started", false}, {"action", "create"}};
        resp->body = out.dump();
        std::cerr << "[http] POST /server/files/upload -> " << out_path << "\n";
        return 201;
    });

    // ── History HTTP endpoints ────────────────────────────────────────────
    svc.GET("/server/history/list", [](HttpRequest*, HttpResponse* resp) -> int {
        json r;
        r["count"]         = 0;
        r["jobs"]          = json::array();
        r["all_jobs_flag"] = false;
        json out; out["result"] = r;
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });
    svc.GET("/server/history/totals", [](HttpRequest*, HttpResponse* resp) -> int {
        json r;
        r["job_totals"] = {
            {"total_jobs", 0}, {"total_time", 0.0},
            {"total_print_time", 0.0}, {"total_filament_used", 0.0},
            {"longest_job", 0.0}, {"longest_print", 0.0}
        };
        json out; out["result"] = r;
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });
    svc.Handle("DELETE", "/server/history/job", [](HttpRequest*, HttpResponse* resp) -> int {
        json out; out["result"] = {{"deleted_jobs", json::array()}};
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });
    svc.POST("/server/history/reset_totals", [](HttpRequest*, HttpResponse* resp) -> int {
        json out; out["result"] = json::object();
        resp->content_type = APPLICATION_JSON; resp->body = out.dump(); return 200;
    });

    // ── File move/copy (HTTP) — used by Mainsail rename/move dialog ───────
    svc.POST("/server/files/move", [](HttpRequest* req, HttpResponse* resp) -> int {
        json body; try { body = json::parse(req->body); } catch (...) {}
        resp->content_type = APPLICATION_JSON;
        std::string src = body.value("source", "");
        std::string dst = body.value("dest", "");
        if (src.empty() || dst.empty()) {
            resp->body = "{\"error\":{\"message\":\"source/dest required\",\"code\":400}}"; return 400;
        }
        auto sp = split_root_path(src);
        auto dp = split_root_path(dst);
        std::string src_path = root_to_path(sp.first) + "/" + sp.second;
        std::string dst_path = root_to_path(dp.first) + "/" + dp.second;
        if (src_path.find("..") != std::string::npos || dst_path.find("..") != std::string::npos) {
            resp->body = "{\"error\":{\"message\":\"Forbidden\",\"code\":403}}"; return 403;
        }
        if (rename(src_path.c_str(), dst_path.c_str()) != 0) {
            resp->body = "{\"error\":{\"message\":\"Move failed\",\"code\":500}}"; return 500;
        }
        json out; out["result"] = {{"item",{{"root",dp.first},{"path",dp.second}}},
                                    {"source_item",{{"root",sp.first},{"path",sp.second}}},
                                    {"action","move_file"}};
        resp->body = out.dump(); return 200;
    });
    svc.POST("/server/files/copy", [](HttpRequest* req, HttpResponse* resp) -> int {
        json body; try { body = json::parse(req->body); } catch (...) {}
        resp->content_type = APPLICATION_JSON;
        std::string src = body.value("source", "");
        std::string dst = body.value("dest", "");
        if (src.empty() || dst.empty()) {
            resp->body = "{\"error\":{\"message\":\"source/dest required\",\"code\":400}}"; return 400;
        }
        auto sp = split_root_path(src);
        auto dp = split_root_path(dst);
        std::string src_path = root_to_path(sp.first) + "/" + sp.second;
        std::string dst_path = root_to_path(dp.first) + "/" + dp.second;
        if (src_path.find("..") != std::string::npos || dst_path.find("..") != std::string::npos) {
            resp->body = "{\"error\":{\"message\":\"Forbidden\",\"code\":403}}"; return 403;
        }
        std::ifstream ifs(src_path, std::ios::binary);
        std::ofstream ofs(dst_path, std::ios::binary);
        if (!ifs || !ofs) {
            resp->body = "{\"error\":{\"message\":\"Copy failed\",\"code\":500}}"; return 500;
        }
        ofs << ifs.rdbuf();
        json out; out["result"] = {{"item",{{"root",dp.first},{"path",dp.second}}},{"action","create_file"}};
        resp->body = out.dump(); return 200;
    });

    // ── Job Queue HTTP endpoints ──────────────────────────────────────────
    svc.GET("/server/job_queue/status", [this](HttpRequest*, HttpResponse* resp) -> int {
        std::lock_guard<std::mutex> lock(jq_mutex);
        json r;
        r["queued_jobs"]  = job_queue;
        r["queue_state"]  = queue_state;
        json out; out["result"] = r;
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });
    svc.POST("/server/job_queue/job", [this](HttpRequest* req, HttpResponse* resp) -> int {
        json body;
        try { body = json::parse(req->body); } catch (...) {}
        resp->content_type = APPLICATION_JSON;
        std::lock_guard<std::mutex> lock(jq_mutex);
        auto add_job_http = [this](const std::string& fname) {
            json job;
            job["job_id"]        = "job_" + std::to_string(jq_id_counter++);
            job["filename"]      = fname;
            job["time_added"]    = (double)time(nullptr);
            job["time_in_queue"] = 0.0;
            job_queue.push_back(job);
        };
        if (body.contains("filenames") && body["filenames"].is_array()) {
            for (auto& fn : body["filenames"]) add_job_http(fn.get<std::string>());
        } else {
            std::string filename = body.value("filename", req->GetParam("filename"));
            if (filename.empty()) {
                resp->body = "{\"error\":{\"message\":\"filename required\",\"code\":400}}"; return 400;
            }
            add_job_http(filename);
        }
        json r; r["queued_jobs"] = job_queue; r["queue_state"] = queue_state;
        json out; out["result"] = r;
        resp->body = out.dump();
        return 200;
    });
    svc.Handle("DELETE", "/server/job_queue/job", [this](HttpRequest* req, HttpResponse* resp) -> int {
        // job_ids can be ?job_ids=id1,id2 or JSON body
        std::string ids_param = req->GetParam("job_ids");
        std::set<std::string> to_remove;
        // parse comma-separated or JSON array
        if (!ids_param.empty()) {
            std::istringstream ss(ids_param);
            std::string tok;
            while (std::getline(ss, tok, ',')) to_remove.insert(tok);
        } else {
            json body;
            try { body = json::parse(req->body); } catch (...) {}
            if (body.contains("job_ids") && body["job_ids"].is_array())
                for (auto& jid : body["job_ids"]) to_remove.insert(jid.get<std::string>());
        }
        std::lock_guard<std::mutex> lock(jq_mutex);
        std::vector<json> remaining;
        json deleted = json::array();
        for (auto& job : job_queue) {
            if (to_remove.count(job["job_id"].get<std::string>())) deleted.push_back(job["job_id"]);
            else remaining.push_back(job);
        }
        job_queue = remaining;
        json r; r["deleted_jobs"] = deleted; r["queued_jobs"] = job_queue; r["queue_state"] = queue_state;
        json out; out["result"] = r;
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });
    svc.POST("/server/job_queue/start", [this](HttpRequest*, HttpResponse* resp) -> int {
        std::lock_guard<std::mutex> lock(jq_mutex);
        queue_state = "loading";
        json r; r["queued_jobs"] = job_queue; r["queue_state"] = queue_state;
        json out; out["result"] = r;
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });
    svc.POST("/server/job_queue/pause", [this](HttpRequest*, HttpResponse* resp) -> int {
        std::lock_guard<std::mutex> lock(jq_mutex);
        queue_state = (queue_state == "paused") ? "ready" : "paused";
        json r; r["queued_jobs"] = job_queue; r["queue_state"] = queue_state;
        json out; out["result"] = r;
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });

    // ── Log file serving ─────────────────────────────────────────────────
    // Mainsail's Log Files panel requests klippy.log and moonraker.log.
    // Serve the actual CC2 log if it exists, otherwise return a stub.
    auto serve_log = [](const std::string& log_path, HttpResponse* resp) -> int {
        resp->SetHeader("Content-Type", "text/plain");
        std::ifstream ifs(log_path);
        if (ifs.good()) {
            std::ostringstream ss;
            ss << ifs.rdbuf();
            resp->body = ss.str();
        } else {
            resp->body = "[moonraker_bridge] Log not found: " + log_path + "\n";
        }
        return 200;
    };
    svc.GET("/server/files/logs/klippy.log", [serve_log](HttpRequest*, HttpResponse* resp) -> int {
        return serve_log("/home/eeb001/printer_data/logs/elegoo.log", resp);
    });
    svc.GET("/server/files/logs/moonraker.log", [serve_log](HttpRequest*, HttpResponse* resp) -> int {
        return serve_log("/home/eeb001/printer_data/logs/elegoo.log", resp);
    });
    // Flat path fallback (some Mainsail versions request without root prefix)
    svc.GET("/server/files/klippy.log", [serve_log](HttpRequest*, HttpResponse* resp) -> int {
        return serve_log("/home/eeb001/printer_data/logs/elegoo.log", resp);
    });

    // GET /server/files/{root}/{path...}  — serve actual file
    svc.GET("/server/files/*", [](HttpRequest* req, HttpResponse* resp) -> int {
        // path format: /server/files/{root}/{relative}
        std::string p = req->path;
        // Strip /server/files/ prefix
        if (p.size() > 15) p = p.substr(15); // strlen("/server/files/") = 15
        auto slash = p.find('/');
        if (slash == std::string::npos) {
            // Just a root name — shouldn't happen after specific handlers, 404 it
            resp->content_type = APPLICATION_JSON;
            resp->body = "{\"error\":{\"message\":\"Not Found\",\"code\":404}}";
            std::cerr << "[http] 404 " << req->path << "\n";
            return 404;
        }
        std::string root    = p.substr(0, slash);
        std::string relpath = p.substr(slash + 1);
        std::string dir     = root_to_path(root);
        if (dir.empty() || relpath.empty()) {
            resp->content_type = APPLICATION_JSON;
            resp->body = "{\"error\":{\"message\":\"Not Found\",\"code\":404}}";
            std::cerr << "[http] 404 " << req->path << "\n";
            return 404;
        }
        std::string full = dir + "/" + relpath;
        // Guard against path traversal
        if (full.find("..") != std::string::npos) {
            resp->content_type = APPLICATION_JSON;
            resp->body = "{\"error\":{\"message\":\"Forbidden\",\"code\":403}}";
            return 403;
        }
        std::ifstream ifs(full, std::ios::binary);
        if (!ifs.good()) {
            resp->content_type = APPLICATION_JSON;
            resp->body = "{\"error\":{\"message\":\"Not Found\",\"code\":404}}";
            std::cerr << "[http] 404 " << req->path << "\n";
            return 404;
        }
        std::ostringstream ss; ss << ifs.rdbuf();
        resp->body = ss.str();
        // Determine content type by extension
        std::string ext;
        auto dot = relpath.rfind('.');
        if (dot != std::string::npos) ext = relpath.substr(dot + 1);
        if (ext == "gcode" || ext == "g" || ext == "gc" || ext == "txt" || ext == "cfg" || ext == "log")
            resp->SetHeader("Content-Type", "text/plain");
        else if (ext == "json")
            resp->content_type = APPLICATION_JSON;
        else
            resp->SetHeader("Content-Type", "application/octet-stream");
        resp->SetHeader("Content-Disposition",
                        "attachment; filename=\"" + relpath.substr(relpath.rfind('/') + 1) + "\"");
        return 200;
    });
    svc.POST("/server/files/*",      json_404);
    // DELETE /server/files/{root}/{path...}  — delete file
    svc.Handle("DELETE", "/server/files/*", [](HttpRequest* req, HttpResponse* resp) -> int {
        std::string p = req->path;
        if (p.size() > 15) p = p.substr(15);
        auto slash = p.find('/');
        resp->content_type = APPLICATION_JSON;
        if (slash == std::string::npos) {
            resp->body = "{\"error\":{\"message\":\"Not Found\",\"code\":404}}"; return 404;
        }
        std::string root    = p.substr(0, slash);
        std::string relpath = p.substr(slash + 1);
        std::string dir     = root_to_path(root);
        if (dir.empty() || relpath.empty() || relpath.find("..") != std::string::npos) {
            resp->body = "{\"error\":{\"message\":\"Forbidden\",\"code\":403}}"; return 403;
        }
        std::string full = dir + "/" + relpath;
        bool ok = (remove(full.c_str()) == 0);
        if (!ok) {
            resp->body = "{\"error\":{\"message\":\"Delete failed\",\"code\":500}}"; return 500;
        }
        json out; out["result"] = {{"item", {{"path", relpath}}}, {"action", "delete"}};
        resp->body = out.dump();
        return 200;
    });
    svc.GET("/server/history/*",     json_404);
    svc.GET("/server/webcams/*",     json_404);
    svc.GET("/server/temperature_store", [](HttpRequest*, HttpResponse* resp) -> int {
        resp->content_type = APPLICATION_JSON;
        resp->body = "{\"result\":{}}";
        return 200;
    });
    svc.GET("/server/gcode_store", [](HttpRequest*, HttpResponse* resp) -> int {
        resp->content_type = APPLICATION_JSON;
        resp->body = "{\"result\":{\"gcode_store\":[]}}";
        return 200;
    });
    // ── /machine/peripherals/*  (Mainsail Devices panel) ────────────────
    svc.GET("/machine/peripherals/serial", [](HttpRequest*, HttpResponse* resp) -> int {
        json out; out["result"] = {{"serial_devices", json::array()}};
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });
    svc.GET("/machine/peripherals/usb", [](HttpRequest*, HttpResponse* resp) -> int {
        json out; out["result"] = {{"usb_devices", json::array()}};
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });
    svc.GET("/machine/peripherals/video", [](HttpRequest*, HttpResponse* resp) -> int {
        json out; out["result"] = {{"v4l2_devices", json::array()}};
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });
    svc.GET("/machine/peripherals/canbus", [](HttpRequest*, HttpResponse* resp) -> int {
        json out; out["result"] = {{"interfaces", json::array()}};
        resp->content_type = APPLICATION_JSON;
        resp->body = out.dump();
        return 200;
    });

    svc.GET("/machine/*",            json_404);
    svc.POST("/machine/*",           json_404);

    // ── Announcements HTTP ────────────────────────────────────────────────
    svc.GET("/server/announcements", [](HttpRequest*, HttpResponse* resp) -> int {
        json out; out["result"] = {{"entries", json::array()}, {"feeds", json::array()}};
        resp->content_type = APPLICATION_JSON; resp->body = out.dump(); return 200;
    });
    svc.POST("/server/announcements/dismiss", [](HttpRequest*, HttpResponse* resp) -> int {
        json out; out["result"] = json::object();
        resp->content_type = APPLICATION_JSON; resp->body = out.dump(); return 200;
    });

    // ── Power devices HTTP ────────────────────────────────────────────────
    svc.GET("/machine/device_power/devices", [](HttpRequest*, HttpResponse* resp) -> int {
        json out; out["result"] = {{"devices", json::array()}};
        resp->content_type = APPLICATION_JSON; resp->body = out.dump(); return 200;
    });
    svc.GET("/machine/device_power/status",  json_404);
    svc.POST("/machine/device_power/on",     json_404);
    svc.POST("/machine/device_power/off",    json_404);
    svc.POST("/machine/device_power/toggle", json_404);

    // ── Update Manager HTTP ───────────────────────────────────────────────
    svc.GET("/machine/update/status", [](HttpRequest*, HttpResponse* resp) -> int {
        json out; out["result"] = {{"busy", false}, {"version_info", json::object()},
                                    {"github_rate_limit_reset_time", 0}};
        resp->content_type = APPLICATION_JSON; resp->body = out.dump(); return 200;
    });

    // ── Spoolman HTTP ─────────────────────────────────────────────────────
    svc.GET("/server/spoolman/spool_id", [](HttpRequest*, HttpResponse* resp) -> int {
        json out; out["result"] = {{"spool_id", json(nullptr)}};
        resp->content_type = APPLICATION_JSON; resp->body = out.dump(); return 200;
    });

    // ── Sensors HTTP ──────────────────────────────────────────────────────
    svc.GET("/server/sensors/list", [](HttpRequest*, HttpResponse* resp) -> int {
        json out; out["result"] = {{"sensors", json::object()}};
        resp->content_type = APPLICATION_JSON; resp->body = out.dump(); return 200;
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
    std::cerr << "[ws] << " << method << "\n";
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

    // Mainsail/Fluidd send this as the very first WS message to identify
    // themselves. Moonraker responds with a unique connection_id.
    if (method == "server.connection.identify") {
        json r;
        r["connection_id"] = (int64_t)(reinterpret_cast<uintptr_t>(ch.get()) & 0x7FFFFFFF);
        send_response(r);
        return;
    }

    // Stub out server.* methods Mainsail polls during init
    if (method == "server.config") {
        json r;
        // Provide a file_manager config path so Mainsail doesn't show
        // "No configuration directory found"
        r["config"] = {
            {"file_manager", {
                {"config_path", "/opt/usr/config"},
                {"log_path",    "/opt/usr/logs"},
                {"queue_gcode_uploads", false},
                {"enable_object_processing", false}
            }}
        };
        r["orig"] = json::object();
        send_response(r);
        return;
    }

    if (method == "server.temperature_store") {
        send_response(json::object());
        return;
    }

    if (method == "server.gcode_store") {
        json r;
        r["gcode_store"] = json::array();
        send_response(r);
        return;
    }

    if (method == "server.files.list") {
        send_response(json::array());
        return;
    }

    if (method == "server.files.roots") {
        json r = json::array();
        json gcodes;
        gcodes["name"]        = "gcodes";
        gcodes["path"]        = "/opt/usr/gcode";
        gcodes["permissions"] = "rw";
        r.push_back(gcodes);
        json config;
        config["name"]        = "config";
        config["path"]        = "/opt/usr/config";
        config["permissions"] = "rw";
        r.push_back(config);
        send_response(r);
        return;
    }

    // ── server.database.* ─────────────────────────────────────────────────
    if (method == "server.database.list") {
        std::lock_guard<std::mutex> lock(db_mutex);
        std::set<std::string> ns_set;
        for (auto& kv : db_store) {
            auto sep = kv.first.find('\x1f');
            if (sep != std::string::npos)
                ns_set.insert(kv.first.substr(0, sep));
        }
        json r;
        r["namespaces"] = json::array();
        for (auto& ns : ns_set) r["namespaces"].push_back(ns);
        send_response(r);
        return;
    }

    if (method == "server.database.get_item") {
        std::string ns  = params.value("namespace", "");
        std::string key = params.value("key", "");
        std::string db_key = ns + "\x1f" + key;
        std::lock_guard<std::mutex> lock(db_mutex);
        auto it = db_store.find(db_key);
        if (it == db_store.end()) {
            send_error("Key not found: " + db_key);
        } else {
            json r;
            r["namespace"] = ns;
            r["key"]       = key;
            r["value"]     = it->second;
            send_response(r);
        }
        return;
    }

    if (method == "server.database.post_item") {
        std::string ns  = params.value("namespace", "");
        std::string key = params.value("key", "");
        json value      = params.value("value", json(nullptr));
        std::string db_key = ns + "\x1f" + key;
        std::lock_guard<std::mutex> lock(db_mutex);
        db_store[db_key] = value;
        json r;
        r["namespace"] = ns;
        r["key"]       = key;
        r["value"]     = value;
        send_response(r);
        return;
    }

    if (method == "server.database.delete_item") {
        std::string ns  = params.value("namespace", "");
        std::string key = params.value("key", "");
        std::string db_key = ns + "\x1f" + key;
        std::lock_guard<std::mutex> lock(db_mutex);
        db_store.erase(db_key);
        send_response(json::object());
        return;
    }

    // ── machine.* ────────────────────────────────────────────────────────
    if (method == "machine.system_info") {
        json info;
        info["cpu_info"] = {
            {"cpu_count", 4}, {"bits", "32bit"}, {"processor", "armv7l"},
            {"cpu_desc", "Allwinner A83T"}, {"hardware_desc", "Elegoo CC2"},
            {"model", "Elegoo CentauriCarbon2"},
            {"total_memory", 524288}, {"memory_units", "kB"}
        };
        info["sd_info"]            = {{"total_bytes",0},{"used_bytes",0},{"manufacturer_id",""}};
        info["distribution"]       = {{"name","TinaLinux"},{"id","tinalinux"},{"version","1.0"}};
        info["available_services"] = json::array();
        info["service_state"]      = json::object();
        info["virtualization"]     = {{"virt_type","none"},{"virt_identifier","none"}};
        info["network"]            = json::object();
        info["canbus"]             = json::object();
        json r; r["system_info"]   = info;
        send_response(r);
        return;
    }

    if (method == "machine.proc_stats") {
        json r;
        r["moonraker_stats"]    = json::array();
        r["throttled_state"]    = {{"bits", 0}, {"flags", json::array()}};
        r["cpu_temp"]           = 45.0;
        r["network"]            = json::object();
        r["system_cpu_usage"]   = {{"cpu", 0.0}};
        r["system_memory"]      = {{"total", 524288}, {"available", 262144}, {"used", 262144}};
        r["websocket_connections"] = (int)ws_clients.size();
        send_response(r);
        return;
    }

    if (method == "machine.update.status") {
        json r;
        r["busy"]           = false;
        r["github_rate_limit_reset_time"] = 0;
        r["version_info"]   = json::object();
        send_response(r);
        return;
    }

    if (method == "server.history.list") {
        json r;
        r["count"] = 0;
        r["jobs"]  = json::array();
        send_response(r);
        return;
    }

    if (method == "server.history.totals") {
        json r;
        r["job_totals"] = {
            {"total_jobs", 0}, {"total_time", 0.0},
            {"total_print_time", 0.0}, {"total_filament_used", 0.0},
            {"longest_job", 0.0}, {"longest_print", 0.0}
        };
        send_response(r);
        return;
    }

    if (method == "server.webcams.list") {
        json r;
        r["webcams"] = json::array();
        send_response(r);
        return;
    }

    if (method == "server.webcams.post_item" ||
        method == "server.webcams.update_item") {
        // Accept webcam creation/update — no actual webcam on CC2,
        // so just echo the params back as a confirmation.
        json r = params; // echo back whatever was sent
        if (!r.contains("uid")) r["uid"] = "";
        send_response(r);
        return;
    }

    if (method == "server.webcams.delete_item") {
        send_response(json::object());
        return;
    }

    // ── Announcements ─────────────────────────────────────────────────────
    if (method == "server.announcements.list") {
        json r;
        r["entries"] = json::array();
        r["feeds"]   = json::array();
        send_response(r);
        return;
    }
    if (method == "server.announcements.dismiss" ||
        method == "server.announcements.update_read") {
        send_response(json::object());
        return;
    }

    // ── Power devices (machine.device_power.*) ────────────────────────────
    if (method == "machine.device_power.devices") {
        json r; r["devices"] = json::array();
        send_response(r);
        return;
    }
    if (method == "machine.device_power.status" ||
        method == "machine.device_power.on"     ||
        method == "machine.device_power.off"    ||
        method == "machine.device_power.toggle") {
        send_response(json::object());
        return;
    }

    // ── Spoolman (optional, stub so panel doesn't error) ─────────────────
    if (method == "server.spoolman.get_spool_id" ||
        method == "server.spoolman.set_active_spool" ||
        method == "server.spoolman.proxy") {
        send_response(json::object());
        return;
    }

    // ── Sensor data ───────────────────────────────────────────────────────
    if (method == "server.sensors.list") {
        json r; r["sensors"] = json::object();
        send_response(r);
        return;
    }

    if (method == "server.files.metadata") {
        // Mainsail polls this for the currently-loaded file.
        // Return an empty object — no file loaded.
        send_response(json::object());
        return;
    }

    if (method == "server.files.get_directory") {
        std::string path = params.value("path", "");
        std::string root_name = path.empty() ? "gcodes" : path.substr(0, path.find('/'));
        std::string root_dir  = root_to_path(root_name);
        std::string sub_path  = (path.find('/') != std::string::npos)
                                    ? path.substr(path.find('/') + 1) : "";
        std::string full_dir  = root_dir.empty() ? "" :
                                (sub_path.empty() ? root_dir : root_dir + "/" + sub_path);
        json r = full_dir.empty()
            ? json({{"dirs", json::array()}, {"files", json::array()},
                    {"disk_usage", {{"total",0},{"used",0},{"free",0}}},
                    {"root_info",  {{"name", root_name}, {"permissions", "rw"}}}})
            : read_directory(full_dir, root_name);
        send_response(r);
        return;
    }

    if (method == "server.files.list") {
        std::string root = params.value("root", "gcodes");
        std::string dir  = root_to_path(root);
        json arr = dir.empty() ? json::array() : list_files_flat(dir);
        send_response(arr);
        return;
    }

    // ── Job queue WS methods ──────────────────────────────────────────────
    if (method == "server.job_queue.status") {
        std::lock_guard<std::mutex> lock(jq_mutex);
        json r; r["queued_jobs"] = job_queue; r["queue_state"] = queue_state;
        send_response(r);
        return;
    }
    if (method == "server.job_queue.post_job") {
        // Mainsail sends filenames as an array: {filenames: ["a.gcode", "b.gcode"]}
        // (but older versions may send singular filename)
        std::lock_guard<std::mutex> lock(jq_mutex);
        auto add_job = [this](const std::string& fname) {
            json job;
            job["job_id"]        = "job_" + std::to_string(jq_id_counter++);
            job["filename"]      = fname;
            job["time_added"]    = (double)time(nullptr);
            job["time_in_queue"] = 0.0;
            job_queue.push_back(job);
        };
        if (params.contains("filenames") && params["filenames"].is_array()) {
            for (auto& fn : params["filenames"]) add_job(fn.get<std::string>());
        } else if (params.contains("filename")) {
            add_job(params["filename"].get<std::string>());
        }
        json r; r["queued_jobs"] = job_queue; r["queue_state"] = queue_state;
        send_response(r);
        return;
    }
    if (method == "server.job_queue.delete_job") {
        // Supports {all: true} to clear queue, or {job_ids: [...]}
        std::lock_guard<std::mutex> lock(jq_mutex);
        if (params.value("all", false)) {
            job_queue.clear();
        } else {
            json ids = params.value("job_ids", json::array());
            std::set<std::string> to_remove;
            if (ids.is_array()) for (auto& jid : ids) to_remove.insert(jid.get<std::string>());
            std::vector<json> remaining;
            for (auto& job : job_queue)
                if (!to_remove.count(job["job_id"].get<std::string>())) remaining.push_back(job);
            job_queue = remaining;
        }
        json r; r["queued_jobs"] = job_queue; r["queue_state"] = queue_state;
        send_response(r);
        return;
    }
    if (method == "server.job_queue.start") {
        std::lock_guard<std::mutex> lock(jq_mutex);
        queue_state = "loading";
        json r; r["queued_jobs"] = job_queue; r["queue_state"] = queue_state;
        send_response(r);
        return;
    }
    if (method == "server.job_queue.pause") {
        std::lock_guard<std::mutex> lock(jq_mutex);
        queue_state = (queue_state == "paused") ? "ready" : "paused";
        json r; r["queued_jobs"] = job_queue; r["queue_state"] = queue_state;
        send_response(r);
        return;
    }
    if (method == "server.job_queue.jump") {
        send_response(json::object());
        return;
    }

    // ── File operations (WS) ──────────────────────────────────────────────
    // server.files.delete_file: {path: "gcodes/file.gcode"}
    if (method == "server.files.delete_file") {
        std::string path = params.value("path", "");
        auto rp = split_root_path(path);
        std::string full = root_to_path(rp.first);
        if (full.empty() || rp.second.empty() || rp.second.find("..") != std::string::npos) {
            send_error("Forbidden"); return;
        }
        full += "/" + rp.second;
        if (remove(full.c_str()) != 0) { send_error("Delete failed"); return; }
        json r; r["item"] = {{"root", rp.first}, {"path", rp.second}}; r["action"] = "delete_file";
        send_response(r);
        return;
    }
    // server.files.move: {source: "gcodes/a.gcode", dest: "gcodes/b.gcode"}
    if (method == "server.files.move") {
        std::string src = params.value("source", ""), dst = params.value("dest", "");
        auto sp = split_root_path(src), dp = split_root_path(dst);
        std::string sp_full = root_to_path(sp.first) + "/" + sp.second;
        std::string dp_full = root_to_path(dp.first) + "/" + dp.second;
        if (sp_full.find("..") != std::string::npos || dp_full.find("..") != std::string::npos) {
            send_error("Forbidden"); return;
        }
        if (rename(sp_full.c_str(), dp_full.c_str()) != 0) { send_error("Move failed"); return; }
        json r;
        r["item"]        = {{"root", dp.first}, {"path", dp.second}};
        r["source_item"] = {{"root", sp.first}, {"path", sp.second}};
        r["action"]      = "move_file";
        send_response(r);
        return;
    }
    // server.files.post_directory: {path: "gcodes/subdir"}
    if (method == "server.files.post_directory") {
        std::string path = params.value("path", "");
        auto rp = split_root_path(path);
        std::string full = root_to_path(rp.first);
        if (full.empty() || rp.second.empty() || rp.second.find("..") != std::string::npos) {
            send_error("Forbidden"); return;
        }
        full += "/" + rp.second;
        mkdir(full.c_str(), 0755);
        json r; r["item"] = {{"root", rp.first}, {"path", rp.second}}; r["action"] = "create_dir";
        send_response(r);
        return;
    }
    // server.files.delete_directory: {path: "gcodes/subdir"}
    if (method == "server.files.delete_directory") {
        std::string path = params.value("path", "");
        auto rp = split_root_path(path);
        std::string full = root_to_path(rp.first);
        if (full.empty() || rp.second.empty() || rp.second.find("..") != std::string::npos) {
            send_error("Forbidden"); return;
        }
        full += "/" + rp.second;
        rmdir(full.c_str());
        json r; r["item"] = {{"root", rp.first}, {"path", rp.second}}; r["action"] = "delete_dir";
        send_response(r);
        return;
    }
    // server.files.metascan: scan file metadata (stub — return empty)
    if (method == "server.files.metascan") {
        send_response(json::object());
        return;
    }
    // server.files.rollover_logs: roll log files (stub — return empty)
    if (method == "server.files.rollover_logs") {
        json r; r["rolled_over"] = json::array(); r["failed"] = json::object();
        send_response(r);
        return;
    }

    // ── Timelapse stubs ───────────────────────────────────────────────────
    if (method == "machine.timelapse.get_settings") {
        json r;
        r["enabled"]       = false;
        r["mode"]          = "layermacro";
        r["camera"]        = "";
        r["fps"]           = 25;
        r["quality"]       = "high";
        r["save_frames"]   = false;
        r["park_head"]     = false;
        r["park_pos"]      = "back_left";
        r["park_time"]     = 0.1;
        r["fw_retract"]    = false;
        r["constant_rate_factor"] = 23;
        r["output_framerate"]     = 30;
        r["variable_fps"]         = false;
        r["variable_fps_min"]     = 5;
        r["variable_fps_max"]     = 60;
        r["previewimage"]         = true;
        r["duplicatelastframe"]   = 0;
        r["extraoutputparams"]    = "";
        r["rotation"]             = 0;
        r["flip_x"]               = false;
        r["flip_y"]               = false;
        r["snapshoturl"]          = "";
        r["blockedhostname"]      = "";
        r["gcode_verbose"]        = false;
        r["run_at_print_start"]   = false;
        send_response(r);
        return;
    }
    if (method == "machine.timelapse.lastframeinfo") {
        json r; r["framecount"] = 0; r["lastframefile"] = "";
        send_response(r);
        return;
    }
    if (method == "machine.timelapse.post_settings") {
        // Echo back the settings, merged with defaults
        json r = params; // whatever was sent
        send_response(r);
        return;
    }

    // ── Spoolman active spool set ─────────────────────────────────────────
    if (method == "server.spoolman.post_spool_id") {
        json r; r["spool_id"] = params.value("spool_id", json(nullptr));
        send_response(r);
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

    // ── Inject virtual Mainsail-required objects ──────────────────────────
    // Mainsail warns if these are absent; CC2 doesn't expose them as gcode
    // macros, so we inject empty stubs so the UI stays quiet.
    static const std::vector<std::string> MAINSAIL_VIRTUAL_OBJECTS = {
        "display_status",
        "gcode_macro pause",
        "gcode_macro resume",
        "gcode_macro cancel_print"
    };

    if (method == "printer.objects.list" &&
        result.contains("objects") && result["objects"].is_array()) {
        auto& objs = result["objects"];
        for (const auto& vobj : MAINSAIL_VIRTUAL_OBJECTS) {
            bool found = false;
            for (const auto& o : objs)
                if (o.get<std::string>() == vobj) { found = true; break; }
            if (!found) objs.push_back(vobj);
        }
    }

    // For query/subscribe: fill in well-typed stubs for any requested objects
    // that CC2 didn't include, including virtual Mainsail objects.
    if ((method == "printer.objects.query" || method == "printer.objects.subscribe") &&
        result.contains("status") &&
        params.contains("objects") && params["objects"].is_object()) {
        auto& status = result["status"];
        for (auto it = params["objects"].begin(); it != params["objects"].end(); ++it) {
            const std::string& key = it.key();
            if (status.contains(key)) continue;

            // Provide proper default status values for known virtual objects
            if (key == "display_status") {
                status[key] = {{"progress", 0.0}, {"message", ""}};
            } else if (key == "gcode_macro pause" ||
                       key == "gcode_macro resume" ||
                       key == "gcode_macro cancel_print" ||
                       key.substr(0, 11) == "gcode_macro") {
                status[key] = {{"variables", json::object()}};
            } else if (key == "configfile") {
                // Inject synthetic configfile.config so Mainsail macro checks pass.
                // Without these sections Mainsail shows "PAUSE macro not found in config".
                json cfg_config = json::object();
                cfg_config["gcode_macro pause"]        = {{"gcode", ""}, {"description", "Pause print"}};
                cfg_config["gcode_macro resume"]       = {{"gcode", ""}, {"description", "Resume print"}};
                cfg_config["gcode_macro cancel_print"] = {{"gcode", ""}, {"description", "Cancel print"}};
                cfg_config["display_status"]           = json::object();
                // Merge in real config sections if CC2 already provided them
                if (status.contains("configfile") && status["configfile"].contains("config")) {
                    for (auto sit = status["configfile"]["config"].begin();
                         sit != status["configfile"]["config"].end(); ++sit) {
                        cfg_config[sit.key()] = sit.value();
                    }
                }
                status[key]["config"]   = cfg_config;
                status[key]["settings"] = json::object();
                status[key]["save_config_pending"] = false;
            } else {
                status[key] = json::object();
            }
        }
    }

    send_response(result);
}
