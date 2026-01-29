# ========================================
# 01_isp_vi_capture - 相机采集示例
# ========================================

set(TARGET_NAME CameraCapture)

# 源文件
set(SOURCES
    main.cpp
)

add_executable(${TARGET_NAME} ${SOURCES})

# 设置编译时日志级别
target_compile_definitions(${TARGET_NAME} PRIVATE 
    SPDLOG_ACTIVE_LEVEL=${SPDLOG_ACTIVE_LEVEL}
)

# 链接 libcamera 和 MPI 库
target_link_libraries(${TARGET_NAME} 
    camera
    ${MPI_LIBS}
    stdc++fs
)