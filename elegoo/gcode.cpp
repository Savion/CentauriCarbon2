/*****************************************************************************
 * @Author       : Louisa
 * @Date         : 2024-11-25 09:53:35
 * @LastEditors  : loping
 * @LastEditTime : 2025-09-27 19:35:58
 * @Description  : Parse gcode commands
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

 #include "gcode.h"
 #include "printer.h"
 #include <sstream>
 #include <unistd.h>
 #include <regex>
 #include <type_traits>
 #include "toolhead.h"
 #include <regex>
 #include <stdexcept>
 #include <iomanip>
 #include "bed_mesh.h"
 #include "print_stats.h"
 #include "heaters.h"
 #include "mmu.h"

 GCodeCommand::GCodeCommand(std::shared_ptr<GCodeDispatch> gcode,
     const std::string& command,
     const std::string& commandline,
     const std::map<std::string, std::string>& params,
     bool need_ack) : command(command),
     commandline(commandline), params(params),
     need_ack(need_ack)
 {
     respond_info = [gcode](const std::string& msg, bool log = true) {
         gcode->respond_info(msg, log);
     };
     respond_raw = [gcode](const std::string msg) {
         gcode->respond_raw(msg);
     };
     respond_feedback = [gcode](const json& msg) {
        gcode->respond_feedback(msg);
    };
     // SPDLOG_DEBUG("create GCodeCommand success!");
 }

 GCodeCommand::GCodeCommand(std::shared_ptr<GCodeDispatch> gcode,
     const std::string& command,
     const std::string& commandline,
     const std::map<std::string, double>& params,
     bool need_ack) : command(command),
     commandline(commandline), params_double(params),
     need_ack(need_ack)
 {
     respond_info = [gcode](const std::string& msg, bool log = true) {
         gcode->respond_info(msg, log);
     };
     respond_raw = [gcode](const std::string msg) {
         gcode->respond_raw(msg);
     };
     respond_feedback = [gcode](const json& msg) {
        gcode->respond_feedback(msg);
    };
     // SPDLOG_DEBUG("create GCodeCommand success!");
 }

 GCodeCommand::~GCodeCommand()
 {
     // SPDLOG_DEBUG("~GCodeCommand");
 }

 std::string GCodeCommand::get_command()
 {
     return command;
 }

 std::string GCodeCommand::get_commandline()
 {
     return commandline;
 }

 std::map<std::string, std::string> GCodeCommand::get_command_parameters()
 {
     return params;
 }

 std::map<std::string, double> GCodeCommand::get_command_parameters_double()
 {
     return params_double;
 }

 std::string GCodeCommand::get_raw_command_parameters()
 {
     std::string cmd = command;

     if (cmd.rfind("M117 ", 0) == 0 || cmd.rfind("M118 ", 0) == 0)
     {
         cmd = cmd.substr(0, 4);
     }

     std::string rawparams = commandline;
     std::string urawparams = rawparams;
     std::transform(urawparams.begin(), urawparams.end(), urawparams.begin(), ::toupper);

     if (urawparams.find(cmd) != 0)
     {
         rawparams = rawparams.substr(urawparams.find(cmd));
         size_t end = rawparams.rfind('*');
         if (end != std::string::npos)
         {
             rawparams = rawparams.substr(0, end);
         }
     }

     rawparams = rawparams.substr(cmd.size());

     if (!rawparams.empty() && rawparams[0] == ' ')
     {
         rawparams = rawparams.substr(1);
     }

     return rawparams;
 }

 bool GCodeCommand::ack(const std::string& msg)
 {
     if (!need_ack)
     {
         return false;
     }

     std::string ok_msg = "ok";
     if (!msg.empty())
     {
         ok_msg += " " + msg;
     }

     respond_raw(ok_msg);
     need_ack = false;

     return true;
 }

 std::string GCodeCommand::get(const std::string& name,
     const std::string& default_value)
 {
     std::string value;
     if(params.count(name) > 0) {
         value = params.at(name);
     } else {
         if(default_value == "_invalid_")
         {
             SPDLOG_ERROR("GCodeCommand {}, no find defauld value", name);
             throw elegoo::common::CommandError("Error on '" + commandline + "': missing " + name);
         }
         return default_value;
     }
     return value;
 }

 int GCodeCommand::get_int(const std::string& name,
     int default_value,
     int minval,
     int maxval)
 {
     std::string value;
     if(params.count(name) > 0) {
         value = params.at(name);
     } else {
         if(default_value == INT_NONE)
         {
             SPDLOG_ERROR("GCodeCommand {}, no find defauld value", name);
             throw elegoo::common::CommandError("Error on '" + commandline + "': missing " + name);
         }

         return default_value;
     }

     int i_value;
     try {
         i_value = std::stoi(value);
     } catch (const std::invalid_argument& e) {
         SPDLOG_ERROR("GCodeCommand {} : {}: cannot be converted to int.", name, value);
     } catch (const std::out_of_range& e) {
         SPDLOG_ERROR("GCodeCommand {} : {}: is too large to be converted to int.", name, value);
     }

     if (minval != INT_NONE && i_value < minval) {
         throw  elegoo::common::CommandError(
             "Error on " + commandline + " : " + name + " must have minimum of " + std::to_string(minval));
     }

     if (maxval != INT_NONE && i_value > maxval) {
         throw  elegoo::common::CommandError(
             "Error on " + commandline + " : " + name + " must have maximum of " + std::to_string(minval));
     }

     return i_value;
 }

 double GCodeCommand::get_double(const std::string& name,
     double default_value,
     double minval,
     double maxval,
     double above,
     double below)
 {
     std::string value;
     if(params.count(name) > 0) {
         value = params.at(name);
     } else {

         if(!std::isinf(default_value)) {
             return default_value;
         }

         throw elegoo::common::CommandError("Error on '" + commandline + "': missing " + name);
         SPDLOG_ERROR("GCodeCommand {}, no find defauld value", name);
     }

     double d_value;
     try {
         d_value = std::stod(value);
     } catch (const std::invalid_argument& e) {
         SPDLOG_ERROR("GCodeCommand {} : {}: cannot be converted to int.", name, value);
     } catch (const std::out_of_range& e) {
         SPDLOG_ERROR("GCodeCommand {} : {}: is too large to be converted to int.", name, value);
     }

     if (!std::isnan(minval) && d_value < minval) {
         throw  elegoo::common::CommandError(
             "Error on " + commandline + " : " + name + " must have minimum of " + std::to_string(minval));
     }

     if (!std::isnan(maxval) && d_value > maxval) {
         throw  elegoo::common::CommandError(
             "Error on " + commandline + " : " + name + " must have maximum of " + std::to_string(maxval));
     }

     if (!std::isnan(above) && d_value <= above) {
         throw  elegoo::common::CommandError(
             "Error on " + commandline + " : " + name + " must be above " + std::to_string(above));
     }

     if (!std::isnan(below) && d_value >= below) {
         throw  elegoo::common::CommandError(
             "Error on " + commandline + " : " + name + " must be below " + std::to_string(below));
     }

     return d_value;
 }

 GCodeDispatch::GCodeDispatch(std::shared_ptr<Printer> printer) :
     printer(printer), is_printer_ready(false), is_ignore(false),channel_b(-1)
 {
     std::unordered_map<std::string, std::string> start_args = printer->get_start_args();
     is_fileinput = start_args.find("debuginput") != start_args.end() && !start_args["debuginput"].empty();
     elegoo::common::SignalManager::get_instance().register_signal(
         "elegoo:ready",
         std::function<void()>([this](){
             SPDLOG_DEBUG("elegoo:ready !");
             handle_ready();
             SPDLOG_DEBUG("elegoo:ready !");
         })
     );
     elegoo::common::SignalManager::get_instance().register_signal(
         "elegoo:shutdown",
         std::function<void()>([this](){
             SPDLOG_DEBUG("elegoo:shutdown !");
             handle_shutdown();
             SPDLOG_DEBUG("elegoo:shutdown !");
         })
     );
     elegoo::common::SignalManager::get_instance().register_signal(
         "elegoo:disconnect",
         std::function<void()>([this](){
             handle_disconnect();
         })
     );

    elegoo::common::SignalManager::get_instance().register_signal(
        "elegoo:initialized",
        std::function<void()>([this](){
            handle_initialized();
        }));

     mutex = printer->get_reactor()->mutex();

     ready_gcode_handlers =
         std::map<std::string, std::function<void(std::shared_ptr<GCodeCommand>)>>();
     base_gcode_handlers =
         std::map<std::string, std::function<void(std::shared_ptr<GCodeCommand>)>>();
     gcode_handlers = &base_gcode_handlers;
     initialize_handlers();
     status_commands = json::object();
     std::cout << "create GCodeDispatch success!" << std::endl;
 }

 GCodeDispatch::~GCodeDispatch()
 {
     SPDLOG_DEBUG("~GCodeDispatch()");
 }

 bool GCodeDispatch::is_traditional_gcode(const std::string& cmd)
 {
     try {
         std::string upper_cmd = cmd;
         for (auto& ch : upper_cmd)
         {
             ch = std::toupper(ch);
         }

         std::string first_part = upper_cmd.substr(0, upper_cmd.find(' '));

         if (!std::isupper(first_part[0]))
         {
             return false;
         }

         std::string number_part = first_part.substr(1);
         float val = std::stof(number_part);  // 检查是否能转换为浮点数

         return std::isdigit(first_part[1]);
     } catch (...) {
         return false;
     }
 }

 std::function<void(std::shared_ptr<GCodeCommand>)>
     GCodeDispatch::register_command(const std::string& cmd,
     std::function<void(std::shared_ptr<GCodeCommand>)> func, bool when_not_ready,
     const std::string& desc)
 {
     SPDLOG_DEBUG("{}: cmd:{}", __FUNCTION__, cmd);
     // 删除
     if (!func)
     {
         SPDLOG_DEBUG("{}: delelte cmd:{}", __FUNCTION__, cmd);
         std::function<void(std::shared_ptr<GCodeCommand>)> old_cmd;
         auto it = ready_gcode_handlers.find(cmd);
         if (it != ready_gcode_handlers.end())
         {
             old_cmd = it->second;
         }
         if (ready_gcode_handlers.find(cmd) != ready_gcode_handlers.end())
         {
             SPDLOG_DEBUG("{}: delelte cmd from ready_gcode_handlers:{}", __FUNCTION__, cmd);
             ready_gcode_handlers.erase(cmd); // 从ready_gcode_handlers中删除
         }

         if (base_gcode_handlers.find(cmd) != base_gcode_handlers.end())
         {
             SPDLOG_DEBUG("{}: delelte cmd from base_gcode_handlers", __FUNCTION__, cmd);
             base_gcode_handlers.erase(cmd); // 从base_gcode_handlers中删除
         }
         build_status_commands();
         return old_cmd;
     }

     SPDLOG_DEBUG("{}: create cmd:{}", __FUNCTION__, cmd);
     if (ready_gcode_handlers.find(cmd) != ready_gcode_handlers.end())
     {
         SPDLOG_ERROR("gcode command " + cmd + " already registered");
         throw elegoo::common::ConfigParserError("gcode command " + cmd + " already registered");
     }

     if(!is_traditional_gcode(cmd))
     {
         std::function<void(std::shared_ptr<GCodeCommand>)> origfunc = func;
         func = [origfunc,this](std::shared_ptr<GCodeCommand> params)
         {
             std::shared_ptr<GCodeCommand> gcmd = get_extended_params(params);
             origfunc(gcmd);
         };
     }

     ready_gcode_handlers[cmd] = func;
     if (when_not_ready)
         base_gcode_handlers[cmd] = func;

     if(!desc.empty())
     {
         gcode_help[cmd] = desc;
     }
     build_status_commands();
     return nullptr;
 }

 void GCodeDispatch::register_mux_command(const std::string& cmd,
     const std::string& key,
     const std::string& value,
     std::function<void(std::shared_ptr<GCodeCommand>)> func,
     const std::string& desc)
 {
     SPDLOG_DEBUG("__func__:{},cmd:{},key:{},value:{},desc:{}",__func__,cmd,key,value,desc);
     // 查找 mux_commands 中是否已有 cmd
     auto it = mux_commands.find(cmd);
     std::pair<std::string, std::map<std::string, std::function<void(std::shared_ptr<GCodeCommand>)>>> prev;

     if (it == mux_commands.end())
     {
         // SPDLOG_DEBUG("__func__:{},mux_commands.size:{}",__func__,mux_commands.size());
         auto handler = [this, cmd](std::shared_ptr<GCodeCommand> gcmd)
         {
             this->cmd_mux(cmd, gcmd);
         };
         // SPDLOG_DEBUG("__func__:{},cmd:{},desc:{}",__func__,cmd,desc);
         register_command(cmd, handler, false, desc);
         prev = std::make_pair(key, std::map<std::string, std::function<void(std::shared_ptr<GCodeCommand>)>>());
         mux_commands[cmd] = prev;
         // SPDLOG_DEBUG("__func__:{},cmd:{},key:{}",__func__,cmd,key);
     }
     else
     {
         // SPDLOG_DEBUG("__func__:{},mux_commands.size:{}",__func__,mux_commands.size());
         prev = it->second;
     }
     it = mux_commands.find(cmd);

     const std::string& prev_key = it->second.first;
     auto& prev_values = it->second.second;

     // SPDLOG_DEBUG("__func__:{},cmd:{},prev_key:{},key:{}",__func__,cmd,prev_key,key);
     if (prev_key != key)
     {
         SPDLOG_ERROR("mux command " + cmd + " " + key + " " + value + " may have only one key (" + prev_key + ")");
         throw elegoo::common::ConfigParserError("mux command " + cmd + " " + key + " " + value + " may have only one key (" + prev_key + ")");
     }

     if (prev_values.find(value) != prev_values.end())
     {
         SPDLOG_ERROR("mux command " + cmd + " " + key + " " + value + " already registered");
         throw elegoo::common::ConfigParserError("mux command " + cmd + " " + key + " " + value + " already registered");
     }

     prev_values[value] = func;
     // SPDLOG_DEBUG("__func__:{},prev_key:{},value:{},mux_commands[cmd:{}].first:{}",__func__,prev_key,value,cmd,mux_commands[cmd].first);
 }

 std::map<std::string, std::string> GCodeDispatch::get_command_help()
 {
     return gcode_help;
 }

 json GCodeDispatch::get_status(double eventtime)
 {
     json status;
     status["commands"] = status_commands;
     return status;
 }

 void GCodeDispatch::register_output_handler(std::function<void(const std::string&, int, elegoo::common::ErrorLevel)> cb)
 {
     output_callbacks.push_back(cb);
 }

 void GCodeDispatch::register_report_handler(std::function<void(int, elegoo::common::ErrorLevel, const std::string&)> cb)
 {
    //  report_callbacks.push_back(cb);
 }

 void GCodeDispatch::run_script_from_command(const std::string& script)
 {
     std::vector<std::string> tokens;
     std::stringstream ss(script);
     std::string token;
     while (std::getline(ss, token, '\n'))
     {
        tokens.push_back(token);
     }
     process_commands(tokens, false);
 }

 void GCodeDispatch::run_script(const std::string& script)
 {
     SPDLOG_DEBUG("run_script start {}", script);
    // GCode指令有可能正在被锁阻塞导致无法立即响应CANCEL,这里是为了即时关闭加热器
    if(script == "CANCEL_PRINT")
        elegoo::common::SignalManager::get_instance().emit_signal("gcode:CANCEL_PRINT_REQUEST");

     mutex->lock();

     std::vector<std::string> tokens =
         elegoo::common::split(script, "\n");

     try
     {
         process_commands(tokens, false);
     }
     catch(...)
     {
         // SPDLOG_ERROR("{} : {}___", __FUNCTION__, __LINE__);
         // SPDLOG_ERROR("Error processing script: {}", script);
         mutex->unlock();
         throw;
     }


     mutex->unlock();
     SPDLOG_DEBUG("run_script start #4");
 }

 std::shared_ptr<ReactorMutex> GCodeDispatch::get_mutex()
 {
     return mutex;
 }

 std::shared_ptr<GCodeCommand> GCodeDispatch::create_gcode_command(const std::string& command,
     const std::string& commandline,
     const std::map<std::string, std::string>& params,
     bool need_ack)
 {
     return std::make_shared<GCodeCommand>(shared_from_this(), command, commandline, params, need_ack);
 }

 std::shared_ptr<GCodeCommand> GCodeDispatch::create_gcode_command_double(const std::string& command,
     const std::string& commandline,
     const std::map<std::string, double>& params,
     bool need_ack)
 {
     return std::make_shared<GCodeCommand>(shared_from_this(), command, commandline, params, need_ack);
 }

 void GCodeDispatch::respond_raw(const std::string& msg)
 {
     for (const auto& cb : output_callbacks)
     {
         cb(msg, elegoo::common::ErrorCode::CODE_OK, elegoo::common::ErrorLevel::INFO);
     }
 }

 void GCodeDispatch::respond_ecode(const std::string& msg, const int error_code,
     const elegoo::common::ErrorLevel error_level)
 {
     SPDLOG_INFO("respond_ecode: {}, error_code: {} error_level {}", msg, static_cast<int>(error_code),static_cast<int>(error_level));
     for (const auto& cb : output_callbacks)
     {
         cb(msg, error_code, error_level);
     }

     if(error_level == elegoo::common::ErrorLevel::CRITICAL) {
        SPDLOG_ERROR("A critical error has been detected. The device will shut down.");
        printer->invoke_shutdown(msg);
    }
 }

 void GCodeDispatch::respond_info(const std::string& msg, bool log)
 {
     if (log)
     {
         // std::cout << msg << std::endl;
        //  SPDLOG_DEBUG("__func__:{},msg:{}",__func__,msg);
        SPDLOG_INFO(msg);
     }

     std::istringstream stream(msg);
     std::string line;
     std::vector<std::string> lines;

     while (std::getline(stream, line)) {
         line.erase(0, line.find_first_not_of(' '));
         line.erase(line.find_last_not_of(' ') + 1);
         lines.push_back("// " + line);
     }

     std::ostringstream result;
     for (size_t i = 0; i < lines.size(); ++i)
     {
         result << lines[i];
         if (i != lines.size() - 1)
         {
             result << "\n";
         }
     }

     respond_raw(result.str());
     SPDLOG_DEBUG("respond_info result:{}",result.str());
 }

 void GCodeDispatch::respond_feedback(const json& msg)
 {
    json res;
    res["id"] = 0;
    res["feedback"] = msg;
    SPDLOG_DEBUG("respond: {}", res.dump());

    if(webhooks)
        webhooks->broadcast_message(res);
 }

 void GCodeDispatch::respond_report(int error_code, elegoo::common::ErrorLevel error_level, std::string& msg)
 {
    // for (const auto& cb : report_callbacks)
    // {
    //     cb(error_code, error_level, msg);
    // }
 }

 void GCodeDispatch::initialize_handlers()
 {
     SPDLOG_DEBUG("initialize_handlers");
     std::vector<std::string> handlers = {"M110", "M112", "M115", "M2202", "RESTART", "FIRMWARE_RESTART", "ECHO", "STATUS", "HELP", "SET_CONDITION", "SET_CONDITION_END", "DEBUG_STATUS", "TEMPERATURE_WAIT"};

     for (const auto& cmd : handlers)
     {
         std::string func_name = "cmd_" + cmd;
         if (cmd == "M110")
         {
             register_command(cmd,
                 [this](std::shared_ptr<GCodeCommand> gcmd){
                     cmd_M110(gcmd);
                 }, true);
         }
         else if (cmd == "M112")
         {
             register_command(cmd,
                 [this](std::shared_ptr<GCodeCommand> gcmd){
                     cmd_M112(gcmd);
                 }, true);
         }
         else if (cmd == "M115")
         {
             register_command(cmd,
                 [this](std::shared_ptr<GCodeCommand> gcmd){
                     cmd_M115(gcmd);
                 }, true);
         }
         else if (cmd == "M2202")
         {
             register_command(cmd,
                 [this](std::shared_ptr<GCodeCommand> gcmd){
                     cmd_M2202(gcmd);
                 }, true);
         }
         else if (cmd == "SET_CONDITION")
         {
             register_command(cmd,
                 [this](std::shared_ptr<GCodeCommand> gcmd){
                     cmd_SET_CONDITION(gcmd);
                 }, true);
         }
         else if (cmd == "SET_CONDITION_END")
         {
             register_command(cmd,
                 [this](std::shared_ptr<GCodeCommand> gcmd){
                     cmd_SET_CONDITION_END(gcmd);
                 }, true);
         }
         else if (cmd == "RESTART")
         {
             register_command(cmd,
                 [this](std::shared_ptr<GCodeCommand> gcmd){
                     cmd_RESTART(gcmd);
                 }, true, "Reload config file and restart host software");
         }
         else if (cmd == "FIRMWARE_RESTART")
         {
             register_command(cmd,
                 [this](std::shared_ptr<GCodeCommand> gcmd){
                     cmd_FIRMWARE_RESTART(gcmd);
                 }, true, "Restart firmware, host, and reload config");
         }
         else if (cmd == "ECHO")
         {
             register_command(cmd,
                 [this](std::shared_ptr<GCodeCommand> gcmd){
                     cmd_ECHO(gcmd);
                 }, true);
         }
         else if (cmd == "STATUS")
         {
             register_command(cmd,
                 [this](std::shared_ptr<GCodeCommand> gcmd){
                     cmd_STATUS(gcmd);
                 }, true, "Report the printer status");
         }
         else if (cmd == "HELP")
         {
             register_command(cmd,
                 [this](std::shared_ptr<GCodeCommand> gcmd){
                     cmd_HELP(gcmd);
                 }, true, "Report the list of available extended G-Code commands");
         }
         else if (cmd == "DEBUG_STATUS")
         {
             register_command(cmd,
                 [this](std::shared_ptr<GCodeCommand> gcmd){
                     cmd_DEBUG_STATUS(gcmd);
                 }, true, "Report detailed debug status of the printer");
         }
         else if (cmd == "TEMPERATURE_WAIT")
         {
             register_command(cmd,
                 [this](std::shared_ptr<GCodeCommand> gcmd){
                     cmd_TEMPERATURE_WAIT(gcmd);
                 }, false, "Wait for a heater or sensor to reach a target temperature");
         }
     }
 }

 void GCodeDispatch::cmd_default(std::shared_ptr<GCodeCommand> gcmd)
 {
     std::string cmd = gcmd->get_command();
     if (cmd == "M105")
     {
         gcmd->ack("T:0");
         return;
     }
     if (cmd == "M21") {
         // Don't warn about sd card init when not ready
         return;
     }
     if (!is_printer_ready)
     {
         throw elegoo::common::CommandError(printer->get_state_message().first);
     }
     if (cmd.empty())
     {
         std::string cmdline = gcmd->get_commandline();
         if (!cmdline.empty())
         {
             // std::cout << "Debug: " << cmdline << std::endl;
         }
         return;
     }

     if (cmd.find("M117") == 0 || cmd.find("M118") == 0)
     {
         auto handler_it = gcode_handlers->find(cmd.substr(0, 4));
         if (handler_it != gcode_handlers->end())
         {
             handler_it->second(gcmd);
             return;
         }
     }
     else if ((cmd == "M140" || cmd == "M104") && gcmd->get_double("S", 0) == 0.0)
     {
         // Don't warn about requests to turn off heaters when not present
         return;
     }
     else if (cmd == "M107" || (cmd == "M106" && (gcmd->get_double("S", 1) == 0.0 || is_fileinput)))
     {
         // Don't warn about requests to turn off fan when fan not present
         return;
     }

     // Default: Handle error for unknown or unprocessed commands
     gcmd->respond_info("Unknown command: " + cmd, true);
 }

 void GCodeDispatch::cmd_M110(std::shared_ptr<GCodeCommand> gcmd)
 {
     // 无用
 }

 void GCodeDispatch::cmd_M112(std::shared_ptr<GCodeCommand> gcmd)
 {
     printer->invoke_shutdown("Shutdown due to M112 command");
     SPDLOG_DEBUG("{} : {}___ ok!", __FUNCTION__, __LINE__);
 }

 void GCodeDispatch::cmd_M115(std::shared_ptr<GCodeCommand> gcmd)
 {
     std::string software_version = printer->get_start_args()["software_version"];
     SPDLOG_DEBUG("{} : {}___ ok!", __FUNCTION__, __LINE__);
 }

 void GCodeDispatch::cmd_M2202(std::shared_ptr<GCodeCommand> gcmd)
 {
    std::string code_name = gcmd->get("GCODE_ACTION_REPORT", "");
    if(!code_name.empty()) {
        std::vector<std::string> eparams = elegoo::common::split(code_name,"=");
        if(eparams.size() == 2) {
            json res;
            res["command"] = "M2202";
            res["result"] = elegoo::common::strip(eparams[1]);
            SPDLOG_INFO("{} res.dump:{}",__func__,res.dump());
            respond_feedback(res);
        }
    }
 }

 void GCodeDispatch::cmd_SET_CONDITION(std::shared_ptr<GCodeCommand> gcmd)
 {

    std::string A = gcmd->get("A","");
    if(A == "MACHINE_MODEL")
    {
        std::string str = "C";
        std::string B = gcmd->get("B", "");
        if(str != B)
        {
            is_ignore = true;
        }
        SPDLOG_INFO("cmd_SET_CONDITION A={}, B={}, machine_model={}", A,B,str);
    } 
    else if(A == "PRINT_CALIBRATE")
    {
        std::shared_ptr<elegoo::extras::BedMesh> bed_mesh = 
            any_cast<std::shared_ptr<elegoo::extras::BedMesh>>(this->printer->lookup_object("bed_mesh"));

        int value = bed_mesh->get_execute_calirate_from_slicer();
        int B = gcmd->get_int("B", -1);
        if(value != B)
        {
            is_ignore = true;
        }
        SPDLOG_INFO("cmd_SET_CONDITION A={}, B={}, PRINT_CALIBRATE={}",A,B,value);
    }
    else if(A == "SURFACE")
    {
        std::shared_ptr<elegoo::extras::BedMesh> bed_mesh = 
            any_cast<std::shared_ptr<elegoo::extras::BedMesh>>(this->printer->lookup_object("bed_mesh"));
        
        int value = bed_mesh->get_print_surface();
        int B = gcmd->get_int("B", -2);
        if(value != B)
        {
            is_ignore = true;
        }
        SPDLOG_INFO("cmd_SET_CONDITION A={}, B={}, SURFACE={}",A,B,value);
    }
    else if(A == "ENABLE_CANVAS")
    {
        std::shared_ptr<elegoo::extras::Canvas> canvas = 
            any_cast<std::shared_ptr<elegoo::extras::Canvas>>(printer->lookup_object("canvas_dev",std::shared_ptr<elegoo::extras::Canvas>()));

        if(canvas)
        {
            int value = canvas->get_connect_state();
            int B = gcmd->get_int("B", -2);
            if(value != B)
            {
                is_ignore = true;
            }
            SPDLOG_INFO("cmd_SET_CONDITION A={}, B={}, ENABLE_CANVAS={}",A,B,value);
        }
        else
        {
            SPDLOG_ERROR("canvas object no find.");
        }
    }
    else if(A == "BED_MESH_DATEA")
    {
        std::shared_ptr<elegoo::extras::PrintStats> print_stats = 
            any_cast<std::shared_ptr<elegoo::extras::PrintStats>>(printer->lookup_object("print_stats", std::shared_ptr<elegoo::extras::PrintStats>()));
        // bed_mesh_detected 01 01 AB面有数据
        //                   01 00 A面有数据
        //                   00 01 B面有数据
        if(print_stats)
        {
            int value = print_stats->get_status(get_monotonic())["bed_mesh_detected"].get<int>();
            bool B = gcmd->get_int("B", -2) == 0 ? false : true;
            is_ignore = ((value & 0x0100) == 0x0100) ^ B;
            SPDLOG_INFO("cmd_SET_CONDITION A={}, B={}, BED_MESH_DATEA={}",A,B,value);
        }
        else
        {
            SPDLOG_ERROR("print_stats object no find.");
        }
    }
    else if(A == "BED_MESH_DATEB")
    {
        std::shared_ptr<elegoo::extras::PrintStats> print_stats = 
            any_cast<std::shared_ptr<elegoo::extras::PrintStats>>(printer->lookup_object("print_stats", std::shared_ptr<elegoo::extras::PrintStats>()));

        if(print_stats)
        {
            int value = print_stats->get_status(get_monotonic())["bed_mesh_detected"].get<int>();
            bool B = gcmd->get_int("B", -2) == 0 ? false : true;
            is_ignore = ((value & 0x0001) == 0x0001) ^ B;
            SPDLOG_INFO("cmd_SET_CONDITION A={}, B={}, BED_MESH_DATEB={}",A,B,value);
        }
        else
        {
            SPDLOG_ERROR("print_stats object no find.");
        }
    }
    else if(A == "BED_MESH_PROFILE")
    {
        std::shared_ptr<elegoo::extras::BedMesh> bed_mesh = 
            any_cast<std::shared_ptr<elegoo::extras::BedMesh>>(this->printer->lookup_object("bed_mesh"));
        
        int value = bed_mesh->get_print_surface();
        int B = gcmd->get_int("B", -2);
        if(value != B)
        {
            is_ignore = true;
        }
        SPDLOG_INFO("cmd_SET_CONDITION A={}, B={}, BED_MESH_PROFILE={}",A,B,value);
    }
    else if(A == "PRINT_TOOL")
    {
        std::shared_ptr<elegoo::extras::Canvas> canvas = 
            any_cast<std::shared_ptr<elegoo::extras::Canvas>>(printer->lookup_object("canvas_dev",std::shared_ptr<elegoo::extras::Canvas>()));

        if(canvas)
        {
            bool is_canvas = canvas->get_connect_state();
            int B = gcmd->get_int("B", -2);
            channel_b = B;
            if(is_canvas)
            {
                int value = canvas->get_T_channel(B);
                int cur_channel = canvas->get_cur_channel();
                if(value != -1 && value == cur_channel)
                {
                    is_ignore = true;
                }
                SPDLOG_INFO("cmd_SET_CONDITION A={}, B={}, PRINT_TOOL={} cur_channel={}",A,B,value,cur_channel);
            }
            else
            {
                is_ignore = true;
                SPDLOG_INFO("cmd_SET_CONDITION A={} current not canvas",A);
            }
        }
        else
        {
            SPDLOG_ERROR("canvas object no find.");
        }
    }
    else if(A == "NOZZLE_TEMP")
    {
        std::shared_ptr<elegoo::extras::PrinterHeaters> pheaters = 
            any_cast<std::shared_ptr<elegoo::extras::PrinterHeaters>>(printer->lookup_object("heaters"));
        double value = pheaters->get_heaters()["extruder"]->get_temp(0).first;
        int B = gcmd->get_int("B", -1);
        if(value >= B)
        {
            is_ignore = true;
        }
        SPDLOG_INFO("cmd_SET_CONDITION A={}, B={}, NOZZLE_TEMP={}",A,B,value);
    }
    
    SPDLOG_INFO("cmd_SET_CONDITION is_ingore = {}", is_ignore);
 }

 void GCodeDispatch::cmd_SET_CONDITION_END(std::shared_ptr<GCodeCommand> gcmd)
 {
    int B = gcmd->get_int("B", -1);
    if(B==-1)
    {
        is_ignore = false;
    }
    else if(channel_b == B)
    {
        is_ignore = false;
    }
    
    SPDLOG_INFO("cmd_SET_CONDITION_END is_ingore = {}", is_ignore);
 }

 void GCodeDispatch::request_restart(const std::string& result)
 {
     if(is_printer_ready)
     {
         std::shared_ptr<ToolHead> toolhead =
             any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));
         double print_time = toolhead->get_last_move_time();
         if(result == "exit")
         {
             std::cout << "Exiting (print time " << print_time << std::endl;
         }
         elegoo::common::SignalManager::get_instance().emit_signal(
             "gcode:request_restart", print_time);
         toolhead->dwell(0.500);
         toolhead->wait_moves();
     }

     printer->request_exit(result);
 }

 void GCodeDispatch::cmd_RESTART(std::shared_ptr<GCodeCommand> gcmd)
 {
     SPDLOG_DEBUG("{} : {}___ ok", __FUNCTION__, __LINE__);
     request_restart("restart");
 }

 void GCodeDispatch::cmd_FIRMWARE_RESTART(std::shared_ptr<GCodeCommand> gcmd)
 {
     SPDLOG_DEBUG("{} : {}___ ok", __FUNCTION__, __LINE__);
     request_restart("firmware_restart");
 }

 void GCodeDispatch::cmd_ECHO(std::shared_ptr<GCodeCommand> gcmd)
 {
     SPDLOG_DEBUG("{} : {}___ ok", __FUNCTION__, __LINE__);
     gcmd->respond_info(gcmd->get_commandline(), false);
 }

 void GCodeDispatch::cmd_STATUS(std::shared_ptr<GCodeCommand> gcmd)
 {
     SPDLOG_DEBUG("{} : {}___ ok", __FUNCTION__, __LINE__);
     if(is_printer_ready)
     {
         respond_state("Ready");
         return;
     }
     std::string msg = printer->get_state_message().first;
     throw elegoo::common::CommandError(msg + "\nElegoo state: Not ready");
 }

 void GCodeDispatch::cmd_HELP(std::shared_ptr<GCodeCommand> gcmd)
 {
     std::vector<std::string> cmdhelp;

     if (!is_printer_ready)
     {
         cmdhelp.push_back("Printer is not ready - not all commands available.");
     }

     cmdhelp.push_back("Available extended commands:");

     if(gcode_handlers->empty())
     {
         SPDLOG_DEBUG("gcode_handlers->empty()");
     }

     std::vector<std::string> sorted_commands;
     for (const auto& handler : *gcode_handlers)
     {
         sorted_commands.push_back(handler.first);
     }
     std::sort(sorted_commands.begin(), sorted_commands.end());

     for (const auto& cmd : sorted_commands)
     {
         auto it = gcode_help.find(cmd);
         if (it != gcode_help.end())
         {
             if (cmd.length() < 10) {
                 cmdhelp.push_back(cmd + std::string(10 - cmd.length(), ' ') + ": " + it->second);
             }
             else {
                cmdhelp.push_back(cmd + ": " + it->second);
             }
         }
     }

     std::string result;
     for (const auto& cmd : cmdhelp)
     {
         result += cmd + "\n";
     }
     gcmd->respond_info(result, false);
 }

 void GCodeDispatch::cmd_DEBUG_STATUS(std::shared_ptr<GCodeCommand> gcmd)
 {
     std::string out;
     auto state = printer->get_state_message();
     out += "State:   " + state.second + "\n";
     out += "Message: " + state.first + "\n";
     out += "Ready:   " + std::string(is_printer_ready ? "yes" : "no") + "\n";

     out += "\nRegistered G-Code commands (" + std::to_string(gcode_handlers->size()) + "):\n";
     for (const auto& h : *gcode_handlers)
     {
         auto it = gcode_help.find(h.first);
         if (it != gcode_help.end())
             out += "  " + h.first + ": " + it->second + "\n";
         else
             out += "  " + h.first + "\n";
     }

     try
     {
         std::shared_ptr<elegoo::extras::PrinterHeaters> pheaters =
             any_cast<std::shared_ptr<elegoo::extras::PrinterHeaters>>(
                 printer->lookup_object("heaters"));
         double eventtime = get_monotonic();
         auto heater_names = pheaters->get_all_heaters();
         if (!heater_names.empty())
         {
             out += "\nTemperatures:\n";
             for (const auto& name : heater_names)
             {
                 auto heater = pheaters->lookup_heater(name);
                 auto temps = heater->get_temp(eventtime);
                 char buf[64];
                 snprintf(buf, sizeof(buf), "  %-16s actual=%.1f  target=%.1f\n",
                     name.c_str(), temps.first, temps.second);
                 out += buf;
             }
         }
     }
     catch (...)
     {
         out += "\nTemperature info unavailable.\n";
     }

     gcmd->respond_info(out, false);
 }

 void GCodeDispatch::cmd_TEMPERATURE_WAIT(std::shared_ptr<GCodeCommand> gcmd)
 {
     std::string sensor = gcmd->get("SENSOR");
     if (sensor == "_invalid_")
     {
         throw elegoo::common::CommandError("TEMPERATURE_WAIT: SENSOR parameter is required");
     }
     double min_temp = gcmd->get_double("MINIMUM", DOUBLE_NONE);
     double max_temp = gcmd->get_double("MAXIMUM", DOUBLE_NONE);
     if (std::isnan(min_temp) && std::isnan(max_temp))
     {
         throw elegoo::common::CommandError("TEMPERATURE_WAIT: MINIMUM and/or MAXIMUM required");
     }

     std::shared_ptr<elegoo::extras::PrinterHeaters> pheaters;
     try
     {
         pheaters = any_cast<std::shared_ptr<elegoo::extras::PrinterHeaters>>(
             printer->lookup_object("heaters"));
     }
     catch (...)
     {
         throw elegoo::common::CommandError("TEMPERATURE_WAIT: heater module not available");
     }

     std::shared_ptr<elegoo::extras::Heater> heater;
     try
     {
         heater = pheaters->lookup_heater(sensor);
     }
     catch (...)
     {
         throw elegoo::common::CommandError("TEMPERATURE_WAIT: unknown sensor '" + sensor + "'");
     }

     std::shared_ptr<SelectReactor> reactor = printer->get_reactor();
     double eventtime = get_monotonic();
     while (!printer->is_shutdown())
     {
         auto temps = heater->get_temp(eventtime);
         double current = temps.first;
         bool above_min = std::isnan(min_temp) || (current >= min_temp);
         bool below_max = std::isnan(max_temp) || (current <= max_temp);
         if (above_min && below_max)
             break;
         char buf[64];
         snprintf(buf, sizeof(buf), "T:%.1f /%.1f", current, temps.second);
         gcmd->respond_raw(std::string(buf));
         eventtime = reactor->pause(eventtime + 1.0);
     }
     gcmd->ack();
 }

 void GCodeDispatch::build_status_commands()
 {
     std::map<std::string, std::map<std::string, std::string>> commands;
     for (const auto& cmd : *gcode_handlers)
         commands[cmd.first] = {};
     for (const auto& help_pair : gcode_help)
     {
         const std::string& cmd = help_pair.first;
         const std::string& help_text = help_pair.second;
         if (commands.find(cmd) != commands.end())
             commands[cmd]["help"] = help_text;
     }
     status_commands = commands;
 }

 void GCodeDispatch::handle_shutdown()
 {
     SPDLOG_DEBUG("{} : {}___", __FUNCTION__, __LINE__);
     if(!is_printer_ready)
     {
         return;
     }

     is_printer_ready = false;
     // gcode_handlers是引用base_gcode_handlers，而不是副本
     gcode_handlers = &base_gcode_handlers;
     if(gcode_handlers->empty())
     {
         SPDLOG_DEBUG("gcode_handlers->empty()");
     }

     build_status_commands();
     respond_state("Shutdown");
     SPDLOG_DEBUG("{} : {}___ ok", __FUNCTION__, __LINE__);
 }

 void GCodeDispatch::handle_disconnect()
 {
     respond_state("Disconnect");
 }

 void GCodeDispatch::handle_initialized()
 {
    json res;
    res["command"] = "M2202";
    res["result"] = "2603";
    respond_feedback(res);   
    SPDLOG_INFO("Device has been initialized");
 }


 void GCodeDispatch::handle_ready()
 {
     is_printer_ready = true;
     gcode_handlers = &ready_gcode_handlers;
     SPDLOG_DEBUG("is_printer_ready:{},gcode_handlers->size:{}",is_printer_ready,gcode_handlers->size());
     if(gcode_handlers->empty())
     {
         SPDLOG_DEBUG("gcode_handlers->empty()");
     }
     webhooks = any_cast<std::shared_ptr<WebHooks>>(printer->lookup_object("webhooks"));
     build_status_commands();
     respond_state("Ready");
 }

//  std::regex args_r("([A-Z_]+|[A-Z*/])", std::regex::optimize);
 void GCodeDispatch::process_commands(std::vector<std::string> commands, bool need_ack)
 {
     for (std::string line : commands)
     {
         SPDLOG_DEBUG("commands:{}, need_ack:{}", line, need_ack);
         std::string origline = line = elegoo::common::strip(line);
         std::size_t cpos = line.find(';');

         if (cpos != std::string::npos)
         {
             line = line.substr(0, cpos);
         }

         if (line.substr(0, 4) == "COMP") 
         {
             line = line.substr(4); 
             line = elegoo::common::strip(line);
         }

         std::string upper_line = line;
         std::transform(upper_line.begin(), upper_line.end(), upper_line.begin(), ::toupper);

        //  std::string args_r2("([A-Z_]+|[A-Z*/])");
        // double monotime = get_monotonic();
         std::vector<std::string> parts = elegoo::common::regex_split(upper_line);
        // printf("__func__:%s,__LINE__:%d upper_line:%s monotime:%f get_monotonic - monotime:%f\n",__func__,__LINE__,upper_line.c_str(),monotime,get_monotonic() - monotime);

        // for(auto ii = 0; ii < parts.size(); ii++)
        // {
        //     printf("parts.size:%d ii:%d parts[ii]:%s\n",parts.size(),ii,parts[ii].c_str());
        // }

         size_t numparts = parts.size();
         std::string cmd = "";

         // 判断条件并设置 cmd
         if (numparts >= 3 && parts[1] != "N")
         {
             cmd = parts[1] + elegoo::common::strip(parts[2]);
         }
         else if (numparts >= 5 && parts[1] == "N")
         {
             cmd = parts[3] + elegoo::common::strip(parts[4]);
         }
         else if(numparts == 2){
             cmd = parts[1];
         }

         std::map<std::string, std::string> params;
         for (size_t i = 1; i < numparts; i += 2)
         {
             if (i + 1 < numparts)
             {
                 params[parts[i]] = elegoo::common::strip(parts[i+1]);
             }
         }
         SPDLOG_DEBUG("upper_line:{},cmd:{},params.size:{},origline:{} numparts:{}", upper_line, cmd, params.size(), origline, numparts);
         // std::shared_ptr<GCodeCommand> gcmd = create_gcode_command(cmd, origline, params);
         std::shared_ptr<GCodeCommand> gcmd = std::make_shared<GCodeCommand>(shared_from_this(), cmd, origline, params, need_ack);
         std::function<void(std::shared_ptr<GCodeCommand>)> handler;
         auto it = gcode_handlers->find(cmd);
         if (it != gcode_handlers->end())
         {
             handler = it->second;
         }
         else
         {
             handler = [this](std::shared_ptr<GCodeCommand> gcmd)
             { cmd_default(gcmd); };
         }

         try
         {
             SPDLOG_DEBUG("gcode_handlers->size:{},gcmd->commandline:{},  gcmd->command:{}", gcode_handlers->size(), gcmd->commandline, gcmd->command);
             if(!is_ignore)
             {
                handler(gcmd);
             }
             else
             {
                if(cmd=="SET_CONDITION_END")
                {
                    handler(gcmd);
                }
             }
         }
         catch (const elegoo::common::CommandError &e)
         {
             respond_error(e.what());
             elegoo::common::SignalManager::get_instance().emit_signal("gcode:command_error");
             if (!need_ack)
                 throw;
         }
         catch (const elegoo::common::MMUError &e)
         {
            throw;
         }
         catch (...)
         {
             std::string msg = "Internal error on command: \"" + cmd + "\"";
             printer->invoke_shutdown(msg);
             respond_error(msg);
             if (!need_ack)
                 throw;
         }
         gcmd->ack();
     }
 }

 void GCodeDispatch::respond_error(const std::string& msg)
 {
     std::cout << msg << std::endl;
     std::vector<std::string> lines;
     std::stringstream ss(trim(msg));
     std::string line;
     while (std::getline(ss, line))
     {
         lines.push_back(line);
     }

     if (lines.size() > 1)
     {
         std::string joined_lines = "";
         for (const auto& l : lines)
         {
             joined_lines += l + "\n";
         }
         respond_info(joined_lines, false);
     }

     respond_raw("!! " + trim(lines[0]));
     if (is_fileinput)
     {
         printer->request_exit("error_exit");
     }
 }

 void GCodeDispatch::respond_state(const std::string& state)
 {
     std::ostringstream msg;
     msg << "Elegoo state: " << state;
     respond_info(msg.str(), false);
 }

 std::vector<std::string> split_args(const std::string& args) {
     std::istringstream iss(args);
     std::vector<std::string> tokens;
     std::string token;

     while (iss >> std::ws) { // 跳过空白字符
         if (iss.peek() == '"') {
             iss.get(); // 吃掉引号
             std::ostringstream oss;
             while (iss.good()) {
                 char ch = iss.get();
                 if (ch == '"') break;
                 if (ch == '\\') {
                     if (iss.peek() == '"' || iss.peek() == '\\') {
                         ch = iss.get();
                     }
                 }
                 oss << ch;
             }
             tokens.push_back(oss.str());
         } else {
             iss >> token;
             tokens.push_back(token);
         }
     }

     return tokens;
 }

 std::regex extended_r(
    R"(^\s*(?:N\d+\s*)?)" 
    R"(([a-zA-Z_]\w*))" 
    R"((?:\s+|$))"                         
    R"(([^"#*;]*(?:"[^"]*"[^"#*;]*)*))"      
    R"(\s*(?:[#*;].*)?$)",                  
    std::regex::optimize
);

 std::shared_ptr<GCodeCommand> GCodeDispatch::get_extended_params(std::shared_ptr<GCodeCommand> gcmd)
 {
     SPDLOG_DEBUG("__func__:{}",__func__);
     std::string str = gcmd->get_commandline();
    //  std::regex extended_r(R"(^\s*(?:N[0-9]+\s*)?([a-zA-Z_][a-zA-Z0-9_]+)(?:\s+|$)([^#*;]*?)\s*(?:[#*;].*)?$)");
     std::smatch match;
    double monotime = get_monotonic();
     bool result = std::regex_match(str, match, extended_r);
    // printf("__func__:%s,__LINE__:%d str:%s monotime:%f get_monotonic - monotime:%f\n",__func__,__LINE__,str.c_str(),monotime,get_monotonic() - monotime);
     if(!result)
     {
         SPDLOG_ERROR("Malformed command {}", gcmd->get_commandline());
         throw elegoo::common::CommandError("Malformed command " + gcmd->get_commandline());
     }
     // for(auto ii = 0; ii < match.size(); ii++)
     // {
     //     SPDLOG_DEBUG("match[ii:{}]:{}",ii,match[ii].str());
     // }
     std::string cmd = match.str(1);
     std::string eargs = match.str(2);
     SPDLOG_DEBUG("cmd:{},eargs:{}",cmd,eargs);
     try{
         std::vector<std::string> eargs_tmp = elegoo::common::shlex_split(eargs);
         std::map<std::string, std::string> params_tmp;
         for(auto earg : eargs_tmp)
         {
             std::vector<std::string> eparams = elegoo::common::split(earg,"=", 1);
             if(eparams.size() == 2)
             {
                 std::transform(eparams[0].begin(), eparams[0].end(), eparams[0].begin(), ::toupper);
                 params_tmp[eparams[0]] = eparams[1];
             }
         }
         gcmd->params = params_tmp;
     }catch (const std::exception& e) {
         SPDLOG_ERROR("Malformed command {}", gcmd->get_commandline());
         throw elegoo::common::CommandError("Malformed command " + gcmd->get_commandline());
     }

     return gcmd;
 }

 void GCodeDispatch::cmd_mux(const std::string& command, std::shared_ptr<GCodeCommand> gcmd)
 {
     // SPDLOG_DEBUG("__func__:{},command:{}",__func__,command);
     auto it = mux_commands.find(command);
     if (it == mux_commands.end()) {
         throw std::runtime_error("Command not found: " + command);
     }

     std::string key = it->second.first;
     std::map<std::string, std::function<void(std::shared_ptr<GCodeCommand>)>> values = it->second.second;

     // SPDLOG_DEBUG("__func__:{},key:{},gcmd->params.size:{}",__func__,key,gcmd->params.size());
     // for(auto pp : gcmd->params)
     // {
     //     printf("gcmd->params[%s] is %s\n",pp.first.c_str(),pp.second.c_str());
     // }
     std::string key_param;
     if (values.find("") != values.end())
     {
         key_param = gcmd->get(key, "");
     }
     else
     {
         key_param = gcmd->get(key);
     }

     // SPDLOG_DEBUG("__func__:{},key_param:{}",__func__,key_param);
     auto value_it = values.find(key_param);
     if (value_it == values.end())
     {
         SPDLOG_ERROR("{},  The value '" + key_param + "' is not valid for " + key, command);
         throw elegoo::common::CommandError("The value '" + key_param + "' is not valid for " + key);
     }

     value_it->second(gcmd);
 }

 std::string GCodeDispatch::trim(const std::string& str,
     bool remove_front, bool remove_back)
 {
     size_t start = 0;
     size_t end = str.length();

     if (remove_front)
     {
         while (start < end && std::isspace(str[start]))
         {
             ++start;
         }
     }

     if (remove_back)
     {
         while (end > start && std::isspace(str[end - 1]))
         {
             --end;
         }
     }

     // 返回处理后的子字符串
     return str.substr(start, end - start);
 }

 GCodeIO::GCodeIO(std::shared_ptr<Printer> printer) :
     printer(printer),
     is_printer_ready(false),
     is_processing_data(false),
     bytes_read(0),
     partial_input(""),
     pending_commands({}),
     fd_handle(nullptr),
     pipe_is_active(true)
 {
     SPDLOG_DEBUG("__func__:{},is_processing_data:{}",__func__,is_processing_data);
     std::unordered_map<std::string, std::string> map = printer->get_start_args();
     if(map.find("gcode_fd") != map.end())
     {
         SPDLOG_DEBUG("map.find('gcode_fd')->second:{}",map.find("gcode_fd")->second);
         fd = std::stoi(map.find("gcode_fd")->second);
     }
     else
     {
         fd = -1;
     }
     is_fileinput = map.find("debuginput") != map.end() && !map["debuginput"].empty();

     gcode = any_cast<std::shared_ptr<GCodeDispatch>>(printer->lookup_object("gcode"));
     gcode_mutex = gcode->get_mutex();
     reactor = printer->get_reactor();

     elegoo::common::SignalManager::get_instance().register_signal(
         "elegoo:ready",
         std::function<void()>([this](){
             SPDLOG_DEBUG("elegoo:ready !");
             handle_ready();
             SPDLOG_DEBUG("elegoo:ready !");
         })
     );
     elegoo::common::SignalManager::get_instance().register_signal(
         "elegoo:shutdown",
         std::function<void()>([this](){
             SPDLOG_DEBUG("elegoo:shutdown !");
             handle_shutdown();
             SPDLOG_DEBUG("elegoo:shutdown !");
         })
     );

     if(!is_fileinput)
     {
         gcode->register_output_handler(
             std::function<void(const std::string& msg, int error_code, elegoo::common::ErrorLevel error_level)>([this](const std::string& msg, const int error_code, const elegoo::common::ErrorLevel error_level){
                 respond_raw(msg, error_code, error_level);
             })
         );
         fd_handle = reactor->register_fd(fd,
             std::function<void(double eventtime)>([this](double eventtime){
                 process_data(eventtime);
             })
         );
         SPDLOG_DEBUG("fd_handle->fd:{}",fd_handle->fd);
     }

     std::cout << "create GCodeIO success!" << std::endl;
 }

 GCodeIO::~GCodeIO()
 {

 }

 std::pair<bool, std::string> GCodeIO::stats(double eventtime)
 {
     std::ostringstream oss;
     oss << "gcodein=" << eventtime;

     return std::make_pair(false, oss.str());
 }

 void GCodeIO::handle_ready()
 {
     is_printer_ready = true;
     SPDLOG_DEBUG("__func__:{},is_fileinput:{},!fd_handle:{}",__func__,is_fileinput,!fd_handle);
     if(is_fileinput && !fd_handle)
     {
         fd_handle = reactor->register_fd(fd, [this](double eventtime) {
             process_data(eventtime);
         });
         SPDLOG_DEBUG("fd_handle->fd:{}",fd_handle->fd);
     }
     SPDLOG_DEBUG("{} : {}___ ok", __FUNCTION__, __LINE__);
 }

 void GCodeIO::dump_debug()
 {
     std::vector<std::string> out;
     std::ostringstream ss;
     ss << "Dumping gcode input " << input_log.size() << " blocks";
     out.push_back(ss.str());

     for (const std::pair<double, std::string>& log : input_log)
     {
         std::ostringstream line;
         line << "Read " << log.first << ": " << log.second;
         out.push_back(line.str());
     }

     std::for_each(out.begin(), out.end(), [](const std::string& line) {
         std::cout << line << std::endl;
     });
 }

 void GCodeIO::handle_shutdown()
 {
     if(!is_printer_ready)
     {
         return;
     }
     is_printer_ready = false;
     dump_debug();

     if(is_fileinput)
     {
         printer->request_exit("error_exit");
     }
 SPDLOG_DEBUG("{} : {}___ ok", __FUNCTION__, __LINE__);
 }

