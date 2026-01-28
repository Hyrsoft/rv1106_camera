/**
 * @file Camera.cpp
 * @brief Luckfox Pico RV1106 相机采集类 - 实现文件
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#include "Camera.hpp"

#include <cstring>

#include <spdlog/spdlog.h>



// ============================================================================
// YuvFrame 实现
// ============================================================================

YuvFrame::YuvFrame(uint32_t pipe_id, uint32_t chn_id,
                   const VIDEO_FRAME_INFO_S& frame_info)
    : frame_info_(frame_info), pipe_id_(pipe_id), chn_id_(chn_id) {
  is_valid_ = (frame_info_.stVFrame.pMbBlk != nullptr);
}

YuvFrame::~YuvFrame() {
  if (is_valid_ && frame_info_.stVFrame.pMbBlk != nullptr) {
    RK_S32 ret = RK_MPI_VI_ReleaseChnFrame(pipe_id_, chn_id_, &frame_info_);
    if (ret != RK_SUCCESS) {
      SPDLOG_WARN("Failed to release VI frame: 0x{:08X}", ret);
    }
  }
}

YuvFrame::YuvFrame(YuvFrame&& other) noexcept
    : frame_info_(other.frame_info_),
      pipe_id_(other.pipe_id_),
      chn_id_(other.chn_id_),
      is_valid_(other.is_valid_) {
  // 转移所有权，将原对象置为无效
  other.is_valid_ = false;
  other.frame_info_.stVFrame.pMbBlk = nullptr;
}

YuvFrame& YuvFrame::operator=(YuvFrame&& other) noexcept {
  if (this != &other) {
    // 释放当前帧
    if (is_valid_ && frame_info_.stVFrame.pMbBlk != nullptr) {
      RK_MPI_VI_ReleaseChnFrame(pipe_id_, chn_id_, &frame_info_);
    }

    // 转移所有权
    frame_info_ = other.frame_info_;
    pipe_id_ = other.pipe_id_;
    chn_id_ = other.chn_id_;
    is_valid_ = other.is_valid_;

    other.is_valid_ = false;
    other.frame_info_.stVFrame.pMbBlk = nullptr;
  }
  return *this;
}

void* YuvFrame::GetVirAddr() const {
  if (!is_valid_ || frame_info_.stVFrame.pMbBlk == nullptr) {
    return nullptr;
  }

  // 优先使用 pVirAddr[0]，如果为空则通过 Handle 获取
  void* vir_addr = frame_info_.stVFrame.pVirAddr[0];
  if (vir_addr == nullptr) {
    vir_addr = RK_MPI_MB_Handle2VirAddr(frame_info_.stVFrame.pMbBlk);
  }
  return vir_addr;
}

uint64_t YuvFrame::GetPhyAddr() const {
  if (!is_valid_ || frame_info_.stVFrame.pMbBlk == nullptr) {
    return 0;
  }
  return RK_MPI_MB_Handle2PhysAddr(frame_info_.stVFrame.pMbBlk);
}

size_t YuvFrame::GetDataSize() const {
  if (!is_valid_ || frame_info_.stVFrame.pMbBlk == nullptr) {
    return 0;
  }
  return static_cast<size_t>(RK_MPI_MB_GetSize(frame_info_.stVFrame.pMbBlk));
}

// ============================================================================
// Camera 实现
// ============================================================================

Camera::Camera(const Config& config) : config_(config) {}

Camera::~Camera() {
  // 按正确的反序释放资源
  if (vi_initialized_) {
    DeinitVi();
  }
  if (isp_initialized_) {
    DeinitIsp();
  }
  if (sys_initialized_) {
    DeinitSystem();
  }

  SPDLOG_INFO("Camera resources released");
}

bool Camera::Initialize() {
  if (is_initialized_) {
    SPDLOG_WARN("Camera already initialized");
    return true;
  }

  SPDLOG_INFO("Initializing camera ({}x{}, format: {})", config_.width,
           config_.height, static_cast<int>(config_.pixel_format));

  // 1. 初始化 MPI 系统
  if (!InitSystem()) {
    SPDLOG_ERROR("Failed to initialize MPI system");
    return false;
  }
  sys_initialized_ = true;

  // 2. 初始化 ISP
  if (!InitIsp()) {
    SPDLOG_ERROR("Failed to initialize ISP");
    return false;
  }
  isp_initialized_ = true;

  // 3. 初始化 VI
  if (!InitVi()) {
    SPDLOG_ERROR("Failed to initialize VI");
    return false;
  }
  vi_initialized_ = true;

  is_initialized_ = true;
  SPDLOG_INFO("Camera initialized successfully");
  return true;
}

bool Camera::InitSystem() {
  SPDLOG_DEBUG("Initializing MPI system...");

  RK_S32 ret = RK_MPI_SYS_Init();
  if (ret != RK_SUCCESS) {
    SPDLOG_ERROR("RK_MPI_SYS_Init failed: 0x{:08X}", ret);
    return false;
  }

  SPDLOG_DEBUG("MPI system initialized");
  return true;
}

bool Camera::InitIsp() {
  SPDLOG_INFO("Initializing ISP (cam_id: {}, iq_path: {})...", config_.cam_id,
            config_.iq_path);

  RK_S32 ret = SAMPLE_COMM_ISP_Init(config_.cam_id, config_.hdr_mode,
                                    config_.multi_cam ? RK_TRUE : RK_FALSE,
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

bool Camera::InitVi() {
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
  bind_pipe.PipeId[0] = vi_pipe_id_;

  ret = RK_MPI_VI_SetDevBindPipe(config_.cam_id, &bind_pipe);
  if (ret != RK_SUCCESS) {
    SPDLOG_ERROR("RK_MPI_VI_SetDevBindPipe failed: 0x{:08X}", ret);
    RK_MPI_VI_DisableDev(config_.cam_id);
    return false;
  }

  // 3. 配置 VI 通道属性
  VI_CHN_ATTR_S chn_attr;
  std::memset(&chn_attr, 0, sizeof(chn_attr));

  // 设置输出尺寸
  chn_attr.stSize.u32Width = config_.width;
  chn_attr.stSize.u32Height = config_.height;

  // 设置像素格式
  chn_attr.enPixelFormat = config_.pixel_format;

  // 设置帧深度，用于 GetChnFrame
  chn_attr.u32Depth = config_.depth;

  // ISP 相关配置
  chn_attr.stIspOpt.u32BufCount = config_.buf_count;
  chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
  chn_attr.stIspOpt.bNoUseLibV4L2 = RK_TRUE;
  chn_attr.stIspOpt.stMaxSize.u32Width = config_.width;
  chn_attr.stIspOpt.stMaxSize.u32Height = config_.height;

  // 设置设备实体名称
  std::strncpy(reinterpret_cast<char*>(chn_attr.stIspOpt.aEntityName),
               config_.dev_name.c_str(), MAX_VI_ENTITY_NAME_LEN - 1);

  ret = RK_MPI_VI_SetChnAttr(vi_pipe_id_, vi_chn_id_, &chn_attr);
  if (ret != RK_SUCCESS) {
    SPDLOG_ERROR("RK_MPI_VI_SetChnAttr failed: 0x{:08X}", ret);
    RK_MPI_VI_DisableDev(config_.cam_id);
    return false;
  }

  ret = RK_MPI_VI_EnableChn(vi_pipe_id_, vi_chn_id_);
  if (ret != RK_SUCCESS) {
    SPDLOG_ERROR("RK_MPI_VI_EnableChn failed: 0x{:08X}", ret);
    RK_MPI_VI_DisableDev(config_.cam_id);
    return false;
  }

  SPDLOG_INFO("VI initialized (pipe: {}, chn: {})", vi_pipe_id_, vi_chn_id_);
  return true;
}

void Camera::DeinitVi() {
  SPDLOG_INFO("Deinitializing VI...");

  RK_MPI_VI_DisableChn(vi_pipe_id_, vi_chn_id_);
  RK_MPI_VI_DisableDev(config_.cam_id);

  vi_initialized_ = false;
  SPDLOG_INFO("VI deinitialized");
}

void Camera::DeinitIsp() {
  SPDLOG_INFO("Deinitializing ISP...");

  SAMPLE_COMM_ISP_Stop(config_.cam_id);

  isp_initialized_ = false;
  SPDLOG_INFO("ISP deinitialized");
}

void Camera::DeinitSystem() {
  SPDLOG_INFO("Deinitializing MPI system...");
  RK_MPI_SYS_Exit();

  sys_initialized_ = false;
  SPDLOG_DEBUG("MPI system deinitialized");
}

std::unique_ptr<YuvFrame> Camera::GetRawFrame(int timeout_ms) {
  if (!is_initialized_) {
    SPDLOG_ERROR("Camera not initialized");
    return nullptr;
  }

  VIDEO_FRAME_INFO_S frame_info;
  std::memset(&frame_info, 0, sizeof(frame_info));

  RK_S32 ret =
      RK_MPI_VI_GetChnFrame(vi_pipe_id_, vi_chn_id_, &frame_info, timeout_ms);

  if (ret != RK_SUCCESS) {
    if (ret != RK_ERR_VI_BUF_EMPTY) {
      SPDLOG_WARN("RK_MPI_VI_GetChnFrame failed: 0x{:08X}", ret);
    }
    return nullptr;
  }

  return std::make_unique<YuvFrame>(vi_pipe_id_, vi_chn_id_, frame_info);
}

uint32_t Camera::GetCurrentFps() const {
  if (!is_initialized_) {
    return 0;
  }

  VI_CHN_STATUS_S chn_status;
  std::memset(&chn_status, 0, sizeof(chn_status));

  RK_S32 ret = RK_MPI_VI_QueryChnStatus(vi_pipe_id_, vi_chn_id_, &chn_status);
  if (ret != RK_SUCCESS) {
    return 0;
  }

  return chn_status.u32FrameRate;
}

bool Camera::SetFrameRate(uint32_t fps) {
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

bool Camera::SetMirrorFlip(bool mirror, bool flip) {
  if (!isp_initialized_) {
    SPDLOG_ERROR("ISP not initialized");
    return false;
  }

  RK_S32 ret = SAMPLE_COMM_ISP_SetMirrorFlip(config_.cam_id, mirror ? 1 : 0,
                                              flip ? 1 : 0);
  if (ret != RK_SUCCESS) {
    SPDLOG_ERROR("Failed to set mirror/flip: {}", ret);
    return false;
  }

  SPDLOG_INFO("Mirror: {}, Flip: {}", mirror, flip);
  return true;
}

