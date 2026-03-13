/*****************************************************************************
 * @Author       : Coconut
 * @Date         : 2024-11-07 14:24:31
 * @LastEditors  : coconut
 * @LastEditTime : 2025-03-20 16:37:57
 * @Description  : 封装Linux GPIO控制
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#include "host_warpper.h"
#include "reactor.h"
#include "clocksync.h"
#include "configfile.h"
#include "printer.h"
#include "pins.h"
#include "exception_handler.h"
#include <algorithm>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

MCU_host_digital_pin::MCU_host_digital_pin(std::shared_ptr<PinParams> pin_params)
{
    // 解析引脚号PG11
    pin_name = *pin_params->pin;
    port = 0;
    pin_idx = 0;
    pin = 0;
    if (pin_name[0] == 'P')
    {
        port = pin_name[1] - 'A';
        pin_idx = std::stoi(pin_name.substr(2));
        pin = port * 32 + pin_idx;
        // SPDLOG_INFO("pin_name {} port {} pin_idx {} pin {}", pin_name, port, pin_idx, pin);
    }
    else
    {
        throw elegoo::common::ConfigParserError("unkown pin_name " + pin_name);
    }

    // 1. 检测引脚是否已经导出
    char pin_dir[256];
    char pin_str[16];
    snprintf(pin_dir, sizeof(pin_dir), "/sys/class/gpio/gpio%d", pin);
    snprintf(pin_str, sizeof(pin_str), "%d", pin);
    // 2. 未导出则进行导出
    if (access(pin_dir, F_OK) != 0)
    {
        // SPDLOG_INFO("ready export gpio {}", pin_str);
        int fd = open("/sys/class/gpio/export", O_WRONLY);
        if (fd == -1)
            throw elegoo::common::PinsError("Can't open /sys/class/gpio/export");
        write(fd, pin_str, strlen(pin_str));
        close(fd);
    }
    // 3. 获取读写接口
    char direction[512];
    char value[512];
    snprintf(direction, sizeof(direction), "%s/direction", pin_dir);
    snprintf(value, sizeof(value), "%s/value", pin_dir);
    if ((fd_direction = open(direction, O_RDWR)) == -1)
        throw elegoo::common::PinsError("Can't open " + std::string(direction));
    if ((fd_value = open(value, O_RDWR)) == -1)
        throw elegoo::common::PinsError("Can't open " + std::string(value));
}

MCU_host_digital_pin::~MCU_host_digital_pin()
{
    if (fd_direction > 0)
        close(fd_direction);
    if (fd_value > 0)
        close(fd_value);
}

void MCU_host_digital_pin::set_digital(uint8_t value)
{
    // SPDLOG_INFO("set_digital {} {} {}", pin_name, pin, value);
    lseek(fd_value, 0, SEEK_SET);
    write(fd_value, (value == 0) ? "0" : "1", 1);
}

void MCU_host_digital_pin::set_direction(uint8_t in)
{
    // SPDLOG_INFO("set_direction {} {} {}", pin_name, pin, in);
    lseek(fd_direction, 0, SEEK_SET);
    write(fd_direction, in ? "in" : "out", in ? sizeof("in") - 1 : sizeof("out") - 1);
}

uint8_t MCU_host_digital_pin::get_digital()
{
    uint8_t value;
    lseek(fd_value, 0, SEEK_SET);
    read(fd_value, &value, 1);
    return value == '1' ? 1 : 0;
}

void HostWarpper::init(std::shared_ptr<ConfigWrapper> config)
{
    this->printer = config->get_printer();
    auto ppins = any_cast<std::shared_ptr<PrinterPins>>(printer->lookup_object("pins"));
    ppins->register_chip("host", shared_from_this());
}

std::shared_ptr<MCU_pins> HostWarpper::setup_pin(const std::string &pin_type, std::shared_ptr<PinParams> pin_params)
{
    if (pin_type == "host_digital_pin")
        return std::dynamic_pointer_cast<MCU_pins>(std::make_shared<MCU_host_digital_pin>(pin_params));
    throw std::runtime_error("Unsupported pin type: " + pin_type);
}
