cmake_minimum_required(VERSION 3.10)

# 项目名称
project(elegoo_printer LANGUAGES C CXX)

# 配置编译选项
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-psabi -pthread")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-psabi -pthread")
set(CMAKE_CXX_FLAGS_RELEASE "-O0")

# 选择编译平台 "x86_64" 或 "arm"
if(CMAKE_C_COMPILER MATCHES "arm")
    set(COMPILER_PLATFORM "arm")
elseif(CMAKE_C_COMPILER MATCHES "gcc")
    set(COMPILER_PLATFORM "x86_64")
else()
    set(COMPILER_PLATFORM "x86_64")
    message(WARNING "The toolchain information is not specified, using the system default configuration.")
endif()

# 头文件路径列表
set(PROJECT_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/chelper
    ${CMAKE_SOURCE_DIR}/extras
    ${CMAKE_SOURCE_DIR}/extras/display
    ${CMAKE_SOURCE_DIR}/extras/bedmesh
    ${CMAKE_SOURCE_DIR}/kinematics
    ${CMAKE_SOURCE_DIR}/api_module
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/communication
    ${CMAKE_SOURCE_DIR}/lib/libco/include
    ${CMAKE_SOURCE_DIR}/lib/simpleini/include
    ${CMAKE_SOURCE_DIR}/lib/nlohmann
    ${CMAKE_SOURCE_DIR}/lib/spdlog/include
    ${CMAKE_SOURCE_DIR}/lib/fftw/include
    ${CMAKE_SOURCE_DIR}/lib/zlib/include
    ${CMAKE_SOURCE_DIR}/lib/curl/include
    ${CMAKE_SOURCE_DIR}/lib/openssl/include
    ${CMAKE_SOURCE_DIR}/lib/mqtt/include
    ${CMAKE_SOURCE_DIR}/lib/libhv/include
    ${CMAKE_SOURCE_DIR}/lib/agora_sdk/include
    ${CMAKE_SOURCE_DIR}/lib/sqlite
)
include_directories(${PROJECT_INCLUDE_DIRS})

