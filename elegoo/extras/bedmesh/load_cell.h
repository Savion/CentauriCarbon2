/*****************************************************************************
 * @Author       : Coconut
 * @Date         : 2025-02-21 12:14:07
 * @LastEditors  : loping
 * @LastEditTime : 2025-06-10 21:09:01
 * @Description  : Load cell support
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#pragma once
#include <cmath>
#include <algorithm>
#include <array>
#include <memory>
#include <array>
#include <vector>
#include <string>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include "json.h"
#include "hx71x.h"
#include "any.h"
#include "bulk_sensor.h"
#include "webhooks.h"

class ConfigWrapper;
class Printer;
class GCodeCommand;
class GCodeDispatch;

namespace elegoo
{
    namespace extras
    {

        class LoadCellCommandHelper;
        class LoadCellSampleCollector;
        class LoadCellGuidedCalibrationHelper;
        class LoadCell;
        class HX71xBase;

        class ApiClientHelper
        {
        public:
            using ApiClientCallback = std::function<bool(const Any &)>;
            ApiClientHelper(std::shared_ptr<Printer> printer) : printer(printer)
            {
                webhooks_start_resp = json::object();
            }
            ~ApiClientHelper() = default;

            void send(const Any &msg)
            {
                for (auto it = client_cbs.begin(); it != client_cbs.end();)
                {
                    // 调用客户端注册的回调函数
                    bool res = (*it)(msg);
                    if (!res)
                    {
                        it = client_cbs.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            void add_client(ApiClientCallback client_cb)
            {
                client_cbs.push_back(client_cb);
            }

            void _add_webhooks_client(WebRequest *web_request)
            {
                auto whbatch = std::make_shared<BatchWebhooksClient>(web_request, any2json);
                add_client([whbatch](const Any &msg) -> bool
                           { whbatch->handle_batch(msg); return true; });
                web_request->send(webhooks_start_resp);
            }

            void add_mux_endpoint(const std::string &path, const std::string &key,
                                  const std::string &value, json &webhooks_start_resp, std::function<json(const Any &)> any2json = nullptr)
            {
                this->webhooks_start_resp = webhooks_start_resp;
                this->any2json = any2json;
                std::shared_ptr<WebHooks> wh = any_cast<std::shared_ptr<WebHooks>>(printer->lookup_object("webhooks"));
                wh->register_mux_endpoint(path, key, value, [this](std::shared_ptr<WebRequest> rq) -> void
                                          { this->_add_webhooks_client(rq.get()); });
            }

        private:
            std::shared_ptr<Printer> printer;
            std::vector<ApiClientCallback> client_cbs;
            json webhooks_start_resp;
            std::function<json(const Any &)> any2json;
        };

        class LoadCellCommandHelper
        {
        public:
            LoadCellCommandHelper(std::shared_ptr<ConfigWrapper> config, std::shared_ptr<LoadCell> load_cell);
            ~LoadCellCommandHelper() = default;
            double get_diagnostic_std_value();

        private:
            void register_commands(const std::string &name);
            void cmd_LOAD_CELL_TARE(std::shared_ptr<GCodeCommand> gcmd);
            void cmd_LOAD_CELL_CALIBRATE(std::shared_ptr<GCodeCommand> gcmd);
            void cmd_LOAD_CELL_READ(std::shared_ptr<GCodeCommand> gcmd);
            void cmd_LOAD_CELL_DIAGNOSTIC(std::shared_ptr<GCodeCommand> gcmd);

            // 校准相关
            std::shared_ptr<Printer> printer;
            std::shared_ptr<LoadCell> load_cell;
            std::shared_ptr<LoadCellGuidedCalibrationHelper> calibration_helper;

            std::string name;
            double diagnostic_std_value;
        };

        class LoadCellGuidedCalibrationHelper
        {
        public:
            LoadCellGuidedCalibrationHelper(std::shared_ptr<Printer> printer, std::shared_ptr<LoadCell> load_cell);
            void reset();
            ~LoadCellGuidedCalibrationHelper() = default;
            void verify_no_active_calibration();
            double counts_per_gram(double grams, int32_t cal_counts);
            int32_t capacity_kg(double counts_per_gram);
            void finalize(bool save_results);
            void cmd_ABORT(std::shared_ptr<GCodeCommand> gcmd);
            void cmd_ACCEPT(std::shared_ptr<GCodeCommand> gcmd);
            void cmd_TARE(std::shared_ptr<GCodeCommand> gcmd);
            void cmd_CALIBRATE(std::shared_ptr<GCodeCommand> gcmd);

        private:
            std::shared_ptr<GCodeDispatch> gcode;
            std::shared_ptr<LoadCell> load_cell;
            double _tare_counts;
            double _counts_per_gram;
            double tare_percent;
        };

        class LoadCellSampleCollector : public std::enable_shared_from_this<LoadCellSampleCollector>
        {
            const double RETRY_DELAY = 0.05;

        public:
            LoadCellSampleCollector(std::shared_ptr<Printer> printer, std::shared_ptr<LoadCell> load_cell);
            ~LoadCellSampleCollector();
            void start_collecting(double min_time = DOUBLE_NONE);
            HX71xBase::HX71xBaseClientSamples stop_collecting();
            HX71xBase::HX71xBaseClientSamples collect_min(int min_count = 1);
            HX71xBase::HX71xBaseClientSamples collect_until(double print_time = DOUBLE_NONE);
            double min_time;
            double max_time;
            double min_count;
            bool is_started;
            std::vector<HX71xBase::HX71xBaseSamples> _samples;
            int _errors, _overflows;

        private:
            bool _on_samples(const Any &msg);
            HX71xBase::HX71xBaseClientSamples _finish_collecting();
            HX71xBase::HX71xBaseClientSamples _collect_until(double timeout);

            std::shared_ptr<Printer> _printer;
            std::shared_ptr<LoadCell> _load_cell;
            std::shared_ptr<SelectReactor> _reactor;
            std::shared_ptr<MCU> _mcu;
        };

        class LoadCell : public std::enable_shared_from_this<LoadCell>
        {
            const double MIN_COUNTS_PER_GRAM = 1.;

        public:
            LoadCell() = default;
            void init(std::shared_ptr<ConfigWrapper> config, std::shared_ptr<HX71xBase> sensor);
            ~LoadCell() = default;
            void add_client(ApiClientHelper::ApiClientCallback client_cb);
            void tare(double tare_counts);
            void set_calibration(double counts_per_gram, double tare_counts);
            double counts_to_grams(int32_t sample);
            std::pair<int32_t, int32_t> saturation_range();
            double counts_to_percent(int32_t counts);
            double avg_counts(int num_samples = 0);
            bool is_tared();
            bool is_calibrated();
            std::shared_ptr<HX71xBase> get_sensor();
            int32_t get_reference_tare_counts();
            int32_t get_tare_counts();
            double get_counts_per_gram();
            std::shared_ptr<LoadCellSampleCollector> get_collector();
            json get_status(double eventtime);
            json any2json(const Any &msg);
            double get_diagnostic_std_value();

            void set_filter_sections(std::vector<std::vector<double>> filter_sections)
            {
                // 剔除a0参数
                filter.clear();
                for (const auto &section : filter_sections)
                {
                    std::vector<double> sos;
                    for (int i = 0; i < section.size(); i++)
                    {
                        if (i != 3)
                            sos.push_back(section[i]);
                    }
                    filter.push_back(sos);
                }

                // for (int i = 0; i < filter.size(); i++)
                // {
                //     printf("filter %d: ", i);
                //     for (int j = 0; j < filter[i].size(); j++)
                //         printf("%f,", filter[i][j]);
                //     printf("\n");
                // }
                reset_filter_state();
            }

            void set_tare_counts(double tare_counts)
            {
                this->tare_counts = (int32_t)tare_counts;
            }

            void set_cal(double slope, double intercept)
            {
                this->slope = slope;
                this->intercept = intercept;
            }

            std::shared_ptr<HX71xBase> sensor;

        private:
            void _handle_ready();
            double _sensor_data_event(const Any &msg);
            bool _on_sample(const Any &msg);
            bool _ss_client(const Any &data);
            json _force_g();

            std::shared_ptr<Printer> printer;
            std::shared_ptr<ApiClientHelper> clients;
            std::string config_name;
            std::string name;
            // std::deque<int32_t> _force_buffer;
            int32_t reference_tare_counts; // 默认的参考去皮值，从校准或者配置文件读取，仅开机后第一次将其用于初始化tare_counts有用。
            int32_t tare_counts;           // 去皮值，通过tare函数实时更新。
            double counts_per_gram;        // 每克对应的计数值，从校准或者配置文件读取。
            bool is_reversed;              // 数据反向
            int reverse;                   // 调用counts_to_grams时对数据进行方向
            std::shared_ptr<LoadCellCommandHelper> load_cell_command_helper;
            std::string ip;
            std::string port;

            double slope, intercept;

#define __MAX_SECTIONS 6
#define __SECTION_WIDTH 5
#define __STATE_WIDTH 2
            std::vector<std::vector<double>> filter;
            double filter_state[__MAX_SECTIONS][__STATE_WIDTH];
            void reset_filter_state()
            {
                for (uint8_t i = 0; i < __MAX_SECTIONS; i++)
                {
                    filter_state[i][0] = 0;
                    filter_state[i][1] = 0;
                }
            }

            double sosfilt(double in)
            {
                double cur_val = in;
                for (int section = 0; section < filter.size(); section++)
                {
                    double next_val = filter[section][0] * cur_val + filter_state[section][0];
                    filter_state[section][0] = filter[section][1] * cur_val - filter[section][3] * next_val + filter_state[section][1];
                    filter_state[section][1] = filter[section][2] * cur_val - filter[section][4] * next_val;
                    cur_val = next_val;
                }
                return cur_val;
            }
        };

        std::shared_ptr<LoadCell> load_cell_load_config(std::shared_ptr<ConfigWrapper> config);
        std::shared_ptr<LoadCell> load_cell_load_config_prefix(std::shared_ptr<ConfigWrapper> config);
    }
}