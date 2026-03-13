/***************************************************************************** 
 * @Author       : Loping
 * @Date         : 2025-2-25 11:03:36
 * @LastEditors  : Loping
 * @LastEditTime : 2025-2-25 11:03:36
 * @Description  : Support for reading acceleration data from an LIS2DH chip
 * 
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/
#include "lis2dh.h"


namespace elegoo 
{
    namespace extras 
    {
        // LIS2DH registers
        static uint8_t REG_LIS2DH_WHO_AM_I_ADDR = 0x0F;
        static uint8_t REG_LIS2DH_CTRL_REG1_ADDR = 0x20;
        static uint8_t REG_LIS2DH_CTRL_REG2_ADDR = 0x21;
        static uint8_t REG_LIS2DH_CTRL_REG3_ADDR = 0x22;
        static uint8_t REG_LIS2DH_CTRL_REG6_ADDR = 0x25;
        static uint8_t REG_LIS2DH_STATUS_REG_ADDR = 0x27;
        static uint8_t REG_LIS2DH_OUT_XL_ADDR = 0x28;
        static uint8_t REG_LIS2DH_OUT_XH_ADDR = 0x29;
        static uint8_t REG_LIS2DH_OUT_YL_ADDR = 0x2A;
        static uint8_t REG_LIS2DH_OUT_YH_ADDR = 0x2B;
        static uint8_t REG_LIS2DH_OUT_ZL_ADDR = 0x2C;
        static uint8_t REG_LIS2DH_OUT_ZH_ADDR = 0x2D;
        static uint8_t REG_LIS2DH_FIFO_CTRL   = 0x2E;
        static uint8_t REG_LIS2DH_FIFO_SAMPLES = 0x2F;
        static uint8_t REG_MOD_READ = 0x80;
        //static uint8_t REG_MOD_MULTI = 0x40;

        static uint8_t LIS2DH_DEV_ID = 0x33;
        static double FREEFALL_ACCEL = 9.80665;
        static double SCALE = (FREEFALL_ACCEL * 1.952 / 4);
        static double BATCH_UPDATES = 0.100;

        // Printer class that controls LIS2DH chip
        Lis2dh::Lis2dh(std::shared_ptr<ConfigWrapper> config)
        {
            SPDLOG_DEBUG("__func__:{} #1",__func__);
            this->printer = config->get_printer();
            this->axes_map = read_axes_map(config,SCALE,SCALE,SCALE);
            this->data_rate = 1600;

            // Setup mcu sensor_lis2dh bulk query code
            this->spi = MCU_SPI_from_config(config, 3, "cs_pin", 5000000);
            this->mcu = this->spi->get_mcu();
            uint32_t oid = this->oid = mcu->create_oid();
            this->query_lis2dh_cmd = {};
            this->mcu->add_config_cmd("config_lis2dw oid=" + std::to_string(oid) + " spi_oid=" + std::to_string(this->spi->get_oid()));
            this->mcu->add_config_cmd("query_lis2dw oid=" + std::to_string(oid) + " rest_ticks=0" + std::to_string(oid),false,true);
            SPDLOG_DEBUG("__func__:{} #2",__func__);
            this->mcu->register_config_callback(
                    [this]()
                    {
                        _build_config();
                    });

            // Bulk sample message reading
            double chip_smooth = this->data_rate * BATCH_UPDATES * 2;
            this->ffreader = std::make_shared<FixedFreqReader>(mcu,chip_smooth,"<hhh");
            this->last_error_count = 0;

            // Process messages in batches
            this->batch_bulk = std::make_shared<BatchBulkHelper>(this->printer,
                    [this](double eventtime)->Any
                    {
                        return _process_batch(eventtime);
                    },
                    [this]()
                    {
                        _start_measurements();
                    },
                    [this]()
                    {
                        _finish_measurements();
                    },
                    BATCH_UPDATES);
            std::vector<std::string> split_name = elegoo::common::split(config->get_name());
            this->name = split_name.back();
            std::vector<std::string> hdr = {"time","x_acceleration","y_acceleration","z_acceleration"};
            json resp = {};
            resp["header"] = hdr;
            this->batch_bulk->add_mux_endpoint("lis2dw/dump_lis2dw", "sensor",this->name, resp);
            SPDLOG_DEBUG("__func__:{} #3",__func__);
        }

        Lis2dh::~Lis2dh()
        {
            SPDLOG_DEBUG("~Lis2dh");
        }
        void Lis2dh::init(std::shared_ptr<ConfigWrapper> config)
        {
            SPDLOG_DEBUG("__func__:{} #1",__func__);
            this->accel_comm = std::make_shared<AccelCommandHelper>(config,shared_from_this());
        }
        
        void Lis2dh::_build_config()
        {
            SPDLOG_DEBUG("__func__:{} #1",__func__);
            std::shared_ptr<command_queue> cmdqueue = this->spi->get_command_queue();
            this->query_lis2dh_cmd = this->mcu->lookup_command("query_lis2dw oid=%c rest_ticks=%u", cmdqueue);
            this->ffreader->setup_query_command("query_lis2dw_status oid=%c",this->oid, cmdqueue);
        }

        uint8_t Lis2dh::read_reg(uint8_t reg)
        {
            SPDLOG_DEBUG("__func__:{} #1",__func__);
            std::vector<uint8_t> data;
            data.emplace_back(reg | REG_MOD_READ);
            data.emplace_back(0x00);
            json params = this->spi->spi_transfer(data);
            if (params.contains("response") && params["response"].is_string())
            {
                std::string resp = params["response"].get<std::string>();
                std::vector<uint8_t> response(resp.begin(),resp.end());
                return response[1];
            }
            else
            {
                return -1;
            }
        }

        void Lis2dh::set_reg(uint8_t reg, uint8_t val,uint64_t minclock)
        {
            SPDLOG_DEBUG("__func__:{} #1",__func__);
            std::vector<uint8_t> data;
            data.emplace_back(reg);
            data.emplace_back(val & 0xFF);
            this->spi->spi_send(data,minclock);
            uint8_t stored_val = read_reg(reg);
            if(stored_val != val)
            {
                std::ostringstream oss;
                oss << std::hex;
                oss << "Failed to set LIS2DH register [0x" << reg << "] to 0x" << val << ": got 0x" << stored_val << ". ";
                std::string failed = oss.str() + "This is generally indicative of connection problems " + "(e.g. faulty wiring) or a faulty lis2dh chip.";
                SPDLOG_ERROR(failed);
                throw elegoo::common::CommandError(failed);
            }
        }

        std::shared_ptr<AccelQueryHelper> Lis2dh::start_internal_client()
        {
            SPDLOG_DEBUG("__func__:{} #1",__func__);
            std::shared_ptr<AccelQueryHelper> aqh = std::make_shared<AccelQueryHelper>(this->printer);
            this->batch_bulk->add_client(
                    [aqh](Any msg)->bool
                    {
                        aqh->handle_batch(msg);
                        return true;
                    });
            return aqh;
        }

        void Lis2dh::_convert_samples(std::vector<FixedFreqReader::FixedFreqReaderSamples> &samples)
        {
            SPDLOG_DEBUG("__func__:{} #1",__func__);
            if(this->axes_map.size() != 3)
            {
                SPDLOG_ERROR("axes_map.size is not 3!");
                throw elegoo::common::CommandError("axes_map.size is not 3!");
            }

            int32_t pos[3] = {},ii = 0,count = 0;
            double scale[3] = {};
            for(auto axes : this->axes_map)
            {
                pos[ii] = std::get<0>(axes);
                scale[ii] = std::get<1>(axes);
                if(++ii >= 3)
                {
                    break;
                }
            }

            SPDLOG_DEBUG("__func__:{} #2",__func__);
            for(auto sample : samples)
            {
                uint32_t rx, ry, rz;
                double ptime = sample.ptime;
                std::vector<uint8_t> data = any_cast<std::vector<uint8_t>>(sample.data);
                if(data.size() != strlen("hhh"))
                {
                    SPDLOG_ERROR("data.size() != strlen('hhh')");
                    throw elegoo::common::CommandError("data.size() != strlen('hhh')");
                }
                else
                {
                    rx = data[0];
                    ry = data[1];
                    rz = data[2];
                }

                std::vector<uint32_t> raw_xyz = {rx,ry,rz};
                double x = static_cast<long long>(round(raw_xyz[pos[0]] * scale[0] * 1000000))/1000000.;
                double y = static_cast<long long>(round(raw_xyz[pos[1]] * scale[1] * 1000000))/1000000.;
                double z = static_cast<long long>(round(raw_xyz[pos[2]] * scale[2] * 1000000))/1000000.;
                double pt = static_cast<long long>(round(ptime * 1000000))/1000000.;
                std::vector<double> dat = {x,y,z};

                samples[count].ptime = pt;
                samples[count].data = dat;
                count += 1;
            }
            SPDLOG_DEBUG("__func__:{} #1",__func__);
        }

        void Lis2dh::_start_measurements()
        {
            SPDLOG_DEBUG("__func__:{} #1",__func__);
            // In case of miswiring, testing lis2dh device ID prevents treating
            // noise or wrong signal as a correctly initialized device
            uint8_t dev_id = read_reg(REG_LIS2DH_WHO_AM_I_ADDR);
            std::ostringstream oss;
            oss << std::hex << "lis2dh_dev_id: 0x" << dev_id;
            SPDLOG_INFO(oss.str());
            if(dev_id != LIS2DH_DEV_ID)
            {
                oss.clear();
                oss << std::hex;
                oss << "Invalid lis2dh id (got 0x" << dev_id << " vs 0x" << LIS2DH_DEV_ID << ").\n";
                std::string err = oss.str() + 
                        "This is generally indicative of connection problems\n" + 
                        "(e.g. faulty wiring) or a faulty lis2dh chip.";
                SPDLOG_ERROR(err);
                throw elegoo::common::CommandError(err);
            }

            // Setup chip in requested query rate
            // ODR/2, +-16g, low-pass filter, Low-noise abled
            set_reg(REG_LIS2DH_CTRL_REG6_ADDR, 0x34);
            // Continuous mode: If the FIFO is full
            // the new sample overwrites the older sample.
            set_reg(REG_LIS2DH_FIFO_CTRL, 0xC0);
            // High-Performance / Low-Power mode 1600/200 Hz
            // High-Performance Mode (14-bit resolution)
            set_reg(REG_LIS2DH_CTRL_REG1_ADDR, 0x94);

            SPDLOG_DEBUG("__func__:{} #2",__func__);
            // Start bulk reading
            uint64_t rest_ticks = this->mcu->seconds_to_clock(4. / this->data_rate);
            this->query_lis2dh_cmd->send({
                    std::to_string(this->oid), 
                    std::to_string(rest_ticks)
                });
            set_reg(REG_LIS2DH_FIFO_CTRL, 0xC0);
            SPDLOG_INFO("LIS2DH starting '{}' measurements",this->name);

            SPDLOG_DEBUG("__func__:{} #3",__func__);
            // Initialize clock tracking
            this->ffreader->note_start();
            this->last_error_count = 0;
        }

        void Lis2dh::_finish_measurements()
        {
            SPDLOG_DEBUG("__func__:{} #1",__func__);
            // Halt bulk reading
            this->set_reg(REG_LIS2DH_FIFO_CTRL, 0x00);
            this->query_lis2dh_cmd->send_wait_ack({
                    std::to_string(this->oid), 
                    std::to_string(0)
                });
            this->ffreader->note_end();
            SPDLOG_INFO("LIS2DH finished '{}' measurements", this->name);
            set_reg(REG_LIS2DH_FIFO_CTRL, 0x00);
        }

        Any Lis2dh::_process_batch(double eventtime)
        {
            SPDLOG_DEBUG("__func__:{} #1",__func__);
            std::vector<FixedFreqReader::FixedFreqReaderSamples> samples = this->ffreader->pull_samples();
            _convert_samples(samples);
            if(!samples.size())
            {
                return {};
            }

            std::map<std::string,Any> msg;
            msg["data"] = samples;
            msg["errors"] = this->last_error_count;
            msg["overflows"] = this->ffreader->get_last_overflows();

            return std::move(msg);
        }
        
        std::shared_ptr<Lis2dh> lis2dh_load_config(std::shared_ptr<ConfigWrapper> config)
        {
            SPDLOG_DEBUG("__func__:{} #1",__func__);
            std::shared_ptr<Lis2dh> lis2dh = std::make_shared<Lis2dh>(config);
            lis2dh->init(config);
            return lis2dh;
        }
        std::shared_ptr<Lis2dh> lis2dh_load_config_prefix(std::shared_ptr<ConfigWrapper> config)
        {
            SPDLOG_DEBUG("__func__:{} #1",__func__);
            std::shared_ptr<Lis2dh> lis2dh = std::make_shared<Lis2dh>(config);
            lis2dh->init(config);
            return lis2dh;
        }
    }
}
