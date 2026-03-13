# 设置库的输出目录
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_library(elegoo_extras SHARED
    gcode_macro.cpp
    gcode_arcs.cpp
    gcode_move.cpp
    output_pin.cpp
    adc_scaled.cpp
    adc_temperature.cpp
    motion_report.cpp
    bulk_sensor.cpp
    fan.cpp
    fan_generic.cpp
    controller_fan.cpp
    stepper_enable.cpp 
    extruder_stepper.cpp
    thermistor.cpp   
    idle_timeout.cpp
    gcode_macro.cpp
    homing.cpp
    force_move.cpp
    sdcard_loop.cpp
    homing_override.cpp 
    homing_heaters.cpp
    heaters.cpp 
    heater_bed.cpp
    heater_fan.cpp 
    tmc_uart.cpp
    tuning_tower.cpp
    pwm_cycle_time.cpp
    pause_resume.cpp
    print_stats.cpp
    pid_calibrate.cpp
    resonance_tester.cpp
    tmc.cpp
    tmc2209.cpp
    shaper_defs.cpp
    shaper_calibrate.cpp
    safe_z_home.cpp
    exclude_object.cpp
    save_variables.cpp
    virtual_sdcard.cpp
    query_adc.cpp
    error_mcu.cpp
    pulse_counter.cpp
    adxl345.cpp
    lis2dw.cpp
    lis2dh.cpp
    input_shaper.cpp
    auto_detect.cpp
    temperature_sensor.cpp
    verify_heater.cpp
    canbus_ids.cpp
    # led.cpp   需要依赖display.cpp
    extras_factory.cpp
    tmc2130.cpp
    bus.cpp
    statistics.cpp
    buttons.cpp
    filament_switch_sensor.cpp
    por.cpp
    z_compensation.cpp
    filament_load_unload.cpp
    filament_motion_sensor.cpp
    servo.cpp
    # cavity_temperature.cpp
    product_test.cpp
    # rs485.cpp
    mmu.cpp
    cover_switch_sensor.cpp
    tangling_switch_sensor.cpp
    cavity_fan.cpp
)

# On x86_64 there is no prebuilt ARM bedmesh library, so provide stubs
if(COMPILER_PLATFORM STREQUAL "x86_64")
    target_sources(elegoo_extras PRIVATE bedmesh_x64_stubs.cpp)
endif()

target_link_libraries(elegoo_extras
    PRIVATE "${CMAKE_SOURCE_DIR}/lib/fftw/bin/${COMPILER_PLATFORM}/libfftw3.so.3"
    PRIVATE utilities
    PRIVATE event_handler
    PRIVATE any
)
