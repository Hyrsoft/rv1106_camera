/**
 * @file VideoCapture.cpp
 * @brief 视频采集模块 - 实现文件
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#include "VideoCapture.hpp"

#include <cstring>

#include <spdlog/spdlog.h>

#include "rk_mpi_vi.h"

namespace rmg {

    VideoCapture::VideoCapture(const Config &config) :
        MediaModule("VideoCapture", ModuleType::kSource), config_(config) {}

    VideoCapture::~VideoCapture() {
        Stop();

        if (vi_initialized_) {
            DeinitVi();
        }
        if (isp_initialized_) {
            DeinitIsp();
        }

        // SystemGuard 析构时自动调用 Deinitialize
        sys_guard_.reset();

        SPDLOG_INFO("VideoCapture resources released");
    }

    bool VideoCapture::Initialize() {
        if (GetState() != ModuleState::kUninitialized) {
            SPDLOG_WARN("VideoCapture already initialized");
            return true;
        }

        SPDLOG_INFO("Initializing VideoCapture ({}x{}, format: {})", config_.width, config_.height,
                    static_cast<int>(config_.pixel_format));

        // 1. 初始化 MPI 系统（通过 SystemGuard）
        sys_guard_ = std::make_unique<SystemGuard>();
        if (!sys_guard_->IsValid()) {
            SPDLOG_ERROR("Failed to initialize MPI system");
            SetState(ModuleState::kError);
            return false;
        }

        // 2. 初始化 ISP
        if (!InitIsp()) {
            SPDLOG_ERROR("Failed to initialize ISP");
            SetState(ModuleState::kError);
            return false;
        }
        isp_initialized_ = true;

        // 3. 初始化 VI
        if (!InitVi()) {
            SPDLOG_ERROR("Failed to initialize VI");
            SetState(ModuleState::kError);
            return false;
        }
        vi_initialized_ = true;

        SetState(ModuleState::kInitialized);
        SPDLOG_INFO("VideoCapture initialized successfully");
        return true;
    }

    bool VideoCapture::Start() {
        if (GetState() != ModuleState::kInitialized && GetState() != ModuleState::kStopped) {
            SPDLOG_ERROR("VideoCapture not in valid state to start");
            return false;
        }

        SPDLOG_INFO("Starting VideoCapture...");

        // 启动采集线程（回调模式）
        running_.store(true);
        capture_thread_ = std::thread(&VideoCapture::CaptureThread, this);

        SetState(ModuleState::kRunning);
        SPDLOG_INFO("VideoCapture started");
        return true;
    }

    void VideoCapture::Stop() {
        if (!running_.load()) {
            return;
        }

        SPDLOG_INFO("Stopping VideoCapture...");

        running_.store(false);

        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }

        SetState(ModuleState::kStopped);
        SPDLOG_INFO("VideoCapture stopped");
    }

    OptionalYuvFrame VideoCapture::GetFrame(int timeout_ms) {
        if (GetState() != ModuleState::kInitialized && GetState() != ModuleState::kRunning) {
            SPDLOG_ERROR("VideoCapture not initialized");
            return std::nullopt;
        }

        VIDEO_FRAME_INFO_S frame_info;
        std::memset(&frame_info, 0, sizeof(frame_info));

        RK_S32 ret = RK_MPI_VI_GetChnFrame(config_.pipe_id, config_.chn_id, &frame_info, timeout_ms);

        if (ret != RK_SUCCESS) {
            if (ret != RK_ERR_VI_BUF_EMPTY) {
                SPDLOG_WARN("RK_MPI_VI_GetChnFrame failed: 0x{:08X}", ret);
            }
            return std::nullopt;
        }

        // 创建带自定义释放器的 YuvFrame（值语义）
        uint32_t pipe_id = config_.pipe_id;
        uint32_t chn_id = config_.chn_id;
        auto release_cb = [pipe_id, chn_id](VIDEO_FRAME_INFO_S *frame) {
            RK_S32 ret = RK_MPI_VI_ReleaseChnFrame(pipe_id, chn_id, frame);
            if (ret != RK_SUCCESS) {
                SPDLOG_WARN("Failed to release VI frame: 0x{:08X}", ret);
            }
        };

        return YuvFrame(frame_info, release_cb);
    }

    ModuleEndpoint VideoCapture::GetEndpoint() const {
        return ModuleEndpoint{
                .mod_id = RK_ID_VI,
                .dev_id = static_cast<int32_t>(config_.pipe_id),
                .chn_id = static_cast<int32_t>(config_.chn_id),
        };
    }

    uint32_t VideoCapture::GetCurrentFps() const {
        if (GetState() == ModuleState::kUninitialized) {
            return 0;
        }

        VI_CHN_STATUS_S chn_status;
        std::memset(&chn_status, 0, sizeof(chn_status));

        RK_S32 ret = RK_MPI_VI_QueryChnStatus(config_.pipe_id, config_.chn_id, &chn_status);
        if (ret != RK_SUCCESS) {
            return 0;
        }

        return chn_status.u32FrameRate;
    }

    bool VideoCapture::SetFrameRate(uint32_t fps) {
        if (!isp_initialized_) {
            SPDLOG_ERROR("ISP not initialized");
            return false;
        }

        RK_S32 ret = SAMPLE_COMM_ISP_SetFrameRate(config_.cam_id, fps);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("Failed to set frame rate: {}", ret);
            return false;
        }

        SPDLOG_INFO("Frame rate set to {} fps", fps);
        return true;
    }

    bool VideoCapture::SetMirrorFlip(bool mirror, bool flip) {
        if (!isp_initialized_) {
            SPDLOG_ERROR("ISP not initialized");
            return false;
        }

        RK_S32 ret = SAMPLE_COMM_ISP_SetMirrorFlip(config_.cam_id, mirror ? 1 : 0, flip ? 1 : 0);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("Failed to set mirror/flip: {}", ret);
            return false;
        }

        SPDLOG_INFO("Mirror: {}, Flip: {}", mirror, flip);
        return true;
    }

    bool VideoCapture::InitIsp() {
        SPDLOG_INFO("Initializing ISP (cam_id: {}, iq_path: {})...", config_.cam_id, config_.iq_path);

        RK_S32 ret = SAMPLE_COMM_ISP_Init(config_.cam_id, config_.hdr_mode, config_.multi_cam ? RK_TRUE : RK_FALSE,
                                          config_.iq_path.c_str());
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("SAMPLE_COMM_ISP_Init failed: {}", ret);
            return false;
        }

        ret = SAMPLE_COMM_ISP_Run(config_.cam_id);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("SAMPLE_COMM_ISP_Run failed: {}", ret);
            SAMPLE_COMM_ISP_Stop(config_.cam_id);
            return false;
        }

        SPDLOG_INFO("ISP initialized and running");
        return true;
    }

    bool VideoCapture::InitVi() {
        SPDLOG_INFO("Initializing VI...");

        RK_S32 ret;

        // 1. 配置 VI 设备属性
        VI_DEV_ATTR_S dev_attr;
        std::memset(&dev_attr, 0, sizeof(dev_attr));
        dev_attr.stMaxSize.u32Width = config_.width;
        dev_attr.stMaxSize.u32Height = config_.height;
        dev_attr.enPixFmt = config_.pixel_format;
        dev_attr.enBufType = VI_V4L2_MEMORY_TYPE_DMABUF;
        dev_attr.u32BufCount = config_.buf_count;

        ret = RK_MPI_VI_SetDevAttr(config_.cam_id, &dev_attr);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VI_SetDevAttr failed: 0x{:08X}", ret);
            return false;
        }

        ret = RK_MPI_VI_EnableDev(config_.cam_id);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VI_EnableDev failed: 0x{:08X}", ret);
            return false;
        }

        // 2. 绑定 Pipe
        VI_DEV_BIND_PIPE_S bind_pipe;
        std::memset(&bind_pipe, 0, sizeof(bind_pipe));
        bind_pipe.u32Num = 1;
        bind_pipe.PipeId[0] = config_.pipe_id;

        ret = RK_MPI_VI_SetDevBindPipe(config_.cam_id, &bind_pipe);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VI_SetDevBindPipe failed: 0x{:08X}", ret);
            RK_MPI_VI_DisableDev(config_.cam_id);
            return false;
        }

        // 3. 配置 VI 通道属性
        VI_CHN_ATTR_S chn_attr;
        std::memset(&chn_attr, 0, sizeof(chn_attr));

        chn_attr.stSize.u32Width = config_.width;
        chn_attr.stSize.u32Height = config_.height;
        chn_attr.enPixelFormat = config_.pixel_format;
        chn_attr.u32Depth = config_.depth;

        chn_attr.stIspOpt.u32BufCount = config_.buf_count;
        chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
        chn_attr.stIspOpt.bNoUseLibV4L2 = RK_TRUE;
        chn_attr.stIspOpt.stMaxSize.u32Width = config_.width;
        chn_attr.stIspOpt.stMaxSize.u32Height = config_.height;

        std::strncpy(reinterpret_cast<char *>(chn_attr.stIspOpt.aEntityName), config_.dev_name.c_str(),
                     MAX_VI_ENTITY_NAME_LEN - 1);

        ret = RK_MPI_VI_SetChnAttr(config_.pipe_id, config_.chn_id, &chn_attr);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VI_SetChnAttr failed: 0x{:08X}", ret);
            RK_MPI_VI_DisableDev(config_.cam_id);
            return false;
        }

        ret = RK_MPI_VI_EnableChn(config_.pipe_id, config_.chn_id);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VI_EnableChn failed: 0x{:08X}", ret);
            RK_MPI_VI_DisableDev(config_.cam_id);
            return false;
        }

        SPDLOG_INFO("VI initialized (pipe: {}, chn: {})", config_.pipe_id, config_.chn_id);
        return true;
    }

    void VideoCapture::DeinitVi() {
        SPDLOG_INFO("Deinitializing VI...");

        RK_MPI_VI_DisableChn(config_.pipe_id, config_.chn_id);
        RK_MPI_VI_DisableDev(config_.cam_id);

        vi_initialized_ = false;
        SPDLOG_INFO("VI deinitialized");
    }

    void VideoCapture::DeinitIsp() {
        SPDLOG_INFO("Deinitializing ISP...");

        SAMPLE_COMM_ISP_Stop(config_.cam_id);

        isp_initialized_ = false;
        SPDLOG_INFO("ISP deinitialized");
    }

    void VideoCapture::CaptureThread() {
        SPDLOG_DEBUG("CaptureThread started");

        while (running_.load()) {
            if (auto frame = GetFrame(100)) {
                if (frame->IsValid()) {
                    // 调用 YUV 帧回调（值语义，移动传递）
                    if (yuv_callback_) {
                        yuv_callback_(std::move(*frame));
                    }
                }
            }
        }

        SPDLOG_DEBUG("CaptureThread exited");
    }

} // namespace rmg
