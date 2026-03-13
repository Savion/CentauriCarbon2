/***************************************************************************** 
 * @Author       : Jack
 * @Date         : 2024-12-05 14:09:18
 * @LastEditors  : Jack
 * @LastEditTime : 2025-09-01 21:14:07
 * @Description  : 
 * 
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/

#pragma once
#include <functional>

#include "json.hpp"

namespace znp {

using json = nlohmann::json;
using RespondMsg = std::function<void(const std::string&)>;   // 回复数据
class CameraBase {
 private:
  /* data */
 public:
  CameraBase() {}
  virtual ~CameraBase() {}

  virtual void Start() = 0;
  virtual void Stop() = 0;
  virtual void StartMjpgStream() {}
  virtual void StartH264Stream() {}
  virtual void StopMjpgStream() {}
  virtual void StopH264Stream() {}

  virtual void CmdHandle(json &j_requset, json &j_respond) {}  // 系统信息获取
  virtual void RegisterRespondFun(RespondMsg respond_msg) {}
};

}  // namespace znp
