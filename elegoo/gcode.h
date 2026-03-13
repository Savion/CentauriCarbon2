/*****************************************************************************
 * @Author       : Louisa
 * @Date         : 2024-11-25 09:53:37
 * @LastEditors  : huangpeiyun
 * @LastEditTime : 2025-07-17 13:00:04
 * @Description  : Parse gcode commands
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#pragma once
#include <memory>
#include <string>
#include <mutex>
#include "reactor.h"
#include "utilities.h"
#include "exception_handler.h"

class GCodeDispatch;
class Printer;
class WebHooks;

class GCodeCommand
{
public:
    GCodeCommand(std::shared_ptr<GCodeDispatch> gcode,
        const std::string& command,
        const std::string& commandline,
        const std::map<std::string, std::string>& params,
        bool need_ack);

    GCodeCommand(std::shared_ptr<GCodeDispatch> gcode,
        const std::string& command,
        const std::string& commandline,
        const std::map<std::string, double>& params,
        bool need_ack);
    ~GCodeCommand();

    std::string get_command();
    std::string get_commandline();
    std::map<std::string, std::string> get_command_parameters();
    std::map<std::string, double> get_command_parameters_double();
    std::string get_raw_command_parameters();
    bool ack(const std::string& msg = "");
    std::string get(const std::string& name,
        const std::string& default_value="_invalid_");
    int get_int(const std::string& name,
        int default_value=INT_NONE,
        int minval=INT_NONE,
        int maxval=INT_NONE);
    double get_double(const std::string& name,
        double default_value=DOUBLE_INVALID,
        double minval=DOUBLE_NONE,
        double maxval=DOUBLE_NONE,
        double above=DOUBLE_NONE,
        double below=DOUBLE_NONE);
    std::function<void(const std::string&, bool)> respond_info;
    std::function<void(const std::string&)> respond_raw;
    std::function<void(const json&)> respond_feedback;
private:
    bool need_ack;
public:
    std::string command;
    std::string commandline;
    std::map<std::string, std::string> params;
    std::map<std::string, double> params_double;
};

class GCodeDispatch : public std::enable_shared_from_this<GCodeDispatch>
{
public:
    GCodeDispatch(std::shared_ptr<Printer> printer);
    ~GCodeDispatch();

    bool is_traditional_gcode(const std::string& cmd);
    std::function<void(std::shared_ptr<GCodeCommand>)>
        register_command(const std::string& cmd,
        std::function<void(std::shared_ptr<GCodeCommand>)> func,
        bool when_not_ready=false,
        const std::string& desc="");
    void register_mux_command(const std::string& cmd,
        const std::string& key,
        const std::string& value,
        std::function<void(std::shared_ptr<GCodeCommand>)> func,
        const std::string& desc="");
    std::map<std::string, std::string> get_command_help();
    json get_status(double eventtime);
    void register_output_handler(std::function<void(const std::string&, int, elegoo::common::ErrorLevel)> cb);
    void register_report_handler(std::function<void(int, elegoo::common::ErrorLevel, const std::string&)> cb);
    void run_script_from_command(const std::string& script);
    void run_script(const std::string& script);
    std::shared_ptr<ReactorMutex> get_mutex();
    std::shared_ptr<GCodeCommand> create_gcode_command(const std::string& command,
        const std::string& commandline,
        const std::map<std::string, std::string>& params,
        bool need_ack = false);

    std::shared_ptr<GCodeCommand> create_gcode_command_double(const std::string& command,
        const std::string& commandline,
        const std::map<std::string, double>& params,
        bool need_ack = false);
    void respond_raw(const std::string& msg);
    void respond_ecode(const std::string& msg, const int error_code = elegoo::common::ErrorCode::CODE_OK, 
                       const elegoo::common::ErrorLevel error_level = elegoo::common::ErrorLevel::INFO);
    void respond_info(const std::string& msg, bool log = true);
    void respond_feedback(const json& msg);
    void respond_report(int error_code, elegoo::common::ErrorLevel error_level, std::string& msg);
    void initialize_handlers();
    void cmd_default(std::shared_ptr<GCodeCommand> gcmd);
    void cmd_M110(std::shared_ptr<GCodeCommand> gcmd);
    void cmd_M112(std::shared_ptr<GCodeCommand> gcmd);
    void cmd_M115(std::shared_ptr<GCodeCommand> gcmd);
    void cmd_M2202(std::shared_ptr<GCodeCommand> gcmd);
    void cmd_SET_CONDITION(std::shared_ptr<GCodeCommand> gcmd);
    void cmd_SET_CONDITION_END(std::shared_ptr<GCodeCommand> gcmd);
    void request_restart(const std::string& result);
    void cmd_RESTART(std::shared_ptr<GCodeCommand> gcmd);
    void cmd_FIRMWARE_RESTART(std::shared_ptr<GCodeCommand> gcmd);
    void cmd_ECHO(std::shared_ptr<GCodeCommand> gcmd);
    void cmd_STATUS(std::shared_ptr<GCodeCommand> gcmd);
    void cmd_HELP(std::shared_ptr<GCodeCommand> gcmd);
    void cmd_DEBUG_STATUS(std::shared_ptr<GCodeCommand> gcmd);
    void cmd_TEMPERATURE_WAIT(std::shared_ptr<GCodeCommand> gcmd);
    void process_commands(std::vector<std::string> commands, bool need_ack=true);
    std::vector<double> coord;
private:
    void build_status_commands();
    void handle_shutdown();
    void handle_disconnect();
    void handle_ready();
    void handle_initialized();
    void respond_error(const std::string& msg);
    void respond_state(const std::string& state);
    std::shared_ptr<GCodeCommand> get_extended_params(std::shared_ptr<GCodeCommand> gcmd);
    void cmd_mux(const std::string& command, std::shared_ptr<GCodeCommand> gcmd);
    std::string trim(const std::string& str,
        bool remove_front=true, bool remove_back=true);
private:
    std::shared_ptr<Printer> printer;
    std::shared_ptr<WebHooks> webhooks;
    bool is_printer_ready;
    std::map<std::string, std::function<void(std::shared_ptr<GCodeCommand>)>>
        ready_gcode_handlers;
    std::map<std::string, std::function<void(std::shared_ptr<GCodeCommand>)>>
        base_gcode_handlers;
    const std::map<std::string, std::function<void(std::shared_ptr<GCodeCommand>)>>
        *gcode_handlers;
    std::map<std::string, std::pair<std::string, std::map<std::string,
        std::function<void(std::shared_ptr<GCodeCommand>)>>>> mux_commands;
    std::map<std::string, std::map<std::string, std::string>> status_commands;
    std::map<std::string, std::string> gcode_help;
    std::vector<std::function<void(const std::string&,int, elegoo::common::ErrorLevel)>> output_callbacks;
    // std::vector<std::function<void(int, elegoo::common::ErrorLevel, const std::string&)>> report_callbacks;
    std::shared_ptr<ReactorMutex> mutex;
    bool is_fileinput;
    bool is_ignore;
    int channel_b;
};

class GCodeIO
{
public:
    GCodeIO(std::shared_ptr<Printer> printer);
    ~GCodeIO();

    std::pair<bool, std::string> stats(double eventtime);

private:
    void handle_ready();
    void dump_debug();
    void handle_shutdown();
    void process_data(double eventtime);
    void respond_raw(const std::string& msg,
                     const int error_code = elegoo::common::ErrorCode::CODE_OK,
                     const elegoo::common::ErrorLevel error_level = elegoo::common::ErrorLevel::INFO);
    std::vector<std::string> split(const std::string& data);
private:
    std::shared_ptr<Printer> printer;
    std::shared_ptr<GCodeDispatch> gcode;
    std::shared_ptr<SelectReactor> reactor;
    bool is_printer_ready;
    bool is_processing_data;
    bool pipe_is_active;
    bool is_fileinput;
    std::shared_ptr<ReactorFileHandler> fd_handle;
    int fd;
    std::deque<std::pair<double, std::string>> input_log;
    int bytes_read;
    std::string partial_input;
    std::vector<std::string> pending_commands;
    std::shared_ptr<ReactorMutex> gcode_mutex;
};

namespace GCODE {

extern "C" {
void gcode_add_early_printer_objects(std::shared_ptr<Printer> printer);
}

}