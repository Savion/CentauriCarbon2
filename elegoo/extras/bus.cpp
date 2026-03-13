/***************************************************************************** 
 * @Author       : Jack
 * @Date         : 2024-12-20 20:26:38
 * @LastEditors  : Ben
 * @LastEditTime : 2024-12-31 10:38:04
 * @Description  : 
 * 
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/

#include "bus.h"

namespace elegoo {
namespace extras {

std::string resolve_bus_name(std::shared_ptr<MCU> mcu, std::string param, std::string bus) {
    // Find enumerations for the given bus
    std::string bus_val = bus;
    auto enumerations = mcu->get_enumerations();
    auto enums_it = enumerations.find(param);
    if (enums_it == enumerations.end()) {
        enums_it = enumerations.find("bus");
    }
    if (enums_it == enumerations.end()) {
        return bus_val;
    }

    // Verify bus is a valid enumeration
    auto ppins_ptr = mcu->get_printer()->lookup_object("pins");
    auto ppins = any_cast<std::shared_ptr<PrinterPins>>(ppins_ptr);
    std::string mcu_name = mcu->get_name();

    // If bus is not specified, try to find default value (0)
    if (bus_val.empty()) {
        std::map<int, std::string> rev_enums;
        for (const auto& pair : enums_it->second) {
            rev_enums[pair.second] = pair.first;
        }
        if (rev_enums.find(0) == rev_enums.end()) {
            throw std::runtime_error("Must specify " + param + " on mcu '" + mcu_name + "'");
        }
        bus_val = rev_enums[0];
    }

    // Check if the provided bus is in the enumeration
    auto bus_it = enums_it->second.find(bus_val);
    if (bus_it == enums_it->second.end()) {
        throw std::runtime_error("Unknown " + param + " '" + bus_val + "'");
    }

    // Check for reserved bus pins
    auto constants = mcu->get_constants();
    std::ostringstream bus_pins_key;
    bus_pins_key << "BUS_PINS_" << bus_val;
    auto reserve_pins_it = constants.find(bus_pins_key.str());
    if (reserve_pins_it != constants.end()) {
        auto pin_resolver = ppins->get_pin_resolver(mcu_name);
        std::istringstream iss(reserve_pins_it->second);
        std::string pin;
        while (std::getline(iss, pin, ',')) {
            std::string bus_str = bus_val;
            pin_resolver->reserve_pin(pin, bus_val);
        }
    }

    return bus_val;
}

MCU_SPI::MCU_SPI(std::shared_ptr<MCU> mcu_, std::string bus_, const std::string& pin, int mode, int speed,
                 const std::tuple<std::string, std::string, std::string> sw_pins, bool cs_active_high)
{
    this->mcu = mcu_;
    this->bus = bus_;
    this->oid = mcu->create_oid();
    this->cmd_queue = mcu->alloc_command_queue();
    this->spi_send_cmd = nullptr; 
    this->spi_transfer_cmd = nullptr;
    
    if (pin.empty()) {
        mcu->add_config_cmd("config_spi_without_cs oid=" + std::to_string(oid));
    } else {
        mcu->add_config_cmd("config_spi oid=" + std::to_string(oid) +
                            " pin=" + pin + " cs_active_high=" + std::to_string(cs_active_high));
    }

    std::string s0 = std::get<0>(sw_pins),s1 = std::get<1>(sw_pins),s2 = std::get<2>(sw_pins);
    if (""  == s0 && "" == s1 && "" == s2) 
    {
        config_fmt = "spi_set_bus oid=" + std::to_string(oid) + " spi_bus=%s mode=" + std::to_string(mode) +
                     " rate=" + std::to_string(speed);
    } 
    else 
    {
        config_fmt = "spi_set_software_bus oid=" + std::to_string(oid) +
                     " miso_pin=" + std::get<0>(sw_pins) +
                     " mosi_pin=" + std::get<1>(sw_pins) +
                     " sclk_pin=" + std::get<2>(sw_pins) +
                     " mode=" + std::to_string(mode) +
                     " rate=" + std::to_string(speed);
    }
    SPDLOG_DEBUG("__func__:{},config_fmt:{}",__func__,config_fmt);

    mcu->register_config_callback([this]() { this->build_config(); });
}

void MCU_SPI::setup_shutdown_msg(const std::vector<uint8_t>& shutdown_seq) {
    std::ostringstream shutdown_msg;
    for (auto byte : shutdown_seq) {
        shutdown_msg << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    mcu->add_config_cmd("config_spi_shutdown oid=" + std::to_string(mcu->create_oid()) +
                        " spi_oid=" + std::to_string(oid) +
                        " shutdown_msg=" + shutdown_msg.str(), true);
}

int MCU_SPI::get_oid() const {
    return oid;
}

std::shared_ptr<MCU> MCU_SPI::get_mcu() const {
    return mcu;
}

std::shared_ptr<command_queue> MCU_SPI::get_command_queue() const {
    return cmd_queue;
}

void MCU_SPI::build_config() {
    SPDLOG_DEBUG("__func__:{} #1",__func__);
    if (config_fmt.find("%") != std::string::npos) {
        auto bus = resolve_bus_name(mcu, "spi_bus", this->bus);
        std::ostringstream oss;
        oss << config_fmt;
        oss << bus;
        config_fmt = oss.str();
    }
    mcu->add_config_cmd(config_fmt);
    spi_send_cmd = mcu->lookup_command("spi_send oid=%c data=%*s", cmd_queue);
    spi_transfer_cmd = mcu->lookup_query_command(
        "spi_transfer oid=%c data=%*s",
        "spi_transfer_response oid=%c response=%*s", oid, cmd_queue);
    SPDLOG_DEBUG("__func__:{} #1",__func__);
}

void MCU_SPI::spi_send(const std::vector<uint8_t>& data, uint32_t minclock, uint32_t reqclock) {
    if (!spi_send_cmd) {
        // Send setup message via mcu initialization
        std::ostringstream data_msg;
        for (auto byte : data) {
            data_msg << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        mcu->add_config_cmd("spi_send oid=" + std::to_string(oid) + " data=" + data_msg.str(), true);
        return;
    }
    std::vector<Any> sendData;
    sendData.push_back(std::to_string(oid));
    sendData.push_back(data);

    spi_send_cmd->send(sendData, minclock, reqclock);
}

json MCU_SPI::spi_transfer(const std::vector<uint8_t>& data, uint32_t minclock, uint32_t reqclock) {
    std::vector<Any> sendData;
    sendData.push_back(std::to_string(oid));
    sendData.push_back(data);
    SPDLOG_DEBUG("__func__:{} #1 {} {} {} {} {}",__func__,oid,sendData.size(),data.size(),data[0],data[1]);
    return spi_transfer_cmd->send(sendData, minclock, reqclock);
}

json MCU_SPI::spi_transfer_with_preface(const std::vector<uint8_t>& preface_data,
                                                        const std::vector<uint8_t>& data,
                                                        uint32_t minclock, uint32_t reqclock) {
    std::vector<Any> sendData;
    sendData.push_back(std::to_string(oid));
    sendData.push_back(data);

    std::vector<Any> prefaceData;
    prefaceData.push_back(std::to_string(oid));
    prefaceData.push_back(preface_data);

    return spi_transfer_cmd->send_with_preface(spi_send_cmd,
                                                prefaceData,
                                                sendData,
                                                minclock, reqclock);
}

std::shared_ptr<MCU_SPI> MCU_SPI_from_config(
    std::shared_ptr<ConfigWrapper> config, int mode,
    const std::string& pin_option,
    int default_speed, const std::string* share_type,
    bool cs_active_high) {

    SPDLOG_DEBUG("__func__:{} #2",__func__);
    // Determine pin from config
    auto ppins_ptr = config->get_printer()->lookup_object("pins");
    auto ppins = any_cast<std::shared_ptr<PrinterPins>>(ppins_ptr);
    std::string cs_pin = config->get(pin_option);

    SPDLOG_DEBUG("__func__:{} #2",__func__);
    auto cs_pin_params = ppins->lookup_pin(cs_pin, share_type);

    SPDLOG_DEBUG("__func__:{} #2",__func__);
    std::string pin = *cs_pin_params->pin;
    if (pin == "None") {
        ppins->reset_pin_sharing(cs_pin_params);
        pin = "";
    }

    SPDLOG_DEBUG("__func__:{} #2",__func__);
    // Load bus parameters
    auto mcu = std::static_pointer_cast<MCU>(cs_pin_params->chip); // Assuming the chip is an MCU
    int speed = config->getint("spi_speed", default_speed, 100000);

    SPDLOG_DEBUG("__func__:{} #2",__func__);
    std::tuple<std::string, std::string, std::string> sw_pins = {};
    std::string bus;
    if (!config->get("spi_software_sclk_pin","").empty()) {
        std::vector<std::string> sw_pin_names = {
            "spi_software_miso_pin", "spi_software_mosi_pin", "spi_software_sclk_pin"
        };
        std::vector<std::shared_ptr<PinParams>> sw_pin_params;
        for (const auto& name : sw_pin_names) {
            sw_pin_params.emplace_back(ppins->lookup_pin(config->get(name,""), &name));
        }
        for (const auto& params : sw_pin_params) {
            if (params->chip != mcu) {
                SPDLOG_ERROR(config->get_name() + ": spi pins must be on same mcu");
                throw std::runtime_error(config->get_name() + ": spi pins must be on same mcu");
            }
        }
        sw_pins = std::make_tuple(*sw_pin_params[0]->pin, *sw_pin_params[1]->pin, *sw_pin_params[2]->pin);
        bus = "";
    } else {
        bus = config->get("spi_bus", "");
        sw_pins = {};
    }

    SPDLOG_DEBUG("__func__:{} #2",__func__);
    // Create MCU_SPI object
    return std::make_shared<MCU_SPI>(mcu, bus, pin, mode, speed,
        sw_pins, cs_active_high);
}






}
}