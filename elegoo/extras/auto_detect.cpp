/*****************************************************************************
 * @Author       : Loping
 * @Date         : 2025-03-10 11:03:36
 * @LastEditors  : loping
 * @LastEditTime : 2025-09-29 11:09:50
 * @Description  : auto detect
 *
 * Copyright (c) 2024 by ELEGOO, All Rights Reserved.
 *****************************************************************************/

#include "auto_detect.h"
#include "controller_fan.h"
#include "heater_fan.h"
#include "extruder.h"
#include "heater_bed.h"
#include "resonance_tester.h"
#include "bed_mesh.h"
#include "pid_calibrate.h"
#include "gcode.h"


namespace elegoo
{
    namespace extras
    {
        // #undef SPDLOG_DEBUG
        // #define SPDLOG_DEBUG SPDLOG_INFO

        #define AUTO_DETECT_CNT (3)
        #define RPM_MAX_DETECT_TIME (1.*60)
        #define TEMP_MAX_DETECT_TIME (1.*60)

        AutoDetect::AutoDetect(std::shared_ptr<ConfigWrapper> config)
        {
            SPDLOG_DEBUG("__func__:{} #1",__func__);
            this->printer = config->get_printer();
            this->reactor = printer->get_reactor();
            this->pheaters = any_cast<std::shared_ptr<PrinterHeaters>>(printer->lookup_object("heaters",std::shared_ptr<PrinterHeaters>()));
            this->heaters = this->pheaters->get_heaters();
            this->controller_fan = any_cast<std::shared_ptr<ControllerFan>>(this->printer->lookup_object("controller_fan board_cooling_fan",std::shared_ptr<ControllerFan>()));
            if(!this->controller_fan)
                SPDLOG_DEBUG("__func__:{} #1",__func__);
            this->heater_fan = any_cast<std::shared_ptr<PrinterHeaterFan>>(this->printer->lookup_object("heater_fan heatbreak_cooling_fan",std::shared_ptr<PrinterHeaterFan>()));
            if(!this->heater_fan)
                SPDLOG_DEBUG("__func__:{} #1",__func__);
            this->bed_mesh = any_cast<std::shared_ptr<BedMesh>>(this->printer->lookup_object("bed_mesh",std::shared_ptr<BedMesh>()));
            if(!this->bed_mesh)
                SPDLOG_DEBUG("__func__:{} #1",__func__);
            this->resonance_tester = any_cast<std::shared_ptr<ResonanceTester>>(this->printer->lookup_object("resonance_tester",std::shared_ptr<ResonanceTester>()));
            if(!this->resonance_tester)
                SPDLOG_DEBUG("__func__:{} #1",__func__);
            this->name = config->get_name();
            this->gcode = any_cast<std::shared_ptr<GCodeDispatch>>(this->printer->lookup_object("gcode"));
            this->gcode->register_command(
                    "AUTO_DETECT"
                    ,[this](std::shared_ptr<GCodeCommand> gcmd){
                        cmd_AUTO_DETECT(gcmd);
                    }
                    ,false
                    ,"auto detect");

            elegoo::common::SignalManager::get_instance().register_signal(
                "autodetect:start_resonance",
                std::function<void()>([this](){
                    start_resonance();
                })
            );

            elegoo::common::SignalManager::get_instance().register_signal(
                "autodetect:start_bed_mesh",
                std::function<void()>([this](){
                    start_bed_mesh();
                })
            );

            this->is_auto_detect = false;
            this->start_detect_time = get_monotonic();
            this->next_state = EXTRUDER_TEMP;
            for(auto ii = 0; ii < AUTO_DETECT_NONE; ++ii)
            {
                this->state_result[ii] = false;
                this->rpm_retry_cnt[ii] = 0;
            }
        }

        AutoDetect::~AutoDetect()
        {
            SPDLOG_DEBUG("~AutoDetect");
        }

        void AutoDetect::start_resonance()
        {
            this->gcode->run_script_from_command("SET_KINEMATIC_POSITION Z=" + std::to_string(20));
            this->gcode->run_script_from_command("G1 Z10 F300");
            this->gcode->run_script_from_command("G1 Z15 F300");
            SPDLOG_INFO("__func__:{} #1 G28 TRY",__func__);
            this->gcode->run_script_from_command("G28 TRY");
            //#5 振纹优化
            SPDLOG_INFO("__func__:{} #1 SHAPER_CALIBRATE",__func__);
            this->gcode->run_script_from_command("SHAPER_CALIBRATE");
            //保存配置
            this->gcode->run_script_from_command("SAVE_CONFIG");
        }

