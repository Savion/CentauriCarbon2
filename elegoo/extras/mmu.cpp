/*****************************************************************************
 * @Author       : Ben
 * @Date         : 2025-08-22 10:28:31
 * @LastEditors  : loping
 * @LastEditTime : 2025-09-30 16:43:57
 * @Description  :
 *
 * Copyright (c) 2025 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#include "mmu.h"
#include "printer.h"
#include "buttons.h"
#include "print_stats.h"
#include "pause_resume.h"
#include "serial_bootloader.h"
#include "virtual_sdcard.h"
#include <fstream>
#include <endian.h>
#include <glob.h>
#include "gcode_move.h"

namespace elegoo
{
namespace extras
{

static constexpr uint8_t CRC8_INIT = 0x66;
static constexpr uint16_t CRC16_INIT = 0x913D;
std::map<uint32_t,std::string> filament_type = {
    {PLA,"PLA"},
    {PETG,"PETG"},
    {ABS,"ABS"},
    {TPU,"TPU"},
    {PA,"PA"},
    {CPE,"CPE"},
    {PC,"PC"},  // 补充PC类型
    {PVA,"PVA"},
    {ASA,"ASA"},
    {BVOH,"BVOH"},
    {EVA,"EVA"},
    {HIPS,"HIPS"},
    {PP,"PP"},
    {PPA,"PPA"},
    {PPS,"PPS"},  // 新增PPS
};

std::map<uint32_t,std::string> filament_detail_type = {
    // PLA
    {PLA_,"PLA"},
    {PLA_Plus,"PLA+"},
    {PLA_Hyper,"Hyper-PLA"},
    {PLA_Silk,"PLA-Silk"},
    {PLA_CF,"PLA-CF"},
    {PLA_Carbon,"PLA-Carbon"},
    {PLA_Matte,"PLA-Matte"},
    {PLA_Fluo,"PLA-Fluo"},
    {PLA_Wood,"PLA-Wood"},
    // PETG
    {PETG_,"PETG"},
    {PETG_CF,"PETG-CF"},
    {PETG_Hyper,"Hyper-PETG"},
    // ABS
    {ABS_,"ABS"},
    {ABS_Hyper,"Hyper-ABS"},
    {ABS_GF,"ABS-GF"},
    // TPU
    {TPU_,"TPU"},
    {TPU_Hyper,"Hyper-TPU"},
    // PA
    {PA_,"PA"},
    {PA_CF,"PA-CF"},
    {PA_Hyper,"Hyper-PA"},
    {PAHT_CF,"PAHT-CF"},
    {PA6,"PA6"},
    {PA6_CF,"PA6-CF"},
    {PA12,"PA12"},
    {PA12_CF,"PA12-CF"},
    // CPE
    {CPE_,"CPE"},
    {CPE_Hyper,"Hyper-CPE"},
    // PC
    {PC_,"PC"},
    {PC_PCTG,"PCTG"},
    {PC_Hyper,"Hyper-PC"},
    // PVA
    {PVA_,"PVA"},
    {PVA_Hyper,"Hyper-PVA"},
    // ASA
    {ASA_,"ASA"},
    {ASA_Hyper,"Hyper-ASA"},
    // BVOH
    {BVOH_,"BVOH"},
    // EVA
    {EVA_,"EVA"},
    // HIPS
    {HIPS_,"HIPS"},
    // PP
    {PP_,"PP"},
    {PP_CF,"PP-CF"},
    {PP_GF,"PP-GF"},
    // PPA
    {PPA_,"PPA"},
    {PPA_CF,"PPA-CF"},
    {PPA_GF,"PPA-GF"},
    // PPS
    {PPS_,"PPS"},
    {PPS_CF,"PPS-CF"},
};
// 预生成的查找表
static const uint8_t crc8_table[256] = {
    0x00, 0x39, 0x72, 0x4B, 0xE4, 0xDD, 0x96, 0xAF, 0xF1, 0xC8, 0x83, 0xBA, 0x15, 0x2C, 0x67, 0x5E,
    0xDB, 0xE2, 0xA9, 0x90, 0x3F, 0x06, 0x4D, 0x74, 0x2A, 0x13, 0x58, 0x61, 0xCE, 0xF7, 0xBC, 0x85,
    0x8F, 0xB6, 0xFD, 0xC4, 0x6B, 0x52, 0x19, 0x20, 0x7E, 0x47, 0x0C, 0x35, 0x9A, 0xA3, 0xE8, 0xD1,
    0x54, 0x6D, 0x26, 0x1F, 0xB0, 0x89, 0xC2, 0xFB, 0xA5, 0x9C, 0xD7, 0xEE, 0x41, 0x78, 0x33, 0x0A,
    0x27, 0x1E, 0x55, 0x6C, 0xC3, 0xFA, 0xB1, 0x88, 0xD6, 0xEF, 0xA4, 0x9D, 0x32, 0x0B, 0x40, 0x79,
    0xFC, 0xC5, 0x8E, 0xB7, 0x18, 0x21, 0x6A, 0x53, 0x0D, 0x34, 0x7F, 0x46, 0xE9, 0xD0, 0x9B, 0xA2,
    0xA8, 0x91, 0xDA, 0xE3, 0x4C, 0x75, 0x3E, 0x07, 0x59, 0x60, 0x2B, 0x12, 0xBD, 0x84, 0xCF, 0xF6,
    0x73, 0x4A, 0x01, 0x38, 0x97, 0xAE, 0xE5, 0xDC, 0x82, 0xBB, 0xF0, 0xC9, 0x66, 0x5F, 0x14, 0x2D,
    0x4E, 0x77, 0x3C, 0x05, 0xAA, 0x93, 0xD8, 0xE1, 0xBF, 0x86, 0xCD, 0xF4, 0x5B, 0x62, 0x29, 0x10,
    0x95, 0xAC, 0xE7, 0xDE, 0x71, 0x48, 0x03, 0x3A, 0x64, 0x5D, 0x16, 0x2F, 0x80, 0xB9, 0xF2, 0xCB,
    0xC1, 0xF8, 0xB3, 0x8A, 0x25, 0x1C, 0x57, 0x6E, 0x30, 0x09, 0x42, 0x7B, 0xD4, 0xED, 0xA6, 0x9F,
    0x1A, 0x23, 0x68, 0x51, 0xFE, 0xC7, 0x8C, 0xB5, 0xEB, 0xD2, 0x99, 0xA0, 0x0F, 0x36, 0x7D, 0x44,
    0x69, 0x50, 0x1B, 0x22, 0x8D, 0xB4, 0xFF, 0xC6, 0x98, 0xA1, 0xEA, 0xD3, 0x7C, 0x45, 0x0E, 0x37,
    0xB2, 0x8B, 0xC0, 0xF9, 0x56, 0x6F, 0x24, 0x1D, 0x43, 0x7A, 0x31, 0x08, 0xA7, 0x9E, 0xD5, 0xEC,
    0xE6, 0xDF, 0x94, 0xAD, 0x02, 0x3B, 0x70, 0x49, 0x17, 0x2E, 0x65, 0x5C, 0xF3, 0xCA, 0x81, 0xB8,
    0x3D, 0x04, 0x4F, 0x76, 0xD9, 0xE0, 0xAB, 0x92, 0xCC, 0xF5, 0xBE, 0x87, 0x28, 0x11, 0x5A, 0x63
};

// CRC-16 查找表（按 MSB-first 左移规则生成）
static const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};



CanvasProtocol::CanvasProtocol(std::shared_ptr<ConfigWrapper> config)
{
    recv_length = 0;
    crc_16 = 0;
    crc_8 = 0;
    data_len = 0;
    recv_buffer = new uint8_t[1024];
    canvas_frame_format = MAGIC;
    is_boot_init = false;

    gear_pole_number = config->getint("gear_pole_number",5);
    motor_pole_number = config->getint("motor_pole_number",2);

    reactor= config->get_printer()->get_reactor();
}

CanvasProtocol::~CanvasProtocol()
{

}


void CanvasProtocol::on_compile_data(uint8_t data)
{
    switch (canvas_frame_format)
    {
    case MAGIC:
        if(data == 0x3D)
        {
            recv_buffer[recv_length] = data;
            recv_length++;
            canvas_frame_format = FLAG;
        }
        else
        {
            recv_length = 0;
        }
        break;
    case FLAG:
        if(data == 0x00)
        {
            recv_buffer[recv_length] = data;
            recv_length++;
            canvas_frame_format = SEQ_H;
        }
        else if(data == 0x80)
        {
            recv_buffer[recv_length] = data;
            recv_length++;
            canvas_frame_format = SHORT_LEN;
        }
        else
        {
            recv_length = 0;
            canvas_frame_format = MAGIC;
        }
        break;
    case SEQ_H:
        recv_buffer[recv_length] = data;
        recv_length++;
        canvas_frame_format = SEQ_L;
        break;
    case SEQ_L:
        recv_buffer[recv_length] = data;
        recv_length++;
        canvas_frame_format = LONG_LEN_H;
        break;
    case SHORT_LEN:
        recv_buffer[recv_length] = data;
        recv_length++;
        canvas_frame_format = CRC_8;
        data_len = data;
        break;
    case LONG_LEN_H:
        recv_buffer[recv_length] = data;
        recv_length++;
        canvas_frame_format = LONG_LEN_L;
        break;
    case LONG_LEN_L:
        recv_buffer[recv_length] = data;
        recv_length++;
        data_len = (recv_buffer[4]&0xFF) << 8 | (recv_buffer[5]&0xFF);
        canvas_frame_format = CRC_8;
        break;
    case CRC_8:
        recv_buffer[recv_length] = data;
        if(get_crc8(recv_buffer,0,recv_length) == data)
        {
            recv_length++;
            canvas_frame_format = DATA;
        }
        else
        {
            recv_length = 0;
            canvas_frame_format = MAGIC;
        }
        break;
    case DATA:
        recv_buffer[recv_length] = data;
        recv_length++;
        if(recv_length >= data_len)
        {
            uint16_t crc_received = ((recv_buffer[recv_length - 2]&0xFF) << 8) | (recv_buffer[recv_length - 1]&0xFF);
            uint16_t crc_calculated = get_crc16(recv_buffer, 0, recv_length - 2);
            if (crc_received == crc_calculated)
            {
                set_receive_data(recv_buffer, data_len);
            }
            recv_length = 0;
            canvas_frame_format = MAGIC;
        }
        break;
    default:
        recv_length = 0;
        canvas_frame_format = MAGIC;
        break;
    }
}

void CanvasProtocol::on_decompile_data(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> frame_data = pack_short_frame(data);
    set_transmit_data(frame_data.data(),frame_data.size());
}

void CanvasProtocol::on_parse_data()
{
    std::vector<std::vector<uint8_t>> all_data = get_receive_data();
    for (std::size_t i = 0; i < all_data.size(); ++i)
    {
        const std::vector<uint8_t>& frame_data = all_data[i];
        std::vector<uint8_t> valid_data;
        if(frame_data[1] == 0x00)
        {
            valid_data.assign(frame_data.begin() + 7, frame_data.end() - 2);
        }
        else if(frame_data[1] == 0x80)
        {
            valid_data.assign(frame_data.begin() + 4, frame_data.end() - 2);
        }

        if(!valid_data.empty())
        {
            dispatch_command(valid_data);
        }
    }
}

CanvasStatus& CanvasProtocol::get_canvas_status()
{
    return canvas_status;
}

bool CanvasProtocol::read_software_version(bool is_block)
{
    std::vector<uint8_t> data;
    data.push_back(CANVAS_DEVICE_ID);
    data.push_back(static_cast<uint8_t>(CanvasCmd::SOFTWARE_VERSION));

    std::unique_lock<std::mutex> lock(mutex_cv);
    read_software_version_block = is_block;
    on_decompile_data(data);

    if(is_block)
    {
        return cv.wait_for(lock, std::chrono::milliseconds(200),
            [this] { return !read_software_version_block; });
    }

    return true;
}

bool CanvasProtocol::read_rfid_data(bool is_block)
{
    std::vector<uint8_t> data;
    data.push_back(CANVAS_DEVICE_ID);
    data.push_back(static_cast<uint8_t>(CanvasCmd::RFID_DATA));

    std::unique_lock<std::mutex> lock(mutex_cv);
    read_rfid_data_block = is_block;
    on_decompile_data(data);

    if(is_block)
    {
        return cv.wait_for(lock, std::chrono::milliseconds(200),
            [this] { return !read_rfid_data_block; });
    }

    return true;
}

bool CanvasProtocol::read_feeders_status(bool is_block)
{
    std::vector<uint8_t> data;
    data.push_back(CANVAS_DEVICE_ID);
    data.push_back(static_cast<uint8_t>(CanvasCmd::ALL_STATUS));

    std::unique_lock<std::mutex> lock(mutex_cv);
    read_feeders_status_block = is_block;
    on_decompile_data(data);

    if(is_block)
    {
        return cv.wait_for(lock, std::chrono::milliseconds(200),
            [this] { return !read_feeders_status_block; });
    }

    return true;
}

bool CanvasProtocol::feeders_leds_control(const std::array<FeederLed, CHANNEL_NUMBER>& feeders_leds, bool is_block)
{
    std::vector<uint8_t> data;
    data.push_back(CANVAS_DEVICE_ID);
    data.push_back(static_cast<uint8_t>(CanvasCmd::LED_CONTROL));
    uint8_t enable_control = 0;
    for (std::size_t i = 0; i < feeders_leds.size(); i++)
    {
        if (feeders_leds[i].enable_control)
        {
            enable_control |= (1 << i);
        }
    }

    data.push_back(enable_control);

    for (std::size_t i = 0; i < feeders_leds.size(); i++)
    {
        data.push_back(static_cast<uint8_t>(feeders_leds[i].red));
        data.push_back(static_cast<uint8_t>(feeders_leds[i].blue));
    }

    std::unique_lock<std::mutex> lock(mutex_cv);
    feeders_leds_control_block = is_block;
    on_decompile_data(data);

    if(is_block)
    {
        return cv.wait_for(lock, std::chrono::milliseconds(200),
            [this] { return !feeders_leds_control_block; });
    }

    return true;
}

bool CanvasProtocol::beeper_control(uint8_t times, uint16_t duration_ms, bool is_block)
{
    std::vector<uint8_t> data;
    data.push_back(CANVAS_DEVICE_ID);
    data.push_back(static_cast<uint8_t>(CanvasCmd::BEEPER_CONTROL));
    data.push_back(times);
    data.push_back((duration_ms >> 8) & 0xFF);
    data.push_back((duration_ms >> 0) & 0xFF);

    std::unique_lock<std::mutex> lock(mutex_cv);
    beeper_control_block = is_block;
    on_decompile_data(data);

    if(is_block)
    {
        return cv.wait_for(lock, std::chrono::milliseconds(200),
            [this] { return !beeper_control_block; });
    }

    return true;
}

bool CanvasProtocol::feeders_filament_stop(uint8_t chs, bool is_block)
{
    if (chs >= 4)
    {
        return false;
    }

    std::vector<uint8_t> data;
    data.push_back(CANVAS_DEVICE_ID);
    data.push_back(static_cast<uint8_t>(CanvasCmd::FEEDER_STOP));
    uint8_t mask = 1 << chs;
    data.push_back(mask);

    std::unique_lock<std::mutex> lock(mutex_cv);
    feeders_filament_stop_block = is_block;
    on_decompile_data(data);

    if(is_block)
    {
        return cv.wait_for(lock, std::chrono::milliseconds(200),
            [this] { return !feeders_filament_stop_block; });
    }

    lock.unlock();

    //无奈之举
    double cur_time = get_monotonic();
    double end_time = cur_time + 3.0;

    while(cur_time < end_time)
    {
        cur_time = get_monotonic() + 0.2;
        reactor->pause(cur_time);

        if(!canvas_status.mcu_status.dragging[chs])
        {
            break;
        }
    }

    if(cur_time >= end_time)
    {
        SPDLOG_ERROR("Execute stop move action timeout...., channel {}", chs);
    }

    return true;
}

bool CanvasProtocol::feeder_filament_control(uint8_t ch, const FeederMotor& feeder_motor, bool is_block)
{
    if (ch < 0 || ch > 3)
    {
        return false;
    }

    int16_t len = feeder_motor.mm;
    int16_t spd = std::abs(feeder_motor.mm_s);
    int16_t acc = std::abs(feeder_motor.mm_ss);

    std::vector<uint8_t> data;
    data.push_back(CANVAS_DEVICE_ID);
    data.push_back(static_cast<uint8_t>(CanvasCmd::FILA_CTRL_SINGLE));
    data.push_back(ch+1);
    data.push_back((len >> 8) & 0xFF);
    data.push_back((len >> 0) & 0xFF);
    data.push_back((spd >> 8) & 0xFF);
    data.push_back((spd >> 0) & 0xFF);
    data.push_back((acc >> 8) & 0xFF);
    data.push_back((acc >> 0) & 0xFF);

    std::unique_lock<std::mutex> lock(mutex_cv);
    feeder_filament_control_block = is_block;
    on_decompile_data(data);

    if(is_block)
    {
        return cv.wait_for(lock, std::chrono::milliseconds(200),
            [this] { return !feeder_filament_control_block; });
    }
    // SPDLOG_INFO("feeder_filament_control channel {}",ch);
    return true;
}

bool CanvasProtocol::feeders_filament_control(const std::array<FeederMotor, CHANNEL_NUMBER>& feeders_motor, bool is_block)
{
    std::vector<uint8_t> data;
    data.push_back(CANVAS_DEVICE_ID);
    data.push_back(static_cast<uint8_t>(CanvasCmd::FILA_CTRL));

    for (std::size_t i = 0; i < CHANNEL_NUMBER; i++) {
        int16_t len = feeders_motor[i].mm;
        int16_t spd = std::abs(feeders_motor[i].mm_s);
        int16_t acc = std::abs(feeders_motor[i].mm_ss);

        data.push_back((len >> 8) & 0xFF);
        data.push_back(len & 0xFF);
        data.push_back((spd >> 8) & 0xFF);
        data.push_back(spd & 0xFF);
        data.push_back((acc >> 8) & 0xFF);
        data.push_back(acc & 0xFF);
    }

    std::unique_lock<std::mutex> lock(mutex_cv);
    feeders_filament_control_block = is_block;
    on_decompile_data(data);

    if(is_block)
    {
        return cv.wait_for(lock, std::chrono::milliseconds(200),
            [this] { return !feeders_filament_control_block; });
    }

    return true;
}

bool CanvasProtocol::rocker_control(uint8_t ch, int8_t dir, bool is_block)
{
    std::vector<uint8_t> data;
    data.push_back(CANVAS_DEVICE_ID);
    data.push_back(static_cast<uint8_t>(CanvasCmd::ROCKER_CTRL));
    data.push_back(ch+1);     // ch
    data.push_back(dir);    // dir

    std::unique_lock<std::mutex> lock(mutex_cv);
    rocker_control_block = is_block;
    on_decompile_data(data);

    if(is_block)
    {
        return cv.wait_for(lock, std::chrono::milliseconds(200),
            [this] { return !rocker_control_block; });
    }
    // SPDLOG_INFO("feeder_filament_control channel {}",ch);
    return true;
}


// 查表法 CRC-8 计算函数 时间复杂度 O(n)
uint8_t CanvasProtocol::get_crc8(const uint8_t* data, int start, int length)
{
    uint8_t crc = CRC8_INIT;
    for (size_t i = start; i < length; ++i)
    {
        crc = crc8_table[crc ^ data[i]];
    }
    return crc;
}

uint16_t CanvasProtocol::get_crc16(const uint8_t* data, int start, int length)
{
    uint16_t crc = CRC16_INIT; // 初始值
    for (size_t i = start; i < length; i++)
    {
        uint8_t tbl_idx = (crc >> 8) ^ data[i];   // 高8位异或数据
        crc = (crc << 8) ^ crc16_table[tbl_idx];
    }
    return crc;
}

std::string CanvasProtocol::uint32_to_hex_string(uint32_t value,std::string hex_prefix,char ch,int32_t hex_w,bool is_uppercase)
{
    std::stringstream ss;
    if(!hex_prefix.empty())
    {
        ss << hex_prefix;               // 前缀
    }

    if(is_uppercase)
    {
        ss << std::uppercase;           // 输出大写
    }
    else
    {
        ss << std::nouppercase;           // 输出小写
    }

    ss << std::hex                      // 切换到16进制模式
    << std::setfill(ch)                 // 填充字符设为 ch
    << std::setw(hex_w)                 // 固定输出宽度为 hex_w
    << value;

    return ss.str();
}

void CanvasProtocol::set_receive_data(const uint8_t* data, int length)
{
    std::lock_guard<std::mutex> lock(mutex_receive);
    std::vector<uint8_t> frame(data, data + length);
    receive_buffers.push_back(frame);
}

std::vector<std::vector<uint8_t>> CanvasProtocol::get_receive_data()
{
    std::lock_guard<std::mutex> lock(mutex_receive);

    std::vector<std::vector<uint8_t>> all_data;
    while (!receive_buffers.empty())
    {
        all_data.push_back(std::move(receive_buffers.front()));
        receive_buffers.pop_front();
    }
    return all_data;
}

void CanvasProtocol::set_transmit_data(const uint8_t* data, int length)
{
    std::lock_guard<std::mutex> lock(mutex_transmit);
    std::vector<uint8_t> frame(data, data + length);
    transmit_buffers.push_back(frame);
}

std::vector<uint8_t> CanvasProtocol::get_transmit_data()
{
    std::lock_guard<std::mutex> lock(mutex_transmit);
    if (!transmit_buffers.empty())
    {
        std::vector<uint8_t> frame = transmit_buffers.front();
        transmit_buffers.pop_front();
        return frame;
    }
    return std::vector<uint8_t>(); // 空表示无数据
}


void CanvasProtocol::dispatch_command(const std::vector<uint8_t>& valid_data)
{
    switch (static_cast<CanvasCmd>(valid_data[1]))
    {
        case CanvasCmd::CONNECT_STATUS:
            PARSE_CONNECT_STATUS(valid_data);
            break;
        case CanvasCmd::HARDWARE_VERSION:
            PARSE_HARDWARE_VERSION(valid_data);
            break;
        case CanvasCmd::SOFTWARE_VERSION:
            PARSE_SOFTWARE_VERSION(valid_data);
            break;
        case CanvasCmd::FILAMENT_PLUG_IN:
            PARSE_FILAMENT_PLUG_IN(valid_data);
            break;
        case CanvasCmd::ODO_VALUE:
            PARSE_ODO_VALUE(valid_data);
            break;
        case CanvasCmd::ENCODER_STATUS:
            PARSE_ENCODER_STATUS(valid_data);
            break;
        case CanvasCmd::MOTOR_STATUS:
            PARSE_MOTOR_STATUS(valid_data);
            break;
        case CanvasCmd::RFID_DATA:
            PARSE_RFID_DATA(valid_data);
            break;
        case CanvasCmd::ALL_STATUS:
            PARSE_ALL_STATUS(valid_data);
            break;
        case CanvasCmd::LED_CONTROL:
            PARSE_LED_CONTROL(valid_data);
            break;
        case CanvasCmd::BEEPER_CONTROL:
            PARSE_BEEPER_CONTROL(valid_data);
            break;
        case CanvasCmd::FEEDER_STOP:
            PARSE_FEEDER_STOP(valid_data);
            break;
        case CanvasCmd::FILA_CTRL:
            PARSE_FILA_CTRL(valid_data);
            break;
        case CanvasCmd::FILA_CTRL_SINGLE:
            PARSE_FILA_CTRL_SINGLE(valid_data);
            break;
        case CanvasCmd::FILA_CTRL_REACH_ACK:
            PARSE_FILA_CTRL_REACH_ACK(valid_data);
            break;
        case CanvasCmd::FILA_CTRL_BOTH_ACK:
            PARSE_FILA_CTRL_BOTH_ACK(valid_data);
            break;
        case CanvasCmd::ROCKER_CTRL:
            PARSE_ROCKER_CTRL_ACK(valid_data);
            break;
        default:
            break;
    }
}

std::vector<uint8_t> CanvasProtocol::pack_short_frame(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> frame;
    uint8_t frame_length = data.size() + 6;
    // 帧头 + 帧标志 + 长度 + CRC8 + 负载数据 + CRC16

    frame.push_back(0x3D);
    frame.push_back(0x80);
    frame.push_back(frame_length);
    frame.push_back(get_crc8(frame.data(), 0, 3));
    frame.insert(frame.end(), data.begin(), data.end());
    uint16_t crc = get_crc16(frame.data(), 0, frame.size());
    frame.push_back((crc >> 8) & 0xFF);
    frame.push_back((crc >> 0) & 0xFF);

    return frame;
}

void CanvasProtocol::PARSE_CONNECT_STATUS(const std::vector<uint8_t>& valid_data)
{
    //无处理
}

void CanvasProtocol::PARSE_HARDWARE_VERSION(const std::vector<uint8_t>& valid_data)
{
    //无处理
}

void CanvasProtocol::PARSE_SOFTWARE_VERSION(const std::vector<uint8_t>& valid_data)
{
    if (valid_data.size() >= 13)
    {
        std::string str(valid_data.begin() + 2, valid_data.begin() + 2 + 11);
        canvas_status.mcu_status.software_version = str;
    }
    SPDLOG_INFO("canvas version: {}", canvas_status.mcu_status.software_version);
    std::lock_guard<std::mutex> lock(mutex_cv);
    if(read_software_version_block)
    {
        read_software_version_block = false;
        cv.notify_all();
    }
}

void CanvasProtocol::PARSE_FILAMENT_PLUG_IN(const std::vector<uint8_t>& valid_data)
{
    //无处理
}

void CanvasProtocol::PARSE_ODO_VALUE(const std::vector<uint8_t>& valid_data)
{
    //无处理
}

void CanvasProtocol::PARSE_ENCODER_STATUS(const std::vector<uint8_t>& valid_data)
{
    //无处理
}

void CanvasProtocol::PARSE_MOTOR_STATUS(const std::vector<uint8_t>& valid_data)
{
    //无处理
}

void CanvasProtocol::PARSE_RFID_DATA(const std::vector<uint8_t>& valid_data)
{
    if (valid_data.size() >= 3 + 8 + 180 && valid_data[2])
    {
        if (valid_data.size() < 3 + 8 + 180)
        {
            return;
        }

        const uint8_t* ptr = valid_data.data() + 3;

        RFIDData data;
        for (int i = 0; i < 8; i++) {
            data.uid |= static_cast<uint64_t>(ptr[i]) << (8 * i);
        }
        ptr += 8;

        std::memcpy(data.data, ptr, sizeof(data.data));
        std::memcpy(&data.rfid_filament_data, data.data + 16, 32);
        // SPDLOG_INFO("RFID Tag Data:\n{}", output);
        data.filament_info.brand = uint32_to_hex_string(be32toh(data.rfid_filament_data.brand),"0x");
        if("0xEEEEEEEE" == data.filament_info.brand)
        {
            data.filament_info.manufacturer = "ELEGOO";
        }
        data.filament_info.code = uint32_to_hex_string(be16toh(data.rfid_filament_data.code),"0x",'0',4);
        data.filament_info.type = uint32_to_hex_string(be32toh(data.rfid_filament_data.type),"0x");
        data.filament_info.type = filament_type.find(be32toh(data.rfid_filament_data.type))->second;
        data.filament_info.detailed_type = uint32_to_hex_string(be16toh(data.rfid_filament_data.name),"0x",'0',4);
        SPDLOG_INFO("filament_info.detailed_code:{}",data.filament_info.detailed_type);
        data.filament_info.detailed_type = filament_detail_type.find(be16toh(data.rfid_filament_data.name))->second;
        SPDLOG_INFO("filament_info.detailed_type:{}",data.filament_info.detailed_type);
        data.filament_info.color = uint32_to_hex_string(data.rfid_filament_data.rgb[0],"0x",'0',2) + uint32_to_hex_string(data.rfid_filament_data.rgb[1],"",'0',2) + uint32_to_hex_string(data.rfid_filament_data.rgb[2],"",'0',2);
        data.filament_info.diameter = std::to_string(be16toh(data.rfid_filament_data.diameter)/100.0);
        data.filament_info.weight = std::to_string(be16toh(data.rfid_filament_data.weight));
        data.filament_info.date = uint32_to_hex_string((be16toh(data.rfid_filament_data.date) >> 8) & 0xFF,"20",'0',2) + "/" + uint32_to_hex_string(be16toh(data.rfid_filament_data.date) & 0x00FF,"",'0',2);
        data.filament_info.nozzle_min_temp = std::to_string(be16toh(data.rfid_filament_data.low_tmp));
        data.filament_info.nozzle_max_temp = std::to_string(be16toh(data.rfid_filament_data.hig_tmp));

        canvas_status.host_status.rfid_data = data;
        // SPDLOG_INFO("filament_info.manufacturer:{}",data.filament_info.manufacturer);
        // SPDLOG_INFO("filament_info.brand:{}",data.filament_info.brand);
        // SPDLOG_INFO("filament_info.code:{}",data.filament_info.code);
        // SPDLOG_INFO("filament_info.type:{}",data.filament_info.type);
        // SPDLOG_INFO("filament_info.detailed_type:{}",data.filament_info.detailed_type);
        // SPDLOG_INFO("filament_info.color:{}",data.filament_info.color);
        // SPDLOG_INFO("filament_info.diameter:{}",data.filament_info.diameter);
        // SPDLOG_INFO("filament_info.weight:{}",data.filament_info.weight);
        // SPDLOG_INFO("filament_info.date:{}",data.filament_info.date);
        // SPDLOG_INFO("filament_info.nozzle_min_temp:{}",data.filament_info.nozzle_min_temp);
        // SPDLOG_INFO("filament_info.nozzle_max_temp:{}",data.filament_info.nozzle_max_temp);
    }

    std::lock_guard<std::mutex> lock(mutex_cv);
    if(read_rfid_data_block)
    {
        read_rfid_data_block = false;
        cv.notify_all();
    }
}

void CanvasProtocol::PARSE_ALL_STATUS(const std::vector<uint8_t>& valid_data)
{
    constexpr std::size_t FEEDER_DATA_SIZE = 9;

    canvas_status.mcu_status.status = valid_data[2];

    std::size_t index = 3; // ID, Command
    for (std::size_t i = 0; i < CHANNEL_NUMBER; i++, index += FEEDER_DATA_SIZE)
    {
        canvas_status.mcu_status.feeders_status[i] = valid_data[index + 0];
        canvas_status.mcu_status.ch_position[i] = (valid_data[index + 1] << 24 | valid_data[index + 2] << 16 |
            valid_data[index + 3] << 8 | valid_data[index + 4] << 0) * 9.4 * M_PI / (2. * gear_pole_number);
        canvas_status.mcu_status.ch_fila_move_dist[i] = (valid_data[index + 5] << 24 | valid_data[index + 6] << 16 |
            valid_data[index + 7] << 7 | valid_data[index + 8] << 0) * 9.4 * M_PI / (2. * motor_pole_number);
        canvas_status.mcu_status.fila_abnormal[i] = (canvas_status.mcu_status.feeders_status[i] >> 7) & 0x01;
        canvas_status.mcu_status.dragging[i] = (canvas_status.mcu_status.feeders_status[i] >> 6) & 0x01;
        canvas_status.mcu_status.drag_compensate_error[i] = (canvas_status.mcu_status.feeders_status[i] >> 4) & 0x01;
        canvas_status.mcu_status.motor_slip[i] = (canvas_status.mcu_status.feeders_status[i] >> 3) & 0x01;
        canvas_status.mcu_status.motor_crtl_err[i] = (canvas_status.mcu_status.feeders_status[i] >> 2) & 0x01;
        canvas_status.mcu_status.fila_in[i] = (canvas_status.mcu_status.feeders_status[i] >> 1) & 0x01;
        canvas_status.mcu_status.motor_fault[i] = (canvas_status.mcu_status.feeders_status[i] >> 0) & 0x01;
    }

    if(!is_boot_init)
    {
        for(size_t i = 0; i < CHANNEL_NUMBER; i++)
        {
            canvas_status.host_status.canvas_lite.status[i] = canvas_status.mcu_status.fila_in[i];
        }
        is_boot_init = true;
    }

    std::lock_guard<std::mutex> lock(mutex_cv);
    if(read_feeders_status_block)
    {
        read_feeders_status_block = false;
        cv.notify_all();
    }
}

void CanvasProtocol::PARSE_LED_CONTROL(const std::vector<uint8_t>& valid_data)
{
    //无处理
}

void CanvasProtocol::PARSE_BEEPER_CONTROL(const std::vector<uint8_t>& valid_data)
{
    //无处理
}

void CanvasProtocol::PARSE_FEEDER_STOP(const std::vector<uint8_t>& valid_data)
{
    //无处理
}

void CanvasProtocol::PARSE_FILA_CTRL(const std::vector<uint8_t>& valid_data)
{
    //无处理
}

void CanvasProtocol::PARSE_FILA_CTRL_SINGLE(const std::vector<uint8_t>& valid_data)
{
    //无处理
}

void CanvasProtocol::PARSE_FILA_CTRL_REACH_ACK(const std::vector<uint8_t>& valid_data)
{
    //无处理
}

void CanvasProtocol::PARSE_FILA_CTRL_BOTH_ACK(const std::vector<uint8_t>& valid_data)
{
    //无处理
}

void CanvasProtocol::PARSE_ROCKER_CTRL_ACK(const std::vector<uint8_t>& valid_data)
{
    //无处理
}

CanvasComm::CanvasComm(std::string port, int baudrate) : port(port), baudrate(baudrate)
{
    serial_port = std::make_shared<SerialPort>(port, baudrate);
    buffer_length = 1024;
    recv_buffer = new uint8_t[buffer_length];
}

CanvasComm::~CanvasComm()
{
    if(receive_thread)
    {
        delete receive_thread;
        receive_thread = nullptr;
    }

    if(transmit_thread)
    {
        delete transmit_thread;
        transmit_thread = nullptr;
    }

    if(analysis_thread)
    {
        delete analysis_thread;
        analysis_thread = nullptr;
    }

    delete[] recv_buffer;
    recv_buffer = nullptr;
}

void CanvasComm::activate(std::shared_ptr<CanvasProtocol> canvas_protocol)
{
    this->canvas_protocol = canvas_protocol;
    connect();
    start_receive();
    start_transmit();
    start_analysis();
}

void CanvasComm::stop()
{
   run_receive = false;
   run_transmit = false;
   run_analysis = false;
}

void CanvasComm::connect()
{
    if (serial_port->open())
    {
        SPDLOG_INFO("Canvas serial connection successful");
    }
    else
    {
        SPDLOG_ERROR("Canvas serial connection fail");
    }
}

void CanvasComm::disconnect()
{
    serial_port->close();
    SPDLOG_WARN("Canvas serial port connection lost");
}

void CanvasComm::start_receive()
{
    run_receive = true;
    receive_thread = new std::thread(&CanvasComm::on_receive, this);
    receive_thread->detach();
}

void CanvasComm::start_transmit()
{
    run_transmit = true;
    transmit_thread = new std::thread(&CanvasComm::on_transmit, this);
    transmit_thread->detach();
}

void CanvasComm::start_analysis()
{
    run_analysis = true;
    analysis_thread = new std::thread(&CanvasComm::on_analysis, this);
    analysis_thread->detach();
}

void CanvasComm::on_receive()
{
    while(run_receive)
    {
        if(!canvas_protocol->get_canvas_status().host_status.updating)
        {
            ssize_t recv_length = serial_port->read(recv_buffer, buffer_length);
            if(recv_length > 0)
            {
                for (int i = 0; i < recv_length; i++)
                {
                    // SPDLOG_ERROR("recv_length[{}] = 0x{:02X}", i, recv_buffer[i]);
                    canvas_protocol->on_compile_data(recv_buffer[i]);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    delete receive_thread;
    receive_thread = nullptr;
}

void CanvasComm::on_transmit()
{
    while(run_transmit)
    {
        if(!canvas_protocol->get_canvas_status().host_status.updating)
        {
            std::vector<uint8_t> tx_data = canvas_protocol->get_transmit_data();

            if (!tx_data.empty())
            {
                // for (size_t i = 0; i < tx_data.size(); ++i)
                // {
                //     printf("0x%02X ", tx_data[i]);
                // }
                // printf("\n");
                ssize_t ret = serial_port->write(tx_data.data(), tx_data.size());
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    delete transmit_thread;
    transmit_thread = nullptr;
}

void CanvasComm::on_analysis()
{
    while(run_analysis)
    {
        canvas_protocol->on_parse_data();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    delete analysis_thread;
    analysis_thread = nullptr;
}


Canvas::Canvas(std::shared_ptr<ConfigWrapper> config)
{
    this->config = config;
    this->printer = config->get_printer();
    this->reactor = printer->get_reactor();
    record_file = "/opt/usr/canvas_record_info.json";
    timeout_count = 0;
    detected_rfid = false;
    is_report = false;
    print_state = "standby";
    canvas_version = "";
    firmware_dir = "/opt/usr/ota";
    initial_boot = true;
    save_filament_info = false;
    is_cancel = false;
    exit_retry = false;
    exec_retry = false;
    last_extruder_pos = -1;
    last_channel_pos = -1;
    last_check_channel = -1;

    elegoo::common::SignalManager::get_instance().register_signal(
        "elegoo:connect",
        std::function<void()>([this]() {
            handle_connect();
        })
    );
    elegoo::common::SignalManager::get_instance().register_signal(
        "elegoo:disconnect",
        std::function<void()>([this]() {
            handle_disconnect();
        })
    );
    elegoo::common::SignalManager::get_instance().register_signal(
        "elegoo:shutdown",
        std::function<void()>([this](){
            handle_shutdown();
        })
    );
    elegoo::common::SignalManager::get_instance().register_signal(
        "elegoo:ready",std::function<void()>([this](){
            handle_ready();
        })
    );
    // elegoo::common::SignalManager::get_instance().register_signal(
    //     "elegoo:shutdown",
    //     std::function<void()>([this](){
    //         save_record_info();
    //     })
    // );
    // elegoo::common::SignalManager::get_instance().register_signal(
    //     "por:power_off",
    //     std::function<void()>([this]() {
    //         save_record_info();
    //     })
    // );

    elegoo::common::SignalManager::get_instance().register_signal(
        "canvas:auto_refill",
        std::function<void()>([this](){
            auto_refill_filament();
        })
    );
    buttons = any_cast<std::shared_ptr<PrinterButtons>>(printer->load_object(config, "buttons"));
    std::string filament_det_pin = config->get("filament_det_pin");
    std::string nozzle_fan_off_pin = config->get("nozzle_fan_off_pin");
    std::string cutting_knife_pin = config->get("cutting_knife_pin");
    std::string wrap_filament_pin = config->get("wrap_filament_pin");
    std::string model_det_pin = config->get("model_det_pin");

    motor_speed = config->getdouble("motor_speed",100,10,1000);
    pre_load_motor_speed = config->getdouble("pre_load_motor_speed",100,10,1000);
    motor_accel = config->getdouble("motor_accel",100,10,10000);
    pre_load_fila_dist = config->getdouble("pre_load_fila_dist",600,0,1000);
    pre_load_fila_time = config->getdouble("pre_load_fila_time",10,0,60);
    load_fila_dist = config->getdouble("load_fila_dist",50,0,1300);
    load_wait_time = config->getdouble("load_wait_time",1.);
    load_fila_det_count = config->getdouble("load_fila_det_count",2.);
    load_fila_det_dist = config->getdouble("load_fila_det_dist",5.);
    load_fila_time = config->getdouble("load_fila_time",3,0,60);
    unload_fila_dist = config->getdouble("unload_fila_dist",80,60,350);
    unload_fila_time = config->getdouble("unload_fila_time",3,0,60);
    fila_det_dist = config->getdouble("fila_det_dist",30,0,1000);
    fila_det_time = config->getdouble("fila_det_time",3,0,60);
    mesh_gear_dist = config->getdouble("mesh_gear_dist",10,0,1000);
    mesh_gear_time = config->getdouble("mesh_gear_time",5,0,60);
    min_check_distance_1 = config->getdouble("min_check_distance_1",10,0,200);
    max_check_distance_1 = config->getdouble("max_check_distance_1",60,0,200);
    min_check_distance_2 = config->getdouble("min_check_distance_2",5,0,200);
    max_check_distance_2 = config->getdouble("max_check_distance_2",30,0,200);
    //
    next_extruder = limit_check(config->getint("T",0),0,31);
    need_extrude_check = limit_check(config->getint("A",0),0,1);
    flush_length_single = limit_check(config->getdouble("K",80),10,120);
    flush_length = limit_check(config->getdouble("L",80),10,1000);
    old_filament_e_feedrate = limit_check(config->getdouble("M",300),10,600);
    new_filament_e_feedrate = limit_check(config->getdouble("N",300),10,600);
    fan_speed_1 = limit_check(config->getdouble("I",80),0,255);
    fan_speed_2 = limit_check(config->getdouble("J",200),0,255);
    cool_time = limit_check(config->getdouble("P",3000),0,20000);
    old_filament_temp = limit_check(config->getdouble("Q",250),200,350);
    new_filament_temp = limit_check(config->getdouble("S",250),200,350);
    double r_offset = limit_check(config->getdouble("r_offset",20),0,200);
    nozzle_temperature_range_high = limit_check(new_filament_temp + r_offset, 200,350);
    old_retract_length_toolchange = limit_check(config->getdouble("C",2),0,10);
    new_retract_length_toolchange = limit_check(config->getdouble("D",2),0,10);
    printable_height = config->getdouble("printable_height",256);
    SPDLOG_INFO("{} printable_height:{} T{} A{} K{} L{} M{} N{} I{} J{} P{} Q{} S{} R{} C{} D{}",__func__,printable_height,next_extruder,need_extrude_check,flush_length_single,flush_length,old_filament_e_feedrate,new_filament_e_feedrate,fan_speed_1,fan_speed_2,cool_time,old_filament_temp,new_filament_temp,nozzle_temperature_range_high,old_retract_length_toolchange,new_retract_length_toolchange);
    //
    z_up_height = limit_check(config->getdouble("z_up_height",3),0,10);
    e_flush_dist = limit_check(config->getdouble("e_flush_dist",23),0,100);
    e_flush_ex_dist = limit_check(config->getdouble("e_flush_ex_dist",6),0,50);
    flush_accel = limit_check(config->getdouble("flush_accel",18999),1000,20000);
    x_flush_cool_pos = limit_check(config->getdouble("x_flush_cool_pos",56),0,1000);
    x_flush_pos1 = limit_check(config->getdouble("x_flush_pos1",54),0,1000);
    x_flush_pos2 = limit_check(config->getdouble("x_flush_pos2",56),0,1000);
    x_flush_pos3 = limit_check(config->getdouble("x_flush_pos3",64),0,1000);
    x_flush_pos4 = limit_check(config->getdouble("x_flush_pos4",80),0,1000);
    y_flush_pos1 = limit_check(config->getdouble("y_flush_pos1",261.1),0,1000);
    y_flush_pos2 = limit_check(config->getdouble("y_flush_pos2",264.1),0,1000);
    SPDLOG_INFO("{} z_up_height:{} e_flush_dist {} e_flush_ex_dist {}  flush_accel {} x_flush_cool_pos {} x_flush_pos1 {} x_flush_pos2 {} x_flush_pos3 {} x_flush_pos4 {} y_flush_pos1 {} y_flush_pos2 {}",
        __func__,z_up_height,e_flush_dist,e_flush_ex_dist,flush_accel,x_flush_cool_pos,
        x_flush_pos1,x_flush_pos2,x_flush_pos3,x_flush_pos4,y_flush_pos1,
        y_flush_pos2);

    canvas_comm = std::make_shared<CanvasComm>(config->get("serial"), config->getint("baud", 250000, 2400));
    canvas_protocol = std::make_shared<CanvasProtocol>(config);

    elegoo::common::SignalManager::get_instance().register_signal(
        "elegoo:filament_det",
        std::function<void(bool)>([this](bool state){
            filament_det_handler(state);
        })
    );

    // elegoo::common::SignalManager::get_instance().register_signal(
    //     "elegoo:cover_off",
    //     std::function<void(bool)>([this](bool state){
    //         nozzle_fan_off_handler(state);
    //     })
    // );

    // elegoo::common::SignalManager::get_instance().register_signal(
    //     "elegoo:wrap_det",
    //     std::function<void(bool)>([this](bool state){
    //         wrap_filament_handler(state);
    //     })
    // );

    elegoo::common::SignalManager::get_instance().register_signal(
        "elegoo:print_stats",
        std::function<void(std::string)>([this](std::string print_state) {
            handle_print_state(print_state);
        })
    );


    elegoo::common::SignalManager::get_instance().register_signal(
        "gcode:CANCEL_PRINT_START",
        std::function<void()>([this]() {
            is_cancel = true;
        })
    );

    buttons->register_buttons({cutting_knife_pin}
        , [this](double eventtime, bool state) {
            return this->cutting_knife_handler(eventtime, state);
        }
    );

    buttons->register_buttons({model_det_pin}
        , [this](double eventtime, bool state) {
            return this->model_det_handler(eventtime, state);
        }
    );

}


Canvas::~Canvas()
{

}


void Canvas::handle_connect()
{

}

void Canvas::handle_disconnect()
{

}

void Canvas::handle_ready()
{
    auto ppins = any_cast<std::shared_ptr<PrinterPins>>(config->get_printer()->lookup_object("pins"));
    std::shared_ptr<PinParams> pin_params = ppins->lookup_pin(config->get("canvas_ctrl_pin"), true, false);

    // if (*pin_params->chip_name == "host")
    // {
    //     std::shared_ptr<MCU_host_digital_pin> canvas_ctrl_pin =
    //         std::static_pointer_cast<MCU_host_digital_pin>(pin_params->chip->setup_pin("host_digital_pin", pin_params));
    //     // 设置输出
    //     canvas_ctrl_pin->set_direction(0);
    //     // 设置高电平
    //     canvas_ctrl_pin->set_digital(1);
    // }

    webhooks = any_cast<std::shared_ptr<WebHooks>>(printer->lookup_object("webhooks"));
    pause_resume = any_cast<std::shared_ptr<PauseResume>>(printer->load_object(config, "pause_resume"));
    print_stats = any_cast<std::shared_ptr<PrintStats>>(printer->load_object(config, "print_stats"));
    v_sd = any_cast<std::shared_ptr<VirtualSD>>(printer->lookup_object("virtual_sdcard"));
    gcode_move = any_cast<std::shared_ptr<GCodeMove>>(printer->load_object(config, "gcode_move"));
    toolhead = any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));
    extruder = any_cast<std::shared_ptr<PrinterExtruder>>(printer->lookup_object("extruder"));
    std::shared_ptr<MCU> mcu = any_cast<std::shared_ptr<MCU>>(printer->lookup_object("mcu"));
    estimated_print_time = std::bind(&MCU::estimated_print_time, mcu, std::placeholders::_1);

    std::shared_ptr<PrinterGCodeMacro> gcode_macro =
        any_cast<std::shared_ptr<PrinterGCodeMacro>>(printer->load_object(config, "gcode_macro"));

    runout_filament_gcode = gcode_macro->load_template(config, "runout_filament_gcode", "\n");
    move_to_cutting_knift_macro = gcode_macro->load_template(config, "move_to_cutting_knift_macro", "\n");
    move_to_cutting_knift_retry_macro = gcode_macro->load_template(config, "move_to_cutting_knift_retry_macro", "\n");
    release_cutting_knift_macro = gcode_macro->load_template(config, "release_cutting_knift_macro", "\n");
    release_cutting_knift_retry_macro = gcode_macro->load_template(config, "release_cutting_knift_retry_macro", "\n");
    move_to_waste_box_macro = gcode_macro->load_template(config, "move_to_waste_box_macro", "\n");
    clean_waste_filament_macro = gcode_macro->load_template(config, "clean_waste_filament_macro", "\n");
    filament_load_gcode_1 = gcode_macro->load_template(config, "filament_load_gcode_1", "\n");
    filament_load_gcode_2 = gcode_macro->load_template(config, "filament_load_gcode_2", "\n");
    load_fila_ex_gcode_1 = gcode_macro->load_template(config, "load_fila_ex_gcode_1", "\n");
    load_fila_ex_gcode_2 = gcode_macro->load_template(config, "load_fila_ex_gcode_2", "\n");
    runout_gcode = gcode_macro->load_template(config, "runout_gcode", "");
    channel_check_gcode_1 = gcode_macro->load_template(config, "channel_check_gcode_1", "\n");
    channel_check_gcode_2 = gcode_macro->load_template(config, "channel_check_gcode_2", "\n");

    gcode = any_cast<std::shared_ptr<GCodeDispatch>>(this->printer->lookup_object("gcode"));
    gcode->register_command(
        "CANVAS_LOAD_FILAMENT"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_canvas_load_filament(gcmd);
        }
        ,false
        ,"CANVAS LOAD FILAMENT");
    gcode->register_command(
        "CANVAS_UNLOAD_FILAMENT"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_canvas_unload_filament(gcmd);
        }
        ,false
        ,"CANVAS UNLOAD FILAMENT");
    gcode->register_command(
        "PRINTER_UNLOAD_FILAMENT"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_printer_unload_filament(gcmd);
        }
        ,false
        ,"printer UNLOAD FILAMENT");
    gcode->register_command(
        "CANVAS_MOVE_TO_WASTE_BOX"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_move_to_waste_box(gcmd);
        }
        ,false
        ,"CANVAS MOVE TO WASTE BOX");
    gcode->register_command(
        "CANVAS_CUT_FILAMENT"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_canvas_cut_filament(gcmd);
        }
        ,false
        ,"CANVAS CUT FILAMENT");
    gcode->register_command(
        "CANVAS_PLUG_IN_FILAMENT"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_canvas_plug_in_filament(gcmd);
        }
        ,false
        ,"CANVAS PLUG IN FILAMENT");
    gcode->register_command(
        "CANVAS_SWITCH_FILAMENT"
        // "T"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_canvas_switch_filament(gcmd);
        }
        ,false
        ,"CANVAS SWITCH FILAMENT");
    gcode->register_command(
        "CANVAS_SET_COLOR_TABLE"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_canvas_set_color_table(gcmd);
        }
        ,false
        ,"CANVAS SET COLOR TABLE");
    gcode->register_command(
        "CANVAS_MESH_GEAR"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_canvas_channel_motor_ctrl(gcmd);
        }
        ,false
        ,"CANVAS MESH GEAR");
    gcode->register_command(
        "CANVAS_PLUG_OUT_FILAMENT"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_canvas_plug_out_filament(gcmd);
        }
        ,false
        ,"CANVAS PLUG OUT FILAMENT");
    gcode->register_command(
        "CANVAS_CLEAN_WASTE_FILAMENT"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_clean_waste_filament(gcmd);
        }
        ,false
        ,"CANVAS CLEAN WASTE FILAMENT");
    gcode->register_command(
        "CANVAS_SET_FILAMENT_INFO"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_canvas_set_filament_info(gcmd);
        }
        ,false
        ,"CANVAS SET FILAMENT INFO");
    gcode->register_command(
        "CANVAS_DETECT_FILAMENT"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_canvas_detect_filament(gcmd);
        }
        ,false
        ,"CANVAS DETECT FILAMENT");
    gcode->register_command(
        "CANVAS_ABNORMAL_RETRY"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_canvas_abnormal_retry(gcmd);
        }
        ,false
        ,"CANVAS ABNORMAL RETRY");

    gcode->register_command(
        "CANVAS_RFID_SELECT"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_canvas_rfid_select(gcmd);
        }
        ,false
        ,"CANVAS RFID SELECT");

    gcode->register_command(
        "CANVAS_SET_AUTO_PLUG_IN"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_canvas_set_auto_plug_in(gcmd);
        }
        ,false
        ,"CANVAS SET AUTO PLUG IN FILAMENT");
        
    gcode->register_command(
        "M6211"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_M6211(gcmd);
        }
        ,false
        ,"M6211");

    gcode->register_command(
        "CANVAS_MOTOR_CONTROL"
        ,[this](std::shared_ptr<GCodeCommand> gcmd){
            CMD_canvas_motor_control(gcmd);
        }
        ,false
        ,"CANVAS MOTOR CONTROL");

    rfid_report_timer = reactor->register_timer(
        [this](double eventtime)
        {
            return info_report_handler(eventtime);
        }
        , _NOW,
        "rfid report handler timer"
    );

    filament_block_check = reactor->register_timer(
        [this](double eventtime)
        {
            return filament_block_handler(eventtime);
        }
        , _NEVER,
        "filament block check timer"
    );

    read_record_info();

    canvas_comm->activate(canvas_protocol);
    canvas_update_thread = std::make_shared<std::thread>(&Canvas::canvas_controller_thread, this);
    canvas_update_thread->detach();
}

void Canvas::handle_shutdown()
{

}

void Canvas::save_record_info()
{
    json status;
    status["auto_plug_in_enable"] = canvas_protocol->get_canvas_status().host_status.auto_plug_in_enable;
    // status["fila_origin_record"] = canvas_protocol->get_canvas_status().host_status.feed_channel_current;

    for(ssize_t i = 0; i < CHANNEL_NUMBER; i++)
    {
        std::string channels_index = "channels" + std::to_string(i);
        // status[channels_index]["status"] =
        //     canvas_protocol->get_canvas_status().host_status.canvas_lite.status[i];
        status[channels_index]["manufacturer"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].manufacturer;
        status[channels_index]["brand"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].brand;
        status[channels_index]["code"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].code;
        status[channels_index]["type"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].type;
        status[channels_index]["detailed_type"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].detailed_type;
        status[channels_index]["color"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].color;
        status[channels_index]["diameter"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].diameter;
        status[channels_index]["weight"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].weight;
        status[channels_index]["date"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].date;
        status[channels_index]["nozzle_min_temp"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].nozzle_min_temp;
        status[channels_index]["nozzle_max_temp"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].nozzle_max_temp;
    }

    // status["extern_channel"]["status"] =
    //     canvas_protocol->get_canvas_status().host_status.extern_filament.status;
    status["extern_channel"]["manufacturer"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.manufacturer;
    status["extern_channel"]["brand"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.brand;
    status["extern_channel"]["code"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.code;
    status["extern_channel"]["type"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.type;
    status["extern_channel"]["detailed_type"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.detailed_type;
    status["extern_channel"]["color"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.color;
    status["extern_channel"]["diameter"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.diameter;
    status["extern_channel"]["weight"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.weight;
    status["extern_channel"]["date"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.date;
    status["extern_channel"]["nozzle_min_temp"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.nozzle_min_temp;
    status["extern_channel"]["nozzle_max_temp"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.nozzle_max_temp;


    status["color_table_size"] = channel_color_table.size();
    for(ssize_t i = 0; i < channel_color_table.size(); ++i)
    {
        status["color_table" + std::to_string(i)]["T"] = std::get<0>(channel_color_table[i]);
        status["color_table" + std::to_string(i)]["color"] = std::get<1>(channel_color_table[i]);
        status["color_table" + std::to_string(i)]["channel"] = std::get<2>(channel_color_table[i]);
    }


    std::ofstream outfile(record_file);
    if (outfile.is_open())
    {
        outfile << status.dump(4);
        outfile.flush();
        outfile.close();
        SPDLOG_INFO("save record info success");
    }
    else
    {
        SPDLOG_WARN("Unable to open file {} for writing", record_file);
    }

    save_filament_info = false;
}


void Canvas::read_record_info()
{
    std::ifstream infile(record_file);
    if (!infile.is_open())
    {
        SPDLOG_WARN("Unable to open file {}",record_file);
        return;
    }

    json jsonData;
    infile >> jsonData;
    infile.close();

    SPDLOG_INFO("jsonData:{}",jsonData.dump());

    if(jsonData.contains("auto_plug_in_enable"))
    {
        canvas_protocol->get_canvas_status().host_status.auto_plug_in_enable = jsonData["auto_plug_in_enable"].get<uint8_t>();
    }

    for(ssize_t i = 0; i < CHANNEL_NUMBER; i++)
    {
        if(jsonData.contains("channels" + std::to_string(i)))
        {
            // canvas_protocol->get_canvas_status().host_status.canvas_lite.status[i] =
            //     jsonData["channels" + std::to_string(i)]["status"].get<int32_t>();
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].manufacturer =
                jsonData["channels" + std::to_string(i)]["manufacturer"].get<std::string>();
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].brand =
                jsonData["channels" + std::to_string(i)]["brand"].get<std::string>();
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].code =
                jsonData["channels" + std::to_string(i)]["code"].get<std::string>();
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].type =
                jsonData["channels" + std::to_string(i)]["type"].get<std::string>();
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].detailed_type =
                jsonData["channels" + std::to_string(i)]["detailed_type"].get<std::string>();
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].color =
                jsonData["channels" + std::to_string(i)]["color"].get<std::string>();
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].diameter =
                jsonData["channels" + std::to_string(i)]["diameter"].get<std::string>();
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].weight =
                jsonData["channels" + std::to_string(i)]["weight"].get<std::string>();
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].date =
                jsonData["channels" + std::to_string(i)]["date"].get<std::string>();
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].nozzle_min_temp =
                jsonData["channels" + std::to_string(i)]["nozzle_min_temp"].get<std::string>();
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].nozzle_max_temp =
                jsonData["channels" + std::to_string(i)]["nozzle_max_temp"].get<std::string>();
        }
    }

    if(jsonData.contains("extern_channel"))
    {
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.manufacturer =
            jsonData["extern_channel"]["manufacturer"].get<std::string>();
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.brand =
            jsonData["extern_channel"]["brand"].get<std::string>();
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.code =
            jsonData["extern_channel"]["code"].get<std::string>();
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.type =
            jsonData["extern_channel"]["type"].get<std::string>();
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.detailed_type =
            jsonData["extern_channel"]["detailed_type"].get<std::string>();
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.color =
            jsonData["extern_channel"]["color"].get<std::string>();
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.diameter =
            jsonData["extern_channel"]["diameter"].get<std::string>();
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.weight =
            jsonData["extern_channel"]["weight"].get<std::string>();
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.date =
            jsonData["extern_channel"]["date"].get<std::string>();
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.nozzle_min_temp =
            jsonData["extern_channel"]["nozzle_min_temp"].get<std::string>();
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.nozzle_max_temp =
            jsonData["extern_channel"]["nozzle_max_temp"].get<std::string>();
    }

    channel_color_table.clear();
    if(jsonData.contains("color_table_size"))
    {
        size_t color_table_size = jsonData["color_table_size"].get<size_t>();
        for(ssize_t i = 0; i < CHANNEL_NUMBER; i++)
        {
            if(jsonData.contains("color_table" + std::to_string(i)))
            {
                std::tuple<std::string,std::string,std::string> tup(
                        jsonData["color_table" + std::to_string(i)]["T"].get<std::string>(),
                        jsonData["color_table" + std::to_string(i)]["color"].get<std::string>(),
                        jsonData["color_table" + std::to_string(i)]["channel"].get<std::string>());
                channel_color_table.emplace_back(tup);
            }
        }
    }
    SPDLOG_INFO("read record info success");
}

void Canvas::state_feedback(std::string command,std::string result)
{
    json res;
    res["command"] = command;
    res["result"] = result;
    SPDLOG_INFO("{}",res.dump());
    gcode->respond_feedback(res);
}

void Canvas::canvas_controller_thread()
{
    while(true)
    {
        //读状态
        if(!canvas_protocol->read_feeders_status(true))
        {
            if(timeout_count > 10 && canvas_protocol->get_canvas_status().mcu_status.connect_status)
            {
                canvas_protocol->get_canvas_status().mcu_status.connect_status = false;
                SPDLOG_INFO("Canvas device lost");
            }
            else if(canvas_protocol->get_canvas_status().mcu_status.connect_status)
            {
                timeout_count++;
            }

            if(initial_boot)
            {
                auto_update_firmware();
                initial_boot = false;
            }
        }
        else
        {
            if(!canvas_protocol->get_canvas_status().mcu_status.connect_status)
            {
                timeout_count = 0;
                canvas_protocol->get_canvas_status().mcu_status.connect_status = true;
                auto_update_firmware();
                initial_boot = false;
                SPDLOG_INFO("Canvas device detected");
            }

            //读rfid
            if(print_state != "printing")
            {
                if((canvas_protocol->get_canvas_status().mcu_status.status & 0x01) && !detected_rfid)
                {
                    canvas_protocol->read_rfid_data(true);
                    detected_rfid = true;
                    SPDLOG_INFO("RFID data detected");
                }
            }

            //自动预进料
            auto_prefeed_filament();
        }

        if(save_filament_info)
        {
            std::thread(&Canvas::save_record_info, this).detach();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Canvas::clean_waste()
{
    int channel = canvas_protocol->get_canvas_status().host_status.feed_channel_current;
    SPDLOG_INFO("channel {}",channel);
    if(channel != -1)
    {
        SPDLOG_INFO("status[channel] {} fila_in[channel] {}",canvas_protocol->get_canvas_status().host_status.canvas_lite.status[channel],canvas_protocol->get_canvas_status().mcu_status.fila_in[channel]);
    }

    if(channel != -1 && canvas_protocol->get_canvas_status().host_status.canvas_lite.status[channel] == 0)
    {
        SPDLOG_INFO("{} channel {}",__func__,channel);
        filament_load_gcode_1->run_gcode_from_command();

        double timeout = get_monotonic();
        while(canvas_protocol->get_canvas_status().host_status.ex_fila_status)
        {
            filament_load_gcode_2->run_gcode_from_command();
            gcode->run_script_from_command("M400");

            if(get_monotonic() - timeout > 360)
            {
                break;
            }
        }
    }
    SPDLOG_INFO("{} __OVER",__func__);
}

bool Canvas::extruder_retract(uint8_t channel)
{
    // uint8_t channel = canvas_protocol->get_canvas_status().host_status.feed_channel_current;
    led_mode_control(channel,CanvasLedMode::WHITE_BREATHING);
    bool ret = false;
    double wait_time = get_monotonic() + unload_fila_time;

    gcode->run_script_from_command("M83\nG1 E-60 F1800");

    double cur_channel_pos = canvas_protocol->get_canvas_status().mcu_status.ch_position[channel];
    FeederMotor feeder_motor;
    feeder_motor.mm = -400;
    feeder_motor.mm_s = motor_speed;
    feeder_motor.mm_ss = motor_accel;
    canvas_protocol->feeder_filament_control(channel,feeder_motor);

    while (get_monotonic() < wait_time)
    {
        reactor->pause(get_monotonic() + 0.1);

        if(!canvas_protocol->get_canvas_status().host_status.ex_fila_status)
        {
            canvas_protocol->get_canvas_status().host_status.feed_channel_current = -1;
            if(fabs(canvas_protocol->get_canvas_status().mcu_status.ch_position[channel] - cur_channel_pos) > unload_fila_dist)
            {
                canvas_protocol->feeders_filament_stop(channel);
                led_mode_control(channel,CanvasLedMode::RELEASE_CONTROL);
                ret = true;

                SPDLOG_INFO("ex_fila_status {} channel {} pre_pos {} cur_pos {}",
                    canvas_protocol->get_canvas_status().host_status.ex_fila_status,channel,cur_channel_pos,
                    canvas_protocol->get_canvas_status().mcu_status.ch_position[channel]);
                break;
            }
        }
        else if(canvas_protocol->get_canvas_status().mcu_status.ch_position[channel] - cur_channel_pos > fila_det_dist)//cur_time - last_time > 5. &&
        {
            canvas_protocol->feeders_filament_stop(channel);
            led_mode_control(channel,CanvasLedMode::RED_FLASHING);
            // canvas_protocol->get_canvas_status().host_status.feed_channel_current = -1;
            canvas_protocol->get_canvas_status().host_status.report_error_code = elegoo::common::ErrorCode::CANVAS_PLUT_OUT_FILA_ABNORMAL;
            // gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_PLUT_OUT_FILA_ABNORMAL,
            //     elegoo::common::ErrorLevel::WARNING);
            SPDLOG_ERROR("Extruder filament plug out failed: filament broken. ex_fila_status {} pre_pos {}  cur_pos {}  channel = {}",
                canvas_protocol->get_canvas_status().host_status.ex_fila_status,
                cur_channel_pos,canvas_protocol->get_canvas_status().mcu_status.ch_position[channel], channel);
            throw elegoo::common::MMUError("Extruder filament plug out failed: filament broken");
        }
    }

    gcode->run_script_from_command("M400");

    if(!ret)
    {
        canvas_protocol->feeders_filament_stop(channel);
        led_mode_control(channel,CanvasLedMode::RED_FLASHING);
        canvas_protocol->get_canvas_status().host_status.report_error_code = elegoo::common::ErrorCode::CANVAS_PLUT_OUT_FILA_TIMEOUT;
        // gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_PLUT_IN_FILA_TIMEOUT,
        //     elegoo::common::ErrorLevel::WARNING);
        SPDLOG_ERROR("Extruder filament plug out failed: timeout. pre_pos {}  cur_pos {}  channel = {}",
            cur_channel_pos,canvas_protocol->get_canvas_status().mcu_status.ch_position[channel], channel);
        throw elegoo::common::MMUError("Extruder filament plug out failed: timeout.");
    }

    return ret;
}

bool Canvas::extruder_feed(uint8_t channel)
{
    led_mode_control(channel,CanvasLedMode::WHITE_BREATHING);

    bool ret = false;
    double wait_time = get_monotonic() + load_fila_time;
    double cur_channel_pos = canvas_protocol->get_canvas_status().mcu_status.ch_position[channel];

    FeederMotor feeder_motor;
    feeder_motor.mm = 1500;
    feeder_motor.mm_s = motor_speed;
    feeder_motor.mm_ss = motor_accel;
    canvas_protocol->feeder_filament_control(channel,feeder_motor);

    while (get_monotonic() < wait_time)
    {
        reactor->pause(get_monotonic() + 0.1);

        if(canvas_protocol->get_canvas_status().host_status.ex_fila_status)
        {
            // reactor->pause(get_monotonic() + load_wait_time);
            load_fila_ex_gcode_1->run_gcode_from_command();
            canvas_protocol->feeders_filament_stop(channel);
            int32_t retry_count = 0;
            double cur_channel_pos = canvas_protocol->get_canvas_status().mcu_status.ch_position[channel];
            while(canvas_protocol->get_canvas_status().mcu_status.ch_position[channel] - cur_channel_pos < load_fila_det_dist && retry_count++ < load_fila_det_count)
            {
                canvas_protocol->get_canvas_status().host_status.feed_channel_current = channel;
                load_fila_ex_gcode_2->run_gcode_from_command();
                SPDLOG_INFO("retry_count {} last_position {} ch_position {}", retry_count, cur_channel_pos, canvas_protocol->get_canvas_status().mcu_status.ch_position[channel]);
            }

            if(retry_count > load_fila_det_count)
            {
                SPDLOG_ERROR("extruder_feed fail, exec unload filament");
                gcode->run_script_from_command("PRINTER_UNLOAD_FILAMENT");
                led_mode_control(channel,CanvasLedMode::RED_FLASHING);
                canvas_protocol->get_canvas_status().host_status.report_error_code = elegoo::common::ErrorCode::CANVAS_PLUT_IN_FILA_BLOCKED;
                // gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_PLUT_IN_FILA_BLOCKED,
                //     elegoo::common::ErrorLevel::WARNING);
                canvas_protocol->get_canvas_status().host_status.feed_channel_current = -1;
                SPDLOG_ERROR("Filament loading process failed: filament block. channel {}", channel);
                throw elegoo::common::MMUError("Filament loading process failed: filament block");
            }
            
            ret = true;
            SPDLOG_INFO("extruder_feed success, channel = {}", channel);
            break;
        }
        else if(canvas_protocol->get_canvas_status().mcu_status.ch_position[channel] - cur_channel_pos > load_fila_dist)//cur_time - last_time > 5. &&
        {
            canvas_protocol->feeders_filament_stop(channel);
            led_mode_control(channel,CanvasLedMode::RED_FLASHING);
            canvas_protocol->get_canvas_status().host_status.report_error_code = elegoo::common::ErrorCode::CANVAS_PLUT_IN_FILA_NOT_TRIGGER;
            // gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_PLUT_IN_FILA_NOT_TRIGGER,
            //     elegoo::common::ErrorLevel::WARNING);
            SPDLOG_ERROR("Filament loading process failed: Out of Movement Rang. channel = {}", channel);
            throw elegoo::common::MMUError("Filament loading process failed: Out of Movement Range");
        }
    }

    if(!ret)
    {
        canvas_protocol->feeders_filament_stop(channel);
        led_mode_control(channel,CanvasLedMode::RED_FLASHING);
        canvas_protocol->get_canvas_status().host_status.report_error_code = elegoo::common::ErrorCode::CANVAS_PLUT_IN_FILA_TIMEOUT;
        // gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_PLUT_IN_FILA_TIMEOUT,
        //     elegoo::common::ErrorLevel::WARNING);
        SPDLOG_ERROR("Filament loading process failed: timeout. pre_pos {}  cur_pos {}  channel = {}",
            cur_channel_pos,canvas_protocol->get_canvas_status().mcu_status.ch_position[channel], channel);
        throw elegoo::common::MMUError("Filament loading process failed: timeout.");
    }

    return ret;
}

void Canvas::extruder_channel_check()
{
    SPDLOG_INFO("start extrude channel check.");

    if(canvas_protocol->get_canvas_status().host_status.feed_channel_current != -1 ||
        !canvas_protocol->get_canvas_status().host_status.ex_fila_status)
    {
        if(!canvas_protocol->get_canvas_status().host_status.ex_fila_status)
        {
            canvas_protocol->get_canvas_status().host_status.feed_channel_current = -1;
        }
        SPDLOG_INFO("extrude channel check result: {} ex_fila_status:{}", canvas_protocol->get_canvas_status().host_status.feed_channel_current,canvas_protocol->get_canvas_status().host_status.ex_fila_status);
        return;
    }

    std::vector<double> last_position;
    for(size_t i = 0; i < CHANNEL_NUMBER; i++)
    {
        last_position.push_back(canvas_protocol->get_canvas_status().mcu_status.ch_position[i]);
    }

    channel_check_gcode_1->run_gcode_from_command();

    std::vector<int> count;
    for(size_t i = 0; i < CHANNEL_NUMBER; i++)
    {
        if(fabs(canvas_protocol->get_canvas_status().mcu_status.ch_position[i] - last_position[i]) >= min_check_distance_1 &&
            fabs(canvas_protocol->get_canvas_status().mcu_status.ch_position[i] - last_position[i]) <= max_check_distance_1)
        {
            count.push_back(i);
        }
        SPDLOG_INFO("step 1 : distance  {}",fabs(canvas_protocol->get_canvas_status().mcu_status.ch_position[i] - last_position[i]));
    }

    if(count.size() == 0)
    {
        gcode->run_script_from_command("CANVAS_CUT_FILAMENT");
        gcode->run_script_from_command("M83\nG1 E" + std::to_string(-80) + "F1800\nM400");

        for(size_t i = 0; i < CHANNEL_NUMBER; i++)
        {
            if(canvas_protocol->get_canvas_status().host_status.canvas_lite.status[i] != 0)
            {
                FeederMotor feeder_motor;
                feeder_motor.mm = -30;
                feeder_motor.mm_s = motor_speed;
                feeder_motor.mm_ss = motor_accel;
                canvas_protocol->feeder_filament_control(i,feeder_motor);
                reactor->pause(get_monotonic() + 3);
                if(!canvas_protocol->get_canvas_status().host_status.ex_fila_status)
                {
                    feeder_motor.mm = -120;
                    canvas_protocol->feeders_filament_stop(i);
                    canvas_protocol->feeder_filament_control(i,feeder_motor);
                    break;
                }
            }
        }
        canvas_protocol->get_canvas_status().host_status.feed_channel_current = -1;
    }
    else if(count.size() == 1)
    {
        canvas_protocol->get_canvas_status().host_status.feed_channel_current = count.at(0);
    }
    else
    {
        for(size_t i = 0; i < count.size(); i++)
        {
            last_position.at(count.at(i)) = canvas_protocol->get_canvas_status().mcu_status.ch_position[count.at(i)];
        }

        channel_check_gcode_2->run_gcode_from_command();

        std::vector<int> second_count;
        for(size_t i = 0; i < count.size(); i++)
        {
            if(fabs(canvas_protocol->get_canvas_status().mcu_status.ch_position[count.at(i)] - last_position.at(count.at(i))) >= min_check_distance_2 &&
                fabs(canvas_protocol->get_canvas_status().mcu_status.ch_position[count.at(i)] - last_position.at(count.at(i))) <= max_check_distance_2)
            {
                second_count.push_back(i);
            }
        }

        if(second_count.size() == 1)
        {
            canvas_protocol->get_canvas_status().host_status.feed_channel_current = second_count.at(0);
        }
        else
        {
            canvas_protocol->get_canvas_status().host_status.feed_channel_current = -1;
        }
    }

    if(canvas_protocol->get_canvas_status().host_status.feed_channel_current == -1 &&
        canvas_protocol->get_canvas_status().host_status.ex_fila_status)
    {
        SPDLOG_ERROR("extrude channel check failed.");
        canvas_protocol->get_canvas_status().host_status.report_error_code = elegoo::common::ErrorCode::CANVAS_ABNORMAL_FILA;
        // gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_ABNORMAL_FILA,
        //     elegoo::common::ErrorLevel::WARNING);
        throw elegoo::common::MMUError("extrude channel check failed.");
    }

    SPDLOG_INFO("extrude channel check result: {}",
        canvas_protocol->get_canvas_status().host_status.feed_channel_current);
}

void Canvas::auto_prefeed_filament()
{
    for(size_t i = 0; i < CHANNEL_NUMBER; i++)
    {
        if(canvas_protocol->get_canvas_status().mcu_status.fila_in[i] &&
            canvas_protocol->get_canvas_status().host_status.feed_channel_current == i)
        {
            canvas_protocol->get_canvas_status().host_status.canvas_lite.status[i] = 2;
        }
        else if(canvas_protocol->get_canvas_status().mcu_status.fila_in[i] &&
            canvas_protocol->get_canvas_status().host_status.feed_channel_current != i &&
            canvas_protocol->get_canvas_status().host_status.canvas_lite.status[i] != 0)
        {
            canvas_protocol->get_canvas_status().host_status.canvas_lite.status[i] = 1;
        }
        else if(canvas_protocol->get_canvas_status().mcu_status.fila_in[i] &&
            canvas_protocol->get_canvas_status().host_status.feed_channel_current != i &&
            canvas_protocol->get_canvas_status().host_status.canvas_lite.status[i] == 0)
        {
            canvas_protocol->get_canvas_status().host_status.canvas_lite.status[i] = 1;
            // canvas_protocol->get_canvas_status().host_status.prefeeder_time[i] = get_monotonic();
            // if(print_state != "printing")
            // {
                FeederMotor feeder_motor;
                feeder_motor.mm = pre_load_fila_dist;
                feeder_motor.mm_s = pre_load_motor_speed;
                feeder_motor.mm_ss = motor_accel;
                canvas_protocol->feeder_filament_control(i, feeder_motor);
            // }
            // led_mode_control(i,CanvasLedMode::WHITE_BREATHING);
        }
        else if(canvas_protocol->get_canvas_status().mcu_status.fila_in[i] == 0
            // && canvas_protocol->get_canvas_status().host_status.feed_channel_current != i
            )
        {
            canvas_protocol->get_canvas_status().host_status.canvas_lite.status[i] = 0;
        }



        // if(canvas_status.host_status.canvas_lite.status[i] == 1 &&
        //     (get_monotonic() > canvas_status.host_status.prefeeder_time[i] + 2) &&
        //     (get_monotonic() < canvas_status.host_status.prefeeder_time[i] + pre_load_fila_time))
        // {
        //     if(canvas_status.mcu_status.dragging[i] == 0)
        //     {
        //         canvas_status.host_status.canvas_lite.status[i] = 2;
        //     }
        // }
        // else if(canvas_status.host_status.canvas_lite.status[i] == 1 &&
        //     (get_monotonic() > canvas_status.host_status.prefeeder_time[i] + pre_load_fila_time))
        // {
        //     //进料超时
        //     canvas_status.host_status.canvas_lite.status[i] = 2;
        // }
    }
}

void Canvas::auto_refill_filament()
{
    SPDLOG_INFO("Start auto refill process");

    bool is_pause = true;

    if(!canvas_protocol->get_canvas_status().mcu_status.connect_status)
    {
        SPDLOG_INFO("Auto feeding skipped: device not connected.");
    }
    else
    {
        if(canvas_protocol->get_canvas_status().host_status.auto_plug_in_enable)
        {
            int channel = canvas_protocol->get_canvas_status().host_status.feed_channel_request;
            std::string color = canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[channel].color;
            std::string type = canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[channel].type;
            SPDLOG_INFO("cur_channel: {} color: {} fila_type: {}", channel, color, type);
            for (size_t i = 0; i < CHANNEL_NUMBER; i++)
            {
                SPDLOG_INFO("channel: {} color: {} fila_type: {}", i,
                    canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].color,
                    canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].type);

                if(channel == i || canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].color.empty())
                {
                    continue;
                }

                if(canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].type == type &&
                    canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].color == color)
                {
                    for(size_t j = 0; j < channel_color_table.size(); j++)
                    {
                        if(canvas_protocol->get_canvas_status().host_status.switch_filment_T == std::stoi(std::get<0>(channel_color_table[j])))
                        {
                            std::get<2>(channel_color_table[j]) = std::to_string(i);
                            break;
                        }
                    }

                    gcode->run_script_from_command("CANVAS_SWITCH_FILAMENT T=" + std::to_string(canvas_protocol->get_canvas_status().host_status.switch_filment_T) + " SELECT=0");
                    if(!canvas_protocol->get_canvas_status().host_status.error_code)
                    {
                        SPDLOG_INFO("filament refill successful, resuming printing");
                        gcode->run_script_from_command("RESUME");
                        is_pause = false;
                    }
                    break;
                }
            }
        }
        else
        {
            SPDLOG_INFO("Auto feeding skipped: auto refill over.");
        }
    }

    if(is_pause)
    {
        runout_gcode->run_gcode_from_command();
        gcode->run_script("\nM400");
    }
}

void Canvas::auto_update_firmware()
{
    glob_t glob_result;
    std::string pattern = firmware_dir + "/canvas_gd32-*-app.bin";
    glob(pattern.c_str(), 0, nullptr, &glob_result);

    std::string firmware_path = (glob_result.gl_pathc > 0) ? glob_result.gl_pathv[0] : "";
    globfree(&glob_result);

    if (firmware_path.empty())
    {
        SPDLOG_INFO("Canvas firmware not found");
        return;
    }

    // 正则：匹配中间的类似 x.x.x.x 的版本号
    std::smatch match;
    if (!std::regex_search(firmware_path, match, std::regex(R"(([0-9A-Fa-f]{2}\.[0-9A-Fa-f]{2}\.[0-9A-Fa-f]{2}\.[0-9A-Fa-f]{2}))")))
    {
        SPDLOG_WARN("No firmware version found in path");
        return;
    }

    bool ret = canvas_protocol->read_software_version(true);
    canvas_protocol->get_canvas_status().host_status.updating = 1;
    bool need_update = false;
    if(!ret)
    {
        SPDLOG_ERROR("Failed to read Canvas software version");

        // 1. Ping bootloader，回应表示APP异常，无回应表示Canvas未连接
        // 2. update
        // 3. 正常运行
        SerialBootloader sb("/dev/ttyS1", 115200);
        sb.connect();
        int count = 10;
        while(count--)
        {
            if (sb.ping())
            {
                SPDLOG_WARN("Canvas No App");
                need_update = true;
                state_feedback("CANVAS_UPDATE", "1");
                break;
            }
            else
            {
                SPDLOG_INFO("Canvas Not Connected");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        sb.disconnect();
    }
    else
    {
        if (canvas_protocol->get_canvas_status().mcu_status.software_version == match[1].str())
        {
            SPDLOG_INFO("Canvas firmware version matched.");
        }
        else
        {
            SPDLOG_INFO("Canvas firmware version mismatched, Start update firmware");
            need_update = true;
        }
    }

    if (need_update)
    {
        state_feedback("CANVAS_UPDATE", "1");
        if (update_firmware(firmware_path))
        {
            SPDLOG_INFO("Canvas firmware updated successfully");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            canvas_protocol->read_software_version();
        }
        else
        {
            SPDLOG_ERROR("Canvas firmware update failed");
        }
        state_feedback("CANVAS_UPDATE", "0");
    }

    canvas_protocol->get_canvas_status().host_status.updating = 0;
}

bool Canvas::update_firmware(std::string firmware_path)
{
    std::string tty = "/dev/ttyS1";
    int badurate = 115200;

    int retry = 5;
    SerialBootloader app_serial(tty, badurate);
    SerialBootloader bootloader_serial(tty, badurate);

    while (--retry >= 0)
    {
        int ping_retry = 10;

        if (!app_serial.connect())
        {
            SPDLOG_INFO("Failed to connect to app_serial");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        app_serial.jump_to_bootloader();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        app_serial.disconnect();

        if (!bootloader_serial.connect())
        {
            SPDLOG_INFO("bootloader_serial.connect() failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        while (--ping_retry >= 0)
        {
            if (bootloader_serial.ping())
            {
                SPDLOG_INFO("bootloader connected {}", ping_retry);
                break;
            }
            bootloader_serial.disconnect();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            bootloader_serial.connect();
        }

        if (ping_retry >= 0) // Ping Bootloader成功，跳出外层循环，结束重试
            break;
    }

    if (retry < 0)
    {
        bootloader_serial.disconnect();
        SPDLOG_INFO("canvas update error connect");
        return false;
    }
    // 执行升级
    if (!bootloader_serial.update(firmware_path.c_str()))
    {
        bootloader_serial.jump_to_app();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        bootloader_serial.disconnect();
        SPDLOG_INFO("canvas update error update");
        return false;
    }

    bootloader_serial.jump_to_app();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bootloader_serial.disconnect();

    return true;
}

// void Canvas::rock_arm_control(int32_t channel, FeederMotor feeder_motor, double wait_time)
// {
//     double cur_time = get_monotonic();
//     double end_time = cur_time + wait_time;

//     canvas_protocol->feeder_filament_control(channel, feeder_motor);

//     while(cur_time < end_time)
//     {
//         cur_time = get_monotonic() + 0.5;
//         reactor->pause(cur_time);
//         if(!canvas_protocol->get_canvas_status().mcu_status.dragging[channel])
//         {
//             break;
//         }
//     }

//     if(cur_time >= end_time)
//     {
//         SPDLOG_ERROR("Execute rocker arm action timeout....");
//     }
// }

void Canvas::rock_arm_control(uint8_t channel, int8_t dir, double wait_time)
{
    canvas_protocol->rocker_control(channel, dir);
    reactor->pause(get_monotonic() + wait_time);
}

void Canvas::led_mode_control(uint8_t channel, CanvasLedMode mode)
{
    std::array<FeederLed, CHANNEL_NUMBER> leds;
    if(mode == CanvasLedMode::RELEASE_CONTROL)
    {
        for (std::size_t i = 0; i < CHANNEL_NUMBER; i++)
        {
            leds[i].enable_control = false;
        }
    }
    else if(mode == CanvasLedMode::RED_FLASHING) //红灯闪烁
    {
        for (std::size_t i = 0; i < CHANNEL_NUMBER; i++)
        {
            if(i == channel)
            {
                leds[i].enable_control = true;
                leds[i].red = FeederLedState::Blink2Hz;
                leds[i].blue = FeederLedState::Off;
            }
            else
            {
                leds[i].enable_control = false;
            }
        }
    }
    else if(mode == CanvasLedMode::WHITE_BREATHING) //白灯呼吸
    {
        for (std::size_t i = 0; i < CHANNEL_NUMBER; i++)
        {
            if(i == channel)
            {
                leds[i].enable_control = true;
                leds[i].red = FeederLedState::Off;
                leds[i].blue = FeederLedState::Breathe;
            }
            else
            {
                leds[i].enable_control = false;
            }
        }
    }

    canvas_protocol->feeders_leds_control(leds);
}

void Canvas::error_pause()
{
    bool is_printing = print_stats->get_status(get_monotonic())["state"].get<std::string>() == "printing";
    if(is_printing)
    {
        reactor->register_callback([this](double eventtime) {
            if(!is_cancel)
            {
                pause_resume->send_pause_command();
                gcode->run_script_from_command("PAUSE");
                gcode->run_script("\nM400");
            }
            SPDLOG_INFO("mmu exec pause!! is_cancel {}",is_cancel);
            return json::object();
        });
    }

    elegoo::common::SignalManager::get_instance().emit_signal(
        "canvas:enable");
}

void Canvas::filament_det_handler(bool state)
{
    if(canvas_protocol->get_canvas_status().host_status.ex_fila_status == state)
    {
        return ;
    }
    canvas_protocol->get_canvas_status().host_status.ex_fila_status = state;

    if(canvas_protocol->get_canvas_status().host_status.ex_fila_status == false)
    {
        canvas_protocol->get_canvas_status().host_status.feed_channel_current = -1;
    }

    state_feedback("CANVAS_FILA_DET_FEEDBACK",std::to_string(state));
    SPDLOG_INFO("filament_det_handler status {}",state);
}

void Canvas::nozzle_fan_off_handler( bool state)
{
    if(canvas_protocol->get_canvas_status().host_status.nozzle_fan_off_status == state)
    {
        return ;
    }

    canvas_protocol->get_canvas_status().host_status.nozzle_fan_off_status = state;

    // reactor->register_callback([this](double eventtime)
    //     { return 0;  });

    state_feedback("CANVAS_FAN_OFF_FEEDBACK",std::to_string(state));
    SPDLOG_INFO("nozzle_fan_off_handler status {}",state);
}

void Canvas::wrap_filament_handler(bool state)
{
    if(canvas_protocol->get_canvas_status().host_status.wrap_filament_status == state)
    {
        return ;
    }

    canvas_protocol->get_canvas_status().host_status.wrap_filament_status = state;
    state_feedback("CANVAS_WRAP_FILA_FEEDBACK",std::to_string(state));
    SPDLOG_INFO("wrap_filament_handler status {}",state);
}

double Canvas::cutting_knife_handler(double eventtime, bool state)
{
    if(canvas_protocol->get_canvas_status().host_status.cutting_knife_status == state)
    {
        return 0;
    }

    canvas_protocol->get_canvas_status().host_status.cutting_knife_status = state;
    state_feedback("CANVAS_CUT_KNIFT_FEEDBACK",std::to_string(state));
    SPDLOG_INFO("cutting_knife_handler status {}",state);
    return 0.0;
}

double Canvas::model_det_handler(double eventtime, bool state)
{
    return 0.0;
}

double Canvas::info_report_handler(double eventtime)
{
    if(detected_rfid && !is_report)
    {
        state_feedback("CANVAS_RFID_TRIG", canvas_protocol->get_canvas_status().host_status.rfid_data.filament_info.type +
            "," + canvas_protocol->get_canvas_status().host_status.rfid_data.filament_info.color);
        is_report = true;
    }

    if(canvas_version != canvas_protocol->get_canvas_status().mcu_status.software_version)
    {
        canvas_version = canvas_protocol->get_canvas_status().mcu_status.software_version;
        state_feedback("CANVAS_SOFTWARE_VERSION", canvas_version);
    }

    return eventtime + 1.0;
}

double Canvas::filament_block_handler(double eventtime)
{
    int channel = canvas_protocol->get_canvas_status().host_status.feed_channel_current;
    if(channel < 0 || channel != last_check_channel || !canvas_protocol->get_canvas_status().mcu_status.fila_in[channel])
    {
        if(channel >= 0)
        {
            last_check_channel = channel;
            last_extruder_pos = extruder->find_past_position(estimated_print_time(eventtime));
            last_channel_pos = canvas_protocol->get_canvas_status().mcu_status.ch_position[channel];

            SPDLOG_INFO("debug =========  channel {} last_extruder_pos {} last_channel_pos {}", 
                channel, last_extruder_pos, last_channel_pos);
        }

        return eventtime + 3.0;
    }

    double cur_extruder_pos = extruder->find_past_position(estimated_print_time(eventtime));
    double cur_channel_pos = canvas_protocol->get_canvas_status().mcu_status.ch_position[channel];

    if(cur_extruder_pos - last_extruder_pos > 20)
    {
        SPDLOG_INFO("debug ------------------  channel {} last_extruder_pos {} cur_extruder_pos {} last_channel_pos {} cur_channel_pos {}", 
            channel,last_extruder_pos, cur_extruder_pos, last_channel_pos,cur_channel_pos);

        if(cur_channel_pos == last_channel_pos)
        {
            SPDLOG_INFO("check filament block!!");
            gcode->respond_ecode("", elegoo::common::ErrorCode::EXTERNAL_FILA_ERROR, 
                elegoo::common::ErrorLevel::WARNING);

            reactor->register_callback([this](double eventtime) { 
                pause_resume->send_pause_command();
                gcode->run_script_from_command("PAUSE");
                gcode->run_script("\nM400");
                return json::object();
            });

            return _NEVER;
        }

        last_extruder_pos = cur_extruder_pos;
        last_channel_pos = cur_channel_pos;
    }

    return eventtime + 3.0;
}


void Canvas::handle_print_state(std::string value)
{
    print_state = value;

    if(print_state != "printing")
    {
        reactor->update_timer(rfid_report_timer,_NOW);
        reactor->update_timer(filament_block_check,_NEVER);
    }
    else
    {
        is_cancel = false;
        reactor->update_timer(rfid_report_timer,_NEVER);
        reactor->update_timer(filament_block_check,_NOW);
    }

    // if(print_state == "paused")
    // {
    //     set_error_code(4);
    // }
}

int32_t Canvas::get_T_channel(int32_t T)
{
    int32_t channel = -1;
    try
    {
        for(size_t i = 0; i < channel_color_table.size(); ++i)
        {
            int32_t color_table_T = std::stoi(std::get<0>(channel_color_table[i]));
            if(color_table_T == T)
            {
                channel = std::stoi(std::get<2>(channel_color_table[i]));
                break;
            }
        }
    }
    catch(...)
    {
        SPDLOG_WARN("channel color table abnormal");
    }

    SPDLOG_INFO("{} channel {}",__func__,channel);
    return channel;
}

json Canvas::get_canvas_power_outage_status(double eventtime)
{
    json status;
    status["printing_used_canvas"] = canvas_protocol->get_canvas_status().mcu_status.connect_status;
    status["switch_filment_T"] = canvas_protocol->get_canvas_status().host_status.switch_filment_T;
    status["color_table_size"] = channel_color_table.size();
    for(auto ii = 0; ii < channel_color_table.size(); ++ii)
    {
        status["color_table" + std::to_string(ii)]["T"] = std::get<0>(channel_color_table[ii]);
        status["color_table" + std::to_string(ii)]["color"] = std::get<1>(channel_color_table[ii]);
        status["color_table" + std::to_string(ii)]["channel"] = std::get<2>(channel_color_table[ii]);
        SPDLOG_DEBUG("{} T:{} color:{} channel:{}","color_table" + std::to_string(ii),std::get<0>(channel_color_table[ii]),std::get<1>(channel_color_table[ii]),std::get<2>(channel_color_table[ii]));
    }

    return status;
}

std::shared_ptr<CanvasProtocol> Canvas::get_canvas_protocol()
{
    return canvas_protocol;
}

json Canvas::get_status(double eventtime)
{
    json status = {};
    status["type"] = "lite";
    status["auto_plug_in_enable"] = canvas_protocol->get_canvas_status().host_status.auto_plug_in_enable;
    status["active_did"] = 0;
    status["active_cid"] = canvas_protocol->get_canvas_status().host_status.feed_channel_current;
    status["canvas_lite"]["did"] = 0;
    status["canvas_lite"]["connected"] = canvas_protocol->get_canvas_status().mcu_status.connect_status;
    for(ssize_t i = 0; i < CHANNEL_NUMBER; i++)
    {
        std::string channels_index = "channels" + std::to_string(i);
        status["canvas_lite"][channels_index]["cid"] = i;
        status["canvas_lite"][channels_index]["status"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.status[i];
        status["canvas_lite"][channels_index]["filament"]["manufacturer"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].manufacturer;
        status["canvas_lite"][channels_index]["filament"]["brand"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].brand;
        status["canvas_lite"][channels_index]["filament"]["code"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].code;
        status["canvas_lite"][channels_index]["filament"]["type"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].type;
        status["canvas_lite"][channels_index]["filament"]["detailed_type"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].detailed_type;
        status["canvas_lite"][channels_index]["filament"]["color"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].color;
        status["canvas_lite"][channels_index]["filament"]["diameter"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].diameter;
        status["canvas_lite"][channels_index]["filament"]["weight"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].weight;
        status["canvas_lite"][channels_index]["filament"]["date"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].date;
        status["canvas_lite"][channels_index]["filament"]["nozzle_min_temp"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].nozzle_min_temp;
        status["canvas_lite"][channels_index]["filament"]["nozzle_max_temp"] =
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[i].nozzle_max_temp;
    }

    status["canvas_lite"]["extern_channel"]["status"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.status;
    status["canvas_lite"]["extern_channel"]["filament"]["manufacturer"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.manufacturer;
    status["canvas_lite"]["extern_channel"]["filament"]["brand"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.brand;
    status["canvas_lite"]["extern_channel"]["filament"]["code"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.code;
    status["canvas_lite"]["extern_channel"]["filament"]["type"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.type;
    status["canvas_lite"]["extern_channel"]["filament"]["detailed_type"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.detailed_type;
    status["canvas_lite"]["extern_channel"]["filament"]["color"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.color;
    status["canvas_lite"]["extern_channel"]["filament"]["diameter"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.diameter;
    status["canvas_lite"]["extern_channel"]["filament"]["weight"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.weight;
    status["canvas_lite"]["extern_channel"]["filament"]["date"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.date;
    status["canvas_lite"]["extern_channel"]["filament"]["nozzle_min_temp"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.nozzle_min_temp;
    status["canvas_lite"]["extern_channel"]["filament"]["nozzle_max_temp"] =
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info.nozzle_max_temp;
    return status;
}

json Canvas::get_status_product_test(double eventtime)
{
    json status = {};
    status["connected"] = canvas_protocol->get_canvas_status().mcu_status.connect_status;
    status["rfid_tag"] = std::to_string(canvas_protocol->get_canvas_status().host_status.rfid_data.uid);

    for (std::size_t i = 0; i < CHANNEL_NUMBER; i++) {
        json feeder;
        std::string feeder_key = "feeder" + std::to_string(i);

        feeder["motor_encoder"] = canvas_protocol->get_canvas_status().mcu_status.ch_fila_move_dist[i];
        feeder["odo_value"] = canvas_protocol->get_canvas_status().mcu_status.ch_position[i];
        feeder["fila_in"] = canvas_protocol->get_canvas_status().mcu_status.fila_in[i];

        status[feeder_key] = feeder;
    }

    return status;
}

void Canvas::CMD_canvas_load_filament(std::shared_ptr<GCodeCommand> gcmd)
{
    if(!canvas_protocol->get_canvas_status().mcu_status.connect_status)
    {
        return;
    }

    elegoo::common::SignalManager::get_instance().emit_signal("canvas:disable");

    try
    {
        SPDLOG_INFO("Execute filament feeding into the extruder head.");
        int32_t channel = gcmd->get_int("CHANNEL",0,0,3);
        canvas_protocol->get_canvas_status().host_status.feed_channel_request = channel;
        state_feedback("CANVAS_FEEDING_FILA_REQUEST",std::to_string(channel));
        state_feedback("M2202","1150");

        state_feedback("M2202","1151");
        gcode->run_script_from_command("CANVAS_MOVE_TO_WASTE_BOX");

        //检测耗材位置
        state_feedback("M2202","1152");
        extruder_channel_check();

        if(canvas_protocol->get_canvas_status().host_status.feed_channel_current != channel)
        {
            state_feedback("M2202","1153");
            gcode->run_script_from_command("CANVAS_CUT_FILAMENT");

            state_feedback("M2202","1154");
            gcode->run_script_from_command("CANVAS_PLUG_OUT_FILAMENT");

            state_feedback("M2202","1155");
            gcode->run_script_from_command("CANVAS_PLUG_IN_FILAMENT");
        }

        state_feedback("M2202","1156");
        gcode->run_script_from_command("CANVAS_CLEAN_WASTE_FILAMENT");

        state_feedback("M2202","1157");

        led_mode_control(channel,CanvasLedMode::RELEASE_CONTROL);
        set_error_code(0);
        SPDLOG_INFO("Execute filament feeding into the extruder head, complete.  channel {}",channel);
    }
    catch(const elegoo::common::MMUError &e)
    {
        state_feedback("M2202","1158");
        set_error_code(1);
        SPDLOG_WARN("Failed to execute filament loading process, reason {}", e.what());
    }

    elegoo::common::SignalManager::get_instance().emit_signal("canvas:enable");
}

void Canvas::CMD_canvas_unload_filament(std::shared_ptr<GCodeCommand> gcmd)
{
    if(!canvas_protocol->get_canvas_status().mcu_status.connect_status)
    {
        return;
    }

    elegoo::common::SignalManager::get_instance().emit_signal("canvas:disable");

    try
    {
        state_feedback("CANVAS_FEEDING_FILA_REQUEST",std::to_string(canvas_protocol->get_canvas_status().host_status.feed_channel_current));
        state_feedback("M2202","1160");
        if(!canvas_protocol->get_canvas_status().host_status.ex_fila_status)
        {
            state_feedback("M2202","1165");
            SPDLOG_INFO("Extruder head filament sensor not triggered");
            return;
        }

        SPDLOG_INFO("Execute filament retraction from the extruder head");

        // int32_t id = gcmd->get_int("ID",0,0,3);
        // int32_t channel = gcmd->get_int("CHANNEL",canvas_protocol->get_canvas_status().host_status.feed_channel_current,0,3);
        state_feedback("M2202","1161");
        gcode->run_script_from_command("CANVAS_MOVE_TO_WASTE_BOX");

        //检测耗材
        state_feedback("M2202","1162");
        extruder_channel_check();

        state_feedback("M2202","1163");
        gcode->run_script_from_command("CANVAS_CUT_FILAMENT");

        state_feedback("M2202","1164");
        gcode->run_script_from_command("CANVAS_PLUG_OUT_FILAMENT");

        state_feedback("M2202","1165");

        set_error_code(0);
        SPDLOG_INFO("Execute filament retraction from the extruder head. complete");
    }
    catch(const elegoo::common::MMUError &e)
    {
        state_feedback("M2202","1166");
        set_error_code(2);
        SPDLOG_WARN("Failed to execute filament unloading process, reason {}", e.what());
    }

    elegoo::common::SignalManager::get_instance().emit_signal("canvas:enable");
}

void Canvas::CMD_printer_unload_filament(std::shared_ptr<GCodeCommand> gcmd)
{
    if(!canvas_protocol->get_canvas_status().mcu_status.connect_status)
    {
        return;
    }

    elegoo::common::SignalManager::get_instance().emit_signal("canvas:disable");

    try
    {
        if(!canvas_protocol->get_canvas_status().host_status.ex_fila_status)
        {
            SPDLOG_INFO("(printer) Extruder head filament sensor not triggered");
            return;
        }

        SPDLOG_INFO("(printer) Execute filament retraction from the extruder head");
        // gcode->run_script_from_command("CANVAS_MOVE_TO_WASTE_BOX");
        gcode->run_script_from_command("G180 S800");

        //检测耗材
        extruder_channel_check();

        gcode->run_script_from_command("CANVAS_CUT_FILAMENT");

        gcode->run_script_from_command("CANVAS_PLUG_OUT_FILAMENT");

        set_error_code(0);
        SPDLOG_INFO("(printer) Execute filament retraction from the extruder head. complete");
    }
    catch(const elegoo::common::MMUError &e)
    {
        state_feedback("M2202","1166");
        set_error_code(2);
        SPDLOG_WARN("(printer) Failed to execute filament unloading process, reason {}", e.what());
    }

    elegoo::common::SignalManager::get_instance().emit_signal("canvas:enable");
}


void Canvas::CMD_canvas_switch_filament(std::shared_ptr<GCodeCommand> gcmd)
{
    if(!canvas_protocol->get_canvas_status().mcu_status.connect_status)
    {
        return;
    }

    elegoo::common::SignalManager::get_instance().emit_signal("canvas:disable");

    int retry_count = 0;

    while(!exit_retry)
    {
        try
        {
            SPDLOG_INFO("Execute filament switch process, retry:{}", retry_count);
            int32_t select = gcmd->get_int("SELECT",7);     // 0x00 执行全流程 0x01 不执行切刀 0x02 不执行退料 0x04 不执行送料
            int32_t T = gcmd->get_int("T",canvas_protocol->get_canvas_status().host_status.switch_filment_T,0,255);
            canvas_protocol->get_canvas_status().host_status.switch_filment_T = T;
            canvas_protocol->get_canvas_status().host_status.feed_channel_request = get_T_channel(T);

            if(canvas_protocol->get_canvas_status().host_status.feed_channel_request == -1)
            {
                SPDLOG_INFO("filament switch, do nothing");
                break;
            }

            extruder_channel_check();

            if(canvas_protocol->get_canvas_status().host_status.feed_channel_current !=
                canvas_protocol->get_canvas_status().host_status.feed_channel_request)
            {
                gcode->run_script_from_command("CANVAS_CUT_FILAMENT");

                gcode->run_script_from_command("CANVAS_PLUG_OUT_FILAMENT");
            }

            gcode->run_script_from_command("CANVAS_PLUG_IN_FILAMENT");

            set_error_code(0);
            SPDLOG_INFO("Execute filament switch process complete, channel {}",canvas_protocol->get_canvas_status().host_status.feed_channel_request);
            break;
        }
        catch(const elegoo::common::MMUError &e)
        {
            SPDLOG_WARN("Failed to execute filament switch process, reason {}  channel : {}", e.what(), canvas_protocol->get_canvas_status().host_status.feed_channel_request);
        }

        retry_count++;
        reactor->pause(get_monotonic() + 1.0);

        if(retry_count >= 2)
        {
            set_error_code(3);
            while(!exit_retry)
            {
                reactor->pause(get_monotonic() + 1.0);
                if(exec_retry)
                {
                    exec_retry = false;
                    break;
                }
            }
        }
    }

    exit_retry = false;
    elegoo::common::SignalManager::get_instance().emit_signal("canvas:enable");
}

void Canvas::CMD_move_to_waste_box(std::shared_ptr<GCodeCommand> gcmd)
{
    SPDLOG_INFO("Move to waste box");
    move_to_waste_box_macro->run_gcode_from_command();
}

void Canvas::CMD_clean_waste_filament(std::shared_ptr<GCodeCommand> gcmd)
{
    SPDLOG_INFO("clean wastet filament");
    clean_waste_filament_macro->run_gcode_from_command();
}

void Canvas::CMD_canvas_set_filament_info(std::shared_ptr<GCodeCommand> gcmd)
{
    SPDLOG_INFO("configuration of device consumable information");
    int32_t id = gcmd->get_int("ID",0,0,3);
    int32_t channel = gcmd->get_int("CHANNEL",0,0,3);
    bool extern_fila = gcmd->get_int("EXTERN_FILA",0,0,1);

    //2 刷新通道耗材信息
    FilamentInfo filament_info;
    filament_info.manufacturer = gcmd->get("MANUFACTURER","ELEGOO");
    filament_info.brand = gcmd->get("BRAND","0xEEEEEEEE");
    filament_info.code = gcmd->get("CODE","0x0001");
    filament_info.type = gcmd->get("TYPE","PLA");
    filament_info.detailed_type = gcmd->get("DETAILED_TYPE","CF");
    filament_info.color = gcmd->get("COLOR","0xFF3700");
    filament_info.diameter = gcmd->get("DIAMETER","1.75");
    filament_info.weight = gcmd->get("WEIGHT","1000");
    filament_info.date = gcmd->get("DATE","2502");
    filament_info.nozzle_min_temp = gcmd->get("NOZZLE_MIN_TEMP","170.0");
    filament_info.nozzle_max_temp = gcmd->get("NOZZLE_MAX_TEMP","250.0");

    if(extern_fila)
    {
        canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info = filament_info;
    }
    else
    {
        canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[channel] = filament_info;
    }

    save_filament_info = true;
}

void Canvas::CMD_canvas_cut_filament(std::shared_ptr<GCodeCommand> gcmd)
{
    if(!canvas_protocol->get_canvas_status().host_status.ex_fila_status)  // 没有耗材插入挤出机
    {
        SPDLOG_INFO("Extruder head filament sensor not triggered");
        return;
    }
    SPDLOG_INFO("Execute cutting action");

    move_to_cutting_knift_macro->run_gcode_from_command();
    // 检测切刀传感器是否触发,若未触发则报错
    if(0 == canvas_protocol->get_canvas_status().host_status.cutting_knife_status)
    {
        SPDLOG_WARN("{} cutting_knife_status:{} error! retry",__func__,canvas_protocol->get_canvas_status().host_status.cutting_knife_status);
        move_to_cutting_knift_retry_macro->run_gcode_from_command();
        if(0 == canvas_protocol->get_canvas_status().host_status.cutting_knife_status)
        {
            SPDLOG_ERROR("error! the cutting knife is not triggered");
            canvas_protocol->get_canvas_status().host_status.report_error_code = elegoo::common::ErrorCode::CANVAS_CUTTING_KNIFET_NOT_TRIGGER;
            // gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_CUTTING_KNIFET_NOT_TRIGGER,
            //     elegoo::common::ErrorLevel::WARNING);
            throw elegoo::common::MMUError("error! the cutting knife is not triggered");
        }
    }

    release_cutting_knift_macro->run_gcode_from_command();
    // 检测切刀传感器是否未触发,若触发则报错
    if(canvas_protocol->get_canvas_status().host_status.cutting_knife_status)
    {
        SPDLOG_WARN("{} cutting_knife_status:{} error! retry",__func__,canvas_protocol->get_canvas_status().host_status.cutting_knife_status);
        release_cutting_knift_retry_macro->run_gcode_from_command();
        if(canvas_protocol->get_canvas_status().host_status.cutting_knife_status)
        {
            SPDLOG_ERROR("error! the cutting knife is triggered");
            canvas_protocol->get_canvas_status().host_status.report_error_code = elegoo::common::ErrorCode::CANVAS_CUTTING_KNIFET_TRIGGER;
            // gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_CUTTING_KNIFET_TRIGGER,
            //     elegoo::common::ErrorLevel::WARNING);
            throw elegoo::common::MMUError("error! the cutting knife is triggered");
        }
    }
    SPDLOG_INFO("Execute cutting action. complete");
    return ;
}

void Canvas::CMD_canvas_plug_out_filament(std::shared_ptr<GCodeCommand> gcmd)
{
    if(!canvas_protocol->get_canvas_status().host_status.ex_fila_status)
    {
        SPDLOG_INFO("Extruder head filament sensor not triggered");
        return;
    }
    
    uint8_t channel = canvas_protocol->get_canvas_status().host_status.feed_channel_current;

    SPDLOG_INFO("Execute extruder head filament plug out. channel: {}", channel);

    clean_waste();

    if(!canvas_protocol->get_canvas_status().host_status.ex_fila_status)
    {
        return;
    }

    gcode->run_script_from_command("CANVAS_MESH_GEAR DIST=-1 CHANNEL=" + std::to_string(channel));

    gcode->run_script_from_command("M83\nG1 E-18 F700\nM400");

    if(channel != -1)
    {
        extruder_retract(channel);
    }
    else
    {
        if(canvas_protocol->get_canvas_status().host_status.ex_fila_status)
        {
            for (size_t i = 0; i < CHANNEL_NUMBER; i++)
            {
                try
                {
                    if(canvas_protocol->get_canvas_status().host_status.canvas_lite.status[i] != 0)
                    {
                        if(extruder_retract(i))
                        {
                            break;
                        }
                    }
                }
                catch(const elegoo::common::MMUError &e)
                {
                    SPDLOG_INFO("unknow channel, {}", e.what());
                } 

                if(i == CHANNEL_NUMBER-1)
                {
                    throw elegoo::common::MMUError("Extruder filament plug out failed: unkonw channel, filament broken");
                }
            }
        }
    }

    // gcode->run_script_from_command("CANVAS_MESH_GEAR CHANNEL=" + std::to_string(channel));

    SPDLOG_INFO("Execute extruder head filament plug out complete, channel: {}", channel);

    return ;
}

void Canvas::CMD_canvas_plug_in_filament(std::shared_ptr<GCodeCommand> gcmd)
{
    if(canvas_protocol->get_canvas_status().host_status.ex_fila_status)
    {
        SPDLOG_INFO("Extruder filament sensor triggered");
        return;
    }

    int32_t channel = gcmd->get_int("CHANNEL",
        canvas_protocol->get_canvas_status().host_status.feed_channel_request,0,3);

    SPDLOG_INFO("Execute filament plug in process. channel: {}",channel);    
    
    if(!canvas_protocol->get_canvas_status().host_status.ex_fila_status && 
        canvas_protocol->get_canvas_status().host_status.canvas_lite.status[channel] == 0)
    {
        canvas_protocol->get_canvas_status().host_status.report_error_code = elegoo::common::ErrorCode::CANVAS_OUT_OF_FILAMENT;
        throw elegoo::common::MMUError("Filament loading process failed: Filament run out");
    }

    // gcode->run_script_from_command("CANVAS_MESH_GEAR DIST=" + std::to_string(mesh_gear_dist) + " CHANNEL=" + std::to_string(channel));

    extruder_feed(channel);

    // gcode->run_script_from_command("CANVAS_MESH_GEAR DIST=" + std::to_string(mesh_gear_dist) + " CHANNEL=" + std::to_string(channel));

    SPDLOG_INFO("Execute filament plug in process complete. channel: {}", channel);
    return ;
}


void Canvas::CMD_canvas_set_color_table(std::shared_ptr<GCodeCommand> gcmd)
{
    SPDLOG_INFO("Configure channel mapping");
    std::string T = gcmd->get("T","0,1,2,3");
    std::string channel_table = gcmd->get("CHANNEL","0,1,2,3");
    std::string color_table = gcmd->get("COLOR","0xFF3700,0x735DF9,0x0080FF,0xFFC800"); // 红色 紫色 蓝色 黄色 最大32色
    auto T_vec = elegoo::common::split(T,",");
    auto channel_table_vec = elegoo::common::split(channel_table,",");
    auto color_table_vec = elegoo::common::split(color_table,",");

    if(T_vec.size())
    {
        channel_color_table.clear();
        if(channel_table_vec.size() < T_vec.size())
        {
            channel_table_vec.resize(T_vec.size(),"0");
        }

        if(color_table_vec.size() < T_vec.size())
        {
            color_table_vec.resize(T_vec.size(),"0xFF3700");
        }
    }

    for (size_t i = 0; i < T_vec.size(); i++)
    {
        std::tuple<std::string,std::string,std::string> tup(T_vec[i],color_table_vec[i],channel_table_vec[i]);
        channel_color_table.emplace_back(tup);
        SPDLOG_INFO("{} #1 T_vec.size {} table_T {} table_color {} table_channel {}",__func__,T_vec.size(),std::get<0>(channel_color_table[i]),std::get<1>(channel_color_table[i]),std::get<2>(channel_color_table[i]));
    }
}

void Canvas::CMD_canvas_channel_motor_ctrl(std::shared_ptr<GCodeCommand> gcmd)
{
    SPDLOG_INFO("Execute rocker arm action");
    int32_t dist = gcmd->get_int("DIST",-1 * mesh_gear_dist);
    // int32_t speed = gcmd->get_int("SPEED",motor_speed);
    // int32_t accel = gcmd->get_int("ACCEL",motor_accel);
    // int32_t time = gcmd->get_int("TIME",mesh_gear_time);
    int32_t channel = gcmd->get_int("CHANNEL", canvas_protocol->get_canvas_status().host_status.feed_channel_current);
    // FeederMotor feeder_motor;
    // feeder_motor.mm = dist;
    // feeder_motor.mm_s = speed;
    // feeder_motor.mm_ss = accel;
    rock_arm_control(channel, dist, 1);
    SPDLOG_INFO("Execute rocker arm action complete, dir {} channel: {} ",dist, channel);
}


void Canvas::CMD_canvas_detect_filament(std::shared_ptr<GCodeCommand> gcmd)
{
    if(!canvas_protocol->get_canvas_status().host_status.ex_fila_status)
    {
        SPDLOG_INFO("Extruder filament sensor no triggered");
        return;
    }

    extruder_channel_check();
}

void Canvas::CMD_canvas_abnormal_retry(std::shared_ptr<GCodeCommand> gcmd)
{
    if(!canvas_protocol->get_canvas_status().mcu_status.connect_status)
    {
        SPDLOG_WARN("canvas is not connect");
        return;
    }
    if(0 == canvas_protocol->get_canvas_status().host_status.error_code)
    {
        SPDLOG_INFO("error_code is zero");
        return;
    }

    int32_t t_resume = gcmd->get_int("RESUME",0);

    SPDLOG_INFO("exec abnormal retry, resume : {} error_code : {}",t_resume, canvas_protocol->get_canvas_status().host_status.error_code);

    if(t_resume)
    {
        switch (canvas_protocol->get_canvas_status().host_status.error_code)
        {
        case 1:
            SPDLOG_INFO("{} feed_channel_request {}",__func__,canvas_protocol->get_canvas_status().host_status.feed_channel_request);
            if(-1 != canvas_protocol->get_canvas_status().host_status.feed_channel_request)
            {
                gcode->run_script_from_command("CANVAS_LOAD_FILAMENT  CHANNEL=" + std::to_string(canvas_protocol->get_canvas_status().host_status.feed_channel_request));
            }
            break;
        case 2:
            gcode->run_script_from_command("CANVAS_UNLOAD_FILAMENT");
            break;
        case 3:
            exec_retry = true;
            break;
        default:
            break;
        }
    }

    if(!t_resume && canvas_protocol->get_canvas_status().host_status.error_code == 3)
    {
        uint64_t file_pos = v_sd->get_status()["file_position"].get<uint64_t>();

        reactor->register_callback([this, file_pos](double eventtime) {
            if(!is_cancel)
            {
                pause_resume->send_pause_command();
                gcode->run_script_from_command("PAUSE");
                gcode->run_script_from_command("M26 S"+ std::to_string(file_pos));
                gcode->run_script("\nM400\nM2202 GCODE_ACTION_REPORT=2506");
            }
            SPDLOG_INFO("mmu exec pause!! is_cancel {}",is_cancel);
            return json::object();
        });

        exit_retry = true;
    }

    if(0 == canvas_protocol->get_canvas_status().host_status.error_code)
    {
        clear_abnormal_feedback();
    }

    SPDLOG_INFO("{} #1 __OVER",__func__);
}


void Canvas::CMD_canvas_rfid_select(std::shared_ptr<GCodeCommand> gcmd)
{
    int id = gcmd->get_int("ID",0,0,3);
    int channel = gcmd->get_int("CHANNEL",0,0,3);
    int cancel = gcmd->get_int("CANCEL",0,0,1);
    SPDLOG_INFO("select_id:{},select_channel:{},cancel_state:{}",id,channel,cancel);
    if(!cancel)
    {
        if(canvas_protocol->get_canvas_status().mcu_status.connect_status)
        {
            canvas_protocol->get_canvas_status().host_status.canvas_lite.filament_info[channel] =
                canvas_protocol->get_canvas_status().host_status.rfid_data.filament_info;
        }
        else
        {
            canvas_protocol->get_canvas_status().host_status.extern_filament.filament_info =
                canvas_protocol->get_canvas_status().host_status.rfid_data.filament_info;
        }

        save_filament_info = true;
    }

    detected_rfid = false;
    is_report = false;
}

void Canvas::CMD_canvas_set_auto_plug_in(std::shared_ptr<GCodeCommand> gcmd)
{
    canvas_protocol->get_canvas_status().host_status.auto_plug_in_enable = gcmd->get_int("ENABLE",1,0,1);
}

double Canvas::limit_check(double value,double min,double max)
{
    if(value > max) value = max;
    if(value < min) value = min;
    return value;
}

void Canvas::CMD_M6211(std::shared_ptr<GCodeCommand> gcmd)
{
    SPDLOG_INFO("M6211 start");
    int32_t next_extruder = limit_check(gcmd->get_int("T",this->next_extruder),0,31);
    bool need_extrude_check = limit_check(gcmd->get_int("A",this->need_extrude_check),0,1);
    double flush_length_single = limit_check(gcmd->get_double("K",this->flush_length_single),10,120);
    double flush_length = limit_check(gcmd->get_double("L",this->flush_length),10,1000);
    double old_filament_e_feedrate = limit_check(gcmd->get_double("M",this->old_filament_e_feedrate),10,600);
    double new_filament_e_feedrate = limit_check(gcmd->get_double("N",this->new_filament_e_feedrate),10,600);
    double fan_speed_1 = limit_check(gcmd->get_double("I",this->fan_speed_1),0,255);
    double fan_speed_2 = limit_check(gcmd->get_double("J",this->fan_speed_2),0,255);
    double cool_time = limit_check(gcmd->get_double("P",this->cool_time),0,20000);
    double old_filament_temp = limit_check(gcmd->get_double("Q",this->old_filament_temp),200,350);
    double new_filament_temp = limit_check(gcmd->get_double("S",this->new_filament_temp),200,350);
    double nozzle_temperature_range_high = limit_check(gcmd->get_double("R",this->nozzle_temperature_range_high),200,350);
    double old_retract_length_toolchange = limit_check(gcmd->get_double("C",this->old_retract_length_toolchange),0,10);
    double new_retract_length_toolchange = limit_check(gcmd->get_double("D",this->new_retract_length_toolchange),0,10);

    SPDLOG_INFO("M6211 printable_height {} T {} A {} K {} L {} M {} N {} I {} J {} P {} Q {} S {} R {} C {} D {}",
        printable_height,next_extruder,need_extrude_check,flush_length_single,flush_length,
        old_filament_e_feedrate,new_filament_e_feedrate,fan_speed_1,fan_speed_2,cool_time,
        old_filament_temp,new_filament_temp,nozzle_temperature_range_high,
        old_retract_length_toolchange,new_retract_length_toolchange);

    if(get_T_channel(next_extruder) == canvas_protocol->get_canvas_status().host_status.feed_channel_current)
    {
        SPDLOG_INFO("M6211 over, same channel");
        return;
    }

    double max_layer_z = gcode_move->get_status(0)["position"][2].get<double>();
    double z_move = std::min(max_layer_z + z_up_height, printable_height + 0.5);
    double accel = toolhead->get_status(get_monotonic())["max_accel"].get<double>();
    SPDLOG_INFO("M6211 z_move {} accel {}",z_move,accel);

    gcode->run_script_from_command("M204 S10000\nG1 Z" + std:: to_string(z_move) + " F1200\nG92 E0\nG1 E-" + std::to_string(old_retract_length_toolchange) + " F1800\nM400\n");
    // true 则移动到垃圾桶
    if(need_extrude_check)
    {
        gcode->run_script_from_command("G180 S10\nM109 S" + std::to_string(old_filament_temp) + "\nM106 S" + std::to_string(fan_speed_1));
    }
    else
    {
        gcode->run_script_from_command("M104 S" + std::to_string(old_filament_temp) + "\nM106 S" + std::to_string(fan_speed_1));
    }

    // T 切换耗材
    gcode->run_script_from_command("CANVAS_SWITCH_FILAMENT T=" + std::to_string(next_extruder) + "\n");
    if(canvas_protocol->get_canvas_status().host_status.error_code)
    {
        gcode->run_script_from_command("M204 S" + std::to_string(accel) + "\n");
        return ;
    }

    // 冲刷
    gcode->run_script_from_command(
        "M400\n"
        "G92 E0\n"
        "M104 S" + std::to_string(nozzle_temperature_range_high) + "\n"
        "G1 E" + std::to_string(e_flush_dist) + " F" + std::to_string(old_filament_e_feedrate) + "\n"
    );

    int32_t flush_times = std::ceil((flush_length - e_flush_dist) / flush_length_single);
    double flush_length_actual = (flush_length - e_flush_dist) / flush_times;
    if(flush_times >= 1 && flush_length_actual > 0)
    {
        gcode->run_script_from_command(
            "G1 E" + std::to_string(flush_length_actual * 0.02) + " F50\n"
            "G1 E" + std::to_string(flush_length_actual * 0.23) + " F" + std::to_string(old_filament_e_feedrate) + "\n"
            "G1 E" + std::to_string(flush_length_actual * 0.02) + " F50\n"
            "G1 E" + std::to_string(flush_length_actual * 0.23) + " F" + std::to_string(new_filament_e_feedrate) + "\n"
            "G1 E" + std::to_string(flush_length_actual * 0.02) + " F50\n"
            "G1 E" + std::to_string(flush_length_actual * 0.23) + " F" + std::to_string(new_filament_e_feedrate) + "\n"
            "G1 E" + std::to_string(flush_length_actual * 0.02) + " F50\n"
            "G1 E" + std::to_string(flush_length_actual * 0.23) + " F" + std::to_string(new_filament_e_feedrate) + "\n"
            "G1 E-" + std::to_string(old_retract_length_toolchange) + " F1800"
            "G1 E" + std::to_string(old_retract_length_toolchange) + " F300"
            "M400"
        );
    }

    // 循环冲刷
    SPDLOG_INFO("M6211 flush_times {} flush_length_actual {}", flush_times,flush_length_actual);
    while (--flush_times > 0)
    {
        gcode->run_script_from_command(
            "G1 X" + std::to_string(x_flush_cool_pos) + " F10000\n"
            "M106 S" + std::to_string(fan_speed_2) + "\n"
            "G4 P" + std::to_string(cool_time) + "\n"
            "M204 S" + std::to_string(flush_accel) + "\n"
            "G1 Y" + std::to_string(y_flush_pos1) + " E-0.1\n"
            "G1 Y" + std::to_string(y_flush_pos2) + " E-0.1\n"
            "M400\n"
            "G1 Y" + std::to_string(y_flush_pos1) + " E-0.1\n"
            "G1 Y" + std::to_string(y_flush_pos2) + " E-0.1\n"
            "M400\n"
            "G1 X" + std::to_string(x_flush_pos3) + " F8000\n"
            "G1 X" + std::to_string(x_flush_pos4) + " E-0.1\n"
            "G1 X" + std::to_string(x_flush_pos3) + "\n"
            "G1 X" + std::to_string(x_flush_pos4) + " E-0.1\n"
            "G1 X52 Y245 F10000\n"
            "G1 Y264 F3000\n"
            "M400\n"
            "M106 S" + std::to_string(fan_speed_1)
        );

        gcode->run_script_from_command(
            "G1 E" + std::to_string(flush_length_actual * 0.18) + " F" + std::to_string(new_filament_e_feedrate) + "\n"
            "G1 E" + std::to_string(flush_length_actual * 0.02) + " F50\n"
            "G1 E" + std::to_string(flush_length_actual * 0.18) + " F" + std::to_string(new_filament_e_feedrate) + "\n"
            "G1 E" + std::to_string(flush_length_actual * 0.02) + " F50\n"
            "G1 E" + std::to_string(flush_length_actual * 0.18) + " F" + std::to_string(new_filament_e_feedrate) + "\n"
            "G1 E" + std::to_string(flush_length_actual * 0.02) + " F50\n"
            "G1 E" + std::to_string(flush_length_actual * 0.18) + " F" + std::to_string(new_filament_e_feedrate) + "\n"
            "G1 E" + std::to_string(flush_length_actual * 0.02) + " F50\n"
            "G1 E" + std::to_string(flush_length_actual * 0.18) + " F" + std::to_string(new_filament_e_feedrate) + "\n"
            "G1 E" + std::to_string(flush_length_actual * 0.02) + " F50\n"
            "G1 E-" + std::to_string(new_retract_length_toolchange) + " F1800\n"
            "G1 E" + std::to_string(new_retract_length_toolchange) + " F300\n"
            "M400"
        );
    }
 
    gcode->run_script_from_command(
        "M106 S" + std::to_string(fan_speed_2) + "\n"
        "G1 X" + std::to_string(x_flush_pos1) + " F10000\n"
        "M109 S" + std::to_string(new_filament_temp) + "\n"
        "G1 E" + std::to_string(e_flush_ex_dist) + " F" + std::to_string(new_filament_e_feedrate) + "\n"
        "M400\n"
        "G92 E0\n"
        "G1 X" + std::to_string(x_flush_pos2) + " F10000\n"
        "M106 S" + std::to_string(fan_speed_2) + "\n"
        "G4 P3000\n"
        "M204 S" + std::to_string(flush_accel) + "\n"
        "G1 Y" + std::to_string(y_flush_pos1) + " E-" + std::to_string(new_retract_length_toolchange/7) + " F10000\n"
        "G1 Y" + std::to_string(y_flush_pos2) + " E-" + std::to_string(new_retract_length_toolchange/7) + " F10000\n"
        "M400\n"
        "G1 Y" + std::to_string(y_flush_pos1) + " E-" + std::to_string(new_retract_length_toolchange/7) + " F10000\n"
        "G1 Y" + std::to_string(y_flush_pos2) + " E-" + std::to_string(new_retract_length_toolchange/7) + " F10000\n"
        "M400\n"
        "G1 X" + std::to_string(x_flush_pos3) + " F8000\n"
        "G1 X" + std::to_string(x_flush_pos4) + " E-" + std::to_string(new_retract_length_toolchange/7) + "\n"
        "G1 X" + std::to_string(x_flush_pos3) + "\n"
        "G1 X" + std::to_string(x_flush_pos4) + " E-" + std::to_string(new_retract_length_toolchange/7) + "\n"
        "G1 X" + std::to_string(x_flush_pos3) + "\n"
        "G1 X" + std::to_string(x_flush_pos4) + " E-" + std::to_string(new_retract_length_toolchange/7) + "\n"
        "G180 S818\n"
        "G1 X52 Y245 F10000\n"
        "G1 Y264 F3000\n"
        "M400\n"
        "M106 S" + std::to_string(fan_speed_1) + "\n"
        "M204 S" + std::to_string(accel)
    );

    SPDLOG_INFO("M6211 over. complete");
}

void Canvas::CMD_canvas_motor_control(std::shared_ptr<GCodeCommand> gcmd)
{
    if(!canvas_protocol->get_canvas_status().mcu_status.connect_status)
    {
        return;
    }

    int channel = gcmd->get_int("CHANNEL",get_cur_channel(),0,3);
    int dis = gcmd->get_int("DISTANCE",100,-1400,1400);
    int speed = gcmd->get_int("SPEED",motor_speed,0,1000);
    int accel = gcmd->get_int("ACCEL",motor_accel,10,10000);
    int timeout = gcmd->get_int("TIMEOUT",60,0,100);
    double wait_time = get_monotonic() + timeout;

    SPDLOG_INFO("exec motor control: channel {} dis {} speed {} accel {} timeout {}",
        channel, dis, speed, accel, timeout);
    if(channel < 0)
    {
        return;
    }

    if(speed > 0)
    {
        double cur_pos = canvas_protocol->get_canvas_status().mcu_status.ch_position[channel];

        FeederMotor feeder_motor;
        feeder_motor.mm = dis > 0 ? 1500 : -1500;
        feeder_motor.mm_s = speed;
        feeder_motor.mm_ss = accel;
        canvas_protocol->feeder_filament_control(channel,feeder_motor);

        while (get_monotonic() < wait_time)
        {
            reactor->pause(get_monotonic() + 0.1);
            // SPDLOG_INFO("{}   {}",fabs(canvas_protocol->get_canvas_status().mcu_status.ch_position[channel] - cur_pos) ,dis  );
            if(fabs(canvas_protocol->get_canvas_status().mcu_status.ch_position[channel] - cur_pos) > dis)
            {
                SPDLOG_INFO("send motor stop!");
                canvas_protocol->feeders_filament_stop(channel);
                SPDLOG_INFO("exec motor control,success!");
                break;
            }
        }

        if(get_monotonic() > wait_time && timeout != 0)
        {
            SPDLOG_INFO("exec motor control, timeout!");
            canvas_protocol->feeders_filament_stop(channel);
        }
    }
    else
    {
        canvas_protocol->feeders_filament_stop(channel);
    }
}

void Canvas::clear_abnormal_feedback()
{
    //
    gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_SERIAL_ERROR,elegoo::common::ErrorLevel::RESUME);
    gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_ABNORMAL_FILA,elegoo::common::ErrorLevel::RESUME);
    gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_CUTTING_KNIFET_NOT_TRIGGER,elegoo::common::ErrorLevel::RESUME);
    gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_CUTTING_KNIFET_TRIGGER,elegoo::common::ErrorLevel::RESUME);
    gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_PLUT_IN_FILA_NOT_TRIGGER,elegoo::common::ErrorLevel::RESUME);
    gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_PLUT_IN_FILA_TIMEOUT,elegoo::common::ErrorLevel::RESUME);
    gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_PLUT_IN_FILA_BLOCKED,elegoo::common::ErrorLevel::RESUME);
    gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_PLUT_OUT_FILA_ABNORMAL,elegoo::common::ErrorLevel::RESUME);
    gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_PLUT_OUT_FILA_TIMEOUT,elegoo::common::ErrorLevel::RESUME);
    //
    // gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_RUNOUT,elegoo::common::ErrorLevel::RESUME);
    // gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_CUT_KNIFET,elegoo::common::ErrorLevel::RESUME);
    // gcode->respond_ecode("", elegoo::common::ErrorCode::CANVAS_WRAP_FILA,elegoo::common::ErrorLevel::RESUME);
    // gcode->respond_ecode("", elegoo::common::ErrorCode::EXTERNAL_FILA_ERROR,elegoo::common::ErrorLevel::RESUME);
    SPDLOG_INFO("{} #1 __OVER",__func__);
}

void Canvas::set_error_code(int32_t error_code)
{
    SPDLOG_INFO("{} error_code {} {} report_error_code {}",__func__,error_code,canvas_protocol->get_canvas_status().host_status.error_code,canvas_protocol->get_canvas_status().host_status.report_error_code);

    canvas_protocol->get_canvas_status().host_status.error_code = error_code;
    if(0 != canvas_protocol->get_canvas_status().host_status.report_error_code)
    {
        if(canvas_protocol->get_canvas_status().host_status.error_code > 0
            && canvas_protocol->get_canvas_status().host_status.error_code <= 3)
        {
            gcode->respond_ecode("", canvas_protocol->get_canvas_status().host_status.report_error_code,elegoo::common::ErrorLevel::WARNING);
        }
        canvas_protocol->get_canvas_status().host_status.report_error_code = 0;
    }
}

bool Canvas::get_connect_state()
{
    return canvas_protocol->get_canvas_status().mcu_status.connect_status;
}

int32_t Canvas::get_cur_channel()
{
    return canvas_protocol->get_canvas_status().host_status.feed_channel_current;
}

std::shared_ptr<Canvas> canvas_dev_load_config(std::shared_ptr<ConfigWrapper> config)
{
    return std::make_shared<Canvas>(config);
}


}

}