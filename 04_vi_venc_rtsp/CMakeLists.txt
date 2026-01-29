# ========================================
# 03_vi_venc_rtsp - RTSP 视频流示例
# ========================================

set(TARGET_NAME ViVencRtsp)

# 源文件
set(SOURCES
    main.cpp
)

add_executable(${TARGET_NAME} ${SOURCES})

# 设置编译时日志级别
target_compile_definitions(${TARGET_NAME} PRIVATE 
    SPDLOG_ACTIVE_LEVEL=${SPDLOG_ACTIVE_LEVEL}
)

# RTSP 静态库路径
set(RTSP_LIB_PATH ${CMAKE_SOURCE_DIR}/third_party/luckfox_pico_rkmpi_example/lib/uclibc)

# 链接 rmg 和 MPI 库
target_link_libraries(${TARGET_NAME} 
    rmg
    ${MPI_LIBS}
    # RTSP 静态库
    ${RTSP_LIB_PATH}/librtsp.a
    stdc++fs
)
