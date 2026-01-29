# ========================================
# 03_h264_record - H.264/HEVC 视频录制示例
# ========================================

set(TARGET_NAME H264Record)

# 源文件
set(SOURCES
    main.cpp
)

add_executable(${TARGET_NAME} ${SOURCES})

# 设置编译时日志级别
target_compile_definitions(${TARGET_NAME} PRIVATE 
    SPDLOG_ACTIVE_LEVEL=${SPDLOG_ACTIVE_LEVEL}
)

# 链接 rmg 和 MPI 库
target_link_libraries(${TARGET_NAME} 
    rmg
    ${MPI_LIBS}
    stdc++fs
)
