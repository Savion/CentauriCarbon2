/***************************************************************************** 
 * @Author       : Gary
 * @Date         : 2025-02-27 14:39:40
 * @LastEditors  : loping
 * @LastEditTime : 2025-09-28 12:26:03
 * @Description  : 
 * 
 * Copyright (c) 2025 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/

#include "buttons.h"
#include <iostream>
#include <bitset>
#include <map>
#include <algorithm>
#include <iostream>
#include <memory>
#include <functional>
#include <tuple>
#include <cmath>

namespace elegoo{
namespace extras{
#define ADC_REPORT_TIME 0.015
#define ADC_DEBOUNCE_TIME 0.025
#define ADC_SAMPLE_TIME 0.001
#define ADC_SAMPLE_COUNT 6
#define QUERY_TIME 0.002
#define RETRANSMIT_COUNT 50

MCU_buttons::MCU_buttons(std::shared_ptr<Printer> printer, std::shared_ptr<MCU> mcu)
    : reactor(printer->get_reactor()), mcu(mcu), invert(0), last_button(0), ack_count(0) {
    mcu->register_config_callback([this](){ build_config(); });
}


void MCU_buttons::setup_buttons(const std::vector<PinParams>& pins, std::function<double(double, int)> callback) {
    SPDLOG_DEBUG("setup_buttons");
    uint8_t mask = 0;
    uint8_t shift = pin_list.size();

    for (const auto& pin_params : pins) {
        if (pin_params.invert) {
            invert |= 1 << pin_list.size();
        }
        mask |= 1 << pin_list.size();
        pin_list.emplace_back(*pin_params.pin, pin_params.pullup);
    }

    callbacks.emplace_back(mask, shift, callback);
}

void MCU_buttons::build_config() {
    if (pin_list.empty()) {
        return;
    }

    oid = mcu->create_oid();

    mcu->add_config_cmd("config_buttons oid=" + std::to_string(oid) + " button_count=" + std::to_string(pin_list.size()));

    for (size_t i = 0; i < pin_list.size(); ++i) {
        //const auto& [pin, pullup] = pin_list[i];
        auto pin = pin_list[i].first;
        auto pullup = pin_list[i].second;
        SPDLOG_DEBUG("MCU_buttons pin:{} pullup:{}",pin,pullup);
        std::string buttons_add_str = "buttons_add oid=" + std::to_string(oid) + " pos=" + std::to_string(i) + " pin=" + pin +  " pull_up=" + std::to_string(pullup);
        SPDLOG_DEBUG("mcu_name {} {}",this->mcu->get_name(),buttons_add_str);
        mcu->add_config_cmd(buttons_add_str, true);
    }

    auto cmd_queue = mcu->alloc_command_queue();
    ack_cmd = mcu->lookup_command("buttons_ack oid=%c count=%c", cmd_queue);
    
    int clock = mcu->get_query_slot(oid);
    uint64_t rest_ticks = mcu->seconds_to_clock(QUERY_TIME);
    
    mcu->add_config_cmd("buttons_query oid=" + std::to_string(oid) + " clock=" + std::to_string(clock) + " rest_ticks=" + std::to_string(rest_ticks) + " retransmit_count="
                         + std::to_string(RETRANSMIT_COUNT) + " invert=" + std::to_string(invert), true);

    mcu->register_response([this](const json &params){ 
        handle_buttons_state(params); 
    }, "buttons_state", oid);
}

void MCU_buttons::handle_buttons_state(const json& params) {
    uint8_t ack_count = this->ack_count;
    int ack_diff = (static_cast<uint8_t>(std::stoi(params["ack_count"].get<std::string>())) - ack_count) & 0xff;
    ack_diff -= (ack_diff & 0x80) << 1;
    int msg_ack_count = ack_count + ack_diff;
    std::string state = params["state"].get<std::string>();
    std::vector<uint8_t> buttons(state.begin(), state.end()) ;
    int new_count = msg_ack_count + buttons.size() - this->ack_count;

    if (new_count <= 0) {
        SPDLOG_INFO("__func__:{} mcu_name:{} buttons.size:{} ack_count:{} new_count:{} state:{}",__func__,this->mcu->get_name(),buttons.size(),this->ack_count,new_count,state);
        return;
    }
    std::vector<uint8_t> new_buttons(buttons.end() - new_count, buttons.end());
    ack_cmd->send({std::to_string(oid), std::to_string(new_count)});
    this->ack_count = static_cast<uint8_t>(this->ack_count + new_count);;
    const double btime = params["#receive_time"].get<double>();

    for (auto button : new_buttons) {
        button ^= invert;
        uint8_t changed = button ^ last_button;
        last_button = button;
        for (const auto& cb  : callbacks) {
            uint8_t mask = std::get<0>(cb);
            uint8_t shift = std::get<1>(cb);
            auto callback = std::get<2>(cb);
            // SPDLOG_INFO("__func__:{} new_buttons.size:{} new_count:{} shift:{} button:{} changed:{} mask:{} state:{} params['state']:{}",__func__,new_buttons.size(),new_count,shift,button,changed,mask,(button & mask) >> shift,params["state"].get<std::string>());
            if (changed & mask) {
                const uint8_t state = (button & mask) >> shift;
                SPDLOG_INFO("__func__:{} mcu_name:{} new_count:{} buttons.size:{} shift:{} button:{} last_button:{} changed:{} mask:{} state:{} params['state']:{}",__func__,this->mcu->get_name(),new_count,buttons.size(),shift,button,last_button,changed,mask,state,params["state"].get<std::string>());
                reactor->register_async_callback([btime, callback, state](double enventtime) {
                    return callback(btime, state);
                });
            }
        }
    }
}

MCU_ADC_buttons::MCU_ADC_buttons(std::shared_ptr<Printer> printer, const std::string& pin, bool pullup)
    : reactor(printer->get_reactor()), pin(pin), pullup(pullup), min_value(999999999999.9), max_value(0.), last_button(INT_NONE), last_pressed(INT_NONE), last_debouncetime(0) {
    std::shared_ptr<PrinterPins> ppins = any_cast<std::shared_ptr<PrinterPins>>(printer->lookup_object("pins"));
    auto mcu_adc_ptr = ppins->setup_pin("adc", pin);
    mcu_adc = std::static_pointer_cast<MCU_adc>(mcu_adc_ptr);
    mcu_adc->setup_adc_sample(ADC_SAMPLE_TIME, ADC_SAMPLE_COUNT);
    mcu_adc->setup_adc_callback(ADC_REPORT_TIME, [this](double read_time, double read_value) {
        this->adc_callback(read_time, read_value);
    });
    auto query_adc = any_cast<std::shared_ptr<QueryADC>>(printer->lookup_object("query_adc"));
    query_adc->register_adc("adc_button:" + elegoo::common::strip(pin), mcu_adc);
}

void MCU_ADC_buttons::setup_button(double min_value, double max_value, std::function<json(double, bool)> callback) {
    this->min_value = std::min(this->min_value, min_value);
    this->max_value = std::max(this->max_value, max_value);
    buttons.emplace_back(min_value, max_value, callback);
}

void MCU_ADC_buttons::adc_callback(double read_time, double read_value) {
    double adc = std::max(0.00001, std::min(0.99999, read_value));
    double value = pullup * adc / (1.0 - adc);

    int btn = INT_NONE;
    if (min_value <= value && value <= max_value) {
        for (size_t i = 0; i < buttons.size(); ++i) {
            const auto bt = buttons[i];
            auto min_value = std::get<0>(bt);
            auto max_value = std::get<1>(bt);
            auto cd = std::get<2>(bt);
            if (min_value < value && value < max_value) {
                btn = i;
                break;
            }
        }
    }

    if (btn != last_button) {
        last_debouncetime = read_time;
    }

    if ((read_time - last_debouncetime) >= ADC_DEBOUNCE_TIME && last_button == btn && last_pressed != btn) {
        if (last_pressed != INT_NONE) {
            call_button(last_pressed, false);
            last_pressed = INT_NONE;
        }
        if (btn != INT_NONE) {
            call_button(btn, true);
            last_pressed = btn;
        }
    }

    last_button = btn;
}

void MCU_ADC_buttons::call_button(int button, bool state) {
    const auto bt = buttons[button];
    auto minval = std::get<0>(bt);
    auto maxval = std::get<1>(bt);
    auto callback = std::get<2>(bt);
    reactor->register_async_callback(
        [callback, state](double eventtime) {
            return callback(eventtime, state);
        }
    );
}

BaseRotaryEncoder::BaseRotaryEncoder(Callback cw_callback, Callback ccw_callback)
    : cw_callback(cw_callback), ccw_callback(ccw_callback), encoder_state(R_START) {}

double BaseRotaryEncoder::encoder_callback(double eventtime, int state) {
    // 需要根据具体的编码器状态表来更新当前状态和回调
    // 这里是一个占位符，子类会重写此方法
    return 0.0;
}

const int FullStepRotaryEncoder::ENCODER_STATES[7][4] = {
    // R_START
    {BaseRotaryEncoder::R_START, R_CW_BEGIN, R_CCW_BEGIN, BaseRotaryEncoder::R_START},
    // R_CW_FINAL
    {R_CW_NEXT, BaseRotaryEncoder::R_START, R_CW_FINAL, BaseRotaryEncoder::R_START | BaseRotaryEncoder::R_DIR_CW},
    // R_CW_BEGIN
    {R_CW_NEXT, R_CW_BEGIN, BaseRotaryEncoder::R_START, BaseRotaryEncoder::R_START},
    // R_CW_NEXT
    {R_CW_NEXT, R_CW_BEGIN, R_CW_FINAL, BaseRotaryEncoder::R_START},
    // R_CCW_BEGIN
    {R_CCW_NEXT, BaseRotaryEncoder::R_START, R_CCW_BEGIN, BaseRotaryEncoder::R_START},
    // R_CCW_FINAL
    {R_CCW_NEXT, R_CCW_FINAL, BaseRotaryEncoder::R_START, BaseRotaryEncoder::R_START | BaseRotaryEncoder::R_DIR_CCW},
    // R_CCW_NEXT
    {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, BaseRotaryEncoder::R_START}
};

FullStepRotaryEncoder::FullStepRotaryEncoder(Callback cw_callback, Callback ccw_callback)
    : BaseRotaryEncoder(cw_callback, ccw_callback) {}

double FullStepRotaryEncoder::encoder_callback(double eventtime, int state) {
    int es = ENCODER_STATES[encoder_state & 0xF][state & 0x3];
    encoder_state = es;

    if (es & R_DIR_MSK == R_DIR_CW) {
        cw_callback(eventtime);
    } else if (es & R_DIR_MSK == R_DIR_CCW) {
        ccw_callback(eventtime);
    }
    return 0.0;
}

const int HalfStepRotaryEncoder::ENCODER_STATES[6][4] = {
    // R_START (00)
    {R_START_M, R_CW_BEGIN, R_CCW_BEGIN, BaseRotaryEncoder::R_START},
    // R_CCW_BEGIN
    {R_START_M | BaseRotaryEncoder::R_DIR_CCW, BaseRotaryEncoder::R_START, R_CCW_BEGIN, BaseRotaryEncoder::R_START},
    // R_CW_BEGIN
    {R_START_M | BaseRotaryEncoder::R_DIR_CW, R_CW_BEGIN, BaseRotaryEncoder::R_START, BaseRotaryEncoder::R_START},
    // R_START_M (11)
    {R_START_M, R_CCW_BEGIN_M, R_CW_BEGIN_M, BaseRotaryEncoder::R_START},
    // R_CW_BEGIN_M
    {R_START_M, R_START_M, R_CW_BEGIN_M, BaseRotaryEncoder::R_START | BaseRotaryEncoder::R_DIR_CW},
    // R_CCW_BEGIN_M
    {R_START_M, R_CCW_BEGIN_M, R_START_M, BaseRotaryEncoder::R_START | BaseRotaryEncoder::R_DIR_CCW},
};

HalfStepRotaryEncoder::HalfStepRotaryEncoder(Callback cw_callback, Callback ccw_callback)
    : BaseRotaryEncoder(cw_callback, ccw_callback) {}

double HalfStepRotaryEncoder::encoder_callback(double eventtime, int state) {
    int es = ENCODER_STATES[encoder_state & 0xF][state & 0x3];
    encoder_state = es;

    if (es & R_DIR_MSK == R_DIR_CW) {
        cw_callback(eventtime);
    } else if (es & R_DIR_MSK == R_DIR_CCW) {
        ccw_callback(eventtime);
    }
    return 0;
}

PrinterButtons::PrinterButtons(std::shared_ptr<ConfigWrapper> config) : printer(config->get_printer()) {
    printer->load_object(config, "query_adc");
}

void PrinterButtons::register_adc_button(const std::string& pin, double min_val, double max_val, bool pullup, std::function<double(double, bool)> callback) {
    auto adc_buttons_iter = adc_buttons.find(pin);
    if (adc_buttons_iter == adc_buttons.end()) {
        adc_buttons[pin] = std::make_shared<MCU_ADC_buttons>(printer, pin, pullup);
    }
    adc_buttons[pin]->setup_button(min_val, max_val, callback);
}

void PrinterButtons::register_adc_button_push(const std::string& pin, double min_val, double max_val, bool pullup, ButtonCallback callback) {
    auto helper = [callback](double eventtime, bool state) {
        if (state) {
            return callback(eventtime);
        }
        return 0.0;
    };
    register_adc_button(pin, min_val, max_val, pullup, helper);
}

void PrinterButtons::register_buttons(const std::vector<std::string>& pins, std::function<double(double, int)> callback) {
    SPDLOG_DEBUG("register_buttons");
    std::shared_ptr<PrinterPins> ppins = any_cast<std::shared_ptr<PrinterPins>>(printer->lookup_object("pins"));
    std::string mcu_name = "";
    std::vector<PinParams> pin_params_list;
     std::shared_ptr<MCU> mcu = nullptr;

    for (const auto& pin : pins) {
        std::shared_ptr<PinParams> pin_params = ppins->lookup_pin(pin, true, true);
        if (mcu != nullptr && std::static_pointer_cast<MCU>(pin_params->chip) != mcu) {
            throw elegoo::common::PinsError("button pins must be on same mcu");
        }
        mcu = std::static_pointer_cast<MCU>(pin_params->chip);
        mcu_name = *pin_params->chip_name;
        pin_params_list.push_back(*pin_params);
    }

    auto mcu_buttons_iter = mcu_buttons.find(mcu_name);
    if (mcu_buttons_iter == mcu_buttons.end() || mcu_buttons[mcu_name]->pin_list.size() + pin_params_list.size() > 8) {
        mcu_buttons[mcu_name] = std::make_shared<MCU_buttons>(printer, mcu);
    }
    // SPDLOG_INFO("mcu_buttons.size:{}",mcu_buttons.size());
    mcu_buttons[mcu_name]->setup_buttons(pin_params_list, callback);
}

void PrinterButtons::register_rotary_encoder(const std::string& pin1, const std::string& pin2, ButtonCallback cw_callback, ButtonCallback ccw_callback, int steps_per_detent) {
    std::shared_ptr<BaseRotaryEncoder> re;
    if (steps_per_detent == 2) {
        re = std::make_shared<HalfStepRotaryEncoder>(cw_callback, ccw_callback);
    } else if (steps_per_detent == 4) {
        re = std::make_shared<FullStepRotaryEncoder>(cw_callback, ccw_callback);
    } else {
        throw elegoo::common::ConfigParserError(std::to_string(steps_per_detent) + "steps per detent not supported");

    }
    register_buttons({pin1, pin2}, [re](double eventtime, bool state) {
        return re->encoder_callback(eventtime, state);
    });
}

void PrinterButtons::register_button_push(const std::string& pin, std::function<double(double, int)> callback) {
    auto helper = [callback](double eventtime, bool state) {
        if (state) {
            return callback(eventtime, state);
        }
        return 0.0;
    };
    register_buttons({pin}, helper);
}

std::shared_ptr<PrinterButtons> buttons_load_config(std::shared_ptr<ConfigWrapper> config) {
    return std::make_shared<PrinterButtons>(config);
}
    
}
}