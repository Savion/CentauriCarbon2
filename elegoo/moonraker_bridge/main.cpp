/*****************************************************************************
 * @Description : Moonraker-compatible bridge for the Elegoo CC2.
 *
 * Usage:
 *   moonraker_bridge [-s /tmp/elegoo_uds] [-p 7125] [-w /opt/usr/mainsail]
 *
 * Options:
 *   -s  Path to elegoo_printer Unix socket  (default: /tmp/elegoo_uds)
 *   -p  HTTP/WS port to listen on          (default: 7125)
 *   -w  Path to web UI static files        (optional, e.g. Mainsail dist/)
 *****************************************************************************/
#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <unistd.h>

#include "cc2_client.h"
#include "bridge_server.h"

static std::atomic<bool> g_running{true};

static void sig_handler(int) {
    g_running = false;
}

int main(int argc, char** argv) {
    std::string socket_path = "/tmp/elegoo_uds";
    std::string webroot;
    int port = 7125;

    int opt;
    while ((opt = getopt(argc, argv, "s:p:w:h")) != -1) {
        switch (opt) {
        case 's': socket_path = optarg; break;
        case 'p': port = std::stoi(optarg); break;
        case 'w': webroot = optarg; break;
        case 'h':
        default:
            std::cerr << "Usage: " << argv[0]
                      << " [-s socket_path] [-p port] [-w webroot]\n";
            return 1;
        }
    }

    std::signal(SIGINT,  sig_handler);
    std::signal(SIGTERM, sig_handler);
    std::signal(SIGPIPE, SIG_IGN); // Ignore broken pipes

    std::cerr << "[moonraker_bridge] Starting up\n"
              << "  CC2 socket : " << socket_path << "\n"
              << "  Listen port: " << port << "\n"
              << "  Web root   : " << (webroot.empty() ? "(none)" : webroot) << "\n";

    auto cc2 = std::make_shared<CC2Client>(socket_path);
    cc2->start();

    BridgeServer bridge(cc2, port, webroot);
    bridge.start();

    // Keep running until SIGINT/SIGTERM
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cerr << "[moonraker_bridge] Shutting down\n";
    bridge.stop();
    cc2->stop();
    return 0;
}
