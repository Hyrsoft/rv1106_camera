cmake_minimum_required(VERSION 3.10)
project(luckfox-pico-ipc-examples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# ========================================
# 全局编译选项
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

# ========================================
# 第三方库路径
# ========================================
set(LUCKFOX_MPI_PATH ${CMAKE_SOURCE_DIR}/third_party/luckfox_pico_rkmpi_example)
set(SPDLOG_PATH ${CMAKE_SOURCE_DIR}/third_party/spdlog)

# ========================================
# MPI 库列表 （取自 luckfox-pico-rkmpi-example）
# ========================================
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

# ========================================
# spdlog 配置
# ========================================
add_subdirectory(${SPDLOG_PATH} spdlog)

# ========================================
# 添加子目录
# ========================================
# 公共库
add_subdirectory(common)

# 示例程序
add_subdirectory(01_isp_vi_capture)
# add_subdirectory(02_xxx)  # 未来的示例
# add_subdirectory(03_xxx)  # 未来的示例

message(STATUS "spdlog compile-time log level: ${SPDLOG_ACTIVE_LEVEL} (0=TRACE, 2=INFO, 6=OFF)")
