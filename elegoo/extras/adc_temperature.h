/***************************************************************************** 
 * @Author       : Jack
 * @Date         : 2024-11-28 17:23:26
 * @LastEditors  : loping
 * @LastEditTime : 2025-04-25 16:02:50
 * @Description  : Obtain temperature using linear interpolation of ADC values
 * 
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
#include "configfile.h"
#include "mcu.h"
#include "printer.h"
#include "heater_base.h"
#include "error_mcu.h"
#include "liner_base.h"

namespace elegoo {
namespace extras {


class HelperTemperatureDiagnostics;
class LinearInterpolate;
class LinearBase;
class LinearVoltage;
class LinearResistance;
class CustomLinearBase;
class CustomLinearVoltage;
class CustomLinearResistance;
class PrinterADCtoTemperature;

class HelperTemperatureDiagnostics {
 public:
  HelperTemperatureDiagnostics(std::shared_ptr<ConfigWrapper> config,
                               std::shared_ptr<MCU_adc> mcu_adc,
                               std::function<double(double)> calc_temp_cb);
  void setup_diag_minmax(double min_temp, double max_temp, double min_adc,
                         double max_adc);
  std::string clarify_adc_range(const std::string& msg,
                const std::map<std::string, std::string>& details) const;

 private:
  std::shared_ptr<Printer> printer;
  std::string name;
  std::shared_ptr<MCU_adc> mcu_adc;
  std::function<double(double)> calc_temp_cb;
  double min_temp, max_temp, min_adc, max_adc;
};

class LinearInterpolate {
 public:
  LinearInterpolate(const std::vector<std::pair<double, double>>& samples);
  double interpolate(double key) const;
  double reverse_interpolate(double value) const;

 private:
  std::vector<double> keys;
  std::vector<std::pair<double, double>> slopes;
};

class LinearVoltage : public LinearBase {
 public:
  LinearVoltage(std::shared_ptr<ConfigWrapper> config,
                const std::vector<std::pair<double, double>>& params);
  double calc_temp(double adc) const override;
  double calc_adc(double temp) const override;

 private:
  std::shared_ptr<LinearInterpolate> li;
};

class LinearResistance : public LinearBase {
 public:
  LinearResistance(std::shared_ptr<ConfigWrapper> config,
                   const std::vector<std::pair<double, double>>& samples);
  double calc_temp(double adc) const override;
  double calc_adc(double temp) const override;

 private:
  double pullup;
  std::shared_ptr<LinearInterpolate> li;
};

class CustomLinearBase {
 public:
  CustomLinearBase(std::shared_ptr<ConfigWrapper> config){};
  virtual std::shared_ptr<PrinterADCtoTemperature> create(
      std::shared_ptr<ConfigWrapper> config){ return nullptr; };

  virtual std::shared_ptr<LinearBase> get_liner(std::shared_ptr<ConfigWrapper> config) { return nullptr; };
  std::string name;
  std::vector<std::pair<double, double>> params;
};

class CustomLinearVoltage : public CustomLinearBase {
 public:
  CustomLinearVoltage(std::shared_ptr<ConfigWrapper> config);
  std::shared_ptr<PrinterADCtoTemperature> create(
      std::shared_ptr<ConfigWrapper> config) override;
  std::shared_ptr<LinearBase> get_liner(std::shared_ptr<ConfigWrapper> config) override;
 private:
};

class CustomLinearResistance : public CustomLinearBase {
 public:
  CustomLinearResistance(std::shared_ptr<ConfigWrapper> config);
  std::shared_ptr<PrinterADCtoTemperature> create(
      std::shared_ptr<ConfigWrapper> config) override;
  std::shared_ptr<LinearBase> get_liner(std::shared_ptr<ConfigWrapper> config) override;
 private:
};

class PrinterADCtoTemperature : public HeaterBase {
 public:
  PrinterADCtoTemperature(std::shared_ptr<ConfigWrapper> config,
                          std::shared_ptr<LinearBase> adc_convert);
  void setup_callback(std::function<void(double, double)> callback);
  double get_report_time_delta() const;
  void adc_callback(double read_time, double read_value);
  void setup_minmax(double min_temp, double max_temp);

 private:
  std::shared_ptr<LinearBase> adc_convert;
  std::shared_ptr<MCU_adc> mcu_adc;
  std::string name;
  bool is_adc_temp_fault;
  std::shared_ptr<HelperTemperatureDiagnostics> diag_helper;
  std::function<void(double, double)> temperature_callback;
};

std::shared_ptr<void> adc_temperature_load_config(std::shared_ptr<ConfigWrapper> config);
std::shared_ptr<void> adc_temperature_load_config_prefix(std::shared_ptr<ConfigWrapper> config); 

}  
}