add_subdirectory(chelper)
add_subdirectory(extras)
add_subdirectory(extras/ai_camera)
add_subdirectory(moonraker_bridge moonraker_bridge_build)
set_target_properties(moonraker_bridge PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(ai_camera PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_subdirectory(kinematics)
add_subdirectory(common)

# 添加源文件路径
set(PROJECT_SOURCES
    ${PROJECT_SOURCE_DIR}/clocksync.cpp
    ${PROJECT_SOURCE_DIR}/configfile.cpp
    ${PROJECT_SOURCE_DIR}/gcode.cpp
    ${PROJECT_SOURCE_DIR}/main.cpp
    ${PROJECT_SOURCE_DIR}/mathutil.cpp
    ${PROJECT_SOURCE_DIR}/mcu.cpp
    ${PROJECT_SOURCE_DIR}/pins.cpp
    ${PROJECT_SOURCE_DIR}/host_warpper.cpp
    ${PROJECT_SOURCE_DIR}/printer.cpp
    ${PROJECT_SOURCE_DIR}/reactor.cpp
    ${PROJECT_SOURCE_DIR}/serialhdl.cpp
    ${PROJECT_SOURCE_DIR}/serial_bootloader.cpp
    ${PROJECT_SOURCE_DIR}/stepper.cpp
    ${PROJECT_SOURCE_DIR}/toolhead.cpp
    ${PROJECT_SOURCE_DIR}/webhooks.cpp
    ${PROJECT_SOURCE_DIR}/msgproto.cpp
    ${PROJECT_SOURCE_DIR}/chipbase.cpp
    # ${PROJECT_SOURCE_DIR}/update_ota.cpp
    ${PROJECT_SOURCE_DIR}/extruder.cpp
)
add_executable(elegoo_printer ${PROJECT_SOURCES})

# 链接库
file(GLOB OPENSSL_ZLIB_FILES "${CMAKE_SOURCE_DIR}/lib/zlib/bin/${COMPILER_PLATFORM}/lib*.so*")
file(GLOB CURL_FILES "${CMAKE_SOURCE_DIR}/lib/curl/bin/${COMPILER_PLATFORM}/libcurl.so*")
file(GLOB OPENSSL_SSL_FILES "${CMAKE_SOURCE_DIR}/lib/openssl/bin/${COMPILER_PLATFORM}/libssl.so*")
file(GLOB OPENSSL_CRYPTO_FILES "${CMAKE_SOURCE_DIR}/lib/openssl/bin/${COMPILER_PLATFORM}/libcrypto.so*")

set(THIRD_PARTY_LIBS
    ${CMAKE_SOURCE_DIR}/lib/libco/bin/${COMPILER_PLATFORM}/libcolib.so
    ${CMAKE_SOURCE_DIR}/lib/simpleini/bin/${COMPILER_PLATFORM}/libconvertutf.so
)

# Glob flexibly for .so and .so.N variants
file(GLOB FFTW_FILES "${CMAKE_SOURCE_DIR}/lib/fftw/bin/${COMPILER_PLATFORM}/libfftw3.so*")
file(GLOB SPDLOG_FILES "${CMAKE_SOURCE_DIR}/lib/spdlog/bin/${COMPILER_PLATFORM}/libspdlog.so*")
file(GLOB BEDMESH_FILE "${CMAKE_SOURCE_DIR}/extras/bedmesh/${COMPILER_PLATFORM}/libelegoo_bed_mesh.so")

list(APPEND THIRD_PARTY_LIBS 
    ${FFTW_FILES}
    ${SPDLOG_FILES}
    ${BEDMESH_FILE}
    ${OPENSSL_ZLIB_FILES}
    ${CURL_FILES}
    ${OPENSSL_SSL_FILES}
    ${OPENSSL_CRYPTO_FILES}
)

# Filter out empty results
list(FILTER THIRD_PARTY_LIBS EXCLUDE REGEX "^$")

target_link_libraries(elegoo_printer
    ${THIRD_PARTY_LIBS}
    elegoo_log
    util
    exception_handler
    event_handler
    utilities
    any
    kinematics
    elegoo_extras
    turbo_core
)

# # 生成console调试程序
# set(CONSOLE_SOURCES
#     ${PROJECT_SOURCE_DIR}/console.cpp
#     ${PROJECT_SOURCE_DIR}/reactor.cpp
#     ${PROJECT_SOURCE_DIR}/serialhdl.cpp
#     ${PROJECT_SOURCE_DIR}/clocksync.cpp
#     ${PROJECT_SOURCE_DIR}/msgproto.cpp
# )

# add_executable(console ${CONSOLE_SOURCES})

# #临时查找路径
# target_link_libraries(console
#     "${CMAKE_SOURCE_DIR}/lib/libco/bin/${COMPILER_PLATFORM}/libcolib.so"
#     "${CMAKE_SOURCE_DIR}/lib/c_helper/bin/${COMPILER_PLATFORM}/c_helper.so"
#     "${CMAKE_SOURCE_DIR}/lib/zlib/bin/${COMPILER_PLATFORM}/libz.so.1.2.11"
#     util
#     exception_handler
#     event_handler
#     utilities
# )

# # 生成parsedump调试程序
# set(PARSEDUMP_SOURCES
#     ${PROJECT_SOURCE_DIR}/parsedump.cpp
#     ${PROJECT_SOURCE_DIR}/msgproto.cpp
# )

# add_executable(parsedump ${PARSEDUMP_SOURCES})

# #临时查找路径
# target_link_libraries(parsedump
#     "${CMAKE_SOURCE_DIR}/lib/zlib/bin/${COMPILER_PLATFORM}/libz.so.1.2.11"
# )

# add_executable(update_ota_test  ${PROJECT_SOURCE_DIR}/update_ota_test.cpp)
# target_link_libraries(update_ota_test
#     ${CURL_FILES}
#     ${OPENSSL_SSL_FILES}
#     ${OPENSSL_CRYPTO_FILES}
# )

# add_executable(socket_client  ${PROJECT_SOURCE_DIR}/socket_client.cpp)
# add_executable(socket_server  ${PROJECT_SOURCE_DIR}/socket_server.cpp)
add_executable(debug_window  ${PROJECT_SOURCE_DIR}/whconsole.cpp)

file(COPY ${THIRD_PARTY_LIBS} DESTINATION ${CMAKE_BINARY_DIR})

# 设置安装根目录，按构建类型分类
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(INSTALL_DIR ${CMAKE_SOURCE_DIR}/install/debug)
else()
    set(INSTALL_DIR ${CMAKE_SOURCE_DIR}/install/release)
endif()

# 安装可执行文件
install(TARGETS
    elegoo_printer
    debug_window
    ai_camera
    moonraker_bridge
    RUNTIME DESTINATION ${INSTALL_DIR}
)

# 安装共享库
install(TARGETS 
    turbo_core    
    elegoo_extras
    elegoo_log
    exception_handler
    utilities
    event_handler
    any
    kinematics
    LIBRARY DESTINATION ${INSTALL_DIR}
)

# 安装第三方依赖库
install(FILES
    ${THIRD_PARTY_LIBS} DESTINATION ${INSTALL_DIR}
)

# 安装参数文件
install(FILES
    ${CMAKE_SOURCE_DIR}/../debugfile/printer.cfg
    ${CMAKE_SOURCE_DIR}/../debugfile/printer_dsp.cfg
    ${CMAKE_SOURCE_DIR}/../debugfile/product_test.cfg
    ${CMAKE_SOURCE_DIR}/../debugfile/temperature_sensors.cfg 
    DESTINATION ${INSTALL_DIR}/config
)