        void AutoDetect::start_bed_mesh()
        {
            try
            {
                //#6 热床调平
                SPDLOG_INFO("__func__:{} #1 BED_MESH_CALIBRATE",__func__);
                this->gcode->run_script_from_command("BED_MESH_CALIBRATE PROFILE=default BED_TEMP=60");
                //保存配置
                this->gcode->run_script_from_command("SAVE_CONFIG");
            }
            catch(...)
            {
                SPDLOG_ERROR("start_bed_mesh failed!");
            }
        }
        
        void AutoDetect::cmd_AUTO_DETECT(std::shared_ptr<GCodeCommand> gcmd)
        {
            SPDLOG_INFO("__func__:{} #1",__func__);
            if(this->is_auto_detect)
            {
                SPDLOG_WARN("cur is auto detected in progress, please wait for a moment do it again...");
                return;
            }
            callback(0.);
            SPDLOG_INFO("__func__:{} #1 __ok",__func__);
        }

        void AutoDetect::extruder_temp_feedback(double detect_time)
        {
            json res;
            if(detect_time <= TEMP_MAX_DETECT_TIME)
            {
                auto heater = any_cast<std::shared_ptr<PrinterExtruder>>(printer->lookup_object("extruder"))->get_heater();
                double last_temp = heater->get_status(0.)["last_temp"].get<double>();
                if(last_temp >= 140 - 3)
                {
                    SPDLOG_INFO("__func__:{} #1 next_state {} {} last_temp:{}",__func__,(int32_t)this->next_state,this->rpm_retry_cnt[next_state],last_temp);
                    ++this->rpm_retry_cnt[next_state];
                }
                else
                {
                    SPDLOG_DEBUG("__func__:{} #1 next_state {} {} last_temp:{}",__func__,(int32_t)this->next_state,this->rpm_retry_cnt[next_state],last_temp);
                    this->rpm_retry_cnt[next_state] = 0;
                }
                if(this->rpm_retry_cnt[next_state] >= AUTO_DETECT_CNT)
                {
                    this->state_result[next_state] = true;
                    this->next_state = HEAD_BED_TEMP;
                    this->start_detect_time = get_monotonic();
                    res["command"] = "detect_extruder_temp";
                    res["result"] = "ok";
                    this->gcode->respond_feedback(res);
                    SPDLOG_INFO("__func__:{} #1 next_state {} {} res:{}",__func__,(int32_t)this->next_state,last_temp,res.dump());
                }
            }
            else
            {
                this->state_result[next_state] = false;
                this->next_state = HEAD_BED_TEMP;
                this->start_detect_time = get_monotonic();
                res["command"] = "detect_extruder_temp";
                res["result"] = "failed";
                this->gcode->respond_feedback(res);
                SPDLOG_INFO("__func__:{} #1 next_state {} res:{}",__func__,(int32_t)this->next_state,res.dump());
            }
        }

        void AutoDetect::head_bed_temp_feedback(double detect_time)
        {
            json res;
            if(detect_time <= TEMP_MAX_DETECT_TIME)
            {
                for(auto heater : this->heaters)
                {
                    double last_temp = heater.second->get_status(0.)["last_temp"].get<double>();
                    if("heater_bed" == heater.first && last_temp >= 60 - 3)
                    {
                        SPDLOG_INFO("__func__:{} #1 next_state {} {} last_temp:{}",__func__,(int32_t)this->next_state,this->rpm_retry_cnt[next_state],last_temp);
                        ++this->rpm_retry_cnt[next_state];
                    }
                    else
                    {
                        SPDLOG_DEBUG("__func__:{} #1 next_state {} {} last_temp:{},detect_time:{}",__func__,(int32_t)this->next_state,this->rpm_retry_cnt[next_state],last_temp,detect_time);
                        this->rpm_retry_cnt[next_state] = 0;
                    }
                    if(this->rpm_retry_cnt[next_state] >= AUTO_DETECT_CNT)
                    {
                        this->state_result[next_state] = true;
                        this->next_state = HEATER_FAN_RPM;
                        this->start_detect_time = get_monotonic();
                        res["command"] = "detect_heater_bed_temp";
                        res["result"] = "ok";
                        this->gcode->respond_feedback(res);
                        SPDLOG_INFO("__func__:{} #1 next_state {} {} res:{}",__func__,(int32_t)this->next_state,last_temp,res.dump());
                    }
                }
            }
            else
            {
                this->state_result[next_state] = false;
                this->next_state = HEATER_FAN_RPM;
                this->start_detect_time = get_monotonic();
                res["command"] = "detect_heater_bed_temp";
                res["result"] = "failed";
                this->gcode->respond_feedback(res);
                SPDLOG_INFO("__func__:{} #1 next_state {} res:{}",__func__,(int32_t)this->next_state,res.dump());
            }
        }

