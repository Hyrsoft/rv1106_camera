/**
 * @file LuckfoxCamera.hpp
 * @brief Luckfox Pico RV1106 相机采集类 - RAII 封装
 *
 * 本文件为 Rockchip RKMPI 和 AIQ ISP 接口提供现代 C++ 封装，
 * 遵循 RAII 原则，确保资源的正确初始化和释放。
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>

// Rockchip MPI headers
#include "rk_comm_mb.h"
#include "rk_comm_video.h"
#include "rk_comm_vi.h"
#include "rk_common.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_vi.h"

// ISP headers
#include "sample_comm_isp.h"

/**
 * @brief YUV 帧包装器，自动管理 MPI 帧资源
 *
 * 使用 RAII 确保帧资源在对象析构时正确释放。
 * 禁止拷贝，允许移动语义。
 */
class YuvFrame {
 public:
  /**
   * @brief 构造函数
   * @param pipe_id VI Pipe ID
   * @param chn_id VI 通道 ID
   * @param frame_info 帧信息结构体
   */
  YuvFrame(uint32_t pipe_id, uint32_t chn_id,
           const VIDEO_FRAME_INFO_S& frame_info);

  /**
   * @brief 析构函数 - 自动释放帧资源
   */
  ~YuvFrame();

  // 禁用拷贝
  YuvFrame(const YuvFrame&) = delete;
  YuvFrame& operator=(const YuvFrame&) = delete;

  // 允许移动
  YuvFrame(YuvFrame&& other) noexcept;
  YuvFrame& operator=(YuvFrame&& other) noexcept;

  /**
   * @brief 获取帧的虚拟地址（CPU 可访问）
   * @return 虚拟地址指针，失败返回 nullptr
   */
  void* GetVirAddr() const;

  /**
   * @brief 获取帧的物理地址
   * @return 物理地址
   */
  uint64_t GetPhyAddr() const;

  /**
   * @brief 获取帧数据大小
   * @return 数据大小（字节）
   */
  size_t GetDataSize() const;

  /**
   * @brief 获取帧宽度
   */
  uint32_t GetWidth() const { return frame_info_.stVFrame.u32Width; }

  /**
   * @brief 获取帧高度
   */
  uint32_t GetHeight() const { return frame_info_.stVFrame.u32Height; }

  /**
   * @brief 获取虚拟宽度（对齐后的宽度）
   */
  uint32_t GetVirWidth() const { return frame_info_.stVFrame.u32VirWidth; }

  /**
   * @brief 获取虚拟高度（对齐后的高度）
   */
  uint32_t GetVirHeight() const { return frame_info_.stVFrame.u32VirHeight; }

  /**
   * @brief 获取像素格式
   */
  PIXEL_FORMAT_E GetPixelFormat() const {
    return frame_info_.stVFrame.enPixelFormat;
  }

  /**
   * @brief 获取时间戳
   */
  uint64_t GetPts() const { return frame_info_.stVFrame.u64PTS; }

  /**
   * @brief 获取原始帧信息结构体引用
   */
  const VIDEO_FRAME_INFO_S& GetFrameInfo() const { return frame_info_; }

  /**
   * @brief 检查帧是否有效
   */
  bool IsValid() const { return is_valid_; }

 private:
  VIDEO_FRAME_INFO_S frame_info_{};
  uint32_t pipe_id_ = 0;
  uint32_t chn_id_ = 0;
  bool is_valid_ = false;
};

/**
 * @brief Luckfox Pico 相机采集类
 *
 * 封装 RV1106 的 ISP 和 VI 模块，提供简洁的相机初始化和帧采集接口。
 *在析构时自动释放所有硬件资源。
 */
class Camera {
 public:
  /**
   * @brief 相机配置结构体，默认配置，可在调用构造函数时修改
   */
  struct Config {
    int cam_id = 0;                         ///< 相机 ID
    uint32_t width = 1920;                  ///< 输出图像宽度
    uint32_t height = 1080;                 ///< 输出图像高度
    std::string iq_path = "/etc/iqfiles";   ///< IQ 文件路径
    std::string dev_name = "/dev/video11";  ///< V4L2 设备名称
    PIXEL_FORMAT_E pixel_format = RK_FMT_YUV420SP;  ///< 像素格式 (NV12)
    uint32_t buf_count = 3;                 ///< VI 缓冲区数量
    uint32_t depth = 2;                     ///< 用户获取帧深度
    rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;  ///< HDR 模式
    bool multi_cam = false;                 ///< 是否多相机模式
  };

  /**
   * @brief 构造函数
   * @param config 相机配置
   */
  explicit Camera(const Config& config);

  /**
   * @brief 析构函数 - 自动释放所有资源
   */
  ~Camera();

  // 禁用拷贝（硬件资源不可共享）
  Camera(const Camera&) = delete;
  Camera& operator=(const Camera&) = delete;

  /**
   * @brief 初始化相机
   *
   * 按顺序初始化 MPI 系统、ISP 和 VI 通道
   *
   * @return true 初始化成功
   * @return false 初始化失败
   */
  bool Initialize();

  /**
   * @brief 采集一帧原始 YUV 图像
   * @param timeout_ms 超时时间（毫秒），-1 表示阻塞等待
   * @return 成功返回 YuvFrame 智能指针，失败返回 nullptr
   */
  std::unique_ptr<YuvFrame> GetRawFrame(int timeout_ms = 1000);

  /**
   * @brief 获取当前帧率
   * @return 当前帧率 (fps)
   */
  uint32_t GetCurrentFps() const;

  /**
   * @brief 设置帧率
   * @param fps 目标帧率
   * @return true 设置成功
   */
  bool SetFrameRate(uint32_t fps);

  /**
   * @brief 设置镜像翻转
   * @param mirror 水平镜像
   * @param flip 垂直翻转
   * @return true 设置成功
   */
  bool SetMirrorFlip(bool mirror, bool flip);

  /**
   * @brief 检查是否已初始化
   */
  bool IsInitialized() const { return is_initialized_; }

  /**
   * @brief 获取配置
   */
  const Config& GetConfig() const { return config_; }

 private:
  /**
   * @brief 初始化 MPI 系统
   */
  bool InitSystem();

  /**
   * @brief 初始化 ISP
   */
  bool InitIsp();

  /**
   * @brief 初始化 VI 设备和通道
   */
  bool InitVi();

  /**
   * @brief 反初始化 VI
   */
  void DeinitVi();

  /**
   * @brief 反初始化 ISP
   */
  void DeinitIsp();

  /**
   * @brief 反初始化 MPI 系统
   */
  void DeinitSystem();

  Config config_;
  bool is_initialized_ = false;
  bool sys_initialized_ = false;
  bool isp_initialized_ = false;
  bool vi_initialized_ = false;

  // VI 参数
  const VI_PIPE vi_pipe_id_ = 0;
  const VI_CHN vi_chn_id_ = 0;
};



