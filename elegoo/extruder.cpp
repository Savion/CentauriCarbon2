/***************************************************************************** 
 * @Author       : Ben
 * @Date         : 2024-11-28 20:15:20
 * @LastEditors  : Ben
 * @LastEditTime : 2025-06-07 18:00:09
 * @Description  : Code for handling printer nozzle extruders
 * 
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/

#include "extruder.h"
#include "printer.h"

ExtruderStepper::ExtruderStepper(std::shared_ptr<ConfigWrapper> config)
{
    printer = config->get_printer();
    SPDLOG_INFO("ExtruderStepper init!");
    std::istringstream iss(config->get_name());
    std::string word;
    std::string name;
    while (iss >> word) 
    {
        name = word;
    }

    this->pressure_advance = 0.;
    this->pressure_advance_smooth_time = 0.;

    this->config_pa = config->getdouble("pressure_advance", 0., 0.);
    this->config_smooth_time = config->getdouble("pressure_advance_smooth_time", 
        0.040, DOUBLE_NONE, 0.200, 0.);
    
    stepper = PrinterStepper(config);
    sk_extruder = std::shared_ptr<stepper_kinematics>(
        extruder_stepper_alloc(),
        extruder_stepper_free
    );
    stepper->set_stepper_kinematics(sk_extruder);
    elegoo::common::SignalManager::get_instance().register_signal(
        "elegoo:connect",
        std::function<void()>([this](){
            SPDLOG_DEBUG("ExtruderStepper connect~~~~~~~~~~~~~~~~~");
            handle_connect();
            SPDLOG_DEBUG("ExtruderStepper connect~~~~~~~~~~~~~~~~~ success!");
        })
    );
    
    std::shared_ptr<GCodeDispatch> gcode = 
        any_cast<std::shared_ptr<GCodeDispatch>>(printer->lookup_object("gcode"));      

    if(name == "extruder")
    {
        gcode->register_mux_command("SET_PRESSURE_ADVANCE", "EXTRUDER", "",
            [this](std::shared_ptr<GCodeCommand> gcmd){
                cmd_default_SET_PRESSURE_ADVANCE(gcmd);
            },
            "Set pressure advance parameters"
            );      
    }

    gcode->register_mux_command("SET_PRESSURE_ADVANCE", "EXTRUDER", name,
        [this](std::shared_ptr<GCodeCommand> gcmd){
            cmd_SET_PRESSURE_ADVANCE(gcmd);
        },
        "Set pressure advance parameters"
        );  

    gcode->register_mux_command("SET_EXTRUDER_ROTATION_DISTANCE", "EXTRUDER", name,
        [this](std::shared_ptr<GCodeCommand> gcmd){
            cmd_SET_E_ROTATION_DISTANCE(gcmd);
        },
        "Set extruder rotation distance"
        );  

    gcode->register_mux_command("SYNC_EXTRUDER_MOTION", "EXTRUDER", name,
        [this](std::shared_ptr<GCodeCommand> gcmd){
            cmd_SYNC_EXTRUDER_MOTION(gcmd);
        },
        "Set extruder stepper motion queue"
        );  
    SPDLOG_INFO("ExtruderStepper init success!");
}

ExtruderStepper::~ExtruderStepper()
{

}


json ExtruderStepper::get_status(double eventtime)
{
    json status;
    status["pressure_advance"] = this->pressure_advance;
    status["smooth_time"] = this->pressure_advance_smooth_time;
    status["motion_queue"] = this->motion_queue;
    return status;
}

double ExtruderStepper::find_past_position(double print_time)
{
    int64_t mcu_pos = stepper->get_past_mcu_position(print_time);
    return stepper->mcu_to_commanded_position(mcu_pos);
}

void ExtruderStepper::sync_to_extruder(const std::string& extruder_name)
{
    std::shared_ptr<ToolHead> toolhead = 
        any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));
    toolhead->flush_step_generation();

    if (extruder_name.empty()) 
    {
        stepper->set_trapq(nullptr);
        motion_queue.clear();
        return;
    }

    std::shared_ptr<PrinterExtruder> extruder = 
        any_cast<std::shared_ptr<PrinterExtruder>>(printer->lookup_object(extruder_name));
    if (!extruder || typeid(*extruder) != typeid(PrinterExtruder)) 
    {
        throw elegoo::common::CommandError("'" + extruder_name + "' is not a valid extruder.");
    }

    stepper->set_position({extruder->last_position, 0.0, 0.0});
    stepper->set_trapq(extruder->get_trapq().get());
    motion_queue = extruder_name;
}

void ExtruderStepper::cmd_default_SET_PRESSURE_ADVANCE(
    std::shared_ptr<GCodeCommand> gcmd)
{
    PrinterExtruder* extruder = static_cast<PrinterExtruder*>(((any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead")))->get_extruder().get()));
    if (!extruder->extruder_stepper) 
    {
        SPDLOG_ERROR("Active extruder does not have a stepper!");
        throw elegoo::common::CommandError("Active extruder does not have a stepper");
    }

    auto strapq = extruder->extruder_stepper->stepper->get_trapq();
    if (strapq != extruder->get_trapq().get()) 
    {
        SPDLOG_ERROR("Unable to infer active extruder stepper!");
        throw elegoo::common::CommandError("Unable to infer active extruder stepper");
    }

    extruder->extruder_stepper->cmd_SET_PRESSURE_ADVANCE(gcmd);
}

void ExtruderStepper::cmd_SET_PRESSURE_ADVANCE(
    std::shared_ptr<GCodeCommand> gcmd)
{
    double pressure_advance = gcmd->get_double("ADVANCE", this->pressure_advance, 0.);
    double smooth_time = gcmd->get_double("SMOOTH_TIME", this->pressure_advance_smooth_time, 0., 0.200);

    set_pressure_advance(pressure_advance, smooth_time);

    std::ostringstream msg;
    msg << std::fixed << std::setprecision(6);
    msg << "pressure_advance: " << pressure_advance << "\n"
        << "pressure_advance_smooth_time: " << smooth_time;

    printer->set_rollover_info(name, name + ": " + msg.str());
    gcmd->respond_info(msg.str(), true);
}

void ExtruderStepper::cmd_SET_E_ROTATION_DISTANCE(
    std::shared_ptr<GCodeCommand> gcmd)
{
    double rotation_dist = gcmd->get_double("DISTANCE", DOUBLE_NONE);
    if (rotation_dist != 0) // isNan
    {
        if (rotation_dist == 0.0) 
        {
            SPDLOG_ERROR("Rotation distance cannot be zero!");
            // throw gcmd.error("Rotation distance cannot be zero");
        }

        std::pair<uint32_t, uint32_t> val = stepper->get_dir_inverted();
        bool invert_dir = val.first;
        bool orig_invert_dir = val.second;
        bool next_invert_dir = orig_invert_dir;

        if (rotation_dist < 0.0) 
        {
            next_invert_dir = !orig_invert_dir;
            rotation_dist = -rotation_dist;
        }

        std::shared_ptr<ToolHead> toolhead = 
            any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));
        toolhead->flush_step_generation();
        stepper->set_rotation_distance(rotation_dist);
        stepper->set_dir_inverted(next_invert_dir);
    } 
    else 
    {
        std::pair<double, double> val = stepper->get_rotation_distance();
        rotation_dist = val.first;
        double spr = val.second;
    }

    
    std::pair<uint32_t, uint32_t> val = stepper->get_dir_inverted();
    bool invert_dir = val.first;
    bool orig_invert_dir = val.second;
    if (invert_dir != orig_invert_dir) 
    {
        rotation_dist = -rotation_dist;
    }

    gcmd->respond_info("Extruder '" + this->name + 
        "' rotation distance set to " + std::to_string(rotation_dist), true);
}

void ExtruderStepper::cmd_SYNC_EXTRUDER_MOTION(
    std::shared_ptr<GCodeCommand> gcmd)
{
    std::string ename = gcmd->get("MOTION_QUEUE");
    sync_to_extruder(ename);
    gcmd->respond_info("Extruder '" + 
        this->name + "' now syncing with '" + ename + "'", true);
}

void ExtruderStepper::handle_connect()
{
    std::shared_ptr<ToolHead> toolhead = 
        any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));
    toolhead->register_step_generator([this](double flush_time){
        stepper->generate_steps(flush_time);});

    set_pressure_advance(this->config_pa, this->config_smooth_time);
}

void ExtruderStepper::set_pressure_advance(
    double pressure_advance, double smooth_time)
{
    SPDLOG_DEBUG("__func__:{},__LINE__:{},pressure_advance:{},smooth_time:{}",__func__,__LINE__,pressure_advance,smooth_time);
    double old_smooth_time = this->pressure_advance_smooth_time;
    if (this->pressure_advance == 0.0) 
    {
        old_smooth_time = 0.0;
    }

    double new_smooth_time = smooth_time;
    if (pressure_advance == 0.0) 
    {
        new_smooth_time = 0.0;
    }

    std::shared_ptr<ToolHead> toolhead = 
        any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));
    if (new_smooth_time != old_smooth_time) 
    {
        toolhead->note_step_generation_scan_time(new_smooth_time * 0.5, old_smooth_time * 0.5);
    }

    toolhead->register_lookahead_callback([=](double print_time) {
        extruder_set_pressure_advance(sk_extruder.get(), print_time, pressure_advance, new_smooth_time);
    });
    this->pressure_advance = pressure_advance;
    this->pressure_advance_smooth_time = smooth_time;
}


PrinterExtruder::PrinterExtruder(std::shared_ptr<ConfigWrapper> config,
    int extruder_num) : DummyExtruder(config->get_printer())
{
    SPDLOG_DEBUG("PrinterExtruder init!!");
    name = config->get_name();
    last_position = 0.;
    std::shared_ptr<elegoo::extras::PrinterHeaters> pheaters = 
        any_cast<std::shared_ptr<elegoo::extras::PrinterHeaters>>(printer->load_object(config, "heaters"));
    std::string gcode_id = "T" + std::to_string(extruder_num);

    SPDLOG_DEBUG("__func__:{},name:{},gcode_id:{}",__func__,name,gcode_id);
    heater = pheaters->setup_heater(config, gcode_id);

    nozzle_diameter = config->getdouble("nozzle_diameter",
        DOUBLE_INVALID, DOUBLE_NONE, DOUBLE_NONE, 0);

    double filament_diameter = config->getdouble(
        "filament_diameter", DOUBLE_INVALID, nozzle_diameter);

    filament_area = M_PI * std::pow(filament_diameter * 0.5, 2);
    double def_max_cross_section = 4.0 * std::pow(nozzle_diameter, 2);
    double def_max_extrude_ratio = def_max_cross_section / filament_area;

    double max_cross_section = config->getdouble("max_extrude_cross_section", 
        def_max_cross_section, DOUBLE_NONE, DOUBLE_NONE, 0);
    max_extrude_ratio = max_cross_section / filament_area;

    std::cout << "Extruder max_extrude_ratio=" << max_extrude_ratio << std::endl;
    std::shared_ptr<ToolHead> toolhead = 
        any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));

    std::pair<double, double> val = toolhead->get_max_velocity();
    double max_velocity = val.first;
    double max_accel = val.second;

    max_e_velocity = config->getdouble(
        "max_extrude_only_velocity", max_velocity * def_max_extrude_ratio, DOUBLE_NONE, DOUBLE_NONE, 0);
    max_e_accel = config->getdouble(
        "max_extrude_only_accel", max_accel * def_max_extrude_ratio, DOUBLE_NONE, DOUBLE_NONE, 0);
    max_e_dist = config->getdouble(
        "max_extrude_only_distance", 50, 0);
    instant_corner_v = config->getdouble(
        "instantaneous_corner_velocity", 1, 0);

    tra = std::shared_ptr<trapq>(
        trapq_alloc(),
        trapq_free
    );

    if (!config->get("step_pin", "").empty() ||
        !config->get("dir_pin", "").empty() ||
        !config->get("rotation_distance", "").empty()) 
    {
        extruder_stepper = std::make_shared<ExtruderStepper>(config);
        extruder_stepper->stepper->set_trapq(tra.get());
    }

    std::shared_ptr<GCodeDispatch> gcode = 
        any_cast<std::shared_ptr<GCodeDispatch>>(printer->lookup_object("gcode"));      

    SPDLOG_WARN("PrinterExtruder name={}", name);
    if(name == "extruder")
    {
        // toolhead->set_extruder(self, 0);
        gcode->register_command("M104", 
            [this](std::shared_ptr<GCodeCommand> gcmd){ 
                cmd_M104(gcmd); 
            }); 

        gcode->register_command("M109", 
            [this](std::shared_ptr<GCodeCommand> gcmd){ 
                cmd_M109(gcmd); 
            });   

    }

    gcode->register_mux_command("ACTIVATE_EXTRUDER", "EXTRUDER", name,
        [this](std::shared_ptr<GCodeCommand> gcmd){
            cmd_ACTIVATE_EXTRUDER(gcmd);
        },
        "Change the active extruder"
        );    
    SPDLOG_INFO("PrinterExtruder init success!");
}

PrinterExtruder::~PrinterExtruder()
{

}


void PrinterExtruder::update_move_time(double flush_time, double clear_history_time)
{
    trapq_finalize_moves(tra.get(), flush_time, clear_history_time);
}

json PrinterExtruder::get_status(double eventtime)
{
    json status = heater->get_status(eventtime);
    status["can_extrude"] = heater->can_extrude;

    if (extruder_stepper) 
    {
        json stepperStatus = extruder_stepper->get_status(eventtime);
        status.update(stepperStatus);
    }

    return status;
}

std::string PrinterExtruder::get_name()
{
    return name;
}

std::shared_ptr<elegoo::extras::Heater> PrinterExtruder::get_heater()
{
    return heater;
}

std::shared_ptr<trapq> PrinterExtruder::get_trapq()
{
    return tra;
}

std::pair<bool, std::string> PrinterExtruder::stats(double eventtime)
{
    return heater->stats(eventtime);
}

void PrinterExtruder::check_move(Move* move)
{
    double axis_r = move->axes_r[3];
    if (!heater->can_extrude)
    {
        throw elegoo::common::CommandError(
            "Extrude below minimum temp\n"
            "See the 'min_extrude_temp' config option for details");
    }

    if ((move->axes_d[0] == 0 && move->axes_d[1] == 0) || axis_r < 0.0)
    {
        if (std::abs(move->axes_d[3]) > this->max_e_dist)
        {
            char buf[256];
            snprintf(buf, sizeof(buf), "Extrude only move too long (%.3fmm vs %.3fmm)\n"
                                       "See the 'max_extrude_only_distance' config option for details",
                     move->axes_d[3], this->max_e_dist);

            throw elegoo::common::CommandError(buf);
        }
        double inv_extrude_r = 1.0 / std::abs(axis_r);
        move->limit_speed(max_e_velocity * inv_extrude_r,
                          max_e_accel * inv_extrude_r);
    }
    else if (axis_r > max_extrude_ratio)
    {
        if (move->axes_d[3] <= nozzle_diameter * max_extrude_ratio)
        {
            return;
        }
        double area = axis_r * filament_area;
        // logging::debug("Overextrude: %f vs %f (area=%.3f dist=%.3f)",
        //                axis_r, max_extrude_ratio, area, move->move_d);
        char buf[256];
        snprintf(buf, sizeof(buf), "Move exceeds maximum extrusion (%.3fmm^2 vs %.3fmm^2)\n"
                                   "See the 'max_extrude_cross_section' config option for details",
                 area, this->max_extrude_ratio * this->filament_area);
        throw elegoo::common::CommandError(buf);
    }
}

double PrinterExtruder::calc_junction(
    Move* prev_move, 
    Move* move)
{
    double diff_r = move->axes_r[3] - prev_move->axes_r[3];
    if (diff_r != 0) 
    {
        return (instant_corner_v / std::abs(diff_r)) * (instant_corner_v / std::abs(diff_r));
    }
    return move->max_cruise_v2;
}

void PrinterExtruder::move(double print_time, Move* move)
{
    double axis_r = move->axes_r[3];
    double accel = move->accel * axis_r;
    double start_v = move->start_v * axis_r;
    double cruise_v = move->cruise_v * axis_r;
    bool can_pressure_advance = false;

    if (axis_r > 0. && (move->axes_d[0] || move->axes_d[1])) 
    {
        can_pressure_advance = true;
    }
    // printf("move: %lf accel_t: %lf cruise_t: %lf decel_t: %lf"
    //         " start_pos: %lf, %d, %d, %lf"
    //         " axes_r: %lf, %d, %d"
    //         " start_v: %lf, cruise_v: %lf, accel: %lf\n",
    //         print_time, move->accel_t, move->cruise_t, move->decel_t,
    //         move->start_pos[3], 0, 0, move->axes_r[3],
    //         1.0, can_pressure_advance, 0,
    //         start_v, cruise_v, accel);
    trapq_append(
        tra.get(),
        print_time,
        move->accel_t, move->cruise_t, move->decel_t,
        move->start_pos[3], 0.0, 0.0,
        1.0, can_pressure_advance, 0.0,
        start_v, cruise_v, accel
    );

    last_position = move->end_pos[3];
}

double PrinterExtruder::find_past_position(double print_time)
{
    if(!extruder_stepper)
        return 0.;
    return extruder_stepper->find_past_position(print_time);
}

void PrinterExtruder::cmd_M104(std::shared_ptr<GCodeCommand> gcmd, bool wait)
{
    double temp = gcmd->get_double("S", 0);
    int index = gcmd->get_int("T", -1, 0); 
    std::shared_ptr<DummyExtruder> extruder;
    if (index != -1) 
    {
        std::string section = "extruder";
        if (index > 0) 
        {
            section = "extruder" + std::to_string(index);
        }
        extruder = any_cast<std::shared_ptr<PrinterExtruder>>(printer->lookup_object(section, nullptr));
        if (extruder == nullptr) 
        {
            if (temp <= 0.0) 
                return;
            throw elegoo::common::CommandError("Extruder not configured");
        }
    } 
    else 
    {
        extruder = any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"))->get_extruder();
    }

    std::shared_ptr<elegoo::extras::PrinterHeaters> pheaters = any_cast<std::shared_ptr<elegoo::extras::PrinterHeaters>>(printer->lookup_object("heaters"));
    pheaters->set_temperature(extruder->get_heater(), temp, wait);
}

void PrinterExtruder::cmd_M109(std::shared_ptr<GCodeCommand> gcmd)
{
    json res;
    res["command"] = "M2202";
    res["result"] = "1045";
    gcmd->respond_feedback(res);
    cmd_M104(gcmd, true);
}

void PrinterExtruder::cmd_ACTIVATE_EXTRUDER(std::shared_ptr<GCodeCommand> gcmd)
{
    std::shared_ptr<ToolHead> toolhead =  any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));
    if (toolhead->get_extruder() == shared_from_this()) 
    {
        gcmd->respond_info("Extruder " + this->name + " already active", true);
        return;
    }

    gcmd->respond_info("Activating extruder " + name, true);
    toolhead->flush_step_generation();
    toolhead->set_extruder(shared_from_this(), last_position);
    elegoo::common::SignalManager::get_instance().emit_signal("extruder:activate_extruder");
}

DummyExtruder::DummyExtruder(std::shared_ptr<Printer> printer)
    : printer(printer)
{
}

DummyExtruder::~DummyExtruder()
{
}

void DummyExtruder::update_move_time(double flush_time, double clear_history_time)
{
}

void DummyExtruder::check_move(Move* move)
{
    throw move->move_error("Extrude when no extruder present");
}

double DummyExtruder::find_past_position(double print_time)
{
    return 0.;
}

double DummyExtruder::calc_junction(Move* prev_move, Move* move)
{
    return move->max_cruise_v2;
}

std::string DummyExtruder::get_name()
{
    return "";
}

std::shared_ptr<elegoo::extras::Heater> DummyExtruder::get_heater()
{
     throw elegoo::common::CommandError("Extruder not configured");
}

std::shared_ptr<trapq> DummyExtruder::get_trapq()
{
     throw elegoo::common::CommandError("Extruder not configured");
}

void DummyExtruder::cmd_M104(std::shared_ptr<GCodeCommand> gcmd, bool wait)
{
}

void DummyExtruder::cmd_M109(std::shared_ptr<GCodeCommand> gcmd)
{
}

void DummyExtruder::cmd_ACTIVATE_EXTRUDER(std::shared_ptr<GCodeCommand> gcmd)
{
}

json DummyExtruder::get_status(double eventtime)
{
    return json::object();
}

std::pair<bool, std::string> DummyExtruder::stats(double eventtime)
{
    return {};
}

void DummyExtruder::move(double print_time, Move* move)
{
}
