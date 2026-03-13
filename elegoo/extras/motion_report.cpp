/*****************************************************************************
 * @Author       : Ben
 * @Date         : 2024-11-28 20:15:20
 * @LastEditors  : coconut
 * @LastEditTime : 2025-02-21 14:10:32
 * @Description  : Diagnostic tool for reporting stepper and kinematic positions
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#include "motion_report.h"
#include "bulk_sensor.h"
#include "trapq.h"
#include "toolhead.h" //need to change to extruder.h

namespace elegoo
{
    namespace extras
    {
        // DumpStepper
        DumpStepper::DumpStepper(std::shared_ptr<Printer> printer, std::shared_ptr<MCU_stepper> mcu_stepper)
            : printer(printer), mcu_stepper(mcu_stepper), last_batch_clock(0)
        {
            batch_bulk = std::make_shared<BatchBulkHelper>(printer, [this](double time) -> Any
                                                           { return this->process_batch(time); });
            json api_resp;
            api_resp["header"] = json::array(); // 初始化为数组类型
            api_resp["header"].push_back("interval");
            api_resp["header"].push_back("count");
            api_resp["header"].push_back("add");
            batch_bulk->add_mux_endpoint("motion_report/dump_stepper", "name", mcu_stepper->get_name(), api_resp);
        }

        std::pair<std::vector<pull_history_steps>, std::vector<std::pair<pull_history_steps, int>>> DumpStepper::get_step_queue(uint64_t start_clock, uint64_t end_clock)
        {
            std::vector<std::pair<pull_history_steps, int>> res;
            while (true)
            {
                auto dump = mcu_stepper->dump_steps(128, start_clock, end_clock);
                if (dump.first == nullptr)
                    break;
                res.emplace_back(*dump.first, dump.second); // Assuming the first element contains required data
                if (dump.second < 128)
                    break;
                end_clock = (dump.first.get() + dump.second - 1)->first_clock; // Update end_clock
            }
            return {extract_data(res), res};
        }

        void DumpStepper::log_steps(const std::vector<pull_history_steps> &data)
        {
            std::stringstream ss;
            if (data.empty())
                return;
            ss << "Dumping stepper '" << mcu_stepper->get_name() << "' ("
               << mcu_stepper->get_mcu()->get_name() << ") "
               << data.size() << " queue_step:\n";
            for (size_t i = 0; i < data.size(); ++i)
            {
                const auto &s = data[i];
                ss << "queue_step " << i << ": t=" << s.first_clock
                   << " p=" << s.start_position << " i=" << s.interval
                   << " c=" << s.step_count << " a=" << s.add << '\n';
            }
            SPDLOG_INFO(ss.str());
        }

        json DumpStepper::process_batch(double eventtime)
        {
            auto queue = get_step_queue(last_batch_clock, std::numeric_limits<long long>::max());
            if (queue.first.empty())
                return {};

            long long first_clock = queue.first.front().first_clock;
            long first_time = mcu_stepper->get_mcu()->clock_to_print_time(first_clock);
            last_batch_clock = queue.first.back().last_clock; // Assuming last_clock exists
            long long last_time = mcu_stepper->get_mcu()->clock_to_print_time(last_batch_clock);

            int64_t mcu_pos = queue.first.front().start_position;
            long start_position = mcu_stepper->mcu_to_commanded_position(mcu_pos);
            long step_dist = mcu_stepper->get_step_dist();

            std::vector<std::tuple<long, long, long>> step_data;
            for (const auto &s : queue.first)
            {
                step_data.emplace_back(s.interval, s.step_count, s.add);
            }

            json ret;
            json step_data_json = json::array();
            for (const auto &s : step_data)
            {
                json step = json::array();
                step.push_back(static_cast<int64_t>(std::get<0>(s)));
                step.push_back(static_cast<int64_t>(std::get<1>(s)));
                step.push_back(static_cast<int64_t>(std::get<2>(s)));
                step_data_json.push_back(step);
            }
            ret["data"] = step_data_json;
            ret["start_position"] = static_cast<int64_t>(start_position);
            ret["start_mcu_positon"] = static_cast<int64_t>(mcu_pos);
            ret["step_distance"] = static_cast<int64_t>(step_dist);
            ret["first_clock"] = static_cast<int64_t>(first_clock);
            ret["first_step_time"] = static_cast<int64_t>(first_time);
            ret["last_clock"] = static_cast<int64_t>(last_batch_clock);
            ret["last_step_time"] = static_cast<int64_t>(last_time);
            return ret;
        }

        std::vector<pull_history_steps> DumpStepper::extract_data(const std::vector<std::pair<pull_history_steps, int>> &res)
        {
            std::vector<pull_history_steps> data;
            for (const auto &val : res)
            {
                for (int i = val.second - 1; i >= 0; --i)
                {
                    data.push_back(val.first);
                }
            }
            std::reverse(data.begin(), data.end());
            return data;
        }

        // DumpTrapQ
        DumpTrapQ::DumpTrapQ(std::shared_ptr<Printer> printer, const std::string &name, std::shared_ptr<trapq> trapq)
        {
            _printer = printer;
            _name = name;
            _trapq = trapq;
            last_batch_msg = {0.0, 0.0, 0.0};
            json api_resp;
            // Create the "header" array
            json header = json::array();
            header.push_back("time");
            header.push_back("duration");
            header.push_back("start_velocity");
            header.push_back("acceleration");
            header.push_back("start_position");
            header.push_back("direction");

            // Add the "header" array to the main JSON object
            api_resp["header"] = header;
            batch_bulk = std::make_shared<BatchBulkHelper>(printer, [this](double time) -> Any
                                                           { return this->_process_batch(time); });
            batch_bulk->add_mux_endpoint("motion_report/dump_trapq", "name", name, api_resp);
        }

        std::pair<std::vector<pull_move>, std::vector<std::pair<pull_move *, int>>> DumpTrapQ::extract_trapq(double start_time, double end_time)
        {
            std::vector<std::pair<pull_move *, int>> res;

            while (true)
            {
                pull_move data[128];
                int count = trapq_extract_old(_trapq.get(), data, 128, start_time, end_time);
                if (count == 0)
                    break;
                res.push_back(std::make_pair(data, count));
                if (count < 128)
                    break;
                end_time = data[count - 1].print_time;
            }

            std::reverse(res.begin(), res.end());
            std::vector<pull_move> data_vec;
            for (auto &data : res)
            {
                for (int i = data.second - 1; i >= 0; --i)
                {
                    data_vec.push_back(data.first[i]);
                }
            }
            return {data_vec, res};
        }

        void DumpTrapQ::log_trapq(const std::vector<pull_move> &data)
        {
            std::stringstream ss;
            if (data.empty())
                return;
            ss << "Dumping trapq '" << _name << "' " << data.size() << " moves:\n";
            for (size_t i = 0; i < data.size(); ++i)
            {
                const auto &m = data[i];
                ss << "move " << i << ": pt=" << m.print_time << " mt=" << m.move_t << " sv=" << m.start_v << " a=" << m.accel
                    << " sp=(" << m.start_x << "," << m.start_y << "," << m.start_z << ") ar=(" << m.x_r << "," << m.y_r << "," << m.z_r << ")\n";
            }
            SPDLOG_INFO(ss.str());
        }

        std::pair<std::tuple<double, double, double>, double> DumpTrapQ::get_trapq_position(double print_time)
        {
            pull_move data[1];
            int count = trapq_extract_old(_trapq.get(), data, 1, 0., print_time);
            if (count == 0)
                return {std::make_tuple(DOUBLE_NONE, DOUBLE_NONE, DOUBLE_NONE), DOUBLE_NONE};

            const auto &move = data[0];
            double move_time = std::max(0., std::min(move.move_t, print_time - move.print_time));
            double dist = (move.start_v + 0.5 * move.accel * move_time) * move_time;

            std::tuple<double, double, double> pos{
                move.start_x + move.x_r * dist,
                move.start_y + move.y_r * dist,
                move.start_z + move.z_r * dist};

            double velocity = move.start_v + move.accel * move_time;
            return {pos, velocity};
        }

        json DumpTrapQ::_process_batch(double eventtime)
        {
            double qtime = std::get<0>(last_batch_msg) + std::min(std::get<1>(last_batch_msg), 0.100);
            auto exdata = extract_trapq(qtime, NEVER_TIME);
            auto data = exdata.first;
            auto cdata = exdata.second;
            std::vector<std::tuple<double, double, double>> d;
            for (const auto &m : data)
            {
                d.emplace_back(m.print_time, m.move_t, m.start_v);
            }

            if (!d.empty() && d.front() == last_batch_msg)
            {
                d.erase(d.begin());
            }

            if (d.empty())
                return {};

            last_batch_msg = d.back();
            json ret;
            json root = json::array();
            // Iterate over the vector and convert each tuple to a JSON array
            for (const auto &t : d)
            {
                json tupleJson = json::array();      // Create a new JSON array for the tuple
                tupleJson.push_back(std::get<0>(t)); // Append the first element of the tuple
                tupleJson.push_back(std::get<1>(t)); // Append the second element of the tuple
                tupleJson.push_back(std::get<2>(t)); // Append the third element of the tuple
                root.push_back(tupleJson);           // Append the array to the root array
            }
            ret["data"] = root;
            return ret;
        }

        // PrinterMotionReport
        PrinterMotionReport::PrinterMotionReport(std::shared_ptr<ConfigWrapper> config)
        {
            SPDLOG_DEBUG("PrinterMotionReport init!");
            printer = config->get_printer();
            next_status_time = 0.;

            // Initialize last_status
            last_status["live_position"] = {0., 0., 0., 0.};
            last_status["live_velocity"] = 0.;
            last_status["live_extruder_velocity"] = 0.;
            last_status["steppers"] = json::array();
            last_status["trapq"] = json::array();

            // Register handlers
            elegoo::common::SignalManager::get_instance().register_signal("elegoo:connect", std::function<void()>([this]()
                                                                                                                   { _connect(); }));
            elegoo::common::SignalManager::get_instance().register_signal("elegoo:shutdown", std::function<void()>([this]()
                                                                                                                    { _shutdown(); }));
        }

        void PrinterMotionReport::register_stepper(std::shared_ptr<ConfigWrapper> config, std::shared_ptr<MCU_stepper> mcu_stepper)
        {
            auto ds = std::make_shared<DumpStepper>(printer, mcu_stepper);
            steppers[mcu_stepper->get_name()] = ds;
            SPDLOG_DEBUG("steppers.size:{},mcu_stepper->get_name():{}", steppers.size(), mcu_stepper->get_name());
        }

        void PrinterMotionReport::_connect()
        {
            // Lookup toolhead trapq
            std::shared_ptr<ToolHead> toolhead = any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));
            auto trapq = toolhead->get_trapq();
            trapqs["toolhead"] = std::make_shared<DumpTrapQ>(printer, "toolhead", trapq);

            // Lookup extruder trapqs
            for (int i = 0; i < 99; i++)
            {
                std::string ename = "extruder" + std::to_string(i);
                if (ename == "extruder0")
                    ename = "extruder";

                // SPDLOG_INFO("Lookup extruder trapqs {} #0", ename);
                try
                {
                    printer->lookup_object(ename);
                }
                catch (...)
                {
                    // SPDLOG_INFO("Lookup extruder trapqs {} #2", ename);
                    break;
                }
                // SPDLOG_INFO("Lookup extruder trapqs {} #3", ename);

                std::shared_ptr<PrinterExtruder> extruder = any_cast<std::shared_ptr<PrinterExtruder>>(printer->lookup_object(ename));
                auto etrapq = extruder->get_trapq();
                trapqs[ename] = std::make_shared<DumpTrapQ>(printer, ename, etrapq);
            }

            // Populate last_status with trapq and steppers
            std::vector<std::string> stepper_keys, trapq_keys;
            for (const auto &kv : steppers)
                stepper_keys.push_back(kv.first);
            for (const auto &kv : trapqs)
                trapq_keys.push_back(kv.first);
            std::sort(stepper_keys.begin(), stepper_keys.end());
            std::sort(trapq_keys.begin(), trapq_keys.end());
            last_status["steppers"] = stepper_keys;
            last_status["trapq"] = trapq_keys;
        }

        double PrinterMotionReport::_dump_shutdown(double eventtime)
        {
            double shutdown_time = NEVER_TIME;
            for (const auto &kv : steppers)
            {
                auto mcu = kv.second->mcu_stepper->get_mcu();
                uint64_t sc = mcu->get_shutdown_clock(); // need to confirm double or int64_t
                if (!sc)
                    continue;

                shutdown_time = std::min(shutdown_time, mcu->clock_to_print_time(sc));
                uint64_t clock_100ms = mcu->seconds_to_clock(0.1);
                uint64_t start_clock = std::max((uint64_t)0, sc - clock_100ms);
                uint64_t end_clock = sc + clock_100ms;
                auto val = kv.second->get_step_queue(start_clock, end_clock);
                kv.second->log_steps(val.first);
            }

            if (shutdown_time >= NEVER_TIME)
                return shutdown_time;

            // Log trapqs at shutdown time
            for (const auto &kv : trapqs)
            {
                auto data = kv.second->extract_trapq(shutdown_time - 0.1, shutdown_time + 0.1);
                kv.second->log_trapq(data.first);
            }

            auto dtrapq = trapqs.find("toolhead");
            if (dtrapq == trapqs.end())
                return shutdown_time;

            auto pos = dtrapq->second->get_trapq_position(shutdown_time);
            if (std::isnan(std::get<0>(pos.first)) || std::isnan(std::get<1>(pos.first)) || std::isnan(std::get<2>(pos.first)))
            {
                std::cout << "Requested toolhead position at shutdown time: " << shutdown_time << ": " << "(" + std::to_string(std::get<0>(pos.first)) + ", " + std::to_string(std::get<1>(pos.first)) + ", " + std::to_string(std::get<2>(pos.first)) + ")" << std::endl;
            }
            return shutdown_time;
        }

        void PrinterMotionReport::_shutdown()
        {
            printer->get_reactor()->register_callback([this](double eventtime) -> double
                                                      { return this->_dump_shutdown(eventtime); });
        }

        json PrinterMotionReport::get_status(double eventtime)
        {
            std::vector<double> xyzpos = {0., 0., 0., 0.};
            double epos = 0.;
            double xyzvelocity = 0.;
            double evelocity = 0.;
            std::tuple<double, double, double> pos;
            double velocity;

            if (eventtime < next_status_time || trapqs.empty())
                return last_status;

            next_status_time = eventtime + STATUS_REFRESH_TIME;

            // Calculate current toolhead position and velocity
            std::shared_ptr<MCU> mcu = any_cast<std::shared_ptr<MCU>>(printer->lookup_object("mcu"));
            double print_time = mcu->estimated_print_time(eventtime);
            std::tie(pos, velocity) = trapqs["toolhead"]->get_trapq_position(print_time);

            if (!(std::isnan(std::get<0>(pos)) && std::isnan(std::get<0>(pos)) && std::isnan(std::get<0>(pos))))
            {
                xyzpos = {std::get<0>(pos), std::get<1>(pos), std::get<2>(pos), 0.};
                xyzvelocity = velocity;
            }

            std::shared_ptr<ToolHead> toolhead = any_cast<std::shared_ptr<ToolHead>>(printer->lookup_object("toolhead"));
            auto ehandler = trapqs[toolhead->get_extruder()->get_name()];
            // printf("ehandler %p\n", ehandler.get());
            if (!ehandler)
            {
                std::tie(pos, velocity) = ehandler->get_trapq_position(print_time);
                if (!(std::isnan(std::get<0>(pos)) && std::isnan(std::get<0>(pos)) && std::isnan(std::get<0>(pos))))
                {
                    epos = std::get<0>(pos);
                    evelocity = velocity;
                }
            }

            xyzpos[3] = epos;
            last_status["live_position"] = xyzpos;
            last_status["live_velocity"] = xyzvelocity;
            last_status["live_extruder_velocity"] = evelocity;
            return last_status;
        }

        std::shared_ptr<PrinterMotionReport> motion_report_load_config(std::shared_ptr<ConfigWrapper> config)
        {
            return std::make_shared<PrinterMotionReport>(config);
        }
    }
}