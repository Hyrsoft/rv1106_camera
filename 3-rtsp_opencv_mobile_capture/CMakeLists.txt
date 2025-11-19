cmake_minimum_required(VERSION 3.15)
project(rtsp_opencv_mobile_capture)
add_definitions(-DRV1106_1103)
set(CMAKE_CXX_STANDARD 17)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# opencv-mobile
set(OpenCV_DIR "../opencv-mobile-4.10.0-luckfox-pico/lib/cmake/opencv4")
find_package(OpenCV REQUIRED)
message(STATUS "Debug OpenCV_LIBS = ${OpenCV_LIBS}")

#Thread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# 设置 RPATH，让可执行文件在运行时去 /oem/usr/lib 目录寻找 .so 文件
set(CMAKE_INSTALL_RPATH "/oem/usr/lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

add_executable(${PROJECT_NAME} main.cpp luckfox_mpi.cpp)

# # 1. 添加编译选项，将函数和数据放入独立段
# add_compile_options(-ffunction-sections -fdata-sections)

# # 2. 添加链接选项，让链接器移除未使用的段
# # 注意：使用 set 而不是 add_link_options 可以更好地处理不同构建类型
# set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")

# # 对于共享库和模块库也可以设置
# set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections")
# set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--gc-sections")

add_compile_options(-g -Wall
                    -DISP_HW_V30 -DRKPLATFORM=ON -DARCH64=OFF
                    -DROCKIVA -DUAPI2
                    -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 
                    )

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include/rknn
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include/librga
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include/rkaiq
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include/rkaiq/uAPI2
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include/rkaiq/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include/rkaiq/xcore
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include/rkaiq/algos
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include/rkaiq/iq_parser
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include/rkaiq/iq_parser_v2
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include/rkaiq/smartIr
)

target_link_directories(${PROJECT_NAME} PRIVATE 
    ${OpenCV_LIB_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/lib/uclibc/
)

target_link_libraries(${PROJECT_NAME} 
    ${OpenCV_LIBS}
    rknnmrt
    Threads::Threads
    rockiva
    sample_comm
    rockit
    rockchip_mpp
    rkaiq
    pthread
    rtsp
    rga
)