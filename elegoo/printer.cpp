/*****************************************************************************
 * @Author       : Louisa
 * @Date         : 2024-11-25 09:54:01
 * @LastEditors  : zhangjxxxx
 * @LastEditTime : 2025-09-05 12:04:52
 * @Description  : Main code for host side printer firmware
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#include <iostream>
#include <unordered_map>
#include <chrono>
#include <unistd.h>

#include "c_helper.h"
#include "printer.h"
#include "gcode.h"
#include "webhooks.h"
#include "msgproto.h"
#include "extras_factory.h"
// #include "connection_manager.h"
#include "update_ota.h"
#include "gcode.h"


Printer::Printer(std::shared_ptr<SelectReactor> main_reactor,
                 const std::unordered_map<std::string, std::string> &start_args) : reactor(main_reactor), start_args(start_args),
                                                                                   in_shutdown_state(false)
{
    reactor->register_callback([this](double eventtime)
                               {
        SPDLOG_DEBUG("reactor->register_callback connect(eventtime)!");
        return connect(eventtime); });

    message_ready = "Printer is ready";
    message_startup = "Printer is not ready \
    The elegoo host software is attempting to connect.  Please \
    retry in a few moments.";
    message_restart = "Once the underlying issue is \
    corrected, use the \"RESTART\"  \
    command to reload the config and restart the host software.\
    Printer is halted";
    state_message = message_startup;

    SPDLOG_INFO("create Printer success!");
}

Printer::~Printer()
{
}

std::unordered_map<std::string, std::string> Printer::get_start_args()
{
    return start_args;
}

std::shared_ptr<SelectReactor> Printer::get_reactor()
{
    return reactor;
}

std::pair<std::string, std::string> Printer::get_state_message()
{
    std::string category;
    if (state_message == message_ready)
    {
        category = "ready";
    }
    else if (state_message == message_startup)
    {
        category = "startup";
    }
    else if (in_shutdown_state)
    {
        category = "shutdown";
    }
    else
    {
        category = "error";
    }

    return {state_message, category};
}

bool Printer::is_shutdown()
{
    return in_shutdown_state;
}

void Printer::update_error_msg(const std::string &oldmsg, const std::string &newmsg)
{
    if (state_message != oldmsg ||
        (state_message == message_ready || state_message == message_startup) ||
        (newmsg == message_ready || newmsg == message_startup))
    {
        return;
    }
    state_message = newmsg;
}

void Printer::add_object(const std::string &name,
                         Any default_object)
{
    if (objects.find(name) != objects.end())
        throw elegoo::common::ConfigParserError("Printer object " + name + "already created");
    objects[name] = default_object;
}

// template <typename T>
Any Printer::lookup_object(const std::string &name, Any default_object)
{
    if (objects.find(name) != objects.end())
    {
        // SPDLOG_DEBUG("__func__:{} #1",__func__);
        return objects[name];
    }
    if (default_object.empty())
        throw elegoo::common::ConfigParserError("Unknown config object " + name);

    SPDLOG_DEBUG("__func__:{} #1", __func__);
    return default_object;
}

// template <typename T>
Any Printer::load_object(std::shared_ptr<ConfigWrapper> config,
                         const std::string &section)
{
    if (objects.find(section) != objects.end())
        return objects[section];
    std::vector<std::string> module_parts = elegoo::common::split(section);
    std::string init_func = module_parts[0] + "_" + "load_config";
    if (module_parts.size() > 1)
        init_func = module_parts[0] + "_" + "load_config_prefix";
    Any object = ExtrasFactory::create_extras(init_func, config->getsection(section));
    if (object.empty())
        return nullptr;
    objects[section] = object;
    return objects[section];
}

std::map<std::string, Any> Printer::lookup_objects(const std::string &module)
{
    std::map<std::string, Any> result;

    // 如果 module 为空，返回所有对象
    if (module.empty())
    {
        for (const auto &item : objects)
        {
            result[item.first] = item.second;
        }
        return result;
    }
    // 否则，查找以 module 为前缀的对象
    std::string prefix = module + " ";
    for (const auto &item : objects)
    {
        if (item.first.find(prefix) == 0)
        {
            result[item.first] = item.second;
        }
    }

    auto it = objects.find(module);
    if (it != objects.end())
    {
        // result.insert(result.begin(), *it);
        result[it->first] = it->second;
    }

    return result;
}

// 该函数主线程在主线程内
std::string Printer::run()
{
    try
    {
        reactor->run();
    }
    catch (const std::exception &e)
    {
        std::string msg = "Unhandled exception during run: " + std::string(e.what());
        SPDLOG_ERROR(msg);
        try
        {
            // 重新运行reactor以完成关机操作
            reactor->register_callback([this, msg](double eventtime)
                                       { return this->invoke_shutdown(msg); });
            reactor->run();
        }
        catch (const std::exception &e2)
        {
            SPDLOG_ERROR("Repeat unhandled exception during run: {}", e2.what());
            run_result = "error_exit";
        }
    }
    std::string result = run_result;
    // reactor停止
    SPDLOG_INFO("reactor end: {}", result);
    try
    {
        // 此时唤起的处理函数并不在协程
        if (result == "firmware_restart")
        {
            elegoo::common::SignalManager::get_instance().emit_signal("elegoo:firmware_restart");
        }
        elegoo::common::SignalManager::get_instance().emit_signal("elegoo:disconnect");
    }
    catch (const std::exception &e)
    {
        SPDLOG_ERROR("Unhandled exception during post run: {}", e.what());
    }

    return result;
}

void Printer::set_rollover_info(std::string, std::string, bool log)
{
    // 待确认日记系统，再来实现
}

double Printer::invoke_shutdown(const std::string &msg,
                                const std::map<std::string, std::string> &details)
{
    SPDLOG_INFO("msg:{} in_shutdown_state:{}", msg, in_shutdown_state);
    if (in_shutdown_state)
        return 0;
    SPDLOG_WARN("Transition to shutdown state: {}", msg);
    in_shutdown_state = true;
    set_state(msg);
    
    std::shared_ptr<GCodeDispatch> gcode = any_cast<std::shared_ptr<GCodeDispatch>>(lookup_object("gcode"));
    gcode->respond_ecode("", elegoo::common::ErrorCode::SYSTEM_ERROR, 
        elegoo::common::ErrorLevel::WARNING);
    
    elegoo::common::SignalManager::get_instance().try_emit_signal(
        "elegoo:shutdown", msg);
    elegoo::common::SignalManager::get_instance().emit_signal(
        "elegoo:notify_mcu_shutdown", msg, details);
    return 0;
}

void Printer::invoke_async_shutdown(const std::string &msg,
                                    const std::map<std::string, std::string> &details)
{
    reactor->register_async_callback([this, msg, details](double enventtime)
                                     { return invoke_shutdown(msg, details); });
}

void Printer::request_exit(std::string result)
{
    if (run_result.empty())
    {
        run_result = result;
    }
    reactor->end();
}

void Printer::set_state(const std::string &msg)
{
    if (state_message == message_ready || state_message == message_startup)
    {
        state_message = msg;
    }

    if (msg != message_ready && !start_args["debuginput"].empty())
    {
        request_exit("error_exit");
    }
}

void Printer::read_config()
{
    SPDLOG_INFO("read_config begin!");
    // 添加GCODE模块对象
    //  add_object("gcode", std::make_shared<GCodeDispatch>(shared_from_this()));
    //  add_object("gcode_io", std::make_shared<GCodeIO>(shared_from_this()));
    GCODE::gcode_add_early_printer_objects(shared_from_this());
    // 添加网络模块对象
    std::shared_ptr<WebHooks> web_hooks = std::make_shared<WebHooks>(shared_from_this());
    add_object("webhooks", web_hooks);
    web_hooks->create_socket();
    // 添加配置模块对象
    std::shared_ptr<PrinterConfig> pconfig = std::make_shared<PrinterConfig>(shared_from_this());
    add_object("configfile", pconfig);
    // 读取配置文件
    config = pconfig->read_main_config();
    // 添加引脚模块对象
    add_object("pins", std::make_shared<PrinterPins>());
    add_object("host_warpper", std::make_shared<HostWarpper>());
    any_cast<std::shared_ptr<HostWarpper>>(lookup_object("host_warpper"))->init(config);

    // 添加MCU模块对象
    std::shared_ptr<ClockSync> mainsync = std::make_shared<ClockSync>(reactor, "mcu");
    std::string name = "mcu";
    std::shared_ptr<MCU> mcu = std::make_shared<MCU>(config->getsection(name), mainsync);
    add_object(name, mcu);
    any_cast<std::shared_ptr<PrinterPins>>(lookup_object("pins"))->register_chip(name, std::static_pointer_cast<ChipBase>(mcu));
    name += " ";
    std::vector<std::shared_ptr<ConfigWrapper>> section_configs = config->get_prefix_sections(name);
    for (int i = 0; i < section_configs.size(); i++)
    {
        name = section_configs.at(i)->get_name();
        if (name.rfind("mcu ", 0) == 0)
            name = name.substr(4);
        auto mcu_i = std::make_shared<MCU>(section_configs.at(i),
                                           std::make_shared<SecondarySync>(reactor, mainsync, name));
        add_object(section_configs.at(i)->get_name(), mcu_i);
        name = mcu_i->get_name();
        any_cast<std::shared_ptr<PrinterPins>>(lookup_object("pins"))->register_chip(name, std::static_pointer_cast<ChipBase>(mcu_i));
    }
    // 加载组件模块
    section_configs = config->get_prefix_sections("");
    for (int i = 0; i < section_configs.size(); i++)
    {
        SPDLOG_DEBUG("section_configs.at(i:{})->get_name():{}", i, section_configs.at(i)->get_name());
        if (section_configs.at(i)->get_name() == "extruder")
            continue;
        load_object(config, section_configs.at(i)->get_name());
    }
    // 添加打印头模块对象
    std::shared_ptr<ToolHead> toolhead = std::make_shared<ToolHead>(config);
    add_object("toolhead", toolhead);
    toolhead->init();
    // 添加挤出机模块对象
    std::string section = "extruder";
    for (int i = 0; i < 99; i++)
    {
        if (i)
            section = section + std::to_string(i);
        if (!config->has_section(section))
            break;
        std::shared_ptr<PrinterExtruder> printer_extruder = std::make_shared<PrinterExtruder>(config->getsection(section), i);
        add_object(section, printer_extruder);
        if (section == "extruder")
            toolhead->set_extruder(printer_extruder, 0);
    }

    // pconfig->check_unused_options(config);
}

json Printer::connect(double eventtime)
{
    try
    {
        SPDLOG_INFO("connect begin!");
        read_config();
        // 在这一步已经完成所有对象的创建
        SPDLOG_INFO("emit elegoo:mcu_identify!");
        elegoo::common::SignalManager::get_instance().emit_signal(
            "elegoo:mcu_identify");
        SPDLOG_INFO("emit elegoo:connect!");
        elegoo::common::SignalManager::get_instance().emit_signal(
            "elegoo:connect");
    }
    catch (const std::exception &e)
    {
        SPDLOG_ERROR("{}", e.what());
        return json::object();
    }

    try
    {
        SPDLOG_DEBUG("__func__:{},message_ready:{}", __func__, message_ready);
        set_state(message_ready);
        SPDLOG_INFO("emit elegoo:ready!");
        elegoo::common::SignalManager::get_instance().emit_signal(
            "elegoo:ready");

        // Custom build startup banner
        std::string sw_ver = start_args.count("software_version") ? start_args["software_version"] : "unknown";
        std::string cpu    = start_args.count("cpu_info")         ? start_args["cpu_info"]         : "unknown";
        SPDLOG_INFO("============================================");
        SPDLOG_INFO("  CentauriCarbon2 Custom Build");
        SPDLOG_INFO("  Compiled : {} {}", __DATE__, __TIME__);
        SPDLOG_INFO("  Firmware : {}", sw_ver);
        SPDLOG_INFO("  CPU      : {}", cpu);
        SPDLOG_INFO("============================================");

        elegoo::common::SignalManager::get_instance().emit_signal(
            "elegoo:initialized");
    }
    catch (const std::exception &e)
    {
        std::cout << "Unhandled exception during ready callback " << std::string(e.what()) << std::endl;
        invoke_shutdown("Internal error during ready callback: " + std::string(e.what()));
    }

    return json::object();
}