/***************************************************************************** 
 * @Author       : Jack
 * @Date         : 2024-12-02 17:15:38
 * @LastEditors  : Jack
 * @LastEditTime : 2024-12-10 18:01:18
 * @Description  : 
 * 
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/

#pragma once
#include <functional>

#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "NetInterface.h"
#include "RtspServer.h"
#include "spdlog/spdlog.h"
#include "uvc_config_info.h"

namespace znp {

#define RTSP_FRAME_MAX_SIZE (300*1024)
#define RTSP_PORT 554
class RtspStreamer {
  using GetFarmeCallBack = std::function<bool(unsigned char *, int *)>;

 public:
  RtspStreamer(std::string ip, int port);
  ~RtspStreamer();

  void Start();
  void Stop();

  void SetServerIP(std::string ip);
  const std::string &GetServerIP();
  void GetServerInfo(std::string &ip, std::string &url);
  void RegisterGetFarmeCallBack(GetFarmeCallBack callback);

 private:
  void RtspServerThread();
  void VideoStreamerThread();  // 视频推流线程

 private:
  int port_;              // 推流地址端口
  unsigned int fps_{CAMERA_FPS};
  std::string rtsp_ip_;   // RTSP服务器IP
  std::string rtsp_url_;  // RTSP推流地址
  std::shared_ptr<xop::EventLoop> eventLoop_{nullptr};

  int frame_len_{0};
  unsigned char * frame_buf_{nullptr};
  std::atomic<bool> is_rtsp_server_alive_{false};
  std::atomic<bool> is_push_frame_alive_{false};
  xop::RtspServer * rtsp_server_{nullptr};
  xop::MediaSessionId sessionId_{0};

  GetFarmeCallBack get_frame_callback_;
};
}
