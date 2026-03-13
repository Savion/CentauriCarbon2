/*****************************************************************************
 * @Author       : Louisa
 * @Date         : 2024-11-25 09:53:39
 * @LastEditors  : loping
 * @LastEditTime : 2025-08-22 12:20:05
 * @Description  : main program entry.
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#include <iostream>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <memory>
#include "printer.h"
#include "common/logger.h"


std::map<std::string, std::string> dictionary;

void import_test()
{
    std::vector<std::string> modules = {"extras", "kinematics"};
    std::string dname = ".";

    for (const auto &mname : modules)
    {
        std::string module_path = dname + "/" + mname;
        DIR *dir = opendir(module_path.c_str());
        if (dir)
        {
            struct dirent *entry;
            while ((entry = readdir(dir)) != nullptr)
            {
                std::string fname = entry->d_name;
                if (fname == "." || fname == "..")
                    continue;
                std::string module_name;
                if (fname.size() > 3 && fname.substr(fname.size() - 3) == ".py" && fname != "__init__.py")
                {
                    module_name = fname.substr(0, fname.size() - 3);
                    std::cout << "Loading module: " << mname + "." + module_name << std::endl;
                }
                else
                {
                    std::string iname = module_path + "/" + fname + "/__init__.py";
                    struct stat buffer;
                    if (stat(iname.c_str(), &buffer) == 0)
                    {
                        module_name = fname;
                        std::cout << "Loading module: " << mname + "." + module_name << std::endl;
                    }
                }
            }
            closedir(dir);
        }
    }
    exit(0);
}

void handle_dictionary(const std::string &value)
{
    std::string key = "dictionary";
    std::string fname = value;

    size_t pos = value.find('=');
    if (pos != std::string::npos)
    {
        std::string mcu_name = value.substr(0, pos);
        fname = value.substr(pos + 1);
        key = "dictionary_" + mcu_name;
    }

    dictionary[key] = fname;
}

void usage(const char *prog_name)
{
    std::cerr << "Usage: " << prog_name << " [options] <config file>" << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  -i, --debuginput <file>      Read commands from file instead of from tty port" << std::endl;
    std::cerr << "  -I, --input-tty <tty>        Input tty name (default is /tmp/printer)" << std::endl;
    std::cerr << "  -a, --api-server <filename>  API server unix domain socket filename" << std::endl;
    std::cerr << "  -l, --logfile <file>         Write log to file instead of stderr" << std::endl;
    std::cerr << "  -v, --verbose                Enable debug messages" << std::endl;
    std::cerr << "  -o, --debugoutput <file>     Write output to file instead of to serial port" << std::endl;
    std::cerr << "  -d, --dictionary <file>      File to read for MCU protocol dictionary" << std::endl;
    std::cerr << "  --import-test                Perform an import module test" << std::endl;
}

void chelper_log(const char *msg)
{
    SPDLOG_ERROR(std::string(msg));
}

int main(int argc, char *argv[])
{
#if 0
    elegoo::common::ElegooLog::get_instance().init_log(
        "elegoo", "/opt/usr/logs/", SPDLOG_LEVEL_INFO, true);
#else
    elegoo::common::ElegooLog::get_instance().init_log(
        "elegoo", "/opt/usr/logs/", SPDLOG_LEVEL_DEBUG, true);
#endif
    // 设置C端日志
    set_python_logging_callback(chelper_log);

    static struct option long_options[] = {
        {"debuginput", required_argument, 0, 'i'},
        {"input-tty", required_argument, 0, 'I'},
        {"api-server", required_argument, 0, 'a'},
        {"logfile", required_argument, 0, 'l'},
        {"verbose", no_argument, 0, 'v'},
        {"debugoutput", required_argument, 0, 'o'},
        {"dictionary", required_argument, 0, 'd'},
        {"import-test", no_argument, 0, 't'},
        {0, 0, 0, 0} // 结束标志
    };

    std::string debuginput;
    std::string inputtty = "./config/printer";
    std::string apiserver = "/tmp/elegoo_uds";
    std::string logfile;
    std::string autosave = "./config/autosave.cfg";
    std::string version_path = "/opt/inst/firmware_version/versions.json";
    std::string debugoutput;
    bool verbose = false;
    int debuglevel = 0;
    bool import_test_flag = false;
    int opt;

    while ((opt = getopt_long(argc, argv, "i:I:a:l:vo:d:ts:", long_options, nullptr)) != -1)
    {
        switch (opt)
        {
        case 'i':
            debuginput = optarg;
            break;
        case 'I':
            inputtty = optarg;
            break;
        case 'a':
            apiserver = optarg;
            break;
        case 'l':
            logfile = optarg;
            break;
        case 'v':
            verbose = true;
            debuglevel = 1;
            break;
        case 'o':
            debugoutput = optarg;
            break;
        case 'd':
            handle_dictionary(optarg);
            break;
        case 't':
            import_test_flag = true;
            break;
        case 's':
            autosave = optarg;
            break;
        default:
            SPDLOG_ERROR("Unknown option");
            return 1;
        }
    }

    if (optind >= argc)
    {
        SPDLOG_ERROR("Error: Incorrect number of arguments");
        return 1;
    }

    std::unordered_map<std::string, std::string> start_args;
    start_args["config_file"] = argv[optind];
    start_args["autosave_file"] = autosave;
    start_args["apiserver"] = apiserver;
    start_args["start_reason"] = "startup";
    int gcode_fd = -1;
    
    if (!debuginput.empty())
    {
        start_args["debuginput"] = debuginput;
        gcode_fd = open(debuginput.c_str(), O_RDONLY);
        if (gcode_fd == -1)
        {
            std::cerr << "Error: Failed to open file: " << debuginput
                      << " - " << std::strerror(errno) << std::endl;
            return 1;
        }
        start_args["gcode_fd"] = std::to_string(gcode_fd);
        std::cout << "File descriptor for " << debuginput << ": " << gcode_fd << std::endl;
    }
    else
    {
        gcode_fd = elegoo::common::create_pty(inputtty);
        start_args["gcode_fd"] = std::to_string(gcode_fd);
        SPDLOG_INFO("inputtty:{},gcode_fd:{}", inputtty, gcode_fd);
    }

    if (!debugoutput.empty())
    {
        start_args["debugoutput"] = debugoutput;
        start_args["dictionary"] = dictionary["dictionary"];
        SPDLOG_INFO("debugoutput:{},dictionary:{}", debugoutput, dictionary["dictionary"]);
    }

    if (!logfile.empty())
    {
        start_args["log_file"] = logfile;
    }
    start_args["cpu_info"] = elegoo::common::get_cpu_info();

    elegoo::common::get_firmware_info(version_path, start_args);
    SPDLOG_INFO("Starting Elegoo...");

    SPDLOG_INFO("start_args.size:{},sizeof(long_options):{}", start_args.size(), sizeof(long_options) / sizeof(struct option));
    for (auto it : start_args)
    {
        SPDLOG_INFO("start_args[{}]:{}", it.first, it.second);
    }
    for (auto it : long_options)
    {
        if (it.name)
            SPDLOG_INFO("long_options[{}]:{}", it.name, (char)it.val);
    }

    while (1)
    {
        std::shared_ptr<SelectReactor> main_reactor = std::make_shared<PollReactor>();
        std::shared_ptr<Printer> printer = std::make_shared<Printer>(main_reactor, start_args);
        printer->run();

        std::this_thread::sleep_for(std::chrono::seconds(1));
        main_reactor->finalize();
        elegoo::common::SignalManager::get_instance().reset();
    }

    return 0;
}