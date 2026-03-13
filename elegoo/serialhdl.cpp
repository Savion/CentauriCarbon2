/*****************************************************************************
 * @Author       : Louisa
 * @Date         : 2024-11-26 14:44:56
 * @LastEditors  : coconut
 * @LastEditTime : 2025-03-14 09:51:04
 * @Description  : Serial port management for firmware communication
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#include "serialhdl.h"
#include "reactor.h"
#include "msgproto.h"
#include "c_helper.h"
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <fcntl.h>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include "utilities.h"
#include "common/logger.h"
#include "exception_handler.h"
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include "dsp_helper.h"

SerialReader::SerialReader(std::shared_ptr<SelectReactor> reactor,
                           const std::string warn_prefix, const std::string name, const std::string serial)
    : reactor(reactor), stats_buf(new char[4096]), last_notify_id(0), name(name), serial(serial)
{
    this->reactor = reactor;
    this->warn_prefix = warn_prefix;
    msgparser = std::make_shared<MessageParser>(warn_prefix);
    default_cmd_queue = alloc_command_queue();
    register_response([this](const json &params)
                      { handle_unknown_init(params); }, "#unknown");
    register_response([this](const json &params)
                      { handle_output(params); }, "#output");
}

SerialReader::~SerialReader()
{
    delete[] stats_buf;
    stats_buf = nullptr;
}

void SerialReader::connect_canbus(const std::string &canbus_uuid,
                                  int canbus_nodeid, const std::string &canbus_iface)
{
    int txid = canbus_nodeid * 2 + 256;
    struct can_filter filters[1];
    filters[0].can_id = txid + 1;
    filters[0].can_mask = 0x7ff;

    // Prepare for SET_NODEID command
    long long uuid;
    try
    {
        uuid = std::stoll(canbus_uuid, nullptr, 16);
    }
    catch (const std::invalid_argument &)
    {
        uuid = -1;
    }

    if (uuid < 0 || uuid > 0xffffffffffff)
    {
        error("Invalid CAN uuid");
    }

    std::vector<uint8_t> uuid_bytes(6);
    for (int i = 0; i < 6; ++i)
    {
        uuid_bytes[i] = (uuid >> (40 - i * 8)) & 0xff;
    }

    const int CANBUS_ID_ADMIN = 0x3f0;
    const uint8_t CMD_SET_NODEID = 0x01;
    std::vector<uint8_t> set_id_cmd = {CMD_SET_NODEID};
    set_id_cmd.insert(set_id_cmd.end(), uuid_bytes.begin(), uuid_bytes.end());
    set_id_cmd.push_back(static_cast<uint8_t>(canbus_nodeid));

    struct can_frame set_id_msg;
    set_id_msg.can_id = CANBUS_ID_ADMIN;
    std::memcpy(set_id_msg.data, set_id_cmd.data(), set_id_cmd.size());
    set_id_msg.can_dlc = static_cast<uint8_t>(set_id_cmd.size());

    std::cout << "Starting CAN connect" << std::endl;
    double start_time = get_monotonic();

    while (true)
    {

        if (get_monotonic() > start_time + 90.0)
        {
            error("Unable to connect");
        }

        int socket_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (socket_fd < 0)
        {
            std::stringstream ss;
            ss << warn_prefix + "Unable to open port: " << std::strerror(errno);
            SPDLOG_ERROR(ss.str());
            reactor->pause(get_monotonic() + 5);
            continue;
        }

        struct ifreq ifr;
        std::strncpy(ifr.ifr_name, canbus_iface.c_str(), IFNAMSIZ - 1);
        if (ioctl(socket_fd, SIOCGIFINDEX, &ifr) < 0)
        {
            std::cerr << "Unable to get interface index: " << std::strerror(errno) << std::endl;
            reactor->pause(get_monotonic() + 5);
            continue;
        }

        setsockopt(socket_fd, SOL_CAN_RAW, CAN_RAW_FILTER, filters, sizeof(filters));

        if (write(socket_fd, &set_id_msg, sizeof(set_id_msg)) != sizeof(set_id_msg))
        {
            std::cerr << "Error sending message: " << std::strerror(errno) << std::endl;
            reactor->pause(get_monotonic() + 5);
            continue;
        }
        bool ret = start_session(socket_fd, 'c', txid);
        if (!ret)
        {
            continue;
        }

        try
        {
            json params = send_with_response("get_canbus_id", "canbus_id");
            long long got_uuid = params["canbus_uuid"].get<int64_t>();
            if (got_uuid == uuid)
            {
                std::cout << "UUIDs match." << std::endl;
                break;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
        }

        std::cout << "Failed to match canbus_uuid - retrying..." << std::endl;
        disconnect();
    }
}

void SerialReader::connect_pipe(const std::string &filename)
{
    double start_time = get_monotonic();
    while (true)
    {
        if (get_monotonic() > start_time + 90.0)
        {
        }

        int fd;
        try
        {
            fd = open(filename.c_str(), O_RDWR | O_NOCTTY);
            if (fd < 0)
            {
                error("Unable to open port");
            }
        }
        catch (const std::exception &e)
        {
            SPDLOG_ERROR(warn_prefix + "Unable to open port: " + e.what());
            reactor->pause(get_monotonic() + 5.0);
            continue;
        }
        bool ret = start_session(fd);
        if (ret)
        {
            break;
        }
        close(fd);
    }
}

void SerialReader::connect_uart(const std::string &serialport,
                                int baud, bool rts)
{
    double start_time = get_monotonic();
    // SPDLOG_INFO("{}: serial open ", warn_prefix);
    while (true)
    {
        if (get_monotonic() > start_time + 90.0)
        {
            error("Unable to connect");
        }
        //
        std::shared_ptr<SerialPort> serial_dev = std::make_shared<SerialPort>(serialport, baud);
        if (!serial_dev->open())
        {
            SPDLOG_ERROR("{}: serial open failed", warn_prefix);
            reactor->pause(get_monotonic() + 5);
            continue;
        }
        SPDLOG_INFO("{}: serial start_session", warn_prefix);
        bool ret = start_session(serial_dev->get_fd());
        if (ret)
        {
            break;
        }
    }
}

void SerialReader::connect_rpmsg(const std::string &serialport)
{
    double start_time = get_monotonic();
    _is_rpmsg = true;
    SPDLOG_INFO("{}: rpmsg open ", warn_prefix);
    while (true)
    {
        if (get_monotonic() > start_time + 90.0)
            error("Unable to connect");

        // if(access("/dev/rpmsg1", F_OK) == 0)
        // {
        //     _ept_id = 1;
        // }
        // else
        {
            // 创建通讯端点
            _ept_id = rpmsg_alloc_ept(serialport.c_str(), "cpu_dsp0");
            if (_ept_id == -1)
            {
                reactor->pause(get_monotonic() + 5);
                continue;
            }
        }
        
        // 打开端点
        std::string ept_name = "/dev/rpmsg" + std::to_string(_ept_id);
        int ept_fd = open(ept_name.c_str(), O_RDWR);
        if (ept_fd == -1)
        {
            SPDLOG_ERROR("rpmsg ept open ept_name {} failed {}", ept_name, std::string(strerror(errno)));
            rpmsg_free_ept(serialport.c_str(), _ept_id);
            reactor->pause(get_monotonic() + 5);
            continue;
        }
        SPDLOG_INFO("{}: rpmsg start_session rpmsg id {}", warn_prefix, _ept_id);

        // 启动会话
        bool ret = start_session(ept_fd, 'r');
        if (ret)
            break;

        SPDLOG_INFO("connect_rpmsg try...");
    }
}

void SerialReader::connect_file(int debugoutput,
                                const std::vector<uint8_t> &dictionary, bool pace)
{
    fd = debugoutput;
    msgparser->process_identify(dictionary, false);
    sq = std::shared_ptr<serialqueue>(
        serialqueue_alloc(fd, 'f', 0),
        serialqueue_free);
}

void SerialReader::set_clock_est(
    double freq, double conv_time, uint64_t conv_clock, uint64_t last_clock)
{
    serialqueue_set_clock_est(sq.get(), freq, conv_time, conv_clock, last_clock);
}

void SerialReader::disconnect()
{
    SPDLOG_ERROR("SerialReader::disconnect()");

    if (sq)
    {
        serialqueue_exit(sq.get());
        if (background_thread && background_thread->joinable())
        {
            background_thread->join();
        }
        background_thread.reset();
        sq.reset();
    }

    if (fd > 0)
    {
        close(fd);
        // 销毁端点
        if (_is_rpmsg)
        {
            SPDLOG_ERROR("is_rpmsg so call rpmsg_free_ept {} id {}", serial, _ept_id);
            rpmsg_free_ept(serial.c_str(), _ept_id);
        }
        fd = -1;
    }

    pending_notifications_mutex.lock();
    for (std::pair<const int, std::shared_ptr<ReactorCompletion>> &pair : pending_notifications)
        pair.second->complete({});
    pending_notifications.clear();
    pending_notifications_mutex.unlock();
}

std::string SerialReader::stats(double eventtime)
{
    if (sq == nullptr)
    {
        return "";
    }
    serialqueue_get_stats(sq.get(), stats_buf, 4096);
    return std::string(stats_buf);
}

std::shared_ptr<SelectReactor> SerialReader::get_reactor()
{
    return reactor;
}

std::shared_ptr<MessageParser> SerialReader::get_msgparser()
{
    return msgparser;
}

std::shared_ptr<serialqueue> SerialReader::get_serialqueue()
{
    return sq;
}

std::shared_ptr<command_queue> SerialReader::get_default_command_queue()
{
    return default_cmd_queue;
}

void SerialReader::register_response(std::function<void(const json &params)> callback, const std::string &name, uint32_t oid)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (!callback)
    {
        handlers.erase({name, oid});
    }
    else
    {
        handlers[{name, oid}] = callback;
    }
}

void SerialReader::raw_send(
    const std::vector<uint8_t> &cmd,
    uint64_t minclock,
    uint64_t reqclock,
    std::shared_ptr<command_queue> cmd_queue)
{
    std::vector<uint8_t> cmd_copy(cmd.begin(), cmd.end());
    serialqueue_send(sq.get(), cmd_queue.get(), cmd_copy.data(), cmd_copy.size(), minclock, reqclock, 0);
}

json SerialReader::raw_send_wait_ack(
    const std::vector<uint8_t> &cmd,
    uint64_t minclock,
    uint64_t reqclock,
    std::shared_ptr<command_queue> cmd_queue)
{
    last_notify_id++;
    int nid = last_notify_id;

    std::shared_ptr<ReactorCompletion> completion = reactor->completion();
    pending_notifications_mutex.lock();
    pending_notifications[nid] = completion;
    pending_notifications_mutex.unlock();
    // 发送命令
    std::vector<uint8_t> cmd_copy(cmd.begin(), cmd.end());
    serialqueue_send(sq.get(), cmd_queue.get(), cmd_copy.data(), cmd_copy.size(), minclock, reqclock, nid);

    // 等待响应
    json params = completion->wait();
    if (params.empty())
    {
        error("Serial connection closed");
    }
    return params;
}

void SerialReader::send(const std::string &msg, int minclock, int reqclock)
{
    std::vector<uint8_t> cmd = msgparser->create_command(msg);
    raw_send(cmd, minclock, reqclock, default_cmd_queue);
}
json SerialReader::send_with_response(const std::string &msg, const std::string &response)
{
    auto cmd = msgparser->create_command(msg);
    std::shared_ptr<SerialRetryCommand> src = std::make_shared<SerialRetryCommand>(shared_from_this(), response);
    // 获取响应
    std::vector<std::vector<uint8_t>> cmdsResp = {};
    cmdsResp.push_back(cmd);
    return src->get_response(cmdsResp, default_cmd_queue);
}
std::shared_ptr<command_queue> SerialReader::alloc_command_queue()
{
    return std::shared_ptr<command_queue>(
        serialqueue_alloc_commandqueue(),
        serialqueue_free_commandqueue);
}

std::string SerialReader::dump_debug()
{
    std::vector<std::string> out;
    char buf[4096];

    std::ostringstream oss;
    oss << "Dumping serial stats: " << stats(get_monotonic());
    out.push_back(oss.str());

    std::unique_ptr<pull_queue_message[]> sdata(new pull_queue_message[1024]);
    std::unique_ptr<pull_queue_message[]> rdata(new pull_queue_message[1024]);
    int scount = serialqueue_extract_old(sq.get(), 1, sdata.get(), 1024);
    int rcount = serialqueue_extract_old(sq.get(), 0, rdata.get(), 1024);
    oss.str("");
    oss.clear();
    oss << "Dumping send queue " << scount << " messages";
    out.push_back(oss.str());

    // 发送
    for (int i = 0; i < scount; ++i)
    {
        const pull_queue_message &msg = sdata[i];
        std::vector<std::string> cmds = msgparser->dump(std::vector<uint8_t>(msg.msg, msg.msg + msg.len));
        snprintf(buf, sizeof(buf), "Sent %d %f %f %d: %s", i, msg.receive_time, msg.sent_time, msg.len, join(cmds, ", ").c_str());
        out.push_back(std::string(buf));
    }

    // 接收
    oss.str("");
    oss.clear();
    oss << "Dumping receive queue " << rcount << " messages";
    out.push_back(oss.str());

    for (int i = 0; i < rcount; ++i)
    {
        const pull_queue_message &msg = rdata[i];
        std::vector<std::string> cmds = msgparser->dump(std::vector<uint8_t>(msg.msg, msg.msg + msg.len));
        snprintf(buf, sizeof(buf), "Receive %d %f %f %d: %s", i, msg.receive_time, msg.sent_time, msg.len, join(cmds, ", ").c_str());
        out.push_back(std::string(buf));
    }

    return join(out, "\n");
}

void SerialReader::handle_unknown(const json &params)
{
    std::cout << warn_prefix << "Unknown message type " << params.at("#msgid")
              << ": " << params.at("#msg") << std::endl;
}

void SerialReader::handle_output(const json &params)
{
    std::string msg = params["#msg"].get<std::string>();
    if (msg.find("Timer too close") != std::string::npos)
    {
        // Timer too close 2803664450 2804131578 command_queue_step
        int matched = sscanf(msg.c_str(), "Timer too close %u %u", &timer_too_close_clock[0], &timer_too_close_clock[1]);
        if (matched == 2)
            timer_too_close = true;
    }
    SPDLOG_INFO(warn_prefix + "got " + "handle_output name: " + params["#name"].get<std::string>() + " message: " + msg);
}

void SerialReader::handle_default(const json &params)
{
    SPDLOG_INFO(warn_prefix + "got " + "handle_default name: " + params["#name"].get<std::string>());
}

// 独立线程
void SerialReader::bg_thread()
{
    std::shared_ptr<pull_queue_message> response = std::make_shared<pull_queue_message>();
    while (true)
    {
        serialqueue_pull(sq.get(), response.get());
        if (response->len < 0)
            break;

        json params;
        if (response->notify_id)
        {
            params["#sent_time"] = response->sent_time;
            params["#receive_time"] = response->receive_time;

            pending_notifications_mutex.lock();
            auto it = pending_notifications.find(response->notify_id);
            if (it != pending_notifications.end())
            {
                std::shared_ptr<ReactorCompletion> completion = it->second;
                pending_notifications.erase(it);
                pending_notifications_mutex.unlock();
                reactor->async_complete(completion, params);
                continue;
            }
            pending_notifications_mutex.unlock();
            continue;
        }

        params = msgparser->parse(std::vector<uint8_t>(response->msg, response->msg + response->len));
        if (params.empty())
        {
            continue;
        }
        params["#sent_time"] = response->sent_time;
        params["#receive_time"] = response->receive_time;

        if ("shutdown" == params["#name"].get<std::string>())
        {
            SPDLOG_WARN("response->len:{},params['clock']:{},params['static_string_id']:{} mcu {} shutdown!",
                        response->len,
                        std::stoul(params["clock"].get<std::string>()),
                        params["static_string_id"].get<std::string>(), warn_prefix);
        }
        std::pair<std::string, int> hdl = std::make_pair(params["#name"].get<std::string>(),
                                                         params.contains("oid") ? std::stoi(params["oid"].get<std::string>()) : 0);

        std::unique_lock<std::mutex> lock(mutex); // 锁定
        try
        {
            auto it = handlers.find(hdl);
            std::function<void(const json &)> handler;
            if (it != handlers.end())
            {
                handler = it->second;
            }
            else
            {
                handler = [this](const json &params)
                {
                    handle_default(params);
                };
            }
            handler(params);
        }
        catch (const std::exception &e)
        {
            SPDLOG_ERROR("{} Exception in serial callback: {}", warn_prefix, std::string(e.what()));
        }
    }
}

void SerialReader::error(const std::string &msg)
{
    throw elegoo::common::SerialError(warn_prefix + msg);
}

json SerialReader::get_identify_data(double eventtime)
{
    std::vector<uint8_t> identify_data;
    while (true)
    {
        std::ostringstream msg_stream;
        msg_stream << "identify offset=" << identify_data.size() << " count=" << 40;
        std::string msg = msg_stream.str();
        json params;
        try
        {
            params = send_with_response(msg, "identify_response");
        }
        catch (const elegoo::common::SerialError &e)
        {
            SPDLOG_ERROR("Error waiting for identify_response: " + std::string(e.what()));
            return json::object();
        }

        if (std::stoul(params["offset"].get<std::string>()) == identify_data.size())
        {
            std::string msgdata = params["data"].get<std::string>();
            if (msgdata.empty())
            {
                std::string str(identify_data.begin(), identify_data.end());
                json value;
                value["data"] = str;
                return value;
            }
            identify_data.insert(identify_data.end(), msgdata.begin(), msgdata.end());
        }
    }
}

bool SerialReader::start_session(int fd, uint8_t serial_fd_type, int client_id)
{
    this->fd = fd;
    sq = std::shared_ptr<serialqueue>(
        serialqueue_alloc(fd, serial_fd_type, client_id),
        serialqueue_free);
    std::shared_ptr<ReactorCompletion> completion =
        reactor->register_callback([this](double eventtime)
                                   { return get_identify_data(eventtime); });
    background_thread = std::make_shared<std::thread>(&SerialReader::bg_thread, this);
    // background_thread->join();
    background_thread->detach();

    json identify_data = completion->wait(get_monotonic() + 5.);
    if (identify_data.empty())
    {
        SPDLOG_WARN("{} Timeout on connect", warn_prefix);
        disconnect();
        return false;
    }

    msgparser = std::make_shared<MessageParser>(warn_prefix);
    std::string strData = identify_data["data"];
    msgparser->process_identify(std::vector<uint8_t>(strData.begin(), strData.end()), true);
    register_response([this](const json &params)
                      { handle_unknown(params); }, "#unknown");
    std::string mcu_version = msgparser->get_constant_str("MCU_VERSION", "");
    SPDLOG_INFO("{} MCU_VERSION: {}", warn_prefix, mcu_version);
    float wire_freq = -1;
    if (serial_fd_type == 'c')
    {
        wire_freq = msgparser->get_constant_float("CANBUS_FREQUENCY"); // 72000000;//
    }
    else
    {
        float defalut = DOUBLE_NONE;
        wire_freq = msgparser->get_constant_float("SERIAL_BAUD", &defalut); // 250000;//
    }

    if (!std::isnan(wire_freq))
    {
        serialqueue_set_wire_frequency(sq.get(), wire_freq);
    }

    int receive_window = -1;
    int default_recv = 192;
    receive_window = msgparser->get_constant_int("RECEIVE_WINDOW", &default_recv); // 192;//
    if (receive_window != -1)
    {
        serialqueue_set_receive_window(sq.get(), receive_window);
    }
    return true;
}

void SerialReader::handle_unknown_init(const json &params)
{
    std::cout << warn_prefix << "Unknown message " << params.at("#msgid")
              << " (len " << params.at("#msg").size() << ") while identifying" << std::endl;
}

std::string SerialReader::join(const std::vector<std::string> &elements, const std::string &delimiter)
{
    std::ostringstream joined;
    for (size_t i = 0; i < elements.size(); ++i)
    {
        if (i > 0)
            joined << delimiter;
        joined << elements[i];
    }
    return joined.str();
}

SerialRetryCommand::SerialRetryCommand(std::shared_ptr<SerialReader> serial,
                                       const std::string &name, uint32_t oid) : serial(serial),
                                                                                name(name), oid(oid)
{
    serial->register_response(
        [this](const json &params)
        {
            handle_callback(params);
        },
        name, oid);
}

SerialRetryCommand::~SerialRetryCommand()
{
}

void SerialRetryCommand::handle_callback(const json &params)
{
    last_params = params;
}

json SerialRetryCommand::get_response(const std::vector<std::vector<uint8_t>> &cmds,
                                      std::shared_ptr<command_queue> cmd_queue,
                                      uint64_t minclock, uint64_t reqclock)
{
    int retries = 20;
    double retry_delay = 0.01;
    while (true)
    {
        for (size_t i = 0; i < cmds.size() - 1; ++i)
        {
            serial->raw_send(std::vector<uint8_t>(cmds[i].begin(), cmds[i].end()), minclock, reqclock, cmd_queue);
        }
        serial->raw_send_wait_ack(std::vector<uint8_t>(cmds[cmds.size() - 1].begin(), cmds[cmds.size() - 1].end()), minclock, reqclock, cmd_queue);
        json params = last_params;
        if (!params.empty())
        {
            serial->register_response(nullptr, name, oid);
            return params;
        }

        if (retries <= 0)
        {
            serial->register_response(nullptr, name, oid);
            throw elegoo::common::SerialError("Unable to obtain " + name + " response");
        }
        serial->get_reactor()->pause(get_monotonic() + retry_delay);
        --retries;
        retry_delay *= 2;
    }
}
