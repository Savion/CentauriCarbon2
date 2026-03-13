/*****************************************************************************
 * @Author       : Coconut
 * @Date         : 2025-03-14 09:51:04
 * @LastEditors  : coconut
 * @LastEditTime : 2025-03-14 09:51:04
 * @Description  : 串口升级通讯协议
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#include "serial_bootloader.h"
#include "serial.h"
#include <string>
#include <memory>
#include <map>
#include <vector>

#include "time.h"

enum
{
    BOOTLOADER_COM_PARSE_STA_A5 = 0,
    BOOTLOADER_COM_PARSE_STA_5A,
    BOOTLOADER_COM_PARSE_STA_CMD,
    BOOTLOADER_COM_PARSE_STA_LEN1,
    BOOTLOADER_COM_PARSE_STA_LEN2,
    BOOTLOADER_COM_PARSE_STA_DATA,
    BOOTLOADER_COM_PARSE_STA_CRC1,
    BOOTLOADER_COM_PARSE_STA_CRC2,
};

#define BOOTALODER_COM_MAGIC_SIZE 2
#define BOOTALODER_COM_LENGTH_SIZE 2
#define BOOTALODER_COM_CMD_SIZE 1
#define BOOTALODER_COM_CRC_SIZE 2

#define BOOTLOADER_COM_MAX_PAYLOAD 4096
#define BOOTLOADER_COM_PACKAGE_HEADER (BOOTALODER_COM_MAGIC_SIZE + BOOTALODER_COM_LENGTH_SIZE + \
                                       BOOTALODER_COM_CMD_SIZE)
#define BOOTLOADER_COM_MIN_PACKAGE_SIZE (BOOTALODER_COM_MAGIC_SIZE + BOOTALODER_COM_LENGTH_SIZE + \
                                         BOOTALODER_COM_CMD_SIZE + BOOTALODER_COM_CRC_SIZE)

enum
{
    BOOTLOADER_COM_CMD_ERASE = 0X00,
    BOOTLOADER_CMD_PROGRAM = 0x01,
    BOOTLOADER_CMD_JUMP = 0x02,
    BOOTLOADER_CMD_PING = 0x03,
};

typedef struct __attribute__((packed))
{
    uint32_t size;
} bootloader_cmd_erase_req_t;

typedef struct __attribute__((packed))
{
    uint32_t size;
} bootloader_cmd_erase_ack_t;

typedef struct __attribute__((packed))
{
    uint32_t offset;
    uint32_t length;
    uint32_t size;
} bootloader_cmd_program_req_t;

typedef struct __attribute__((packed))
{
    uint32_t length;
    uint32_t offset;
} bootloader_cmd_program_ack_t;

typedef struct __attribute__((packed))
{
    uint32_t ping;
} bootloader_cmd_ping_req_t;

typedef struct __attribute__((packed))
{
    uint32_t pong;
} bootloader_cmd_ping_ack_t;

static double get_monotonic(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * .000000001;
}

uint16_t bootloader_crc16_ccitt(uint8_t *buf, uint_fast16_t len)
{
    uint16_t crc = 0xffff;
    while (len--)
    {
        uint8_t data = *buf++;
        data ^= crc & 0xff;
        data ^= data << 4;
        crc = ((((uint16_t)data << 8) | (crc >> 8)) ^ (uint8_t)(data >> 4) ^ ((uint16_t)data << 3));
    }
    return crc;
}

void bootloader_package_data(uint8_t *buf, uint32_t size, uint8_t cmd, uint8_t *payload, uint32_t len)
{
    if (size < BOOTLOADER_COM_MIN_PACKAGE_SIZE + len)
    {
        printf("bootloader_package_data check failed\n");
        return;
    }
    uint8_t *pbuf = buf;
    uint16_t crc16 = bootloader_crc16_ccitt(payload, len);
    *pbuf++ = 0xa5;
    *pbuf++ = 0x5a;
    *pbuf++ = cmd;
    *pbuf++ = len >> 8;
    *pbuf++ = len & 0xff;
    pbuf += len;
    *pbuf++ = crc16 >> 8;
    *pbuf++ = crc16 & 0xff;
}

SerialBootloader::SerialBootloader(const std::string &port, int baudrate) : port(port), baudrate(baudrate)
{
    serial = std::make_shared<SerialPort>(port, baudrate);
}

SerialBootloader::~SerialBootloader()
{
    disconnect();
}

bool SerialBootloader::connect()
{
    if (!serial->open())
        return false;
    return true;
}

void SerialBootloader::disconnect()
{
    serial->close();
}

bool SerialBootloader::send_and_wait_ack(
    uint8_t *req_buf, uint32_t req_len,
    uint8_t cmd,
    uint8_t *payload, uint32_t payload_len,
    uint8_t *ack_buf, uint32_t ack_len,
    double timeout)
{
    // TX
    bootloader_package_data(req_buf, req_len, cmd, payload, payload_len);
    ssize_t wlen = serial->write(req_buf, req_len);
    if (wlen != req_len)
        return false;
    // if (wlen > 0)
    // {
    // printf("TX %d: ", wlen);
    // for (int i = 0; i < wlen; i++)
    // {
    //     printf("%x ", req_buf[i]);
    // }
    // printf("\n");
    // }
    // RX
    double end_time = get_monotonic() + timeout;
    uint8_t com_parse_buf[1024];
    uint8_t com_parse_cmd;
    uint8_t com_parse_sta = BOOTLOADER_COM_PARSE_STA_A5;
    uint16_t com_parse_count = 0, com_parse_len = 0, com_parse_crc = 0;
    uint8_t com_payload[BOOTLOADER_COM_MAX_PAYLOAD];
    uint8_t rx_done = 0;

    do
    {
        ssize_t rlen = serial->read(com_parse_buf, sizeof(com_parse_buf));

        // if (rlen > 0)
        // {
        //     printf("RX %d: ", rlen);
        //     for (int i = 0; i < rlen; i++)
        //     {
        //         printf("%x ", com_parse_buf[i]);
        //     }
        //     printf("\n");
        // }
        // 解析数据
        uint32_t i = 0;
        while (i < rlen)
        {
            // printf("com_parse_sta %d i %d rlen %d\n", com_parse_sta, i, rlen);
            switch (com_parse_sta)
            {
            case BOOTLOADER_COM_PARSE_STA_A5:
                if (com_parse_buf[i] == 0xa5)
                    com_parse_sta = BOOTLOADER_COM_PARSE_STA_5A;
                else
                    com_parse_sta = BOOTLOADER_COM_PARSE_STA_A5;
                break;
            case BOOTLOADER_COM_PARSE_STA_5A:
                if (com_parse_buf[i] == 0x5a)
                    com_parse_sta = BOOTLOADER_COM_PARSE_STA_CMD;
                else
                    com_parse_sta = BOOTLOADER_COM_PARSE_STA_A5;
                break;
            case BOOTLOADER_COM_PARSE_STA_CMD:
                com_parse_cmd = com_parse_buf[i];
                com_parse_sta = BOOTLOADER_COM_PARSE_STA_LEN1;
                break;
            case BOOTLOADER_COM_PARSE_STA_LEN1:
                com_parse_len = com_parse_buf[i];
                com_parse_sta = BOOTLOADER_COM_PARSE_STA_LEN2;
                break;
            case BOOTLOADER_COM_PARSE_STA_LEN2:
                com_parse_len = (com_parse_len << 8) | com_parse_buf[i];
                if (com_parse_len <= sizeof(com_payload))
                {
                    com_parse_count = 0;
                    com_parse_sta = BOOTLOADER_COM_PARSE_STA_DATA;
                }
                else
                {
                    com_parse_sta = BOOTLOADER_COM_PARSE_STA_A5;
                }
                break;
            case BOOTLOADER_COM_PARSE_STA_DATA:
                // 直接拷贝剩余数据
                {
                    uint32_t l = rlen - i;
                    l = l < com_parse_len - com_parse_count ? l : com_parse_len - com_parse_count;
                    memcpy(com_payload + com_parse_count, com_parse_buf + i, l);
                    com_parse_count += l;
                    i += l;
                    if (com_parse_count == com_parse_len)
                        com_parse_sta = BOOTLOADER_COM_PARSE_STA_CRC1;
                    else
                        com_parse_sta = BOOTLOADER_COM_PARSE_STA_DATA;
                    goto next;
                }
                break;
            case BOOTLOADER_COM_PARSE_STA_CRC1:
                com_parse_crc = com_parse_buf[i];
                com_parse_sta = BOOTLOADER_COM_PARSE_STA_CRC2;
                break;
            case BOOTLOADER_COM_PARSE_STA_CRC2:
                com_parse_crc = (com_parse_crc << 8) | com_parse_buf[i];
                if (bootloader_crc16_ccitt(com_payload, com_parse_len) == com_parse_crc)
                {
                    if (com_parse_cmd == cmd)
                    {
                        memcpy(ack_buf, com_payload, ack_len);
                        rx_done = 1;
                    }
                    // Reset status
                    com_parse_sta = BOOTLOADER_COM_PARSE_STA_A5;
                    com_parse_count = 0;
                    com_parse_len = 0;
                    com_parse_crc = 0;
                }
                com_parse_sta = BOOTLOADER_COM_PARSE_STA_A5;
                break;
            }
            i++;
        next:
            continue;
        }
    } while (!rx_done && get_monotonic() < end_time);

    if (!rx_done)
    {
        // printf("timeout\n");
        return false;
    }
    return true;
}

bool SerialBootloader::ping()
{
    uint8_t req[sizeof(bootloader_cmd_ping_req_t) + BOOTLOADER_COM_MIN_PACKAGE_SIZE];
    bootloader_cmd_ping_req_t *header = (bootloader_cmd_ping_req_t *)(req + BOOTLOADER_COM_PACKAGE_HEADER);
    header->ping = 0x12345678;
    bootloader_cmd_ping_ack_t ack;
    if (send_and_wait_ack(req, sizeof(req), BOOTLOADER_CMD_PING, (uint8_t *)header, sizeof(*header), (uint8_t *)&ack, sizeof(ack), 0.2))
    {
    }
    else
    {
        printf("ping bootloader failed\n");
        return false;
    }
    return header->ping == ack.pong;
}

bool SerialBootloader::erase_flash(uint32_t size)
{
    uint8_t req[sizeof(bootloader_cmd_erase_req_t) + BOOTLOADER_COM_MIN_PACKAGE_SIZE];
    bootloader_cmd_erase_req_t *header = (bootloader_cmd_erase_req_t *)(req + BOOTLOADER_COM_PACKAGE_HEADER);
    header->size = size;
    bootloader_cmd_erase_ack_t ack;

    if (send_and_wait_ack(req, sizeof(req), BOOTLOADER_COM_CMD_ERASE, (uint8_t *)header, sizeof(*header), (uint8_t *)&ack, sizeof(ack), 5.0))
    {
        // printf("erase_flash header size %u ack %u\n", header->size, ack.size);
    }
    else
    {
        // printf("erase_flash failed header size %u ack %u\n", header->size, ack.size);
        return false;
    }
    return header->size <= ack.size;
}

uint16_t SerialBootloader::program_flash(uint8_t *buf, uint32_t offset, uint16_t length, uint32_t size)
{
#define PACKAGE_SIZE 1024
    // 限制写入范围
    if (offset + length > size)
        length = size - offset;
    if (length > PACKAGE_SIZE)
        length = PACKAGE_SIZE;
    uint8_t req[sizeof(bootloader_cmd_program_req_t) + BOOTLOADER_COM_MIN_PACKAGE_SIZE + PACKAGE_SIZE];
    bootloader_cmd_program_req_t *header = (bootloader_cmd_program_req_t *)(req + BOOTLOADER_COM_PACKAGE_HEADER);
    header->offset = offset;
    header->length = length;
    header->size = size;

    memcpy((uint8_t *)header + sizeof(bootloader_cmd_program_req_t), buf, length);
    bootloader_cmd_program_ack_t ack;
    if (send_and_wait_ack(req, sizeof(req), BOOTLOADER_CMD_PROGRAM, (uint8_t *)header, sizeof(*header) + length, (uint8_t *)&ack, sizeof(ack), 5.0))
    {
        // printf("program_flash offset %u length %u size %u ack length %u offset %u\n", offset, length, size, ack.length, ack.offset);
    }
    else
    {
        // printf("program_flash failed\n");
        return 0;
    }

    return ack.length;
}

bool SerialBootloader::jump_to_app()
{
    uint8_t req[BOOTLOADER_COM_MIN_PACKAGE_SIZE];
    bootloader_package_data(req, sizeof(req), BOOTLOADER_CMD_JUMP, NULL, 0);
    ssize_t wlen = serial->write(req, sizeof(req));
    // if (wlen > 0)
    // {
    //     printf("TX %d: ", wlen);
    //     for (int i = 0; i < wlen; i++)
    //     {
    //         printf("%x ", req[i]);
    //     }
    //     printf("\n");
    // }
    if (wlen != sizeof(req))
        return false;
    return true;
}

bool SerialBootloader::jump_to_bootloader()
{
    // printf("try jump to bootloader\n");
#define MAGIC " \x1c Request Serial Bootloader!! ~"
    serial->write(MAGIC, sizeof(MAGIC) - 1);
    return true;
}

bool SerialBootloader::update(const char *firmware)
{
    uint8_t buf[4096];
    uint32_t size = 0;
    // 获取固件信息
    FILE *fp = fopen(firmware, "rb");
    if (!fp)
    {
        // printf("fopen %s failed\n", firmware);
        return false;
    } // 获取文件大小
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // 获取固件二进制大小
    uint32_t fsize = size;
    uint32_t foffset = 0;
    ssize_t flength = 0;

    // 擦除
    if (!erase_flash(fsize))
    {
        printf("erase_flash failed\n");
        return false;
    }
    else
    {
        printf("erase_flash complted\n");
    }
    do
    {
        // 读取文件
        flength = sizeof(buf) < fsize - foffset ? sizeof(buf) : fsize - foffset;
        flength = fread(buf, 1, flength, fp);
        // 编程FLASH
        uint32_t flash_start = foffset;
        uint32_t flash_end = foffset + flength;
        printf("flash_start %u flash_end %u flength %zd\n", flash_start, flash_end, flength);
        uint32_t len = 0;
        do
        {
            uint16_t plen = program_flash(buf + len, flash_start, flash_end - flash_start, fsize);
            if (plen <= 0)
            {
                printf("program_flash failed\n");
                return false;
            }
            len += plen;
            flash_start += plen;
            // printf("#2 flash_start %d  flash_end %d plen %d\n", flash_start, flash_end, plen);
        } while (flash_start < flash_end);

        foffset += flength;
    } while (foffset < fsize);
    printf("program_flash done\n");
    fclose(fp);
    return true;
}