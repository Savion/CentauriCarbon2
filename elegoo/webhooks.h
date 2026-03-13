/***************************************************************************** 
 * @Author       : Gary
 * @Date         : 2024-11-19 05:57:38
 * @LastEditors  : zhangjxxxx
 * @LastEditTime : 2025-09-12 11:41:01
 * @Description  : The webhooks module in Klipper is used to interact with the printer
 * through a network interface. It provides a set of RESTful API endpoints that allow
 * users to remotely send commands, retrieve printer status information, manage files,
 * and more via the HTTP protocol.
 * 
 * Copyright (c) 2025 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/
#ifndef WEBHOOKS_H
#define WEBHOOKS_H
#include <iostream>
#include <string>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include "json.h"
#include <map>
#include <vector>
#include <functional>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <errno.h>
#include <sstream>
#include <sys/socket.h>
#include "reactor.h"
#include "gcode.h"
#include "exception_handler.h"
#include "utilities.h"

class WebHooks;
class Printer;
class ServerSocket; // Forward declaration
class WebRequest;   // Forward declaration
class QueryStatusHelper;
class GCodeHelper;

class ClientConnection : public std::enable_shared_from_this<ClientConnection>
{
public:
    ClientConnection(std::shared_ptr<ServerSocket> server, int sock);
    ~ClientConnection();

    void dump_request_log();
    void set_client_info(const json &client_info, const std::string &state_msg = "");
    void close();
    bool is_closed() const;
    void process_received(double eventtime);
    void json_send(const json &data);
    bool is_blocking;
    int blocking_count;
    int uid; // Unique identifier for the client
    std::shared_ptr<SelectReactor> reactor;
    std::shared_ptr<ServerSocket> server;
    std::shared_ptr<WebHooks> webhooks;
    std::shared_ptr<Printer> printer;

private:
    json process_request(std::shared_ptr<WebRequest> web_request);
    void do_send(double eventtime);
    int sock;
    std::shared_ptr<ReactorFileHandler> fd_handle;
    std::deque<std::pair<double, std::string>> request_log;
    std::string send_buffer;
    std::string partial_data;
};

class Sentinel
{
};

class WebRequest
{
public:
    WebRequest(std::shared_ptr<ClientConnection> client_conn, const std::string &request);
    ~WebRequest();
    std::shared_ptr<ClientConnection> get_client_connection() const;

    std::string get_method() const;
    std::string get_params() const;
    std::string get_result() const;
    std::string get_str(const std::string &item, std::string default_value = "");
    int get_int(const std::string &item, int default_value = INT_NONE);
    double get_double(const std::string &item, double default_value = DOUBLE_NONE);
    json get_dict(const std::string &item, json defalue_value = json::object());
    int get_client_connection_id();
    void set_error(const elegoo::common::WebRequestError &error);
    void send(const json &data);
    json finish();
    int error(const std::string &);
    bool is_method_need(std::string method);
    bool get_is_response() const { return is_response; }
    //   Json::value get(std::string& item, )
private:
    std::shared_ptr<ClientConnection> client_conn;
    json params;
    json result;
    int64_t id;
    std::string method;
    json response;
    bool is_error;
    bool is_response;
};

class ServerSocket : public std::enable_shared_from_this<ServerSocket>
{
public:
    ServerSocket(std::shared_ptr<WebHooks> webhooks, std::shared_ptr<Printer> printer);
    ~ServerSocket();

public:
    std::pair<bool, std::string> stats(double eventtime);
    void broadcast_message(const json &json);
    std::shared_ptr<SelectReactor> reactor;
    std::shared_ptr<WebHooks> webhooks;
    std::shared_ptr<Printer> printer;
    std::shared_ptr<ReactorFileHandler> fd_handle;

private:
    void handle_accept(double time);
    void handle_disconnect();
    void handle_shutdown();
    void remove_socket_file(const std::string &file_path);
    void pop_client(int client_id);
    int sockfd;
    std::map<int, std::shared_ptr<ClientConnection>> clients;
};

class WebHooks : public std::enable_shared_from_this<WebHooks>
{
public:
    WebHooks(std::shared_ptr<Printer> printer);
    void create_socket();
    void register_endpoint(const std::string &path, std::function<void(std::shared_ptr<WebRequest>)> callback);
    void register_mux_endpoint(const std::string &path, const std::string &key, const std::string &value, std::function<void(std::shared_ptr<WebRequest>)> callback);
    std::shared_ptr<ServerSocket> get_connection();
    void call_remote_method(const std::string &method, const json &params);

    // Handle functions
    void handle_list_endpoints(std::shared_ptr<WebRequest> web_request);
    void handle_info_request(std::shared_ptr<WebRequest> web_request);
    void handle_estop_request(std::shared_ptr<WebRequest> web_request);
    void handle_debug_info_request(std::shared_ptr<WebRequest> web_request);
    std::shared_ptr<WebHooks> get_shared();
    std::pair<bool, std::string> stats(double eventtime);
    void broadcast_message(const json &json);
    void report_error(int error_code, elegoo::common::ErrorLevel error_level, const std::string& msg);
    json get_status();
    std::function<void(std::shared_ptr<WebRequest>)> get_callback(const std::string &path);
    void handle_mux(std::shared_ptr<WebRequest> webRequest);
    std::string getDirectoryName(const std::string &filepath);
    std::string getParentPath(const std::string &path);

private:
    std::shared_ptr<Printer> printer;
    std::shared_ptr<GCodeHelper> gcode_helper;
    std::shared_ptr<QueryStatusHelper> query_status_helper;
    std::unordered_map<std::string, std::function<void(std::shared_ptr<WebRequest>)>> endpoints;
    std::unordered_map<std::string, std::pair<std::string, std::unordered_map<std::string, std::function<void(std::shared_ptr<WebRequest>)>>>> mux_endpoints;
    std::map<std::string, std::map<std::shared_ptr<ClientConnection>, json>> remote_methods; // Connection ID as key
    std::shared_ptr<ServerSocket> sconn;
};

class GCodeHelper
{
public:
    GCodeHelper(std::shared_ptr<Printer> printer);

    // Handler methods
    void handle_help(std::shared_ptr<WebRequest> web_request);
    void handle_script(std::shared_ptr<WebRequest> web_request);
    void handle_restart(std::shared_ptr<WebRequest> web_request);
    void handle_firmware_restart(std::shared_ptr<WebRequest> web_request);
    void handle_subscribe_output(std::shared_ptr<WebRequest> web_request);
    void handle_subscribe_report(std::shared_ptr<WebRequest> web_request);
    json handle_map_to_json(std::map<std::string, std::string> map);
    void report_callback(int error_code, 
        elegoo::common::ErrorLevel error_level, const std::string& msg);//临时
private:
    void output_callback(const std::string &msg, int error_code = elegoo::common::ErrorCode::CODE_OK, elegoo::common::ErrorLevel error_level = elegoo::common::ErrorLevel::INFO);

    std::shared_ptr<Printer> printer;
    std::shared_ptr<GCodeDispatch> gcode;
    bool is_output_registered;
    std::unordered_map<std::shared_ptr<ClientConnection>, json> output_clients;
    bool is_report_registered;
    std::vector<std::shared_ptr<ClientConnection>> report_clients;
};

class QueryStatusHelper
{
public:
    QueryStatusHelper(std::shared_ptr<Printer> printer);

    // Handler methods
    void handle_list(std::shared_ptr<WebRequest> web_request);
    void handle_query(std::shared_ptr<WebRequest> web_request, bool is_subscribe = false);
    void handle_subscribe(std::shared_ptr<WebRequest> web_request);

private:
    double do_query(double eventtime);
    std::shared_ptr<Printer> printer;
    std::unordered_map<std::shared_ptr<ClientConnection>, std::tuple<std::shared_ptr<ClientConnection>, json, std::function<void(const json &)>, json>> clients;
    std::vector<std::tuple<std::shared_ptr<ClientConnection>, json, std::function<void(const json &)>, json>> pending_queries;
    std::shared_ptr<ReactorTimer> query_timer;
    std::unordered_map<std::string, json> last_query;
};

namespace WEBHOOKS
{

    extern "C"
    {
        void webhooks_add_early_printer_objects(std::shared_ptr<Printer> printer);
    }

}

#endif
