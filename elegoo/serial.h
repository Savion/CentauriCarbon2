/*****************************************************************************
 * @Author       : Coconut
 * @Date         : 2025-03-14 09:51:04
 * @LastEditors  : coconut
 * @LastEditTime : 2025-03-14 09:51:04
 * @Description  : 串口通讯
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/
#pragma once
#include <string>
#include <memory>
#include <functional>
#include <map>
#include <vector>

#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

// termios2 is a kernel struct for custom baud rates; not exposed by glibc.
// Guard against asm-generic/termbits.h already providing these definitions.
#ifndef __ASM_GENERIC_TERMBITS_H
# ifndef BOTHER
#  define BOTHER 0010000
#  define TCGETS2 _IOR('T', 0x2A, struct termios2)
#  define TCSETS2 _IOW('T', 0x2B, struct termios2)
struct termios2 {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_line;
    cc_t     c_cc[19];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};
# endif
#endif

class SerialPort
{
public:
    SerialPort(const std::string &port, int baudrate) : port(port), fd(-1), baudrate(baudrate)
    {
        baudrate_map = {
            {0, B0},
            {50, B50},
            {75, B75},
            {110, B110},
            {134, B134},
            {150, B150},
            {200, B200},
            {300, B300},
            {600, B600},
            {1200, B1200},
            {1800, B1800},
            {2400, B2400},
            {4800, B4800},
            {9600, B9600},
            {19200, B19200},
            {38400, B38400},
            {57600, B57600},
            {115200, B115200},
            {230400, B230400},
            {460800, B460800},
            {500000, B500000},
            {576000, B576000},
            {921600, B921600},
            {1000000, B1000000},
            {1152000, B1152000},
            {1500000, B1500000},
            {2000000, B2000000},
            {2500000, B2500000},
            {3000000, B3000000},
            {3500000, B3500000},
            {4000000, B4000000},
        };
    }

    ~SerialPort()
    {
        // 这里不要关闭FD,因为后面start_session依赖于这个FD没有关闭
        // ::close(fd);
    }

    bool open()
    {
        fd = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd == -1)
        {
            printf("open: %d %s\n", errno, strerror(errno));
            return false;
        }

        struct termios tty;
        if (tcgetattr(fd, &tty) != 0)
        {
            printf("tcgetattr: %d %s\n", errno, strerror(errno));
            ::close(fd);
            return false;
        }

        // 奇偶校验
        tty.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
        // tty.c_cflag |= PARENB;  // Set parity bit, enabling parity
        // tty.c_cflag |= PARODD;
        // 停止位
        tty.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
        // tty.c_cflag |= CSTOPB;  // Set stop field, two stop bits used in communication

        // 设置数据位
        tty.c_cflag &= ~CSIZE; // Clear all the size bits, then use one of the statements below
        // tty.c_cflag |= CS5;    // 5 bits per byte
        // tty.c_cflag |= CS6;    // 6 bits per byte
        // tty.c_cflag |= CS7;    // 7 bits per byte
        tty.c_cflag |= CS8; // 8 bits per byte (most common)

        // 设置流控
        tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
        // tty.c_cflag |= CRTSCTS;  // Enable RTS/CTS hardware flow control

        // 关闭控制信号启动读取
        tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)

        // 关闭规范模式
        tty.c_lflag &= ~ICANON;
        tty.c_lflag &= ~ECHO;   // Disable echo
        tty.c_lflag &= ~ECHOE;  // Disable erasure
        tty.c_lflag &= ~ECHONL; // Disable new-line echo
        tty.c_lflag &= ~ISIG;   // Disable interpretation of INTR, QUIT and SUSP
        // 输入处理
        tty.c_iflag &= ~(IXON | IXOFF | IXANY);                                      // Turn off s/w flow ctrl
        tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL); // Disable any special handling of received bytes

        // 输出处理
        tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
        tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed
                               // tty.c_oflag &= ~OXTABS; // Prevent conversion of tabs to spaces (NOT PRESENT IN LINUX)
                               // tty.c_oflag &= ~ONOEOT; // Prevent removal of C-d chars (0x004) in output (NOT PRESENT IN LINUX)

        tty.c_cc[VTIME] = 0;
        tty.c_cc[VMIN] = 0;

        // 设置波特率
        if (baudrate_map.find(baudrate) != baudrate_map.end())
        {
            cfsetispeed(&tty, baudrate_map.at(baudrate));
            cfsetospeed(&tty, baudrate_map.at(baudrate));
        }
        else
        {
            cfsetispeed(&tty, baudrate);
            cfsetospeed(&tty, baudrate);
        }

        if (tcsetattr(fd, TCSANOW, &tty) != 0)
        {
            printf("tcgetattr: %d %s\n", errno, strerror(errno));
            ::close(fd);
            return false;
        }

        // 设置阻塞
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1)
        {
            printf("fcntl F_GETFL: %d %s\n", errno, strerror(errno));
            return 1;
        }
        flags |= O_NONBLOCK;
        if (fcntl(fd, F_SETFL, flags) == -1)
        {
            printf("fcntl F_SETFL: %d %s\n", errno, strerror(errno));
            return false;
        }

        // 兼容非标波特率
        if (baudrate_map.find(baudrate) == baudrate_map.end())
        {
            struct termios2 tio = {0};
            int ret = ioctl(fd, TCGETS2, &tio);

            tio.c_cflag &= ~CBAUD;
            tio.c_cflag |= BOTHER;
            tio.c_ispeed = baudrate;
            tio.c_ospeed = baudrate;
            ret = ioctl(fd, TCSETS2, &tio);
        }
        tcflush(fd, TCIOFLUSH);
        return true;
    }

    ssize_t read(void *buf, size_t size)
    {

        return ::read(fd, buf, size);
    }

    ssize_t write(const void *buf, size_t size)
    {
        return ::write(fd, buf, size);
    }

    void close()
    {
        if (fd != -1)
            ::close(fd);
        fd = -1;
    }

    int get_fd() const { return fd; }
    int get_baud() const { return baudrate; }

    void set_rts(bool state)
    {
        int status;
        ioctl(fd, TIOCMGET, &status);
        if (state)
            status |= TIOCM_RTS;
        else
            status &= ~TIOCM_RTS;
        ioctl(fd, TIOCMSET, &status);
    }

    void set_dtr(bool state)
    {
        int status;
        ioctl(fd, TIOCMGET, &status);
        if (state)
            status |= TIOCM_DTR;
        else
            status &= ~TIOCM_DTR;
        ioctl(fd, TIOCMSET, &status);
    }

private:
    std::string port;
    int baudrate;
    int fd;
    std::map<int, speed_t> baudrate_map;
};