        void AutoDetect::heater_fan_rpm_feedback(double detect_time)
        {
            json res;
            if(this->heater_fan && detect_time <= RPM_MAX_DETECT_TIME)
            {
                double rpm = this->heater_fan->get_status(0.)["rpm"].get<double>();
                if(rpm > 0.)
                {
                    SPDLOG_INFO("__func__:{} #1 next_state {} {} rpm:{}",__func__,(int32_t)this->next_state,this->rpm_retry_cnt[next_state],rpm);
                    ++this->rpm_retry_cnt[next_state];
                }
                else
                {
                    SPDLOG_DEBUG("__func__:{} #1 next_state {} {} rpm:{}",__func__,(int32_t)this->next_state,this->rpm_retry_cnt[next_state],rpm);
                    this->rpm_retry_cnt[next_state] = 0;
                }
                if(this->rpm_retry_cnt[next_state] >= AUTO_DETECT_CNT)
                {
                    this->state_result[next_state] = true;
                    this->next_state = CONTROL_FAN_RPM;
                    this->start_detect_time = get_monotonic();
                    res["command"] = "detect_heater_bed_fan";
                    res["result"] = "ok";
                    this->gcode->respond_feedback(res);
                    SPDLOG_INFO("__func__:{} #1 next_state {} {} res:{}",__func__,(int32_t)this->next_state,rpm,res.dump());
                }
            }
            else
            {
                this->state_result[next_state] = false;
                this->next_state = CONTROL_FAN_RPM;
                this->start_detect_time = get_monotonic();
                res["command"] = "detect_heater_bed_fan";
                res["result"] = "failed";
                this->gcode->respond_feedback(res);
                SPDLOG_INFO("__func__:{} #1 next_state {} res:{}",__func__,(int32_t)this->next_state,res.dump());
            }
        }

        void AutoDetect::control_fan_rpm_feedback(double detect_time)
        {
            SPDLOG_DEBUG("__func__:{} #1 detect_time:{}",__func__,detect_time);
            json res;
            if(this->controller_fan && detect_time <= RPM_MAX_DETECT_TIME)
            {
                double rpm = this->controller_fan->get_status(0.)["rpm"].get<double>();
                SPDLOG_DEBUG("__func__:{} #1 rpm:{}",__func__,rpm);
                if(rpm > 0.)
                {
                    SPDLOG_INFO("__func__:{} #1 next_state {} {} rpm:{}",__func__,(int32_t)this->next_state,this->rpm_retry_cnt[next_state],rpm);
                    ++this->rpm_retry_cnt[next_state];
                }
                else
                {
                    SPDLOG_DEBUG("__func__:{} #1 next_state {} {} rpm:{}",__func__,(int32_t)this->next_state,this->rpm_retry_cnt[next_state],rpm);
                    this->rpm_retry_cnt[next_state] = 0;
                }
                if(this->rpm_retry_cnt[next_state] >= AUTO_DETECT_CNT)
                {
                    this->state_result[next_state] = true;
                    res["command"] = "detect_controller_fan";
                    res["result"] = "ok";
                    this->gcode->respond_feedback(res);
                    SPDLOG_INFO("__func__:{} #1 next_state {} {} res:{}",__func__,(int32_t)this->next_state,rpm,res.dump());
                    if(this->state_result[EXTRUDER_TEMP]
                        && this->state_result[HEAD_BED_TEMP]
                        && this->state_result[HEATER_FAN_RPM]
                        && this->state_result[CONTROL_FAN_RPM]
                        )
                    {
                        this->next_state = RESONANCE_TESTER;
                        elegoo::common::SignalManager::get_instance().emit_signal("autodetect:start_resonance");
                    }
                    else
                    {
                        this->next_state = AUTO_DETECT_NONE;
                        //detect_resonance_tester
                        this->state_result[RESONANCE_TESTER] = false;
                        res["command"] = "detect_resonance_tester";
                        res["result"] = "failed";
                        this->gcode->respond_feedback(res);
                        SPDLOG_INFO("__func__:{} #1 next_state {} res:{}",__func__,(int32_t)this->next_state,res.dump());
                        //detect_bed_mesh
                        this->state_result[BED_MESH] = false;
                        res["command"] = "detect_bed_mesh";
                        res["result"] = "failed";
                        this->gcode->respond_feedback(res);
                        SPDLOG_INFO("__func__:{} #1 next_state {} res:{}",__func__,(int32_t)this->next_state,res.dump());
                    }
                }
            }
            else
            {
                this->next_state = AUTO_DETECT_NONE;
                this->state_result[CONTROL_FAN_RPM] = false;
                res["command"] = "detect_controller_fan";
                res["result"] = "failed";
                this->gcode->respond_feedback(res);
                SPDLOG_INFO("__func__:{} #1 next_state {} res:{}",__func__,(int32_t)this->next_state,res.dump());
                //detect_resonance_tester
                this->state_result[RESONANCE_TESTER] = false;
                res["command"] = "detect_resonance_tester";
                res["result"] = "failed";
                this->gcode->respond_feedback(res);
                SPDLOG_INFO("__func__:{} #1 next_state {} res:{}",__func__,(int32_t)this->next_state,res.dump());
                //detect_bed_mesh
                this->state_result[BED_MESH] = false;
                res["command"] = "detect_bed_mesh";
                res["result"] = "failed";
                this->gcode->respond_feedback(res);
                SPDLOG_INFO("__func__:{} #1 next_state {} res:{}",__func__,(int32_t)this->next_state,res.dump());
            }
        }

