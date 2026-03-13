/*****************************************************************************
 * @Author       : Louisa
 * @Date         : 2024-11-22 15:58:51
 * @LastEditors  : huangpeiyun
 * @LastEditTime : 2025-07-16 10:29:17
 * @Description  : PWM and digital output pin handling
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#include "output_pin.h"
#include "printer.h"

namespace elegoo
{
    namespace extras
    {
        const double PIN_MIN_TIME = 0.180;

        GCodeRequestQueue::GCodeRequestQueue(
            std::shared_ptr<ConfigWrapper> config,
            std::shared_ptr<MCU> mcu,
            std::function<std::pair<std::string, double>(double, double)> callback)
            : mcu(mcu), callback(callback)
        {
            printer = config->get_printer();
            next_min_flush_time = 0;
            mcu->register_flush_callback([this](double print_time, double clock)
                                         { flush_notification(print_time, clock); });
            elegoo::common::SignalManager::get_instance().register_signal(
                "elegoo:connect",
                std::function<void()>([this]()
                                      { handle_connect(); }));
        }

        GCodeRequestQueue::~GCodeRequestQueue()
        {
        }

        void GCodeRequestQueue::queue_gcode_request(double value)
        {
            toolhead->register_lookahead_callback([this, value](double print_time)
                                                  { queue_request(print_time, value); });
        }

        void GCodeRequestQueue::send_async_request(double value, double print_time)
        {
            if (std::isnan(print_time))
            {
                double systime = get_monotonic();
                print_time = mcu->estimated_print_time(systime) + PIN_MIN_TIME;
            }

            while (true)
            {
                double next_time = std::max(print_time, next_min_flush_time);

                std::string action = "normal";
                double min_wait = 0.0;
                auto ret = callback(next_time, value);

                if (ret.first != "")
                {
                    action = ret.first;
                    min_wait = ret.second;
                    if (action == "discard")
                        break;
                }
                next_min_flush_time = next_time + std::max(min_wait, PIN_MIN_TIME);
                if (action != "delay")
                    break;
            }
        }

        void GCodeRequestQueue::handle_connect()
        {
            toolhead = any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));
        }

        void GCodeRequestQueue::flush_notification(double print_time, double clock)
        {
            while (!rqueue.empty())
            {
                double next_time = std::max(rqueue[0].first, next_min_flush_time);
                if (next_time > print_time)
                {
                    return;
                }

                int pos = 0;
                while (pos + 1 < rqueue.size() && rqueue[pos + 1].first <= next_time)
                {
                    ++pos;
                }
                // print_time, value
                std::pair<double, double> req = rqueue[pos];

                double min_wait = 0.0;
                std::pair<std::string, double> ret = callback(next_time, req.second);
                if (ret.first != "")
                {
                    min_wait = ret.second;
                    if (ret.first == "discard")
                    {
                        rqueue.erase(rqueue.begin(), rqueue.begin() + pos + 1);
                        continue;
                    }
                    if (ret.first == "delay")
                    {
                        pos--;
                    }
                }
                rqueue.erase(rqueue.begin(), rqueue.begin() + pos + 1);
                next_min_flush_time = next_time + std::max(min_wait, PIN_MIN_TIME);
                toolhead->note_mcu_movequeue_activity(next_min_flush_time);
            }
        }

        void GCodeRequestQueue::queue_request(double print_time, double value)
        {
            rqueue.emplace_back(print_time, value);
            toolhead->note_mcu_movequeue_activity(print_time);
        }

        PrinterOutputPin::PrinterOutputPin(std::shared_ptr<ConfigWrapper> config) : config(config)
        {
            SPDLOG_INFO("PrinterOutputPin init!");
            printer = config->get_printer();
            std::shared_ptr<PrinterPins> ppins =
                any_cast<std::shared_ptr<PrinterPins>>(printer->lookup_object("pins"));

            is_pwm = config->getboolean("pwm", BoolValue::BOOL_FALSE);

            if (is_pwm)
            {
                mcu_pin = ppins->setup_pin("pwm", config->get("pin"));
                double cycle_time = config->getdouble("cycle_time", 0.1, DOUBLE_NONE, 5, 0);
                bool hardware_pwm = config->getboolean("hardware_pwm", BoolValue::BOOL_FALSE);
                mcu_pin->setup_cycle_time(cycle_time, hardware_pwm);
                scale = config->getdouble("scale", 1, DOUBLE_NONE, DOUBLE_NONE, 0);
            }
            else
            {
                mcu_pin = ppins->setup_pin("digital_out", config->get("pin"));
                scale = 1;
            }

            mcu_pin->setup_max_duration(0.);
            last_value = config->getdouble("value", 0, 0, scale) / scale;
            shutdown_value = config->getdouble("shutdown_value", 0, 0, scale) / scale;

            mcu_pin->setup_start_value(last_value, shutdown_value);
            gcrq = std::make_shared<GCodeRequestQueue>(config, mcu_pin->get_mcu(),
                                                       [this](double print_time, double value)
                                                       {
                                                           return set_pin(print_time, value);
                                                       });

            std::istringstream iss(config->get_name());
            std::string word;
            std::string pin_name;
            while (iss >> word)
            {
                pin_name = word;
            }

            gcode = any_cast<std::shared_ptr<GCodeDispatch>>(printer->lookup_object("gcode"));

            gcode->register_mux_command("SET_PIN", "PIN", pin_name, [this](std::shared_ptr<GCodeCommand> gcmd)
                                        { cmd_SET_PIN(gcmd); }, "Set the value of an output pin");
            SPDLOG_INFO("PrinterOutputPin init success!!");
        }

        PrinterOutputPin::~PrinterOutputPin()
        {
        }

        json PrinterOutputPin::get_status(double eventtime)
        {
            json status;
            status["value"] = last_value;
            status["led_value"] = led_value;
            return status;
        }

        void PrinterOutputPin::cmd_SET_PIN(std::shared_ptr<GCodeCommand> gcmd)
        {
            SPDLOG_INFO("{} : {}___", __FUNCTION__, __LINE__);
            double value = gcmd->get_double("VALUE", DOUBLE_INVALID, 0, scale);
            value /= scale;

            if (!is_pwm && value != 0.0f && value != 1.0f)
            {
                // throw gcmd->error("Invalid pin value");
            }

            
            std::string cfgname = config->get_name();
            SPDLOG_INFO("{} : {}___value:{},scale:{},cfgname:{}", __FUNCTION__, __LINE__, value,scale,cfgname);
            if(cfgname == "output_pin led_pin"
                || cfgname == "output_pin heart_nozzle_pin"
                || cfgname == "output_pin heart_bed_pin"
                || cfgname == "output_pin control_fan_pin"
                || cfgname == "output_pin fan1_pin"
                || cfgname == "output_pin box_fan_pin"
                || cfgname == "output_pin heart_fan_pin"
                || cfgname == "output_pin fan_pin"
                ) 
            {
                gcrq->send_async_request(value);
                led_value = value;
            } else {
                // 将请求值排队
                gcrq->queue_gcode_request(value);
            }
            
            // std::string cfgname = config->get_name();
            // if(cfgname == "output_pin led_pin") {
            //     std::shared_ptr<PrinterConfig> configfile =
            //         any_cast<std::shared_ptr<PrinterConfig>>(printer->lookup_object("configfile"));
            //     configfile->set(cfgname, "value", std::to_string(value));
            //     configfile->cmd_SAVE_CONFIG(gcode->create_gcode_command("SAVE_CONFIG", "SAVE_CONFIG", std::map<std::string, std::string>()));
            // }

        }

        std::pair<std::string, double> PrinterOutputPin::set_pin(double print_time, double value)
        {
            if (value == last_value)
            {
                return {"discard", 0.0};
            }

            last_value = value;

            if (is_pwm)
            {
                mcu_pin->set_pwm(print_time, value);
            }
            else
            {
                mcu_pin->set_digital(print_time, value);
            }

            return {"normal", 0.0};
        }

        PrinterTemplateEvaluator::PrinterTemplateEvaluator(std::shared_ptr<ConfigWrapper> config)
        {
        }

        PrinterTemplateEvaluator::~PrinterTemplateEvaluator()
        {
        }

        void PrinterTemplateEvaluator::_activate_timer()
        {
        }

        void PrinterTemplateEvaluator::set_template(std::shared_ptr<GCodeCommand> gcmd, std::function<void(const std::string &)> callback, std::function<void(void)> flush_callback)
        {
        }

        std::shared_ptr<PrinterTemplateEvaluator> lookup_template_eval(std::shared_ptr<ConfigWrapper> config)
        {
            return nullptr;
        }

        std::shared_ptr<PrinterOutputPin> output_pin_load_config_prefix(
            std::shared_ptr<ConfigWrapper> config)
        {
            return std::make_shared<PrinterOutputPin>(config);
        }

    }
}