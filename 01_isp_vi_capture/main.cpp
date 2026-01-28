#include <iostream>
#include <vector>
#include <fstream>
#include <memory>
#include <filesystem>
#include <thread>
#include <chrono>

// spdlog 日志库
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"

// Rockchip MPI & AIQ headers
#include "rk_common.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_sys.h"
#include "sample_comm.h"
#include "rk_aiq_user_api2_sysctl.h" // 必须包含这个来操作 Scene

namespace fs = std::filesystem;
using namespace std::chrono_literals;

class CSICamera {
public:
    CSICamera(int width, int height, const std::string& iq_path, std::shared_ptr<spdlog::logger> logger) 
        : width_(width), height_(height), iq_path_(iq_path), logger_(logger), init_success_(false) {
        
        // 步骤顺序：SYS Init -> ISP Init -> VI Init
        logger_->info("Initializing MPI system");
        if (RK_MPI_SYS_Init() != RK_SUCCESS) {
            logger_->error("SYS Init Failed");
            return;
        }
        
        logger_->info("Initializing ISP");
        if (init_isp() != RK_SUCCESS) {
            logger_->error("ISP Init Failed");
            return;
        }
        
        logger_->info("Initializing VI channel");
        if (init_vi() != RK_SUCCESS) {
            logger_->error("VI Init Failed");
            return;
        }
        
        init_success_ = true;
        logger_->info("Camera initialization complete");
    }
    
    bool is_initialized() const {
        return init_success_;
    }

    ~CSICamera() {
        logger_->info("Cleaning up camera resources");
        RK_MPI_VI_DisableChn(vi_pipe_, vi_chn_);
        SAMPLE_COMM_ISP_Stop(0);
        RK_MPI_SYS_Exit();
        logger_->info("Cleanup complete");
    }

    bool capture_to_file(const std::string& filename) {
        VIDEO_FRAME_INFO_S frame;
        // 4K 采集耗时较长，超时时间给足 2000ms
        logger_->debug("Requesting frame from VI channel");
        RK_S32 s32Ret = RK_MPI_VI_GetChnFrame(vi_pipe_, vi_chn_, &frame, 2000);
        if (s32Ret != RK_SUCCESS) {
            logger_->error("Get Frame Failed: 0x{:x}", s32Ret);
            return false;
        }

        void* data = RK_MPI_MB_Handle2VirAddr(frame.stVFrame.pMbBlk);
        size_t buffer_size = frame.stVFrame.u32Width * frame.stVFrame.u32Height * 3 / 2;
        logger_->info("Frame captured: {}x{}, size: {} bytes", frame.stVFrame.u32Width, frame.stVFrame.u32Height, buffer_size);

        std::ofstream ofs(filename, std::ios::binary);
        if (ofs.is_open()) {
            ofs.write(reinterpret_cast<const char*>(data), buffer_size);
            ofs.close();
            logger_->info("Successfully saved 4K YUV frame to {}", filename);
        } else {
            logger_->error("Failed to open file: {}", filename);
        }

        RK_MPI_VI_ReleaseChnFrame(vi_pipe_, vi_chn_, &frame);
        return true;
    }

private:
    int width_, height_;
    std::string iq_path_;
    const int vi_pipe_ = 0;
    const int vi_chn_ = 0;
    std::shared_ptr<spdlog::logger> logger_;
    bool init_success_;

    RK_S32 init_isp() {
        logger_->info("Starting ISP with IQ Path: {}", iq_path_);
        
        // 初始化 ISP 系统
        RK_S32 s32Ret = SAMPLE_COMM_ISP_Init(0, RK_AIQ_WORKING_MODE_NORMAL, RK_FALSE, iq_path_.c_str());
        if (s32Ret != RK_SUCCESS) {
            logger_->error("SAMPLE_COMM_ISP_Init failed: 0x{:x}", s32Ret);
            return s32Ret;
        }
        
        logger_->debug("ISP initialized successfully");

        // 启动 ISP 线程
        s32Ret = SAMPLE_COMM_ISP_Run(0);
        if (s32Ret != RK_SUCCESS) {
            logger_->error("SAMPLE_COMM_ISP_Run failed: 0x{:x}", s32Ret);
        } else {
            logger_->debug("ISP running successfully");
        }
        return s32Ret;
    }

    RK_S32 init_vi() {
        logger_->debug("Configuring VI channel: {}x{}", width_, height_);
        VI_CHN_ATTR_S stChnAttr;
        memset(&stChnAttr, 0, sizeof(VI_CHN_ATTR_S));
        
        // 4K 内存优化配置
        stChnAttr.stIspOpt.u32BufCount = 2; // 将缓冲区从 3 降到 2，节省约 12MB 内存
        stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_MMAP;
        stChnAttr.stSize.u32Width = width_;
        stChnAttr.stSize.u32Height = height_;
        stChnAttr.enPixelFormat = RK_FMT_YUV420SP; // NV12
        stChnAttr.enCompressMode = COMPRESS_MODE_NONE;

        RK_MPI_VI_SetChnAttr(vi_pipe_, vi_chn_, &stChnAttr);
        RK_S32 s32Ret = RK_MPI_VI_EnableChn(vi_pipe_, vi_chn_);
        if (s32Ret != RK_SUCCESS) {
            logger_->error("VI channel enable failed: 0x{:x}", s32Ret);
        } else {
            logger_->debug("VI channel enabled successfully");
        }
        return s32Ret;
    }
};

int main() {
    // 初始化 spdlog 日志
    // 创建控制台日志（彩色输出）
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);
    
    // 创建文件日志
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("CameraCapture.log", true);
    file_sink->set_level(spdlog::level::debug);
    
    // 合并两个 sink
    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
    auto logger = std::make_shared<spdlog::logger>("CameraCapture", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::debug);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::register_logger(logger);
    
    logger->info("========================================");
    logger->info("Camera Capture Application Started");
    logger->info("========================================");
    
    // 先清理可能残留在后台的服务
    logger->info("Killing residual processes...");
    system("killall luckfox_rtsp_opencv 2>/dev/null");
    system("killall rkaiq_3A_server 2>/dev/null");
    
    logger->info("Creating camera instance: 3840x2160 @ /etc/iqfiles");
    CSICamera cam(3840, 2160, "/etc/iqfiles", logger);
    
    if (!cam.is_initialized()) {
        logger->error("Camera initialization failed!");
        return -1;
    }

    logger->info("Waiting for 3A (AE/AWB) to stabilize...");
    std::this_thread::sleep_for(3s); 

    if (cam.capture_to_file("capture_4k.yuv")) {
        logger->info("View the file using:");
        logger->info("ffplay -f rawvideo -pixel_format nv12 -video_size 3840x2160 capture_4k.yuv");
    }
    
    logger->info("========================================");
    logger->info("Capture completed successfully");
    logger->info("========================================");
    
    return 0;
}