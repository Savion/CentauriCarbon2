/*****************************************************************************
 * @Author       : Ben
 * @Date         : 2024-11-28 20:15:19
 * @LastEditors  : loping
 * @LastEditTime : 2025-09-29 16:56:51
 * @Description  : G-Code G1 movement commands (and associated coordinate
 * manipulation)
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#include "gcode_move.h"
#include "printer.h"
#include "gcode.h"
#include "toolhead.h"
#include "kinematics_factory.h"
#include "gcode_macro.h"

namespace elegoo
{
    namespace extras
    {
        G180::G180(std::shared_ptr<ConfigWrapper> config)
        {
            printer = config->get_printer();
            gcode = any_cast<std::shared_ptr<GCodeDispatch>>(printer->lookup_object("gcode"));
            std::shared_ptr<PrinterGCodeMacro> gcode_macro;
            gcode_macro = any_cast<std::shared_ptr<PrinterGCodeMacro>>(printer->load_object(config, "gcode_macro"));
            gcode->register_command("G180",
                                    [this](std::shared_ptr<GCodeCommand> gcmd)
                                    {
                                        cmd_G180(gcmd);
                                    });

            // 加载配置里的宏，至多支持999个宏
            std::string select;
            tws.clear();
            for (int i = 1; i < 999; i++)
            {
                select = "S" + std::to_string(i);
                tws.push_back(gcode_macro->load_template(config, select, "\n"));
            }
        }

        G180::~G180()
        {
        }

        void G180::cmd_G180(std::shared_ptr<GCodeCommand> gcmd)
        {
            int select = gcmd->get_int("S", 0);
            SPDLOG_INFO("G180 select {}", select);
            if (select > 0 && select < tws.size())
                tws[select - 1]->run_gcode_from_command();
        }

        GCodeMove::GCodeMove(std::shared_ptr<ConfigWrapper> config)
            : base_position({0, 0, 0, 0}), last_position({0, 0, 0, 0}), homing_position({0, 0, 0, 0})
        {
            SPDLOG_INFO("GCodeMove init!");
            printer = config->get_printer();

            // 创建G180
            g180 = std::make_shared<G180>(config->getsection("g180"));

            elegoo::common::SignalManager::get_instance().register_signal(
                "elegoo:ready",
                std::function<void()>([this]()
                                      {
             SPDLOG_DEBUG("elegoo:ready !");
             handle_ready();
             SPDLOG_DEBUG("elegoo:ready !"); }));
            elegoo::common::SignalManager::get_instance().register_signal(
                "elegoo:shutdown",
                std::function<void()>([this]()
                                      {
             SPDLOG_DEBUG("elegoo:shutdown !");
             handle_shutdown();
             SPDLOG_DEBUG("elegoo:shutdown !"); }));
            elegoo::common::SignalManager::get_instance().register_signal(
                "toolhead:set_position",
                std::function<void()>([this]()
                                      { reset_last_position(); }));
            elegoo::common::SignalManager::get_instance().register_signal(
                "toolhead:manual_move",
                std::function<void()>([this]()
                                      { reset_last_position(); }));
            elegoo::common::SignalManager::get_instance().register_signal(
                "gcode:command_error",
                std::function<void()>([this]()
                                      { reset_last_position(); }));
            elegoo::common::SignalManager::get_instance().register_signal(
                "extruder:activate_extruder",
                std::function<void()>([this]()
                                      { handle_activate_extruder(); }));
            elegoo::common::SignalManager::get_instance().register_signal(
                "homing:home_rails_end",
                std::function<void(std::shared_ptr<Homing>, std::vector<std::shared_ptr<PrinterRail>>)>(
                    [this](std::shared_ptr<Homing> homing_state,
                           std::vector<std::shared_ptr<PrinterRail>> rails)
                    {
                        handle_home_rails_end(homing_state, rails);
                    }));

            is_printer_ready = false;
            initialize_handlers();
            // 不重要
            // Coord = gcode->coord;
            absolute_coord = true;
            absolute_extrude = true;
            speed = 25.0,
            speed_factor = 1.0 / 60.0,
            extrude_factor = 1.0;
            z_offset = 0.;
            bed_roughness_offset = 0.;
            position_with_transform = []()
            { return std::vector<double>{0, 0, 0, 0}; };

            SPDLOG_INFO("GCodeMove init success!!");
        }

        GCodeMove::~GCodeMove()
        {
        }

        void GCodeMove::initialize_handlers()
        {
            std::shared_ptr<GCodeDispatch> gcode =
                any_cast<std::shared_ptr<GCodeDispatch>>(printer->lookup_object("gcode"));
            std::vector<std::string> handlers = {"G1", "G20", "G21", "M82", "M83", "G90",
                                                 "G91", "G92", "M220", "M221", "SET_GCODE_OFFSET", "SET_BED_ROUGHNESS_OFFSET", "SAVE_GCODE_STATE",
                                                 "RESTORE_GCODE_STATE", "MANUAL_MOVE"};

            for (const auto &cmd : handlers)
            {
                std::string func_name = "cmd_" + cmd;
                if (cmd == "G1")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_G1(gcmd);
                                            });
                }
                else if (cmd == "G20")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_G20(gcmd);
                                            });
                }
                else if (cmd == "G21")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_G21(gcmd);
                                            });
                }
                else if (cmd == "M82")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_M82(gcmd);
                                            });
                }
                else if (cmd == "M83")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_M83(gcmd);
                                            });
                }
                else if (cmd == "G90")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_G90(gcmd);
                                            });
                }
                else if (cmd == "G91")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_G91(gcmd);
                                            });
                }
                else if (cmd == "G92")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_G92(gcmd);
                                            });
                }
                else if (cmd == "M220")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_M220(gcmd);
                                            });
                }
                else if (cmd == "M221")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_M221(gcmd);
                                            });
                }
                else if (cmd == "SET_GCODE_OFFSET")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_SET_GCODE_OFFSET(gcmd);
                                            });
                }
                else if (cmd == "SET_BED_ROUGHNESS_OFFSET")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_SET_BED_ROUGHNESS_OFFSET(gcmd);
                                            });
                }
                else if (cmd == "SAVE_GCODE_STATE")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_SAVE_GCODE_STATE(gcmd);
                                            });
                }
                else if (cmd == "RESTORE_GCODE_STATE")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_RESTORE_GCODE_STATE(gcmd);
                                            });
                }
                else if (cmd == "MANUAL_MOVE")
                {
                    gcode->register_command(cmd,
                                            [this](std::shared_ptr<GCodeCommand> gcmd)
                                            {
                                                cmd_MANUAL_MOVE(gcmd);
                                            });
                }
            }

            gcode->register_command("G0",
                                    [this](std::shared_ptr<GCodeCommand> gcmd)
                                    {
                                        cmd_G1(gcmd);
                                    });

            gcode->register_command("M114", [this](std::shared_ptr<GCodeCommand> gcmd)
                                    { cmd_M114(gcmd); }, true);

            gcode->register_command("GET_POSITION", [this](std::shared_ptr<GCodeCommand> gcmd)
                                    { cmd_GET_POSITION(gcmd); }, true, "Return information on the current location of the toolhead");
        }

        std::shared_ptr<GCodeMoveTransform> GCodeMove::set_move_transform(std::shared_ptr<GCodeMoveTransform> transform, bool force)
        {
            SPDLOG_INFO("set_move_transform transform: force:{}", force);
            if (move_transform != nullptr && !force)
            {
                throw elegoo::common::ConfigParserError("G-Code move transform already specified");
            }

            std::shared_ptr<GCodeMoveTransform> old_transform = move_transform;
            if (old_transform == nullptr)
            {
                try
                {
                    std::shared_ptr<ToolHead> toolhead = any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));
                    old_transform = toolhead->move_transform;
                }
                catch (const elegoo::common::ConfigParserError &e)
                {
                    old_transform = nullptr;
                }
            }

            move_transform = transform;
            move_with_transform = transform->move_with_transform;
            position_with_transform = transform->position_with_transform;
            return old_transform;
        }

        json GCodeMove::_padding_position(const std::vector<double> &pos)
        {
            json list = json::array();
            char axis[] = "XYZE";
            for (int i = 0; i < pos.size(); i++)
            {
                json p;
                p[std::string(1, axis[i])] = pos[i];
                list.push_back(p);
            }
            return list;
        }

        json GCodeMove::get_status(double eventtime)
        {
            std::vector<double> move_position = get_gcode_position();
            // TODO CHECK
            json status;
            status["speed_factor"] = get_gcode_speed_override();
            status["speed"] = get_gcode_speed();
            status["extrude_factor"] = extrude_factor;
            status["absolute_coordinates"] = absolute_coord;
            status["absolute_extrude"] = absolute_extrude;

            // Convert homing_position and last_position to Coord format
            // TODO..
            // status["homing_origin"] = _padding_position(homing_position);
            // status["position"] = _padding_position(last_position);
            // status["gcode_position"] = _padding_position(move_position);
            status["homing_origin"] = homing_position;
            status["position"] = last_position;
            status["gcode_position"] = move_position;
            return status;
        }

        void GCodeMove::reset_last_position()
        {
            // SPDLOG_INFO("reset_last_position start");
            if (is_printer_ready)
            {
                last_position = position_with_transform();
                // printf("last_position %f %f %f %f\n", last_position[0], last_position[1], last_position[2], last_position[3]);
            }
            // SPDLOG_INFO("reset_last_position end");
        }

        void GCodeMove::cmd_G1(std::shared_ptr<GCodeCommand> gcmd, bool is_round)
        {
            SPDLOG_DEBUG("__func__:{},gcmd->commandline:{}", __func__, gcmd->commandline);

            if (is_round)
            {
                std::map<std::string, double> params = gcmd->get_command_parameters_double();
                try
                {
                    const std::string axes = "XYZ";
                    for (int pos = 0; pos < 3; ++pos)
                    {
                        std::string axis(1, axes[pos]);
                        if (params.find(axis) != params.end())
                        {
                            double v = params.at(axis);
                            if (!absolute_coord)
                            {
                                last_position[pos] += v;
                            }
                            else
                            {
                                last_position[pos] = v + base_position[pos];
                            }
                        }
                    }

                    if (params.find("E") != params.end())
                    {
                        double v = params["E"] * extrude_factor;
                        if (!absolute_coord || !absolute_extrude)
                        {
                            last_position[3] += v;
                        }
                        else
                        {
                            last_position[3] = v + base_position[3];
                        }
                    }

                    if (params.find("F") != params.end())
                    {
                        double gcode_speed = params["F"];
                        if (gcode_speed <= 0.0f)
                        {
                            throw elegoo::common::CommandError("Invalid speed in '" + gcmd->get_commandline() + "'");
                        }
                        speed = gcode_speed * speed_factor;
                    }
                }
                catch (const elegoo::common::ValueError &e)
                {
                    throw elegoo::common::ConfigParserError("Unable to parse move '" + gcmd->get_commandline() + "'");
                }
            }
            else
            {
                std::map<std::string, std::string> params = gcmd->get_command_parameters();
                try
                {
                    const std::string axes = "XYZ";
                    for (int pos = 0; pos < 3; ++pos)
                    {
                        std::string axis(1, axes[pos]);
                        if (params.find(axis) != params.end())
                        {
                            double v = std::stod(params.at(axis));
                            if (!absolute_coord)
                            {
                                last_position[pos] += v;
                            }
                            else
                            {
                                last_position[pos] = v + base_position[pos];
                            }
                        }
                    }

                    if (params.find("E") != params.end())
                    {
                        double v = std::stod(params["E"]) * extrude_factor;
                        if (!absolute_coord || !absolute_extrude)
                        {
                            last_position[3] += v;
                        }
                        else
                        {
                            last_position[3] = v + base_position[3];
                        }
                    }

                    if (params.find("F") != params.end())
                    {
                        double gcode_speed = std::stod(params["F"]);
                        if (gcode_speed <= 0.0f)
                        {
                            throw elegoo::common::CommandError("Invalid speed in '" + gcmd->get_commandline() + "'");
                        }
                        speed = gcode_speed * speed_factor;
                    }
                }
                catch (const elegoo::common::ValueError &e)
                {
                    throw elegoo::common::ConfigParserError("Unable to parse move '" + gcmd->get_commandline() + "'");
                }
            }
            move_with_transform(last_position, speed);
        }

        void GCodeMove::cmd_MANUAL_MOVE(std::shared_ptr<GCodeCommand> gcmd)
        {
            std::shared_ptr<ToolHead> toolhead = any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));
            std::shared_ptr<Kinematics> kin = toolhead->get_kinematic();
            json kin_status = kin->get_status(get_monotonic());

            std::shared_ptr<GCodeDispatch> gcode = any_cast<std::shared_ptr<GCodeDispatch>>(printer->lookup_object("gcode"));
            std::map<std::string, std::string> params = gcmd->get_command_parameters();
            std::vector<std::vector<double>> rails_range = kin->get_rails_range();
            const std::string axes = "XYZE";
            // 1. 判断移动的轴是否已经归零
            bool need[3] = {gcmd->get("X", "None") != "None",
                            gcmd->get("Y", "None") != "None",
                            gcmd->get("Z", "None") != "None"};
            bool homed[3] = {!(kin_status["homed_axes"].get<std::string>().find('x') == std::string::npos),
                             !(kin_status["homed_axes"].get<std::string>().find('y') == std::string::npos),
                             !(kin_status["homed_axes"].get<std::string>().find('z') == std::string::npos)};
            // 2. 限制行程范围
            std::vector<double> __last_position = get_gcode_position();
            double E_last_position = 0.;
            for (int pos = 0; pos < 4; ++pos)
            {
                std::string axis(1, axes[pos]);
                if (params.find(axis) != params.end())
                {
                    double v = std::stod(params.at(axis));
                    if (axis != "E")
                    {
                        if (need[pos] && !homed[pos])
                        {
                            SPDLOG_ERROR("Must home axis first before manual move (pos: " + std::to_string(pos) + ")");
                            return;
                        }
                        __last_position[pos] += v;
                        if (__last_position[pos] > rails_range[pos][1])
                            __last_position[pos] = rails_range[pos][1];
                        else if (__last_position[pos] < rails_range[pos][0])
                            __last_position[pos] = rails_range[pos][0];
                    }
                    else
                    {
                        E_last_position = v;
                        if (E_last_position >= 0)
                        {
                            gcode->run_script_from_command("M2202 GCODE_ACTION_REPORT=1061"); // E进料中
                        }
                        else
                        {
                            gcode->run_script_from_command("M2202 GCODE_ACTION_REPORT=1062"); // E退料中
                        }
                    }
                }
            }
            // 3. 获取速度
            double gcode_speed = params.find("V") != params.end() ? std::stod(params["V"]) : get_gcode_speed();

            // 4. 执行G1
            bool ac = absolute_coord;
            bool ae = absolute_extrude;
            const std::string axis = "E";
            absolute_coord = true;
            if (params.find(axis) != params.end())
            {
                absolute_extrude = false;
            }

            try
            {
                gcode->run_script_from_command("G1 X" + std::to_string(__last_position[0]) + " Y" + std::to_string(__last_position[1]) + " Z" + std::to_string(__last_position[2]) + " E" + std::to_string(E_last_position) + " F" + std::to_string(gcode_speed));
            }
            catch (...)
            {
                absolute_coord = ac;
                if (params.find(axis) != params.end())
                {
                    absolute_extrude = ae;
                    if (E_last_position >= 0)
                    {
                        gcode->run_script_from_command("M2202 GCODE_ACTION_REPORT=1063"); // E进料完成
                    }
                    else
                    {
                        gcode->run_script_from_command("M2202 GCODE_ACTION_REPORT=1064"); // E退料完成
                    }
                }
                throw;
            }
            absolute_coord = ac;
            if (params.find(axis) != params.end())
            {
                absolute_extrude = ae;
                if (E_last_position >= 0)
                {
                    gcode->run_script_from_command("M2202 GCODE_ACTION_REPORT=1063"); // E进料完成
                }
                else
                {
                    gcode->run_script_from_command("M2202 GCODE_ACTION_REPORT=1064"); // E退料完成
                }
            }
        }

        void GCodeMove::cmd_G20(std::shared_ptr<GCodeCommand> gcmd)
        {
            throw elegoo::common::CommandError("Machine does not support G20 (inches) command");
        }

        void GCodeMove::cmd_G21(std::shared_ptr<GCodeCommand> gcmd)
        {
        }

        void GCodeMove::cmd_M82(std::shared_ptr<GCodeCommand> gcmd)
        {
            absolute_extrude = true;
        }

        void GCodeMove::cmd_M83(std::shared_ptr<GCodeCommand> gcmd)
        {
            absolute_extrude = false;
        }

        void GCodeMove::cmd_G90(std::shared_ptr<GCodeCommand> gcmd)
        {
            // SPDLOG_INFO("cmd_G90");
            absolute_coord = true;
        }

        void GCodeMove::cmd_G91(std::shared_ptr<GCodeCommand> gcmd)
        {
            // SPDLOG_INFO("cmd_G91");
            absolute_coord = false;
        }

        void GCodeMove::cmd_G92(std::shared_ptr<GCodeCommand> gcmd)
        {
            SPDLOG_DEBUG("__func__:{},gcmd->commandline:{}", __func__, gcmd->commandline);
            std::vector<double> offsets =
                {
                    gcmd->get_double("X", DOUBLE_NONE),
                    gcmd->get_double("Y", DOUBLE_NONE),
                    gcmd->get_double("Z", DOUBLE_NONE),
                    gcmd->get_double("E", DOUBLE_NONE)};

            bool all_offsets_none = true;

            for (size_t i = 0; i < offsets.size(); ++i)
            {
                if (!std::isnan(offsets[i]))
                {
                    all_offsets_none = false;
                    double offset = offsets[i];
                    if (i == 3)
                    {
                        offset *= extrude_factor;
                    }
                    base_position[i] = last_position[i] - offset;
                }
                SPDLOG_DEBUG("__func__:{},all_offsets_none:{},i:{},offsets[i]:{},extrude_factor:{},base_position[i]:{},last_position[i]:{},gcmd->commandline:{}", __func__, all_offsets_none, i, offsets[i], extrude_factor, base_position[i], last_position[i], gcmd->commandline);
            }

            if (all_offsets_none)
            {
                base_position = last_position;
            }
        }

        void GCodeMove::cmd_M114(std::shared_ptr<GCodeCommand> gcmd)
        {
            std::vector<double> position = get_gcode_position();
            char buffer[100];
            std::snprintf(buffer, sizeof(buffer), "X:%.3f Y:%.3f Z:%.3f E:%.3f",
                          position[0], position[1], position[2], position[3]);
            SPDLOG_INFO("{} {}", __func__, std::string(buffer));
            gcmd->respond_raw(buffer);
        }

        void GCodeMove::cmd_M220(std::shared_ptr<GCodeCommand> gcmd)
        {
            double value = gcmd->get_double("S", 100., DOUBLE_NONE, DOUBLE_NONE, 0.) / (60.0f * 100.0f);
            speed = get_gcode_speed() * value;
            speed_factor = value;
            SPDLOG_INFO("{} speed {} speed_factor {} {}",__func__,speed,speed_factor,gcmd->get_double("S", 100., DOUBLE_NONE, DOUBLE_NONE, 0.));
        }

        void GCodeMove::cmd_M221(std::shared_ptr<GCodeCommand> gcmd)
        {
            double new_extrude_factor = gcmd->get_double("S", 100., DOUBLE_NONE, DOUBLE_NONE, 0.) / 100.0f;
            double last_e_pos = last_position[3];
            double e_value = (last_e_pos - base_position[3]) / extrude_factor;
            base_position[3] = last_e_pos - e_value * new_extrude_factor;
            extrude_factor = new_extrude_factor;
        }

        void GCodeMove::cmd_SET_GCODE_OFFSET(std::shared_ptr<GCodeCommand> gcmd)
        {
            std::vector<double> move_delta = {0.0f, 0.0f, 0.0f, 0.0f};
            for (size_t pos = 0; pos < 4; ++pos)
            {
                char axis = "XYZE"[pos];
                double offset = gcmd->get_double(std::string(1, axis), DOUBLE_NONE);
                // 没有设置Z
                if (std::isnan(offset))
                {
                    // 增量偏移
                    offset = gcmd->get_double(std::string(1, axis) + "_ADJUST", DOUBLE_NONE);
                    // 设置了增量偏移
                    if (!std::isnan(offset))
                        offset += homing_position[pos];
                    // 当前是Z轴且没有设置增量偏移
                    else if (std::string(1, axis) == "Z")
                    {
                        this->bed_roughness_offset = gcmd->get_double("BED_ROUGHNESS", 0.);
                        offset = this->z_offset + this->bed_roughness_offset;
                        SPDLOG_INFO("{} pos:{} base_position[pos]:{} homing_position[pos]:{} bed_roughness_offset:{} z_offset:{} offset:{}", __func__, pos, base_position[pos], homing_position[pos], bed_roughness_offset, z_offset, offset);
                    }
                    else
                        continue;
                }
                // 设置Z
                else if (std::string(1, axis) == "Z")
                {
                    // 保存Z偏移
                    this->z_offset = offset;
                    offset += this->bed_roughness_offset;
                    SPDLOG_INFO("{} pos:{} base_position[pos]:{} homing_position[pos]:{} bed_roughness_offset:{} z_offset:{} offset:{}", __func__, pos, base_position[pos], homing_position[pos], bed_roughness_offset, z_offset, offset);
                }

                double delta = offset - homing_position[pos];
                move_delta[pos] = delta;
                base_position[pos] += delta;
                homing_position[pos] = offset;
            }

            if (gcmd->get_int("MOVE", 0))
            {
                double move_speed = gcmd->get_double("MOVE_SPEED", speed, DOUBLE_NONE, DOUBLE_NONE, 0.);
                for (size_t pos = 0; pos < move_delta.size(); ++pos)
                {
                    last_position[pos] += move_delta[pos];
                }
                move_with_transform(last_position, move_speed);
            }
            SPDLOG_INFO("{} : {}___ ok", __FUNCTION__, __LINE__);
        }

        void GCodeMove::cmd_SET_BED_ROUGHNESS_OFFSET(std::shared_ptr<GCodeCommand> gcmd)
        {
            this->bed_roughness_offset = gcmd->get_double("BED_ROUGHNESS", 0.);
            SPDLOG_INFO("{} bed_roughness_offset:{}", __func__, bed_roughness_offset);
        }

        void GCodeMove::cmd_SAVE_GCODE_STATE(std::shared_ptr<GCodeCommand> gcmd)
        {
            std::string state_name = gcmd->get("NAME", "default");
            this->saved_states[state_name]["absolute_coord"] = this->absolute_coord;
            this->saved_states[state_name]["absolute_extrude"] = this->absolute_extrude;
            for (auto ii = 0; ii < this->base_position.size(); ++ii)
            {
                this->saved_states[state_name]["base_position"][ii] = this->base_position[ii];
            }
            for (auto ii = 0; ii < this->last_position.size(); ++ii)
            {
                this->saved_states[state_name]["last_position"][ii] = this->last_position[ii];
            }
            for (auto ii = 0; ii < this->homing_position.size(); ++ii)
            {
                this->saved_states[state_name]["homing_position"][ii] = this->homing_position[ii];
            }
            this->saved_states[state_name]["speed"] = this->speed;
            this->saved_states[state_name]["speed_factor"] = this->speed_factor;
            this->saved_states[state_name]["extrude_factor"] = this->extrude_factor;
            this->saved_states[state_name]["gcode_position"] = this->get_gcode_position();
            SPDLOG_INFO("{} : {}___ ok", __FUNCTION__, __LINE__);
        }

        void GCodeMove::cmd_RESTORE_GCODE_STATE(std::shared_ptr<GCodeCommand> gcmd)
        {
            std::string state_name = gcmd->get("NAME", "default");

            if (!saved_states.contains(state_name))
            {
                SPDLOG_INFO("Unknown g-code state: " + state_name);
                return;
                // gcmd.error("Unknown g-code state: " + state_name);
            }

            json state = saved_states[state_name];

            absolute_coord = state["absolute_coord"].get<bool>();
            absolute_extrude = state["absolute_extrude"].get<bool>();

            for (int i = 0; i < 4; ++i)
            {
                base_position[i] = state["base_position"][i].get<double>();
                homing_position[i] = state["homing_position"][i].get<double>();
            }

            speed = state["speed"].get<double>();
            speed_factor = state["speed_factor"].get<double>();
            extrude_factor = state["extrude_factor"].get<double>();

            double e_diff = last_position[3] - state["last_position"][3].get<double>();
            base_position[3] += e_diff;

            if (gcmd->get_int("MOVE", 0))
            {
                float above = 0;
                double move_speed = gcmd->get_double(
                    "MOVE_SPEED", speed, DOUBLE_NONE, DOUBLE_NONE, 0);
                for (int i = 0; i < 3; ++i)
                {
                    last_position[i] = state["last_position"][i].get<double>();
                }
                move_with_transform(last_position, move_speed);
            }
            SPDLOG_INFO("{} : {}___ ok", __FUNCTION__, __LINE__);
        }

        void GCodeMove::cmd_GET_POSITION(std::shared_ptr<GCodeCommand> gcmd)
        {
            std::shared_ptr<ToolHead> toolhead = any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));
            if (!toolhead)
            {
                // throw gcmd.error("Printer not ready");
            }

            std::shared_ptr<Kinematics> kin = toolhead->get_kinematic();
            std::vector<std::shared_ptr<MCU_stepper>> steppers = kin->get_steppers();
            std::ostringstream mcu_pos, stepper_pos, kin_pos, toolhead_pos, gcode_pos, base_pos, homing_pos;

            for (std::shared_ptr<MCU_stepper> s : steppers)
            {
                mcu_pos << s->get_name() << ":" << s->get_mcu_position() << " ";
            }

            std::map<std::string, double> cinfo;
            for (std::shared_ptr<MCU_stepper> s : steppers)
            {
                double pos = s->get_commanded_position();
                cinfo[s->get_name()] = pos;
                stepper_pos << s->get_name() << ":" << pos << " ";
            }

            // auto kin_position = kin->calc_position(cinfo);
            // for (size_t i = 0; i < kin_position.size(); ++i) {
            //     kin_pos << "XYZ"[i] << ":" << kin_position[i] << " ";
            // }

            auto toolhead_position = toolhead->get_position();
            for (size_t i = 0; i < toolhead_position.size(); ++i)
            {
                toolhead_pos << "XYZE"[i] << ":" << toolhead_position[i] << " ";
            }

            for (size_t i = 0; i < last_position.size(); ++i)
            {
                gcode_pos << "XYZE"[i] << ":" << last_position[i] << " ";
            }

            for (size_t i = 0; i < base_position.size(); ++i)
            {
                base_pos << "XYZE"[i] << ":" << base_position[i] << " ";
            }

            for (size_t i = 0; i < homing_position.size() - 1; ++i)
            {
                homing_pos << "XYZ"[i] << ":" << homing_position[i] << " ";
            }

            SPDLOG_INFO("{} : {}___ ok", __FUNCTION__, __LINE__);
            // Respond with the full information
            gcmd->respond_info(
                "mcu: " + mcu_pos.str() + "\n" +
                    "stepper: " + stepper_pos.str() + "\n" +
                    "kinematic: " + kin_pos.str() + "\n" +
                    "toolhead: " + toolhead_pos.str() + "\n" +
                    "gcode: " + gcode_pos.str() + "\n" +
                    "gcode base: " + base_pos.str() + "\n" +
                    "gcode homing: " + homing_pos.str(),
                true);
        }

        void GCodeMove::handle_ready()
        {
            is_printer_ready = true;
            std::shared_ptr<ToolHead> toolhead = any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));
            if (move_transform == nullptr)
            {
                move_with_transform = std::bind(&ToolHead::move, toolhead, std::placeholders::_1, std::placeholders::_2);
                position_with_transform = std::bind(&ToolHead::get_position, toolhead);
            }
            reset_last_position();
        }

        json GCodeMove::get_gcode_state() { return this->saved_states; }
        void GCodeMove::clean_gcode_state()
        {
            this->saved_states.clear();
        }
        void GCodeMove::set_e_last_pos(double e_pos) { last_position[3] = e_pos; }

        void GCodeMove::handle_shutdown()
        {
            if (!is_printer_ready)
            {
                return;
            }
            is_printer_ready = false;

            char buffer[1024];
            std::snprintf(buffer, sizeof(buffer),
                          "gcode state: absolute_coord=%d absolute_extrude=%d"
                          " base_position=(%f %f %f %f) last_position=(%f %f %f %f) homing_position=(%f %f %f %f)"
                          " speed_factor=%f extrude_factor=%f speed=%f\n",
                          absolute_coord, absolute_extrude,
                          base_position[0], base_position[1], base_position[2], base_position[3],
                          last_position[0], last_position[1], last_position[2], last_position[3],
                          homing_position[0], homing_position[1], homing_position[2], homing_position[3],
                          speed_factor, extrude_factor, speed);
            SPDLOG_WARN(buffer);
        }

        void GCodeMove::handle_activate_extruder()
        {
            reset_last_position();
            extrude_factor = 1.0;
            base_position[3] = last_position[3];
            SPDLOG_DEBUG("{} : {}___ ok", __FUNCTION__, __LINE__);
        }

        void GCodeMove::handle_home_rails_end(std::shared_ptr<Homing> homing_state,
                                              std::vector<std::shared_ptr<PrinterRail>> rails)
        {
            reset_last_position();
            for (int axis : homing_state->get_axes())
            {
                base_position[axis] = homing_position[axis];
            }
        }

        std::vector<double> GCodeMove::get_gcode_position()
        {
            std::vector<double> position = {0, 0, 0, 0};
            for (size_t i = 0; i < 4; ++i)
            {
                position[i] = last_position[i] - base_position[i];
            }
            position[3] /= extrude_factor;
            return position;
        }

        double GCodeMove::get_gcode_speed()
        {
            return speed / speed_factor;
        }

        void GCodeMove::clear_gcode_speed()
        {
            speed = 0;
        }

        double GCodeMove::get_gcode_speed_override()
        {
            return speed_factor * 60.0f;
        }

        std::vector<double> GCodeMove::json_to_vector(const json &jsonArray)
        {
            std::vector<double> result;
            for (const auto &item : jsonArray)
            {
                result.push_back(item.get<double>());
            }
            return result;
        }

        std::shared_ptr<GCodeMove> gcode_move_load_config(
            std::shared_ptr<ConfigWrapper> config)
        {
            return std::make_shared<GCodeMove>(config);
        }

    }
}