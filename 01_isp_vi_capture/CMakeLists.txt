cmake_minimum_required(VERSION 3.10)
project(CameraCapture)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# 禁用异常支持（减少代码体积和运行时开销）
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-exceptions")

set(LUCKFOX_MPI_PATH ../third_party/luckfox_pico_rkmpi_example)
set(SPDLOG_PATH ../third_party/spdlog)

# 添加 spdlog 为子目录
add_subdirectory(${SPDLOG_PATH} spdlog)

# 静态链接库路径 - 使用绝对路径
set(MPI_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${LUCKFOX_MPI_PATH}/lib/uclibc")

# 使用完整的绝对路径指定静态库
set(MPI_LIBS 
    "${MPI_LIB_DIR}/libsample_comm.a"
    "${MPI_LIB_DIR}/librockit.a"
    "${MPI_LIB_DIR}/librockchip_mpp.a"
    "${MPI_LIB_DIR}/librkaiq.a"
    "${MPI_LIB_DIR}/librga.a"
    "${MPI_LIB_DIR}/librtsp.a"
    pthread
    dl
)

add_executable(${PROJECT_NAME} main.cpp)

# 包含路径
target_include_directories(${PROJECT_NAME} PUBLIC
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

target_link_libraries(${PROJECT_NAME} ${MPI_LIBS} spdlog::spdlog stdc++fs)