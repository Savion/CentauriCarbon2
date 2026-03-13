/***************************************************************************** 
 * @Author       : Gary
 * @Date         : 2024-11-19 05:57:38
 * @LastEditors  : loping
 * @LastEditTime : 2025-09-16 15:16:48
 * @Description  : The webhooks module in Klipper is used to interact with the printer
 * through a network interface. It provides a set of RESTful API endpoints that allow
 * users to remotely send commands, retrieve printer status information, manage files,
 * and more via the HTTP protocol.
 * 
 * Copyright (c) 2025 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/

#include "webhooks.h"
#include "printer.h"
#include "extras_factory.h"

const double SUBSCRIPTION_REFRESH_TIME = 0.5;
// Constructor implementation
ClientConnection::ClientConnection(std::shared_ptr<ServerSocket> server, int sock)
    : server(server), sock(sock), is_blocking(false), blocking_count(0)
{
    uid = reinterpret_cast<std::uintptr_t>(this);
    reactor = server->reactor;
    webhooks = server->webhooks;
    printer = server->printer;
    fd_handle = reactor->register_fd(sock, [this](double time)
                                     { process_received(time); }, [this](double time)
                                     { do_send(time); });

    SPDLOG_DEBUG("fd_handle->fd:{}", fd_handle->fd);
    partial_data = send_buffer = "";
    set_client_info(json::object(), "New connection");
}

// Destructor implementation
ClientConnection::~ClientConnection()
{
    SPDLOG_INFO("~ClientConnection()");
    close();
}

void ClientConnection::dump_request_log()
{
    std::cout << "Dumping " << request_log.size() << " requests for client " << uid << "\n";
    for (const auto &entry : request_log)
    {
        std::cout << "Received " << entry.first << ": " << std::string(entry.second.begin(), entry.second.end()) << "\n";
    }
}

void ClientConnection::set_client_info(const json &client_info, const std::string &state_msg)
{
    std::string msg = state_msg.empty() ? "Client info " + client_info.dump() : state_msg;
    SPDLOG_INFO("webhooks client " + std::to_string(uid) + ": " + msg);
    std::string log_id = "webhooks" + std::to_string(uid);
    if (client_info.empty())
    {
        printer->set_rollover_info(log_id, "", false);
        return;
    }

    std::string rollover_msg = "webhooks client " + std::to_string(uid) + ": " + client_info.dump();
    printer->set_rollover_info(log_id, rollover_msg, false);
}

void ClientConnection::close()
{
    if (!fd_handle)
        return;
    set_client_info(json::object(), "Disconnected");
    reactor->unregister_fd(fd_handle);
    fd_handle = nullptr;
    ::close(sock);
    sock = -1;
}

bool ClientConnection::is_closed() const
{
    return fd_handle == nullptr;
}

void ClientConnection::process_received(double eventtime)
{
    try
    {
        char buffer[4096];
        ssize_t bytes_received = recv(sock, buffer, sizeof(buffer), 0);
        std::string data(buffer, (bytes_received > 0) ? bytes_received : 0);
        // SPDLOG_DEBUG("__func__:{},data:{}",__func__,data);
        if (data.empty())
        {
            close();
            return;
        }

        std::vector<std::string> requests =
            elegoo::common::split(data, std::string(1, '\x03'));

        requests[0] = partial_data + requests[0];
        partial_data = requests.back(); // 获取最后一个元素
        requests.pop_back();
        SPDLOG_DEBUG("__func__:{},data:{}\n,requests.size:{}", __func__, data, requests.size());
        for (const auto &req : requests)
        {
            request_log.emplace_back(eventtime, req);
            try
            {
                std::shared_ptr<WebRequest> web_request = std::make_shared<WebRequest>(shared_from_this(), req);
                reactor->register_callback(
                    [this, web_request](int e)
                    {
                        return this->process_request(web_request);
                    });
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error decoding Server Request: " << req
                          << "\nException: " << e.what() << std::endl;
                continue;
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
    }
}

json ClientConnection::process_request(std::shared_ptr<WebRequest> web_request)
{
    try
    {
        // SPDLOG_INFO("web_request->get_method(): {}", web_request->get_method());
        auto func = webhooks->get_callback(web_request->get_method());
        if (func)
        {
            func(web_request);
        }
        else
        {
            throw elegoo::common::CommandError("No callback found for method");
        }
    }
    catch (const elegoo::common::CommandError &e)
    {
        SPDLOG_WARN("process_request {}", e.what());
        web_request->set_error(elegoo::common::WebRequestError(e.what()));
    }
    catch (const elegoo::common::MMUError &e)
    {
        SPDLOG_WARN("canvas error: {}", e.what());
        web_request->set_error(elegoo::common::WebRequestError(e.what()));
    }
    catch (const std::exception &e)
    {
        std::string msg = "Internal Error on WebRequest: " + web_request->get_method();
        SPDLOG_ERROR(msg);
        web_request->set_error(elegoo::common::WebRequestError(e.what()));
        printer->invoke_shutdown(msg);
    }

    json result = web_request->finish();
    if (result.is_null())
    {
        return json::object();
    }

    SPDLOG_INFO("return total: {}", result.dump());
    json_send(result);
    return json::object();
}

void ClientConnection::json_send(const json &data)
{
    std::string jmsg = data.dump();
    jmsg += "\x03";
    send_buffer.insert(send_buffer.end(), jmsg.begin(), jmsg.end());
    if (!is_blocking)
    {
        do_send(0.0);
    }
}

void ClientConnection::do_send(double eventtime)
{
    if (!fd_handle)
        return;
    ssize_t sent = send(sock, send_buffer.data(), send_buffer.size(), 0);
    if (sent < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            SPDLOG_ERROR("webhooks: socket write error {}", uid);
            close();
            return;
        }
        sent = 0;
    }

    send_buffer.erase(send_buffer.begin(), send_buffer.begin() + sent);
    if (send_buffer.empty())
    {
        if(is_blocking)
        {
            reactor->set_fd_wake(fd_handle, true, false);
            is_blocking = false;
        }
    }
    else
    {
        if (!is_blocking)
        {
            reactor->set_fd_wake(fd_handle, false, true);
            is_blocking = true;
            blocking_count = 5;
        }
        // Reset the blocking count
        // Set up for async wake or similar mechanism...
    }
}

WebRequest::WebRequest(std::shared_ptr<ClientConnection> client_conn, const std::string &request)
    : client_conn(client_conn), is_error(false)
{
    nlohmann::json base_request = json::parse(request);

    if (!base_request.is_object())
    {
        throw std::invalid_argument("Not a top-level dictionary");
    }

    if (base_request.contains("id"))
    {
        id = base_request["id"].get<int64_t>();
    }
    else
    {
        id = INT_NONE;
    }
    method = base_request.value("method", "");
    params = base_request.value("params", json::object());
    is_response = base_request.contains("result") || base_request.contains("error");
    if (is_response)
    {
        result = base_request.value("result", json::object());
    }

    if (method.empty() || !params.is_object())
    {
        throw std::invalid_argument("Invalid request type");
    }
}

WebRequest::~WebRequest()
{
    SPDLOG_INFO("~WebRequest()");
}

std::shared_ptr<ClientConnection> WebRequest::get_client_connection() const
{
    return client_conn;
}

std::string WebRequest::get_method() const
{
    return method;
}

std::string WebRequest::get_params() const
{
    return params.dump();
}

std::string WebRequest::get_result() const
{
    return result.dump();
}

std::string WebRequest::get_str(const std::string &item, std::string default_value)
{
    std::string value = params.value(item, default_value);
    if (value.empty())
    {
        throw elegoo::common::WebRequestError("Invalid Argument Type [" + item + "]");
    }
    return value;
}

int WebRequest::get_int(const std::string &item, int default_value)
{
    int value = params.value(item, default_value);
    return value;
}

double WebRequest::get_double(const std::string &item, double default_value)
{
    double value = params.value(item, default_value);
    return value;
}

json WebRequest::get_dict(const std::string &item, json default_value)
{
    json value = params.value(item, default_value);
    return value;
}

void WebRequest::set_error(const elegoo::common::WebRequestError &error)
{
    is_error = true;
    response = error.to_dict();
}

void WebRequest::send(const json &data)
{
    if (!response.is_null())
    {
        throw elegoo::common::WebRequestError("Multiple calls to send not allowed");
    }
    response = data;
}

int WebRequest::get_client_connection_id()
{
    return 0;
}

int WebRequest::error(const std::string &)
{
    return -1;
}

json WebRequest::finish()
{
    if (id == INT_NONE)
    {
        return json::value_t::null;
    }

    std::string rtype = "result";
    if (is_error)
    {
        rtype = "error";
    }

    json result;
    result["id"] = id;
    if (is_method_need(get_method()))
    {
        result["method"] = get_method();
    }
    if (response.is_null())
    {
        response = json::object();
    }
    result[rtype] = response;

    return result;
}

bool WebRequest::is_method_need(std::string method)
{
    static const std::set<std::string> allowed_methods = {
        "get_network_access_cfg",
        "bind_func",
        "sync_info"
    };
    return allowed_methods.find(method) != allowed_methods.end();
}

ServerSocket::ServerSocket(std::shared_ptr<WebHooks> webhooks, std::shared_ptr<Printer> printer)
{
    this->printer = printer;
    this->webhooks = webhooks;
    reactor = printer->get_reactor();

    fd_handle = nullptr;
    std::unordered_map<std::string, std::string> start_args = printer->get_start_args();
    std::string server_address = start_args["apiserver"];
    SPDLOG_INFO("create ServerSocket success! {}", server_address);
    bool is_fileinput = (start_args["debuginput"] != "");
    if (is_fileinput || server_address.empty())
    {
        return;
    }
    remove_socket_file(server_address);
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        throw std::runtime_error("Failed to create socket");
    }

    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1)
    {
        close(sockfd);
        throw std::runtime_error("Failed to get socket flags");
    }

    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        close(sockfd);
        throw std::runtime_error("Failed to set non-blocking mode");
    }

    // 创建服务器地址结构
    sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, server_address.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        close(sockfd);
        throw std::runtime_error("Failed to bind socket");
    }

    if (listen(sockfd, 1) < 0)
    {
        close(sockfd);
        throw std::runtime_error("Failed to listen on socket");
    }
    fd_handle = reactor->register_fd(sockfd, [this](double time)
                                     { this->handle_accept(time); }, nullptr);
    SPDLOG_DEBUG("fd_handle->fd:{}", fd_handle->fd);

    elegoo::common::SignalManager::get_instance().register_signal(
        "elegoo:disconnect",
        std::function<void()>([this]()
                              { handle_disconnect(); }));
    elegoo::common::SignalManager::get_instance().register_signal(
        "elegoo:shutdown",
        std::function<void()>([this]()
                              {
            SPDLOG_DEBUG("elegoo:shutdown !");
            handle_shutdown();
            SPDLOG_DEBUG("elegoo:shutdown !"); }));

    SPDLOG_INFO("create ServerSocket success!");
}

ServerSocket::~ServerSocket()
{
    handle_disconnect();
    if (sockfd >= 0)
    {
        close(sockfd);
    }
}

void ServerSocket::handle_accept(double time)
{
    // SPDLOG_DEBUG("__func__:{},__LINE__:{}",__func__,__LINE__);
    try
    {
        int client_sock = accept(sockfd, nullptr, nullptr);
        if (client_sock < 0)
        {
            SPDLOG_ERROR("Error accepting connection:{}", strerror(errno));
            return;
        }

        int flags = fcntl(client_sock, F_GETFL, 0);
        if (flags == -1 || fcntl(client_sock, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            SPDLOG_ERROR("Failed to set non-blocking mode");
            close(client_sock);
            return;
        }

        std::shared_ptr<ClientConnection> client =
            std::make_shared<ClientConnection>(shared_from_this(), client_sock);
        clients[client->uid] = client;
    }
    catch (const std::exception &e)
    {
        SPDLOG_ERROR("Exception in handle_accept: {}", e.what());
    }
}

void ServerSocket::handle_disconnect()
{
    for (auto &pair : clients)
    {
        pair.second->close();
    }
    clients.clear();
    if (fd_handle != nullptr)
    {
        reactor->unregister_fd(fd_handle);
    }
    SPDLOG_INFO("{} : {}___ ok", __FUNCTION__, __LINE__);
}

void ServerSocket::handle_shutdown()
{
    for (auto &pair : clients)
    {
        pair.second->dump_request_log();
    }
    SPDLOG_INFO("{} : {}___ ok", __FUNCTION__, __LINE__);
}

void ServerSocket::remove_socket_file(const std::string &file_path)
{
    try
    {
        unlink(file_path.c_str());
    }
    catch (const std::exception &e)
    {
        struct stat buffer;
        if (stat(file_path.c_str(), &buffer) == 0)
        {
            throw std::runtime_error("Failed to remove socket file.");
        }
    }
}

void ServerSocket::pop_client(int client_id)
{
    auto it = clients.find(client_id);
    if (it != clients.end())
    {
        clients.erase(it);
    }
}

std::pair<bool, std::string> ServerSocket::stats(double eventtime)
{
    // Check for idle clients
    for (auto it = clients.begin(); it != clients.end();)
    {
        std::shared_ptr<ClientConnection> client = it->second;
        if (client->is_blocking)
        {
            client->blocking_count--;
            if (client->blocking_count < 0)
            {
                SPDLOG_INFO("Closing unresponsive client {}", client->uid);
                client->close();
                it = clients.erase(it); // Remove and move to next
            }
            else
            {
                ++it;
            }
        }
        else
        {
            ++it;
        }
    }
    return {true, ""};
}

void ServerSocket::broadcast_message(const json &json) // 发送广播到客户端
{
    for (auto it = clients.begin(); it != clients.end();)
    {
        std::shared_ptr<ClientConnection> client = it->second;
        if (!client->is_closed())
        {
            client->json_send(json);
        }
        ++it;
    }
}

WebHooks::WebHooks(std::shared_ptr<Printer> printer) : printer(printer)
{
    register_endpoint("list_endpoints", [this](std::shared_ptr<WebRequest> req)
                      { handle_list_endpoints(req); });
    register_endpoint("info", [this](std::shared_ptr<WebRequest> req)
                      { handle_info_request(req); });
    register_endpoint("emergency_stop", [this](std::shared_ptr<WebRequest> req)
                      { handle_estop_request(req); });
    SPDLOG_INFO("create WebHooks success!");
}

void WebHooks::create_socket()
{
    sconn = std::make_shared<ServerSocket>(shared_from_this(), printer);
    gcode_helper = std::make_shared<GCodeHelper>(printer);
    query_status_helper = std::make_shared<QueryStatusHelper>(printer);
}

void WebHooks::register_endpoint(const std::string &path,
                                 std::function<void(std::shared_ptr<WebRequest>)> callback)
{
    if (endpoints.find(path) != endpoints.end())
    {
        throw elegoo::common::WebRequestError("Path already registered to an endpoint");
    }
    endpoints[path] = callback;
}

void WebHooks::register_mux_endpoint(const std::string &path,
                                     const std::string &key, const std::string &value,
                                     std::function<void(std::shared_ptr<WebRequest>)> callback)
{
    auto it = mux_endpoints.find(path);
    if (it == mux_endpoints.end())
    {
        register_endpoint(path, [this](std::shared_ptr<WebRequest> req)
                          { handle_mux(req); });

        mux_endpoints[path] = {key, {}};
    }

    auto prev_key = mux_endpoints[path].first;
    auto prev_values = mux_endpoints[path].second;

    if (prev_key != key)
    {
        throw std::runtime_error("mux endpoint may have only one key");
    }
    if (prev_values.find(value) != prev_values.end())
    {
        throw std::runtime_error("mux endpoint already registered");
    }
    prev_values[value] = callback;
}

std::string WebHooks::getDirectoryName(const std::string &filepath)
{
    size_t pos = filepath.find_last_of("/\\");
    return (pos == std::string::npos) ? "" : filepath.substr(0, pos);
}

std::string WebHooks::getParentPath(const std::string &path)
{
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos)
    {
        return ""; // 没有上级目录
    }
    return path.substr(0, pos);
}

void WebHooks::handle_mux(std::shared_ptr<WebRequest> webRequest)
{
    auto &method = mux_endpoints[webRequest->get_method()];
    std::string key_param = webRequest->get_str(method.first);
    auto it = method.second.find(key_param);

    if (it == method.second.end())
    {
        webRequest->error("Method not found in mux_endpoints");
        return;
    }
    it->second(webRequest);
}

void WebHooks::handle_list_endpoints(std::shared_ptr<WebRequest> web_request)
{
    json response;
    for (const auto x : endpoints)
    {
        response["endpoints"].push_back(x.first);
    }
    web_request->send(response);
}

void WebHooks::handle_info_request(std::shared_ptr<WebRequest> web_request)
{
    // Implement information gathering and sending response
    json client_info = web_request->get_dict("client_info");
    if (!client_info.empty())
        web_request->get_client_connection()->set_client_info(client_info);
    std::string state_message, state;
    std::tie(state_message, state) = printer->get_state_message();

    // Prepare response
    json response;
    response["state"] = state;
    response["state_message"] = state_message;
    // Get hostname
    char hostname[HOST_NAME_MAX];
    if (gethostname(hostname, sizeof(hostname)) == 0)
        response["hostname"] = hostname;
    response["elegoo_path"] = "/home/eeb001/elegoo";
    response["python_path"] = "/home/eeb001/elegoo-env/bin/python3";
    // response["process_id"] = getpid();
    // response["user_id"] = getuid();
    // response["group_id"] = getgid();
    std::unordered_map<std::string, std::string> start_args = printer->get_start_args();
    for (const auto &key : {"log_file", "config_file", "software_version", "cpu_info"})
        response[key] = start_args[key];
    response["config_file"] = "/home/eeb001/printer_data/config/printer.cfg";
    response["log_file"] = "/home/eeb001/printer_data/logs/elegoo.log";
    response["software_version"] = "v0.12.0-458-gd886c176";
    web_request->send(response);
}

void WebHooks::handle_estop_request(std::shared_ptr<WebRequest> web_request)
{
    printer->invoke_shutdown("Shutdown due to webhooks request");
}

std::function<void(std::shared_ptr<WebRequest>)> WebHooks::get_callback(const std::string &path)
{
    return endpoints[path];
}

std::shared_ptr<WebHooks> WebHooks::get_shared()
{

    return shared_from_this();
}

std::shared_ptr<ServerSocket> WebHooks::get_connection()
{
    return sconn;
}

json WebHooks::get_status()
{
    auto meg = printer->get_state_message();
    return {{"state", meg.second}, {"state_message", meg.first}};
}
std::pair<bool, std::string> WebHooks::stats(double eventtime)
{
    return sconn->stats(eventtime);
}

void WebHooks::broadcast_message(const json &json)
{
    sconn->broadcast_message(json);
}

void WebHooks::report_error(int error_code, 
    elegoo::common::ErrorLevel error_level, const std::string& msg)
{
    if(gcode_helper) {
        gcode_helper->report_callback(error_code, error_level, msg);
    }
}

void WebHooks::call_remote_method(const std::string &method, const json &params)
{
    auto it = remote_methods.find(method);
    if (it == remote_methods.end())
    {
        throw std::runtime_error("Remote method not registered");
    }

    std::map<std::shared_ptr<ClientConnection>, json> valid_conns;
    auto &conn_map = it->second;
    for (const auto x : conn_map)
    {
        if (!x.first)
        { // check if connection is valid
            valid_conns[x.first] = x.second;
            json out;
            out["params"] = params;
            out.push_back(x.second);
            x.first->json_send(out);
        }
    }

    if (valid_conns.empty())
    {
        remote_methods.erase(method);
        throw std::runtime_error("No active connections for method");
    }
    remote_methods[method] = valid_conns;
}

json GCodeHelper::handle_map_to_json(std::map<std::string, std::string> map)
{
    json obj;
    for (const auto &it : map)
    {
        obj[it.first] = it.second;
    }

    return obj;
}

GCodeHelper::GCodeHelper(std::shared_ptr<Printer> printer)
    : printer(printer), is_output_registered(false), is_report_registered(false)
{
    gcode = any_cast<std::shared_ptr<GCodeDispatch>>(printer->lookup_object("gcode"));
    std::shared_ptr<WebHooks> wh = any_cast<std::shared_ptr<WebHooks>>(printer->lookup_object("webhooks"));
    wh->register_endpoint("gcode/help", [this](std::shared_ptr<WebRequest> req)
                          { handle_help(req); });
    wh->register_endpoint("gcode/script", [this](std::shared_ptr<WebRequest> req)
                          { handle_script(req); });
    wh->register_endpoint("gcode/restart", [this](std::shared_ptr<WebRequest> req)
                          { handle_restart(req); });
    wh->register_endpoint("gcode/firmware_restart", [this](std::shared_ptr<WebRequest> req)
                          { handle_firmware_restart(req); });
    // wh->register_endpoint("gcode/subscribe_output", [this](std::shared_ptr<WebRequest> req)
    //                       { handle_subscribe_output(req); });
    wh->register_endpoint("gcode/subscribe_report", [this](std::shared_ptr<WebRequest> req)
                          { handle_subscribe_output(req); });
}

void GCodeHelper::handle_help(std::shared_ptr<WebRequest> web_request)
{
    web_request->send(handle_map_to_json(gcode->get_command_help()));
}

void GCodeHelper::handle_script(std::shared_ptr<WebRequest> web_request)
{
    std::string script = web_request->get_str("script");
    if(script.find("SET_PIN PIN=led_pin") != std::string::npos ||
        script.find("CANVAS_ABNORMAL_RETRY PRINTING=1") != std::string::npos )
    {
        gcode->run_script_from_command(script);
    } 
    else 
    {
        gcode->run_script(script);
    }
    
}

void GCodeHelper::handle_restart(std::shared_ptr<WebRequest> web_request)
{
    gcode->run_script("restart");
}

void GCodeHelper::handle_firmware_restart(std::shared_ptr<WebRequest> web_request)
{
    gcode->run_script("firmware_restart");
}

void GCodeHelper::output_callback(const std::string &msg, int error_code, 
    elegoo::common::ErrorLevel error_level)
{
    // SPDLOG_DEBUG("GCodeHelper::output_callback msg:{}", msg);
    for (auto it = output_clients.begin(); it != output_clients.end();)
    {
        auto &cconn = it->first;
        if (cconn->is_closed())
        {
            it = output_clients.erase(it); // Remove closed connections
        }
        else
        {
            json tmp = it->second;
            tmp["id"] = 0; // Set id to 0 for output messages
            tmp["report"]["message"] = msg;
            tmp["report"]["error_code"] = error_code;
            tmp["report"]["error_level"] = error_level;
            cconn->json_send(tmp); // Send message to client
            ++it;
        }
    }

}

void GCodeHelper::report_callback(int error_code,
    elegoo::common::ErrorLevel error_level, const std::string& msg)
{
    for (auto it = report_clients.begin(); it != report_clients.end(); ) 
    {
        if (!(*it) || (*it)->is_closed()) 
        {
            it = report_clients.erase(it); 
        } 
        else 
        {
            json tmp;
            tmp["id"] = 0;
            tmp["report"]["error_code"] = error_code;
            tmp["report"]["error_level"] = error_level;
            tmp["report"]["message"] = msg;
            SPDLOG_INFO("{} tmp.dump:{}",__func__,tmp.dump());
            (*it)->json_send(tmp); // Send message to client
            ++it;
        }
    }
}


void GCodeHelper::handle_subscribe_output(std::shared_ptr<WebRequest> web_request)
{
    auto cconn = web_request->get_client_connection();
    json template_response = web_request->get_dict("response_template");
    output_clients[cconn] = template_response;

    if (!is_output_registered)
    {
        gcode->register_output_handler([this](const std::string &msg, int error_code, elegoo::common::ErrorLevel error_level)
                                       { output_callback(msg, error_code, error_level); });
        is_output_registered = true;
    }
}

void GCodeHelper::handle_subscribe_report(std::shared_ptr<WebRequest> web_request)
{
    auto cconn = web_request->get_client_connection();
    report_clients.push_back(cconn);


    // if (!is_report_registered)
    // {
    //     gcode->register_report_handler(
    //         [this](int error_code, 
    //             elegoo::common::ErrorLevel error_level, const std::string& msg) { report_callback(error_code, error_level, msg); 
    //         });
    //     is_report_registered = true;
    // }
}

QueryStatusHelper::QueryStatusHelper(std::shared_ptr<Printer> printer)
    : printer(printer)
{
    query_timer = nullptr;
    std::shared_ptr<WebHooks> webhooks = any_cast<std::shared_ptr<WebHooks>>(printer->lookup_object("webhooks"));
    webhooks->register_endpoint("objects/list", [this](std::shared_ptr<WebRequest> req)
                                { handle_list(req); });
    webhooks->register_endpoint("objects/query", [this](std::shared_ptr<WebRequest> req)
                                { handle_query(req); });
    webhooks->register_endpoint("objects/subscribe", [this](std::shared_ptr<WebRequest> req)
                                { handle_subscribe(req); });
}

void QueryStatusHelper::handle_list(std::shared_ptr<WebRequest> web_request)
{
    auto objects = printer->lookup_objects();
    json data;
    std::vector<std::string> name_set;

    // 只添加有get_status接口的模块
    const std::vector<std::string> has_status_objects = {
        "configfile",
        "gcode",
        "mcu",
        "toolhead",
        "webhooks",
        "bed_mesh",
        "controller_fan",
        "exclude_object",
        "fan_generic",
        "fan",
        "filament_switch_sensor",
        "gcode_button",
        // "gcode_macro",
        "gcode_move",
        "heater_fan",
        "heater_bed",
        "heaters",
        "idle_timeout",
        "input_shaper",
        "led",
        // "load_cell_probe",
        // "load_cell",
        "motion_report",
        "output_pin",
        "pause_resume",
        "print_stats",
        "probe",
        "pwm_cycle_time",
        "pwm_tool",
        "query_endstops",
        "save_variables",
        "system_stats",
        "stepper_enable",
        "temperature_fan",
        "temperature_sensor",
        "tmc",
        "virtual_sdcard",
        "extruder",
        "corexy",
    };

    for (const auto &p : objects)
    {
        // 获取模块名称
        std::vector<std::string> name_parts = elegoo::common::split(p.first);
        std::string name = name_parts[0];

        //
        if (std::find(has_status_objects.begin(), has_status_objects.end(), name) == has_status_objects.end()
            //  && std::find(name_set.begin(), name_set.end(), name) != name_set.end()
        )
            continue;
        if (!p.second.empty())
        {
            data["objects"].push_back(p.first);
            name_set.push_back(p.first);
        }
    }

    web_request->send(data);
}

double QueryStatusHelper::do_query(double eventtime)
{
    std::unordered_map<std::string, json> query;

    // 获取挂起的查询
    std::vector<std::tuple<std::shared_ptr<ClientConnection>, json, std::function<void(const json &)>, json>> msglist = pending_queries;
    pending_queries.clear();

    for (const auto &p : clients)
    {
        msglist.push_back(p.second);
    }

    // cconn, subscription, send_func, template
    for (const auto &msg : msglist)
    {
        bool is_query = (std::get<0>(msg) == nullptr);
        if (!is_query && std::get<0>(msg)->is_closed())
        {
            clients.erase(std::get<0>(msg));
            continue;
        }

        // 扫描订阅对象
        json cquery;
        json subscription = std::get<1>(msg);
        for (json::iterator it = subscription.begin(); it != subscription.end(); ++it)
        {
            std::string obj_name = it.key();
            json req_items = it.value();
            json res;
            if (query.find(obj_name) == query.end())
            {
                try
                {
                    auto po = this->printer->lookup_object(obj_name);
                    if (po.empty())
                        res = query[obj_name] = {};
                    else
                        res = query[obj_name] = elegoo::extras::getStatus(po, obj_name, eventtime);
                }
                catch (const elegoo::common::ConfigParserError &e)
                {
                    // SPDLOG_ERROR("do_query: Failed to get object status #2 for: {}", obj_name);
                    continue;
                }
            } 
            else
            {
                res = query[obj_name];
            }
            
            // 请求值"null"代表请求所有
            if (req_items.is_null())
            {
                req_items = nlohmann::json::array();
                for (auto &item : res.items())
                {
                    req_items.push_back(item.key());
                }
                if (!req_items.empty())
                {
                    subscription[obj_name] = req_items;
                }
            }

            json lres;
            json cres;
            if (last_query.find(obj_name) != last_query.end())
            {
                lres = last_query.find(obj_name)->second;
            }

            for (const auto &element : req_items)
            {
                json rd = res[element.get<std::string>()];
                if (is_query || rd != lres[element.get<std::string>()])
                {
                    cres[element.get<std::string>()] = rd;
                }
            }

            if (!cres.empty() || is_query)
            {
                cquery[obj_name] = cres;
            }
        }

        if (!cquery.is_null() || is_query)
        {
            json tmp = std::get<3>(msg);
            tmp["id"] = 0; //无用，为了统一格式
            tmp["params"]["eventtime"] = eventtime;
            tmp["params"]["status"] = cquery;
            std::get<2>(msg)(tmp);
        }
    }

    last_query = query;
    // if (query.empty())
    // {
    //     auto reactor = printer->get_reactor();
    //     if (query_timer)
    //     {
    //         reactor->unregister_timer(query_timer);
    //         query_timer = nullptr;
    //     }
    //     return reactor->NEVER;
    // }
    return eventtime + SUBSCRIPTION_REFRESH_TIME;
}

// 处理查询和订阅
void QueryStatusHelper::handle_query(std::shared_ptr<WebRequest> web_request, bool is_subscribe)
{
    // "objects" 是对象名+查询段名，值是"null"或一个段的名称
    json objects = web_request->get_dict("objects");
    SPDLOG_INFO("handle_query is_subscribe {} : {}", is_subscribe, objects.dump());

    // 校验参数
    for (json::const_iterator it = objects.begin(); it != objects.end(); ++it)
    {
        const nlohmann::json &k = it.key();
        const nlohmann::json &v = it.value();

        if (!k.is_string() || (!v.is_null() && !v.is_array()))
            throw elegoo::common::WebRequestError("Invalid Argument");

        if (!v.is_null())
        {
            for (size_t i = 0; i < v.size(); ++i)
            {
                if (!v[i].is_string())
                    throw elegoo::common::WebRequestError("Invalid Argument");
            }
        }
    }

    auto cconn = web_request->get_client_connection();
    json template_response = web_request->get_dict("response_template");

    if (is_subscribe && clients.find(cconn) != clients.end())
        clients.erase(cconn);

    json js = json::object(); // need to confirm
    auto reactor = printer->get_reactor();
    auto completion = reactor->completion();
    pending_queries.emplace_back(std::tuple<std::shared_ptr<ClientConnection>, json,
                                            std::function<void(const json &)>, json>(
        nullptr,
        objects,
        [completion](const json &value)
        { completion->complete(value); },
        js));

    if (is_subscribe)
    {
        clients[cconn] = std::make_tuple(
            cconn,
            objects, [cconn](const json &msg)
            { cconn->json_send(msg); },
            template_response);
    }

    if (!query_timer)
    {
        query_timer = reactor->register_timer([this](double eventtime)
                                              { return this->do_query(eventtime); }, reactor->NOW, "webhooks");
    }

    json msg = completion->wait();
    web_request->send(msg["params"]);
}

void QueryStatusHelper::handle_subscribe(std::shared_ptr<WebRequest> web_request)
{
    handle_query(web_request, true);
}

namespace WEBHOOKS
{
    extern "C"
    {
        // Function to add early printer objects
        void webhooks_add_early_printer_objects(std::shared_ptr<Printer> printer)
        {
            printer->add_object("webhooks", [](std::shared_ptr<Printer> printer)
                                { return std::make_shared<WebHooks>(printer); }(printer));
        }
    }
}
