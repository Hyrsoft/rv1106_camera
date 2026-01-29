# ========================================
# libcamera 静态库
# ========================================

set(CAMERA_SOURCES
    Camera.cpp
)

set(CAMERA_HEADERS
    Camera.hpp
)

add_library(camera STATIC ${CAMERA_SOURCES} ${CAMERA_HEADERS})

# 包含路径
target_include_directories(camera PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LUCKFOX_MPI_PATH}/include
    ${LUCKFOX_MPI_PATH}/include/rkaiq
    ${LUCKFOX_MPI_PATH}/include/rkaiq/uAPI2
    ${LUCKFOX_MPI_PATH}/include/rkaiq/common
    ${LUCKFOX_MPI_PATH}/include/rkaiq/xcore
    ${LUCKFOX_MPI_PATH}/include/rkaiq/iq_parser
    ${LUCKFOX_MPI_PATH}/include/rkaiq/iq_parser_v2
    ${LUCKFOX_MPI_PATH}/include/rkaiq/algos
)

# 链接目录
target_link_directories(camera PUBLIC
    ${LUCKFOX_MPI_PATH}/lib/uclibc
)

# 链接 spdlog（camera 库内部使用）
target_link_libraries(camera PUBLIC spdlog::spdlog)

# 设置编译时日志级别
target_compile_definitions(camera PRIVATE 
    SPDLOG_ACTIVE_LEVEL=${SPDLOG_ACTIVE_LEVEL}
)