        void AutoDetect::resonance_tester_feedback(double detect_time)
        {
            json res;
            if(this->resonance_tester)
            {
                json st = this->resonance_tester->get_status(0.);
                if(st.contains("result"))
                {
                    std::string status = st["result"].get<std::string>();
                    if("completed" == status)
                    {
                        this->next_state = BED_MESH;
                        res["command"] = "detect_resonance_tester";
                        res["result"] = "ok";
                        this->gcode->respond_feedback(res);
                        SPDLOG_INFO("__func__:{} #1 next_state {} {} res:{}",__func__,(int32_t)this->next_state,status,res.dump());
                        elegoo::common::SignalManager::get_instance().emit_signal("autodetect:start_bed_mesh");
                    }
                    else if("failed" == status)
                    {
                        this->next_state = AUTO_DETECT_NONE;
                        res["command"] = "detect_resonance_tester";
                        res["result"] = "failed";
                        this->gcode->respond_feedback(res);
                        SPDLOG_INFO("__func__:{} #1 next_state {} res:{}",__func__,(int32_t)this->next_state,res.dump());
                    }
                }
                else
                {
                    ++this->rpm_retry_cnt[next_state];
                    if(this->rpm_retry_cnt[next_state] < 5)
                    {
                        return;
                    }
                    this->next_state = AUTO_DETECT_NONE;
                    res["command"] = "detect_resonance_tester";
                    res["result"] = "failed";
                    this->gcode->respond_feedback(res);
                    SPDLOG_INFO("__func__:{} #1 next_state {} res:{}",__func__,(int32_t)this->next_state,res.dump());
                }
            }
            else
            {
                this->next_state = AUTO_DETECT_NONE;
                res["command"] = "detect_resonance_tester";
                res["result"] = "failed";
                this->gcode->respond_feedback(res);
                SPDLOG_INFO("__func__:{} #1 next_state {} res:{}",__func__,(int32_t)this->next_state,res.dump());
            }
        }

