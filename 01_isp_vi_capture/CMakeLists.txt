cmake_minimum_required(VERSION 3.10)
project(CameraCapture)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# ========================================
# 编译选项
# ========================================
# spdlog 编译时日志级别控制
# 设置此选项可在编译时完全移除低于该级别的日志代码（零开销）
# 可选值:
#   0 = TRACE (显示所有日志)
#   1 = DEBUG
#   2 = INFO (默认，推荐用于发布版本)
#   3 = WARN
#   4 = ERROR
#   5 = CRITICAL
#   6 = OFF (完全禁用所有日志)
set(SPDLOG_ACTIVE_LEVEL "2" CACHE STRING "Compile-time log level (0=TRACE, 6=OFF)")

# 不能禁用异常，因为 spdlog 依赖异常处理

set(LUCKFOX_MPI_PATH ../third_party/luckfox_pico_rkmpi_example) # luckfox提供的RKMPI库路径
set(SPDLOG_PATH ../third_party/spdlog) # spdlog库路径
set(COMMON_PATH ../common) # 公共代码路径

# 使用完整的绝对路径指定静态库
set(MPI_LIBS 
    sample_comm
    rockit
    rockchip_mpp
    rkaiq
    rga
    rtsp
    pthread
    dl
)

# 源文件
set(SOURCES
    main.cpp
    ${COMMON_PATH}/Camera.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

# 包含路径
target_include_directories(${PROJECT_NAME} PUBLIC
    ${COMMON_PATH}
    ${LUCKFOX_MPI_PATH}/include
    ${LUCKFOX_MPI_PATH}/include/rkaiq
    ${LUCKFOX_MPI_PATH}/include/rkaiq/uAPI2
    ${LUCKFOX_MPI_PATH}/include/rkaiq/common
    ${LUCKFOX_MPI_PATH}/include/rkaiq/xcore
    ${LUCKFOX_MPI_PATH}/include/rkaiq/iq_parser
    ${LUCKFOX_MPI_PATH}/include/rkaiq/iq_parser_v2
    ${LUCKFOX_MPI_PATH}/include/rkaiq/algos
)

target_link_directories(${PROJECT_NAME} PUBLIC
    "${LUCKFOX_MPI_PATH}/lib/uclibc"
)

# ========================================
# spdlog 配置
# ========================================
add_subdirectory(${SPDLOG_PATH} spdlog)

# 设置编译时日志级别（SPDLOG_ACTIVE_LEVEL）
# 低于此级别的 SPDLOG_XXX 宏调用会被编译器完全优化掉
target_compile_definitions(${PROJECT_NAME} PRIVATE 
    SPDLOG_ACTIVE_LEVEL=${SPDLOG_ACTIVE_LEVEL}
)

target_link_libraries(${PROJECT_NAME} ${MPI_LIBS} spdlog::spdlog stdc++fs)

message(STATUS "spdlog compile-time log level: ${SPDLOG_ACTIVE_LEVEL} (0=TRACE, 2=INFO, 6=OFF)")