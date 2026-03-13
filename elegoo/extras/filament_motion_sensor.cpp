/*****************************************************************************
 * @Author       : loping
 * @Date         : 2025-07-24 16:44:35
 * @LastEditors  : loping
 * @LastEditTime : 2025-08-20 17:05:20
 * @Description  : 
 * @Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#include "filament_motion_sensor.h"
#include "filament_switch_sensor.h"
#include "idle_timeout.h"
#include "mmu.h"
#include "print_stats.h"

namespace elegoo
{
    namespace extras
    {
        #undef SPDLOG_DEBUG
        #define SPDLOG_DEBUG SPDLOG_INFO

        constexpr double CHECK_RUNOUT_TIMEOUT = .50;

        EncoderSensor::EncoderSensor(std::shared_ptr<ConfigWrapper> config) : sensor_enable(false)
        {
            SPDLOG_INFO("EncoderSensor init start");
            this->printer = config->get_printer();
            this->reactor = printer->get_reactor();
            this->extruder_name = config->get("extruder","extruder");
            this->detection_length = config->getdouble("detection_length",7.,DOUBLE_NONE,DOUBLE_NONE,0.);
            std::string switch_pin = config->get("switch_pin");
            std::string motion_pin = config->get("motion_pin");
            std::shared_ptr<PrinterButtons> buttons = any_cast<std::shared_ptr<PrinterButtons>>(printer->load_object(config, "buttons"));
            buttons->register_buttons({switch_pin}, [this](double eventtime, bool state) {
                return this->switch_event(eventtime, state);
            });
            buttons->register_buttons({motion_pin}, [this](double eventtime, bool state) {
                return this->encoder_event(eventtime, state);
            });


            this->runout_helper = std::make_shared<RunoutHelper>(config);
            this->get_status = std::bind(&RunoutHelper::get_status, runout_helper, std::placeholders::_1);
            this->filament_runout_pos = DOUBLE_NONE;
            //
            elegoo::common::SignalManager::get_instance().register_signal(
                "elegoo:ready",
                std::function<void()>([this](){
                    _handle_ready();
                })
            );
            elegoo::common::SignalManager::get_instance().register_signal(
                "idle_timeout:printing",
                std::function<void(double)>([this](double print_time){
                    _handle_printing(print_time);
                })
            );
            elegoo::common::SignalManager::get_instance().register_signal(
                "idle_timeout:ready",
                std::function<void(double)>([this](double print_time){
                    _handle_not_printing(print_time);
                })
            );
            elegoo::common::SignalManager::get_instance().register_signal(
                "idle_timeout:idle",
                std::function<void(double)>([this](double print_time){
                    _handle_not_printing(print_time);
                })
            );
            
            SPDLOG_INFO("EncoderSensor init over");
        }
        
        EncoderSensor::~EncoderSensor()
        {
            SPDLOG_WARN("~EncoderSensor");
        }

        void EncoderSensor::_update_filament_runout_pos(double eventtime)
        {
            if(eventtime == DOUBLE_NONE)
            {
                eventtime = get_monotonic();
            }
            this->filament_runout_pos = _get_extruder_pos(eventtime) + this->detection_length;
            // SPDLOG_INFO("EncoderSensor {} filament_runout_pos:{}",__func__,filament_runout_pos);
        }
        
        void EncoderSensor::_handle_ready()
        {
            this->extruder = any_cast<std::shared_ptr<PrinterExtruder>>(printer->lookup_object(extruder_name));
            std::shared_ptr<MCU> mcu = any_cast<std::shared_ptr<MCU>>(printer->lookup_object("mcu"));
            this->estimated_print_time = std::bind(&MCU::estimated_print_time, mcu, std::placeholders::_1);
            _update_filament_runout_pos();
            this->_extruder_pos_update_timer = reactor->register_timer(
                [this](double eventtime){ 
                    return _extruder_pos_update_event(eventtime); 
                }
                , _NEVER, "extruder pos update timer"
            );
            this->gcode = any_cast<std::shared_ptr<GCodeDispatch>>(this->printer->lookup_object("gcode"));
            this->gcode->register_command(
                    "ENCODER_EVENT_SWITCH"
                    ,[this](std::shared_ptr<GCodeCommand> gcmd){
                        CMD_encoder_sensor_switch(gcmd);
                    }
                    ,false
                    ,"ENCODER EVENT SWITCH");

            canvas = any_cast<std::shared_ptr<Canvas>>(printer->lookup_object("canvas_dev", std::shared_ptr<Canvas>()));
            SPDLOG_INFO("EncoderSensor {} __OVER",__func__);
        }

        void EncoderSensor::CMD_encoder_sensor_switch(std::shared_ptr<GCodeCommand> gcmd)
        {
            enable = gcmd->get_int("SWITCH",1,0,1);
            SPDLOG_INFO("CMD_encoder_sensor_switch:{}", enable);
        }

        void EncoderSensor::_handle_printing(double print_time)
        {
            if (enable && (!canvas || !canvas->get_connect_state())) 
            {   
                SPDLOG_INFO("Enable Single-Filament tangling detection ");
                reactor->update_timer(_extruder_pos_update_timer,_NOW);
            }
        }
        
        void EncoderSensor::_handle_not_printing(double print_time)
        {
            if (enable && (!canvas || !canvas->get_connect_state())) 
            {   
                SPDLOG_INFO("End Single-Color tangle detection");
                reactor->update_timer(_extruder_pos_update_timer,_NEVER);
            }
        }
        
        double EncoderSensor::_get_extruder_pos(double eventtime)
        {
            if(eventtime == DOUBLE_NONE)
            {
                eventtime = get_monotonic();
            }
            double print_time = estimated_print_time(eventtime);
            return extruder->find_past_position(print_time);
        }

        double EncoderSensor::_extruder_pos_update_event(double eventtime)
        {
            if(enable && sensor_enable)
            {
                double extruder_pos = _get_extruder_pos(eventtime);
                runout_helper->note_filament_present(extruder_pos < filament_runout_pos);
            }
            return eventtime + CHECK_RUNOUT_TIMEOUT;
        }

        double EncoderSensor::encoder_event(double eventtime, bool state)
        {
            if(extruder)
            {
                _update_filament_runout_pos(eventtime);
                runout_helper->note_filament_present(true);
            }

            return 0.0;
        }
        
        double EncoderSensor::switch_event(double eventtime, bool state)
        {
            sensor_enable = state;
            SPDLOG_DEBUG("filament motion sensor, switch state = {}", state);
            return 0.0;
        }

        std::shared_ptr<EncoderSensor> encoder_sensor_load_config_prefix(std::shared_ptr<ConfigWrapper> config)
        {
            SPDLOG_INFO("encoder_sensor_load_config_prefix");
            return std::make_shared<EncoderSensor>(config);
        }
    }
}