std::regex m112_r(R"(^([nN][0-9]+)?\s*[mM]112(\s|$))", std::regex::optimize);
 void GCodeIO::process_data(double eventtime)
 {
     std::string data;
     char buffer[4096];
     try {
         ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
         if (bytes_read == -1){
             std::cerr << "Error reading from file descriptor" << std::endl;
             return;
         }
         data = std::string(buffer, bytes_read);
     } catch (const std::exception& e) {
         std::cerr << "Read g-code error: " << e.what() << std::endl;
         return;
     }

     input_log.emplace_back(eventtime, data);
     bytes_read += data.length();

     std::vector<std::string> lines = elegoo::common::split(data,"\n");
     lines[0] = partial_input + lines[0];
     partial_input = lines.back();
     lines.pop_back();
     pending_commands.insert(pending_commands.end(), lines.begin(), lines.end());
     pipe_is_active = true;

     if (data.empty() && is_fileinput)
     {
         if (!is_processing_data)
         {
             reactor->unregister_fd(fd_handle);
             fd_handle = nullptr;
             gcode->request_restart("exit");
         }
         pending_commands.push_back("");
     }

    //  std::regex m112_r(R"(^([nN][0-9]+)?\s*[mM]112(\s|$))");
     if (is_processing_data || pending_commands.size() > 1)
     {
         if (pending_commands.size() < 20)
         {
             for (const std::string& line : lines)
             {
                double monotime = get_monotonic();
                 if (std::regex_match(line, m112_r))
                 {
                    // printf("__func__:%s,__LINE__:%d line:%s monotime:%f get_monotonic - monotime:%f\n",__func__,__LINE__,line.c_str(),monotime,get_monotonic() - monotime);
                     gcode->cmd_M112({});
                 }
                // printf("__func__:%s,__LINE__:%d line:%s monotime:%f get_monotonic - monotime:%f\n",__func__,__LINE__,line.c_str(),monotime,get_monotonic() - monotime);
             }
         }

         if (is_processing_data)
         {

             if (pending_commands.size() >= 20)
             {
                 reactor->unregister_fd(fd_handle);
                 fd_handle = nullptr;
             }
             return;
         }
     }

     is_processing_data = true;

     std::vector<std::string> pd_cmds = pending_commands;

     SPDLOG_DEBUG("__func__:{}, pd_cmds.size:{}",__func__,pd_cmds.size());
     while (!pd_cmds.empty())
     {
         pending_commands.clear();
         gcode_mutex->lock();
         gcode->process_commands(pd_cmds);
         gcode_mutex->unlock();

         pd_cmds = pending_commands;
     }
     SPDLOG_DEBUG("__func__:{}, pd_cmds.empty !",__func__);

     is_processing_data = false;
     if(!fd_handle && !reactor)
     {
         SPDLOG_DEBUG("pd_cmds.size:{},is_processing_data:{}",pd_cmds.size(),is_processing_data);
         fd_handle = reactor->register_fd(fd, [this](double eventtime) {
             process_data(eventtime);
         });
         SPDLOG_DEBUG("fd_handle->fd:{}",fd_handle->fd);
     }
 }

 void GCodeIO::respond_raw(const std::string& msg, const int error_code, const elegoo::common::ErrorLevel error_level)
 {
     if(pipe_is_active)
     {
         try {
             SPDLOG_DEBUG("msg:{}",msg);
             // 拼接消息和换行符，并写入管道
             std::string full_msg = msg + "\n";
             ssize_t bytes_written = write(fd, full_msg.c_str(), full_msg.size());

             SPDLOG_DEBUG("fd:{},full_msg:{},full_msg.size:{},bytes_written:{}",fd,full_msg,full_msg.size(),bytes_written);
             if (bytes_written == -1)
             {
                 throw std::runtime_error(std::strerror(errno));
             }
         } catch (const std::exception& e) {
             // 捕获写入异常，记录日志
             std::cerr << "Write g-code response error: " << e.what() << std::endl;
             pipe_is_active = false;
         }
     }
 }

 std::vector<std::string> GCodeIO::split(const std::string& data)
 {
     std::vector<std::string> result;
     std::istringstream stream(data);
     std::string line;

     // Split by newline
     while (std::getline(stream, line, '\n'))
     {
         result.push_back(line);
     }

     return result;
 }


 namespace GCODE {

 extern "C" {
 // Function to add early printer objects
 void gcode_add_early_printer_objects(std::shared_ptr<Printer> printer)
 {
     SPDLOG_DEBUG("gcode_add_early_printer_objects");
     printer->add_object("gcode", [](std::shared_ptr<Printer> printer)
                         { return std::make_shared<GCodeDispatch>(printer); }(printer));
     printer->add_object("gcode_io", [](std::shared_ptr<Printer> printer)
                         { return std::make_shared<GCodeIO>(printer); }(printer));
 }
 }
 }
