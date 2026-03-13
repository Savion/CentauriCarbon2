cmake_minimum_required(VERSION 3.10)

set(BRIDGE_SOURCES
    main.cpp
    cc2_client.cpp
    bridge_server.cpp
)

add_executable(moonraker_bridge ${BRIDGE_SOURCES})

target_include_directories(moonraker_bridge PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/lib/nlohmann
    ${CMAKE_SOURCE_DIR}/lib/libhv/include
    ${CMAKE_SOURCE_DIR}/lib/spdlog/include
)

target_compile_definitions(moonraker_bridge PRIVATE
    ENABLE_UDS
)

target_compile_options(moonraker_bridge PRIVATE
    -Wno-psabi
)

file(GLOB SPDLOG_FILES "${CMAKE_SOURCE_DIR}/lib/spdlog/bin/${COMPILER_PLATFORM}/libspdlog.so*")
file(GLOB OPENSSL_SSL_FILES "${CMAKE_SOURCE_DIR}/lib/openssl/bin/${COMPILER_PLATFORM}/libssl.so*")
file(GLOB OPENSSL_CRYPTO_FILES "${CMAKE_SOURCE_DIR}/lib/openssl/bin/${COMPILER_PLATFORM}/libcrypto.so*")

target_link_libraries(moonraker_bridge PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/libhv/lib/${COMPILER_PLATFORM}/libhv.so
    ${SPDLOG_FILES}
    ${OPENSSL_SSL_FILES}
    ${OPENSSL_CRYPTO_FILES}
    pthread
)

# RUNTIME_OUTPUT_DIRECTORY is set by the parent CMakeLists.txt
# (same pattern as ai_camera)