        void AutoDetect::bed_mesh_feedback(double detect_time)
        {
            json res;
            if(this->bed_mesh)
            {
                json st = this->bed_mesh->get_status(0.);
                if(st.contains("web_feedback") && st["web_feedback"].contains("result"))
                {
                    std::string status = st["web_feedback"]["result"].get<std::string>();
                    if("completed" == status)
                    {
                        this->next_state = AUTO_DETECT_NONE;
                        res["command"] = "detect_bed_mesh";
                        res["result"] = "ok";
                        this->gcode->respond_feedback(res);
                        SPDLOG_INFO("__func__:{} #1 next_state {} {} res:{}",__func__,(int32_t)this->next_state,status,res.dump());
                    }
                    else if("failed" == status)
                    {
                        this->next_state = AUTO_DETECT_NONE;
                        res["command"] = "detect_bed_mesh";
                        res["result"] = "failed";
                        this->gcode->respond_feedback(res);
                        SPDLOG_INFO("__func__:{} #1 next_state {} res:{}",__func__,(int32_t)this->next_state,res.dump());
                    }
                }
                else
                {
                    ++this->rpm_retry_cnt[next_state];
                    if(this->rpm_retry_cnt[next_state] < 5)
                    {
                        return;
                    }
                    this->next_state = AUTO_DETECT_NONE;
                    res["command"] = "detect_bed_mesh";
                    res["result"] = "failed";
                    this->gcode->respond_feedback(res);
                    SPDLOG_INFO("__func__:{} #1 next_state {} res:{}",__func__,(int32_t)this->next_state,res.dump());
                }
            }
            else
            {
                this->next_state = AUTO_DETECT_NONE;
                res["command"] = "detect_bed_mesh";
                res["result"] = "failed";
                this->gcode->respond_feedback(res);
                SPDLOG_INFO("__func__:{} #1 next_state {} res:{}",__func__,(int32_t)this->next_state,res.dump());
            }
        }

        double AutoDetect::callback(double eventtime)
        {
            SPDLOG_INFO("__func__:{} #1 next_state:{} is_auto_detect:{}",__func__,(int32_t)this->next_state,this->is_auto_detect);
            //加热热床60°
            this->gcode->run_script_from_command("M140 S60");
            SPDLOG_DEBUG("__func__:{} #1 next_state:{} is_auto_detect:{}",__func__,(int32_t)this->next_state,this->is_auto_detect);
            //加热喉管140°
            this->gcode->run_script_from_command("M104 S140");
            SPDLOG_DEBUG("__func__:{} #1 next_state:{} is_auto_detect:{}",__func__,(int32_t)this->next_state,this->is_auto_detect);
            //喉管加热 #1 temp
            this->gcode->run_script_from_command("M109 S140");
            SPDLOG_DEBUG("__func__:{} #1 next_state:{} is_auto_detect:{}",__func__,(int32_t)this->next_state,this->is_auto_detect);
            //热床加热 #2 temp 喉管散热风扇 #3 rpm
            this->gcode->run_script_from_command("M190 S60");
            SPDLOG_DEBUG("__func__:{} #1 next_state:{} is_auto_detect:{}",__func__,(int32_t)this->next_state,this->is_auto_detect);

            this->is_auto_detect = true;
            this->next_state = EXTRUDER_TEMP;
            SPDLOG_INFO("__func__:{} #1 next_state:{} is_auto_detect:{}",__func__,(int32_t)this->next_state,this->is_auto_detect);
            this->start_detect_time = get_monotonic();
            while (this->is_auto_detect)
            {
                json res;
                double detect_time = get_monotonic() - this->start_detect_time;
                switch (next_state)
                {
                case EXTRUDER_TEMP:
                    extruder_temp_feedback(detect_time);
                    break;
                case HEAD_BED_TEMP:
                    head_bed_temp_feedback(detect_time);
                    break;
                
                case HEATER_FAN_RPM:
                    heater_fan_rpm_feedback(detect_time);
                    break;
                
                case CONTROL_FAN_RPM:
                    control_fan_rpm_feedback(detect_time);
                    break;
                
                case RESONANCE_TESTER:
                    resonance_tester_feedback(detect_time);
                    break;
                case BED_MESH:
                    bed_mesh_feedback(detect_time);
                    break;
                default:
                    this->is_auto_detect = false;
                    break;
                }
                this->reactor->pause(get_monotonic() + 1.);
            }
            SPDLOG_INFO("__func__:{} #1 next_state:{} is_auto_detect:{}",__func__,(int32_t)this->next_state,this->is_auto_detect);
            return 0.0;
        }

        std::shared_ptr<AutoDetect> rfauto_detect_load_config(std::shared_ptr<ConfigWrapper> config)
        {
            SPDLOG_DEBUG("__func__:{} #1",__func__);
            std::shared_ptr<AutoDetect> auto_detect = std::make_shared<AutoDetect>(config);
            return auto_detect;
        }
    }
}
