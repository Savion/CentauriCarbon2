/***************************************************************************** 
 * @Author       : Jack
 * @Date         : 2024-10-31 21:52:39
 * @LastEditors  : Jack
 * @LastEditTime : 2024-12-10 11:54:40
 * @Description  : 
 * 
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/

#pragma once

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

namespace znp {
using V4l2CallBack = std::function<void(uint8_t*, int)>;
class V4L2 {
 public:
  V4L2(const char* device_name, int video_width, int video_hight,
       unsigned int video_format, unsigned int fps);
  ~V4L2();

  void Start();
  void Stop();
  bool IsOpen() const;

  bool Open();
  void Close();
  bool GetOneFrame(uint8_t* pbuf, int* len);
  void RegisterVideoReceivedCallBack(V4l2CallBack callback);

 private:
  bool OpenDevice();
  bool CloseDevice();
  void PrintSupportFormat(int fd);
  bool SetFormat();
  bool RequestBuffers();
  bool MapBuffers();
  bool UnmapBuffers();
  bool EnqueueBuffers();
  bool StartStreaming();
  bool StopStreaming();
  bool ReleaseBuffers();
  void GetFramesThread();

 private:
  int fd_;
  std::string device_;
  int video_width_;
  int video_hight_;
  int fps_;
  unsigned int video_format_;
  unsigned char* buffers_[4];
  struct v4l2_buffer buf_;
  std::atomic<bool> isAlive_;
  V4l2CallBack v4l2_callback_;
  std::mutex video_ioctl_mutex_;  // 视频操作互斥锁
};
}
