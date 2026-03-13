/***************************************************************************** 
 * @Author       : Jack
 * @Date         : 2024-10-31 21:52:39
 * @LastEditors  : Ben
 * @LastEditTime : 2025-04-10 12:23:42
 * @Description  : 
 * 
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/

#pragma once
#include <functional>

#include <map>
#include "uart.h"
#include "uart_protocol.h"
#include "camera_uc03.h"
#include "uvc_config_info.h"
#include "uvc_v4l2.h"
#include "video_list.h"
#include "aiCamera_cmd.h"
#include "store_time.h"
namespace znp {

class UVCCamera {
  using UartCallBack = std::function<void(uint8_t, uint8_t, uint8_t *)>;
  using Handler = std::function<void(uint8_t *, uint16_t)>;
  using CmdHandlerMap = std::map<uint8_t, Handler>;

 public:
  UVCCamera();
  ~UVCCamera();

  void Start();
  void Stop();
  void HeartBeat();
  bool IsVideoAlive() const;
  bool IsUARTAlive() const;
  void RegisterUartReceivedCallBack(UartCallBack callback);
  bool SendCmdToCamera(CameraCmdCode cmd);
  bool StartStreamOfMjpeg();
  bool StopStreamOfMjpeg();
  bool GetFrameOfMjpeg(unsigned char *pBuf, int *pLen);   // 获取一帧JPEG图片 (用于延时摄影抓拍)
  bool GetFrameOfH264(unsigned char *pBuf, int *pLen);

  bool LedClose();    // 关闭LED
  bool LedOpen();     // 打开LED
  bool LedGetStatus();// 获取LED开关状态

  bool AIGetStatus(); // 获取AI开关状态
  bool AIOpen();      // 打开AI功能
  bool AIClose();     // 关闭AI功能

  bool CatchPictureOn();  // 打开JPEG抓拍图片
  bool CatchPictureOFF(); // 关闭JPEG抓拍图片
  bool CatchPictureClean(); // 清除抓拍图片标志

  bool VersionInfo();     // 获取版本号

  bool WireDrawingCheckStart();   // 打开炒面识别功能
  bool WireDrawingCheckOnce();    // 炒面识别一次
  bool WireDrawingCheckStop();    // 关闭炒面识别功能

  bool ForeignBodyCheckStart();   // 打开异物识别功能
  bool ForeignBodyCheckOnce();    // 异物识别一次
  bool ForeignBodyCheckStop();    // 打关闭异物识别功能

 private:
  bool AnalysisCmd(CameraCmdCode cmd);  // 命令解析
  bool ControlDevice(const uint8_t cmd, const uint8_t param);
  void UartReceivedCallBack(uint8_t *data, int len);
  void V4l2ReceivedCallBack(uint8_t *data, int len);

  void HeartBeatHandle(uint8_t *param, uint16_t len);
  void CatchPictureHandle(uint8_t *param, uint16_t len);
  void CatchPictureCleanHandle(uint8_t *param, uint16_t len);
  void WireDrawingCheckHandle(uint8_t *param, uint16_t len);
  void VersionHandle(uint8_t *param, uint16_t len);
  void ForeignBodyCheckHandle(uint8_t *param, uint16_t len);
  void LedHandle(uint8_t *param, uint16_t len);
  void AiHandle(uint8_t *param, uint16_t len);

 private:
  std::shared_ptr<znp::Uart> uart_;
  std::shared_ptr<znp::V4L2> v4l2_h264_;
  std::shared_ptr<znp::V4L2> v4l2_jpeg_;
  znp::Protocol protocol_;                    // 串口通信协议
  znp::FrameMessage message_;                 // 帧头
  std::shared_ptr<VideoList> videoList_;                       // 视频缓冲队列
  std::atomic<uint64_t> uart_time_stamp_{0};  // 串口数据时间戳
  std::atomic<bool> isUartBusy;               // 串口是否空闲状态
  std::mutex uart_write_mutex_;               // 串口写入互斥锁
  std::mutex get_h264_mutex_;                 // 获取H264流互斥锁

  const UvcCameraInfo uvc_camera_info;
  UartCallBack uart_received_callback_;  // 上层回调->获取串口解析的数据
  CmdHandlerMap handler_map_;            // 串口数据处理
};

}  // namespace znp
