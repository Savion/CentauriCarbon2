/*****************************************************************************
 * @Author       : Ben
 * @Date         : 2024-11-28 20:15:20
 * @LastEditors  : coconut
 * @LastEditTime : 2025-03-01 17:57:09
 * @Description  : Abstract class for kinematics objects.
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "printer.h"
#include "c_helper.h"
#include "homing.h"
#include "logger.h"

using namespace elegoo::extras;
class Kinematics
{
public:
    virtual ~Kinematics() = default;

    virtual std::vector<std::shared_ptr<MCU_stepper>> get_steppers() = 0;

    virtual std::vector<double> calc_position(
        const std::map<std::string, double> &stepper_positions) = 0;
    virtual void set_position(std::vector<double> newpos,
                              const std::vector<int> &homing_axes) = 0;
    virtual void home(std::shared_ptr<Homing> homing_state) = 0;
    virtual void check_move(Move *move) = 0;
    virtual json get_status(double eventtime) = 0;
    virtual std::vector<std::shared_ptr<PrinterRail>> get_rails() { return {}; };
    virtual void clear_homing_state(const std::vector<int> &clear_axes) {};
    virtual std::vector<std::vector<double>> get_rails_range() { return {}; };
    virtual double get_max_z_velocity() { return 0.0; };
    virtual double get_max_z_accel() { return 0.0; };
};