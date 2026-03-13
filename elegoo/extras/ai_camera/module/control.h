/*****************************************************************************
 * @Author       : Jack
 * @Date         : 2024-12-05 12:28:44
 * @LastEditors  : Jack
 * @LastEditTime : 2025-07-21 10:29:12
 * @Description  :
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#pragma once
#include <functional>

#include <iostream>

#include "camera_base.h"
// #include "communicator.h"
#include "server_socket.h"
#include "uvc_camera_without_uart.h"
#include "hv/EventLoop.h"
#include "hv/hv.h"
#include "ai_camera.h"
#include "uvc_config_info.h"

namespace znp {

class Control {
using Handler = std::function<void(json&, json&)>;
 public:
  Control(std::string socket_path);
  ~Control();

  void Start();
  void Stop();
  void Loop();

 private:
  void CameraStatusMonitor();
  void MessageCmdParse(json &jsonRequest, json &jsonResponse);
  void RegisterCmdHandler(Handler handler);
  void OnPairMessageReceived(const std::string &message);  // 接收点对点数据
  // void MessageReceivedHandle(json &jsonResponse, std::string &request_type, json &params);

 private:
  std::atomic<bool> isCameraDevice{true};    // 摄像头设备文件是否存在 true: 存在; false: 不存在
  std::string socket_path_;
  Handler camera_handler_;  // 摄像头处理外部命令回调函数指针
  std::shared_ptr<CameraBase> ai_camera_;
  std::shared_ptr<ServerSocket> server_socket_;
  std::shared_ptr<hv::EventLoop> timer_loop_;
};

}  // namespace znp
