/***************************************************************************** 
 * @Author       : Loping
 * @Date         : 2025-2-25 11:03:36
 * @LastEditors  : loping
 * @LastEditTime : 2025-08-07 15:24:48
 * @Description  : Support for reading acceleration data from an adxl345 chip
 * 
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/
#ifndef __ADXl345_H__
#define __ADXl345_H__

#include <string>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <functional>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include "printer.h"
#include "configfile.h"
#include "extras/bulk_sensor.h"
#include "extras/bus.h"


namespace elegoo 
{
    namespace extras 
    {
        class Adxl345;
        
        struct AccelMeasurement
        {
            double time;
            double accel_x;
            double accel_y;
            double accel_z;
        };
        
        class AccelQueryHelper
        {
        public:
            AccelQueryHelper(std::shared_ptr<Printer> printer);
            ~AccelQueryHelper();
            void finish_measurements();
            bool handle_batch(Any msg);
            bool has_valid_samples();
            std::vector<AccelMeasurement> get_samples();
            void write_to_file(std::string filename);
            void write_impl(std::tuple<std::string,std::vector<AccelMeasurement>> tp);
        private:
            std::shared_ptr<Printer> printer;
            bool is_finished;
            double request_start_time;
            double request_end_time;
            std::vector<Any> msgs;
            std::vector<AccelMeasurement> samples;
        };
        
        class AccelChip
        {
        public:
            AccelChip(){};
            AccelChip(std::shared_ptr<ConfigWrapper> config){};
            virtual ~AccelChip(){SPDLOG_DEBUG("~AccelChip");};
            virtual void init(std::shared_ptr<ConfigWrapper> config){};
            virtual uint8_t read_reg(uint8_t reg){ return 0; };
            virtual void set_reg(uint8_t reg, uint8_t val,uint64_t minclock = 0){};
            virtual std::shared_ptr<AccelQueryHelper> start_internal_client(){ return nullptr; };
            virtual json get_accel_status(){ return {}; };
            virtual std::string get_name(){return name;};
        private:
            virtual void _build_config(){};
            virtual void _convert_samples(std::vector<FixedFreqReader::FixedFreqReaderSamples> &samples){};
            virtual void _start_measurements(){};
            virtual void _finish_measurements(){};
            virtual Any _process_batch(double eventtime){ return {}; };
        // protected:
        public:
            std::shared_ptr<Printer> printer;
            std::shared_ptr<GCodeDispatch> gcode;
            std::vector<std::tuple<int32_t,double>> axes_map;
            int32_t data_rate;
            std::shared_ptr<MCU_SPI> spi;
            std::shared_ptr<MCU> mcu;
            uint32_t oid;
            std::shared_ptr<CommandWrapper> query_adxl345_cmd;
            std::shared_ptr<FixedFreqReader> ffreader;
            int32_t last_error_count;
            std::shared_ptr<BatchBulkHelper> batch_bulk;
            std::string name;
            json accel_status;
        };
        
        class AccelCommandHelper
        {
        public:
            AccelCommandHelper(std::shared_ptr<ConfigWrapper> config, std::shared_ptr<AccelChip> chip);
            ~AccelCommandHelper();
            void register_commands(std::string name);
            void cmd_ACCELEROMETER_MEASURE(std::shared_ptr<GCodeCommand> gcmd);
            void cmd_ACCELEROMETER_QUERY(std::shared_ptr<GCodeCommand> gcmd);
            void cmd_ACCELEROMETER_DEBUG_READ(std::shared_ptr<GCodeCommand> gcmd);
            void cmd_ACCELEROMETER_DEBUG_WRITE(std::shared_ptr<GCodeCommand> gcmd);
        private:
            std::string cmd_ACCELEROMETER_MEASURE_help = "Start/stop accelerometer";
            std::string cmd_ACCELEROMETER_QUERY_help = "Query accelerometer for the current values";
            std::string cmd_ACCELEROMETER_DEBUG_READ_help = "Query register (for debugging)";
            std::string cmd_ACCELEROMETER_DEBUG_WRITE_help = "Set register (for debugging)";
            std::shared_ptr<Printer> printer;
            std::shared_ptr<AccelChip> chip;
            std::shared_ptr<AccelQueryHelper> bg_client;
            std::string base_name;
            std::string name;
        };

        std::vector<std::tuple<int32_t,double>> read_axes_map(std::shared_ptr<ConfigWrapper> config,double scale_x,double scale_y,double scale_z);

        class Adxl345 : public std::enable_shared_from_this<Adxl345>, public AccelChip
        {
        public:
            Adxl345(std::shared_ptr<ConfigWrapper> config);
            ~Adxl345();
            void init(std::shared_ptr<ConfigWrapper> config) override ;
            uint8_t read_reg(uint8_t reg) override ;
            void set_reg(uint8_t reg, uint8_t val,uint64_t minclock = 0) override ;
            std::shared_ptr<AccelQueryHelper> start_internal_client() override ;
            json get_accel_status() override ;
        private:
            void _build_config() override ;
            void _convert_samples(std::vector<FixedFreqReader::FixedFreqReaderSamples> &samples) override ;
            void _start_measurements() override ;
            void _finish_measurements() override ;
            Any _process_batch(double eventtime) override ;
        private:
            // std::shared_ptr<Printer> printer;
            // std::map<int32_t,double> axes_map;
            // int32_t data_rate;
            // std::shared_ptr<MCU_SPI> spi;
            // std::shared_ptr<MCU> mcu;
            // uint32_t oid;
            std::shared_ptr<CommandWrapper> query_adxl345_cmd;
            // std::shared_ptr<FixedFreqReader> ffreader;
            // int32_t last_error_count;
            // std::shared_ptr<BatchBulkHelper> batch_bulk;
            // std::string name;
            
        };
        
        std::shared_ptr<Adxl345> adxl345_load_config(std::shared_ptr<ConfigWrapper> config);
        std::shared_ptr<Adxl345> adxl345_load_config_prefix(std::shared_ptr<ConfigWrapper> config);
    }
}

#endif
