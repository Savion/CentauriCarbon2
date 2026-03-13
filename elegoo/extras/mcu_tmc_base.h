/***************************************************************************** 
 * @Author       : Jack
 * @Date         : 2024-12-21 11:05:06
 * @LastEditors  : Ben
 * @LastEditTime : 2025-02-28 12:02:59
 * @Description  : 
 * 
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/
#pragma once

#include "tmc.h"

namespace elegoo {
namespace extras {

class FieldHelper;

class MCU_TMC_Base {
public:
  virtual ~MCU_TMC_Base() = default;
  virtual std::shared_ptr<FieldHelper> get_fields() const { return nullptr; }
  virtual uint32_t get_register(const std::string& reg_name) { return 0; }
  virtual void set_register(const std::string& reg_name, int64_t val, double print_time = DOUBLE_NONE) {}  //to confirm
  virtual uint32_t get_tmc_frequency() const { return 0; };

 private: 

};

}
}
