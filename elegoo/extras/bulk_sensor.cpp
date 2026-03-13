/*****************************************************************************
 * @Author       : Gary
 * @Date         : 2024-11-01 05:23:08
 * @LastEditors  : coconut
 * @LastEditTime : 2025-02-12 16:15:09
 * @Description  : Tools for reading bulk sensor data from the mcu.
 * This "bulk sensor" module facilitates the processing of sensor chip
 * measurements that do not require the host to respond with low
 * latency.  This module helps collect these measurements into batches
 * that are then processed periodically by the host code (as specified
 * by BatchBulkHelper.batch_interval).  It supports the collection of
 * thousands of sensor measurements per second.
 *
 * Processing measurements in batches reduces load on the mcu, reduces
 * bandwidth to/from the mcu, and reduces load on the host.  It also
 * makes it easier to export the raw measurements via the webhooks
 * system (aka API Server).
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#include <iostream>
#include <vector>
#include <functional>
#include <stdexcept>
#include <memory>
#include <chrono>
#include <thread>
#include <mutex>
#include "bulk_sensor.h"
#include "webhooks.h"
namespace elegoo
{
    namespace extras
    {
#define MAX_BULK_MSG_SIZE 51

        class BulkCommandError : public std::runtime_error
        {
        public:
            using std::runtime_error::runtime_error;
        };

        BatchBulkHelper::BatchBulkHelper(std::shared_ptr<Printer> printer, BatchCallback batch_cb,
                                         std::function<void()> start_cb,
                                         std::function<void()> stop_cb,
                                         double batch_interval)
            : printer(printer), batch_cb(batch_cb), batch_interval(batch_interval), is_started(false)
        {
            this->start_cb = start_cb ? start_cb : []() {};
            this->stop_cb = stop_cb ? stop_cb : []() {};
        }

        void BatchBulkHelper::_start()
        {
            if (is_started)
            {
                return;
            }
            is_started = true;

            try
            {
                // SPDLOG_INFO("BatchBulkHelper::_start call start_cb");
                start_cb();
            }
            catch (const BulkCommandError &e)
            {
                std::cerr << "BatchBulkHelper start callback error: " << e.what() << "\n";
                is_started = false;
                client_cbs.clear();
                throw;
            }

            double systime = get_monotonic();
            double waketime = systime + batch_interval;

            batch_timer = [this](double eventtime)
            { return this->_proc_batch(eventtime); };
            timer_handler = printer->get_reactor()->register_timer(batch_timer, waketime, "bulk_sensor");
        }

        void BatchBulkHelper::_stop()
        {
            client_cbs.clear();
            if (timer_handler)
            {
                printer->get_reactor()->unregister_timer(timer_handler);
                timer_handler = nullptr;
            }

            if (!is_started)
            {
                return;
            }

            try
            {
                // SPDLOG_INFO("BatchBulkHelper::_stop call stop_cb");
                stop_cb();
            }
            catch (const BulkCommandError &e)
            {
                // SPDLOG_INFO("BatchBulkHelper batch callback error: " + std::string(e.what()));
                client_cbs.clear();
            }
            is_started = false;

            if (!client_cbs.empty())
            {
                _start();
            }
        }

        // 定时器唤醒，在协程里启动
        double BatchBulkHelper::_proc_batch(double eventtime)
        {
            // 处理数据
            // SPDLOG_DEBUG("BulkDataQueue::_proc_batch");
            Any msg;
            try
            {
                msg = batch_cb(eventtime);
            }
            catch (const BulkCommandError &e)
            {
                // SPDLOG_INFO("BatchBulkHelper batch callback error: " + std::string(e.what()));
                _stop();
                return printer->get_reactor()->NEVER;
            }

            if (msg.empty())
                return eventtime + batch_interval;

            for (auto it = client_cbs.begin(); it != client_cbs.end();)
            {
                // 调用客户端注册的回调函数
                //SPDLOG_DEBUG("BulkDataQueue::_proc_batch call client\n");
                bool res = (*it)(msg);
                if (!res)
                {
                    // printf("_proc_batch remove %p --- %d\n", *it, client_cbs.size());
                    it = client_cbs.erase(it);
                    if (client_cbs.empty())
                    {
                        // SPDLOG_INFO("BulkDataQueue::_proc_batch stop\n");
                        _stop();
                        return printer->get_reactor()->NEVER;
                    }
                }
                else
                {
                    ++it;
                }
            }
            return eventtime + batch_interval;
        }

        void BatchBulkHelper::add_client(ClientCallback client_cb)
        {
            SPDLOG_DEBUG("add_client\n");
            client_cbs.push_back(client_cb);
            _start();
        }

        void BatchBulkHelper::_add_api_client(WebRequest *web_request)
        {
            auto whbatch = std::make_shared<BatchWebhooksClient>(web_request, any2json);
            add_client([whbatch](const Any &msg) -> bool
                       { whbatch->handle_batch(msg); return true; });
            web_request->send(webhooks_start_resp);
        }

        void BatchBulkHelper::add_mux_endpoint(const std::string &path, const std::string &key, const std::string &value, json &webhooks_start_resp, std::function<json(const Any &)> any2json)
        {
            this->webhooks_start_resp = webhooks_start_resp;
            this->any2json = any2json;
            std::shared_ptr<WebHooks> wh = any_cast<std::shared_ptr<WebHooks>>(printer->lookup_object("webhooks"));
            wh->register_mux_endpoint(path, key, value, [this](std::shared_ptr<WebRequest> rq) -> void
                                      { this->_add_api_client(rq.get()); });
        }

        BatchWebhooksClient::BatchWebhooksClient(WebRequest *web_request, std::function<json(const Any &)> any2json)
        {
            this->any2json = any2json;
            cconn = web_request->get_client_connection();
            template_data = web_request->get_dict("response_template");
        }

        bool BatchWebhooksClient::handle_batch(const Any &msg)
        {
            if (cconn->is_closed())
                return false;
            json tmp = template_data;
            if (any2json)
                tmp["params"] = any2json(msg);
            cconn->json_send(tmp);
            return true;
        }

        BulkDataQueue::BulkDataQueue(std::shared_ptr<MCU> mcu, const std::string &msg_name, uint32_t oid)
            : mcu(mcu), oid(oid), msg_name(msg_name)
        {
            // 此时this指针还不存在，放在init函数初始化
            //  printf("BulkDataQueue Init #1  msg_name %s raw_samples %p this %p\n", msg_name.c_str(), &raw_samples, this);
        }

        void BulkDataQueue::init(void)
        {
            // 注册"sensor_bulk_data"回调
            // printf("BulkDataQueue Init  #2  msg_name %s raw_samples %p this %p\n", msg_name.c_str(), &raw_samples, this);
            mcu->register_response(
                [this](const json &params)
                { this->_handle_data(params); }, msg_name, oid);
        }

        void BulkDataQueue::_handle_data(const json &params)
        {
            // SPDLOG_INFO("BulkDataQueue::_handle_data {} sequence {}", raw_samples.size(), std::stoul(params["sequence"].get<std::string>()));
            std::lock_guard<std::mutex> guard(lock);
            std::string dat = params["data"].get<std::string>();
            // printf("#1 BulkDataQueue::_handle_data raw_samples %d raw_samples %p this %p\n", raw_samples.size(), &raw_samples, this);
            raw_samples.emplace_back(BulkDataQueue::BulkDataQueueSamples{(uint16_t)(std::stoul((params["sequence"].get<std::string>()))),
                                                                         std::vector<uint8_t>(dat.begin(), dat.end())});
            // printf("#2 BulkDataQueue::_handle_data raw_samples %d raw_samples %p this %p\n", raw_samples.size(), &raw_samples, this);
        }

        std::vector<BulkDataQueue::BulkDataQueueSamples> BulkDataQueue::pull_queue()
        {
            std::lock_guard<std::mutex> guard(lock);
            std::vector<BulkDataQueueSamples> samples;
            // printf("#1 samples %d raw_samples %d raw_samples %p this %p\n", samples.size(), raw_samples.size(), &raw_samples, this);
            raw_samples.swap(samples);
            // printf("#2 samples %d raw_samples %d\n", samples.size(), raw_samples.size());
            return samples;
        }

        void BulkDataQueue::clear_queue()
        {
            // printf("clear_queue\n");
            pull_queue();
        }

        ClockSyncRegression::ClockSyncRegression(std::shared_ptr<MCU> mcu, double chip_clock_smooth, double decay)
            : mcu(mcu), chip_clock_smooth(chip_clock_smooth), decay(decay),
              last_chip_clock(0.0), last_exp_mcu_clock(0.0),
              mcu_clock_avg(0.0), mcu_clock_variance(0.0),
              chip_clock_avg(0.0), chip_clock_covariance(0.0) {}

        void ClockSyncRegression::reset(double mcu_clock, double chip_clock)
        {
            mcu_clock_avg = last_mcu_clock = mcu_clock;
            chip_clock_avg = chip_clock;

            mcu_clock_variance = chip_clock_covariance = 0.0;
            last_chip_clock = last_exp_mcu_clock = 0.0;
        }

        void ClockSyncRegression::update(double mcu_clock, double chip_clock)
        {
            // mcu_clock_avg, chip_clock_avg, inv_chip_freq
            double diff_mcu_clock = mcu_clock - mcu_clock_avg;
            mcu_clock_avg += decay * diff_mcu_clock;
            mcu_clock_variance = (1.0 - decay) * (mcu_clock_variance + diff_mcu_clock * diff_mcu_clock * decay);

            double diff_chip_clock = chip_clock - chip_clock_avg;
            chip_clock_avg += decay * diff_chip_clock;
            chip_clock_covariance = (1.0 - decay) * (chip_clock_covariance + diff_mcu_clock * diff_chip_clock * decay);
        }

        void ClockSyncRegression::set_last_chip_clock(double chip_clock)
        {
            double base_mcu, base_chip, inv_cfreq;
            std::tie(base_mcu, base_chip, inv_cfreq) = get_clock_translation();
            last_chip_clock = chip_clock;
            last_exp_mcu_clock = base_mcu + (chip_clock - base_chip) * inv_cfreq;
        }

        std::tuple<double, double, double> ClockSyncRegression::get_clock_translation()
        {
            double inv_chip_freq = mcu_clock_variance / chip_clock_covariance;
            if (last_chip_clock == 0.0)
            {
                return {mcu_clock_avg, chip_clock_avg, inv_chip_freq};
            }

            // Find mcu clock associated with future chip_clock
            double s_chip_clock = last_chip_clock + chip_clock_smooth;
            double scdiff = s_chip_clock - chip_clock_avg;
            double s_mcu_clock = mcu_clock_avg + scdiff * inv_chip_freq;
            // Calculate frequency to converge at future point
            double mdiff = s_mcu_clock - last_exp_mcu_clock;
            double s_inv_chip_freq = mdiff / chip_clock_smooth;
            return {last_exp_mcu_clock, last_chip_clock, s_inv_chip_freq};
        }

        std::tuple<double, double, double> ClockSyncRegression::get_time_translation()
        {
            double base_mcu, base_chip, inv_cfreq;
            std::tie(base_mcu, base_chip, inv_cfreq) = get_clock_translation();
            double base_time = mcu->clock_to_print_time(base_mcu);
            double inv_freq = mcu->clock_to_print_time(base_mcu + inv_cfreq) - base_time;
            return {base_time, base_chip, inv_freq};
        }

        FixedFreqReader::FixedFreqReader(std::shared_ptr<MCU> mcu, double chip_clock_smooth, const std::string &unpack_fmt)
            : mcu(mcu), clock_sync(mcu, chip_clock_smooth), unpack(unpack_fmt)
        {
            bytes_per_sample = unpack.size();
            // 一个包有多个采样值
            samples_per_block = MAX_BULK_MSG_SIZE / bytes_per_sample;
            SPDLOG_INFO("unpack_fmt:{} chip_clock_smooth:{} bytes_per_sample:{} samples_per_block:{}", unpack_fmt, chip_clock_smooth, bytes_per_sample, samples_per_block);
            last_sequence = max_query_duration = 0;
            last_overflows = 0;
            bulk_queue = nullptr;
            oid = 0;
            query_status_cmd = nullptr;
        }

        void FixedFreqReader::setup_query_command(const std::string &msgformat, uint32_t oid, std::shared_ptr<command_queue> cq)
        {
            this->oid = oid;
            query_status_cmd = mcu->lookup_query_command(
                msgformat, "sensor_bulk_status oid=%c clock=%u query_ticks=%u next_sequence=%hu buffered=%u possible_overflows=%hu",
                oid, cq);
            bulk_queue = std::make_shared<BulkDataQueue>(mcu, "sensor_bulk_data", oid);
            bulk_queue->init();
        }

        int FixedFreqReader::get_last_overflows()
        {
            return last_overflows;
        }

        void FixedFreqReader::_clear_duration_filter()
        {
            max_query_duration = 1 << 31;
        }

        void FixedFreqReader::note_start()
        {
            last_sequence = 0;
            last_overflows = 0;
            bulk_queue->clear_queue();
            _clear_duration_filter();
            _update_clock(true);
            _clear_duration_filter();
        }

        void FixedFreqReader::note_end()
        {
            bulk_queue->clear_queue();
        }

        void FixedFreqReader::_update_clock(bool is_reset)
        {
            // 获取响应
            json params = query_status_cmd->send({std::to_string(oid)});
            uint64_t mcu_clock = mcu->clock32_to_clock64(std::stoul(params["clock"].get<std::string>()));

            // 序列号
            uint16_t seq_diff = (std::stoul(params["next_sequence"].get<std::string>()) - last_sequence) & 0xffff;
            last_sequence += seq_diff;
            uint32_t buffered = std::stoul(params["buffered"].get<std::string>());

            // 溢出计数
            uint16_t po_diff = (std::stoul(params["possible_overflows"].get<std::string>()) - last_overflows) & 0xffff;
            last_overflows += po_diff;

            // 查询时间
            uint32_t duration = std::stoul(params["query_ticks"].get<std::string>());
            if (duration > max_query_duration)
            {
                max_query_duration = std::max(static_cast<uint64_t>(2 * max_query_duration), mcu->seconds_to_clock(0.000005));
                return;
            }
            max_query_duration = 2 * duration;

            uint64_t msg_count = (last_sequence * samples_per_block + buffered / bytes_per_sample);
            uint64_t chip_clock = msg_count + 1;
            uint64_t avg_mcu_clock = mcu_clock + duration / 2;

            if (is_reset)
            {
                clock_sync.reset(avg_mcu_clock, chip_clock);
            }
            else
            {
                clock_sync.update(avg_mcu_clock, chip_clock);
            }
        }

        // 协程里运行
        std::vector<FixedFreqReader::FixedFreqReaderSamples> FixedFreqReader::pull_samples()
        {
            // 更新时钟
            _update_clock();

            auto raw_samples = bulk_queue->pull_queue();
            if (raw_samples.empty())
                return std::vector<FixedFreqReader::FixedFreqReaderSamples>();

            double time_base, chip_base, inv_freq;
            std::tie(time_base, chip_base, inv_freq) = clock_sync.get_time_translation();

            // 存储解析二进制后的数据
            std::vector<FixedFreqReader::FixedFreqReaderSamples> samples;
            uint64_t seq = 0;
            int i = 0;
            for (const auto &params : raw_samples)
            {
                uint64_t seq_diff = ((params.sequence - last_sequence) & 0xffff);
                seq_diff -= (seq_diff & 0x8000) << 1;
                seq = last_sequence + seq_diff;
                uint64_t msg_cdiff = seq * samples_per_block - chip_base;

                // 处理二进制数据，一次采样可能包含多次数据
                auto &data = params.data;
                for (i = 0; i < data.size() / bytes_per_sample; ++i)
                {
                    double ptime = time_base + (msg_cdiff + i) * inv_freq;
                    Any udata = unpack.from(data, i * bytes_per_sample);
                    samples.emplace_back(FixedFreqReader::FixedFreqReaderSamples{ptime, udata});
                }
            }
            clock_sync.set_last_chip_clock(seq * samples_per_block + i);
            return samples;
        }
    }
}