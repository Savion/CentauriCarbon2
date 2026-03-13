/*****************************************************************************
 * @Author       : Coconut
 * @Date         : 2025-03-14 09:51:04
 * @LastEditors  : coconut
 * @LastEditTime : 2025-03-14 09:51:04
 * @Description  : DSP辅助函数
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
#include "rpmsg.h"
#include "common/logger.h"

#define RPMSG_NAME_SIZE (32)

static inline int remoterpc_ctrl(int start)
{
    SPDLOG_INFO("remoterpc_ctrl {}", start);
    int fd = open("/sys/class/remoteproc/remoteproc0/state", O_WRONLY);
    if (fd == -1)
        return -1;
    const char *cmd = start ? "start" : "stop";
    write(fd, cmd, strlen(cmd));
    close(fd);
    return 0;
}

static inline int rpmsg_alloc_ept(const char *ctrl_dev_path, const char *name)
{
    SPDLOG_INFO("rpmsg_alloc_ept {} {}", ctrl_dev_path, name);
    struct rpmsg_ept_info info;
    strncpy(info.name, name, RPMSG_NAME_SIZE);
    info.id = -1;

    // 1. 打开控制设备
    int ctrl_fd = open(ctrl_dev_path, O_RDWR);
    if (ctrl_fd == -1)
    {
        SPDLOG_ERROR("rpmsg ctrl open failed {}", std::string(strerror(errno)));
        return -1;
    }

    // 2. 请求创建端点
    int ret = ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &info);
    if (ret < 0)
    {
        close(ctrl_fd);
        SPDLOG_ERROR("rpmsg ctrl ioctl failed {}", std::string(strerror(errno)));
        return -1;
    }
    close(ctrl_fd);

    return info.id;
}

static inline int rpmsg_free_ept(const char *ctrl_dev_path, int ept_id)
{
    SPDLOG_INFO("rpmsg_free_ept {} {}", ctrl_dev_path, ept_id);
    struct rpmsg_ept_info info;
    info.id = ept_id;

    // 1. 打开控制设备
    int ctrl_fd = open(ctrl_dev_path, O_RDWR);
    if (ctrl_fd == -1)
    {
        SPDLOG_ERROR("rpmsg ctrl open failed {}", std::string(strerror(errno)));
        return -1;
    }

    // 2. 请求创建端点
    int ret = ioctl(ctrl_fd, RPMSG_DESTROY_EPT_IOCTL, &info);
    if (ret < 0)
    {
        close(ctrl_fd);
        SPDLOG_ERROR("rpmsg ctrl ioctl failed {}", std::string(strerror(errno)));
        return -1;
    }
    close(ctrl_fd);

    return 0;
}
