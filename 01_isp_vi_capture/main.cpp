/**
 * @file main.cpp
 * @brief Luckfox Pico 相机采集示例程序
 *
 * 演示如何使用 Camera 类进行相机初始化和 YUV 帧采集。
 * 默认会将采集到的 YUV 帧保存到可执行文件所在目录。
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#include <unistd.h>

#include <cinttypes>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#include "Camera.hpp"

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

// 全局运行标志
static volatile bool g_running = true;

/**
 * @brief 信号处理函数
 */
static void SignalHandler(int sig) {
  (void)sig;
  SPDLOG_INFO("Received signal {}, stopping...", sig);
  g_running = false;
}

/**
 * @brief 获取可执行文件所在目录
 */
static std::string GetExecutableDir() {
  char path[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (len != -1) {
    path[len] = '\0';
    return fs::path(path).parent_path().string();
  }
  // 如果获取失败，返回当前目录
  return ".";
}

/**
 * @brief 将 YUV 帧保存到文件
 */
static bool SaveFrameToFile(const YuvFrame& frame,
                            const std::string& filepath) {
  void* data = frame.GetVirAddr();
  if (data == nullptr) {
    SPDLOG_ERROR("Failed to get virtual address for frame");
    return false;
  }

  size_t size = frame.GetDataSize();
  if (size == 0) {
    // 如果 GetDataSize 返回 0，手动计算 NV12 大小
    // NV12: Y 平面 = width * height, UV 平面 = width * height / 2
    size = frame.GetVirWidth() * frame.GetVirHeight() * 3 / 2;
    SPDLOG_WARN("GetDataSize returned 0, using calculated size: {} bytes", size);
  }

  std::ofstream file(filepath, std::ios::binary);
  if (!file.is_open()) {
    SPDLOG_ERROR("Failed to open file for writing: {}", filepath);
    return false;
  }

  file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
  file.close();

  SPDLOG_INFO("Saved frame to {} ({} bytes)", filepath, size);
  return true;
}

/**
 * @brief 打印帧信息
 */
static void PrintFrameInfo(const YuvFrame& frame, uint32_t index) {
  SPDLOG_INFO("Frame #{}: {}x{} (vir: {}x{}), format: {}, pts: {}, size: {}",
              index, frame.GetWidth(), frame.GetHeight(), frame.GetVirWidth(),
              frame.GetVirHeight(), static_cast<int>(frame.GetPixelFormat()),
              frame.GetPts(), frame.GetDataSize());
}

/**
 * @brief 打印使用帮助
 */
static void PrintUsage(const char* prog_name) {
  printf("Usage: %s [options]\n", prog_name);
  printf("Options:\n");
  printf("  -w <width>    Set capture width (default: 1920)\n");
  printf("  -h <height>   Set capture height (default: 1080)\n");
  printf("  -n <count>    Number of frames to capture (default: 50)\n");
  printf("  -k <skip>     Number of warmup frames to skip for AE convergence (default: 30)\n");
  printf("  -d <delay>    Delay in seconds after init for AE to stabilize (default: 1)\n");
  printf("  -s            Save frame to YUV file (after warmup)\n");
  printf("  -o <path>     Output directory for YUV file (default: executable dir)\n");
  printf("  -v            Verbose mode (debug level logging)\n");
  printf("  --help        Show this help message\n");
  printf("\n");
  printf("Example:\n");
  printf("  %s -w 1920 -h 1080 -n 50 -k 30 -d 1 -s\n", prog_name);
  printf("\n");
  printf("Note: The first few frames after camera init are usually dark due to\n");
  printf("      Auto Exposure (AE) convergence. Use -k to skip warmup frames.\n");
  printf("\n");
  printf("View saved YUV file with ffplay:\n");
  printf("  ffplay -video_size 1920x1080 -pixel_format nv12 frame_1920x1080.yuv\n");
}

int main(int argc, char* argv[]) {
  // 设置信号处理
  signal(SIGINT, SignalHandler);
  signal(SIGTERM, SignalHandler);

  // 默认日志级别
  spdlog::set_level(spdlog::level::info);
  // 设置日志格式：[时间] [级别] 消息
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

  // 解析命令行参数
  uint32_t width = 1920;
  uint32_t height = 1080;
  uint32_t capture_count = 50;   // 默认采集50帧
  uint32_t skip_frames = 30;     // 默认跳过前30帧（AE收敛）
  uint32_t init_delay_sec = 1;   // 初始化后延时秒数
  bool save_frame = false;
  std::string output_dir;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
      width = static_cast<uint32_t>(atoi(argv[++i]));
    } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
      height = static_cast<uint32_t>(atoi(argv[++i]));
    } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
      capture_count = static_cast<uint32_t>(atoi(argv[++i]));
    } else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
      skip_frames = static_cast<uint32_t>(atoi(argv[++i]));
    } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
      init_delay_sec = static_cast<uint32_t>(atoi(argv[++i]));
    } else if (strcmp(argv[i], "-s") == 0) {
      save_frame = true;
    } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
      output_dir = argv[++i];
    } else if (strcmp(argv[i], "-v") == 0) {
      spdlog::set_level(spdlog::level::debug);
    } else if (strcmp(argv[i], "--help") == 0) {
      PrintUsage(argv[0]);
      return 0;
    }
  }

  // 如果未指定输出目录，使用可执行文件所在目录
  if (output_dir.empty()) {
    output_dir = GetExecutableDir();
  }

  SPDLOG_INFO("Luckfox Camera Capture Demo");
  SPDLOG_INFO("Configuration: {}x{}, capture {} frames, skip {} warmup frames", 
              width, height, capture_count, skip_frames);
  SPDLOG_INFO("Init delay: {} second(s), Output directory: {}", init_delay_sec, output_dir);

  // 配置相机
  Camera::Config config;
  config.width = width;
  config.height = height;
  config.iq_path = "/etc/iqfiles";
  config.dev_name = "/dev/video11";
  config.pixel_format = RK_FMT_YUV420SP;  // NV12
  config.buf_count = 3;
  config.depth = 2;

  // 创建相机实例
  Camera camera(config);

  // 初始化相机
  if (!camera.Initialize()) {
    SPDLOG_ERROR("Failed to initialize camera!");
    return -1;
  }

  SPDLOG_INFO("Camera initialized successfully!");

  // 等待 AE（自动曝光）收敛
  // ISP 启动后需要一定时间调整曝光参数，初始帧会比较暗
  if (init_delay_sec > 0) {
    SPDLOG_INFO("Waiting {} second(s) for AE (Auto Exposure) to stabilize...", init_delay_sec);
    std::this_thread::sleep_for(std::chrono::seconds(init_delay_sec));
  }

  // 跳过热身帧（AE 收敛阶段的暗帧）
  if (skip_frames > 0) {
    SPDLOG_INFO("Skipping {} warmup frames for AE convergence...", skip_frames);
    for (uint32_t i = 0; i < skip_frames && g_running; ++i) {
      auto frame = camera.GetRawFrame(1000);
      if (frame && frame->IsValid()) {
        SPDLOG_DEBUG("Warmup frame #{} skipped (AE converging)", i + 1);
      } else {
        SPDLOG_WARN("Failed to get warmup frame #{}", i + 1);
      }
    }
    SPDLOG_INFO("Warmup complete, AE should be converged now.");
  }

  SPDLOG_INFO("Starting real capture...");

  // 采集循环
  uint32_t frame_count = 0;
  uint32_t error_count = 0;
  uint32_t saved_count = 0;
  const uint32_t max_errors = 10;

  while (g_running && frame_count < capture_count) {
    // 获取帧
    auto frame = camera.GetRawFrame(1000);  // 1 秒超时

    if (frame && frame->IsValid()) {
      frame_count++;
      PrintFrameInfo(*frame, frame_count);

      // 保存第一帧到可执行文件所在目录
      if (save_frame && saved_count == 0) {
        // 构建文件名：frame_宽x高.yuv
        char filename[128];
        snprintf(filename, sizeof(filename), "frame_%ux%u.yuv",
                 frame->GetWidth(), frame->GetHeight());

        // 构建完整路径
        fs::path filepath = fs::path(output_dir) / filename;

        if (SaveFrameToFile(*frame, filepath.string())) {
          saved_count++;
          SPDLOG_INFO("YUV file saved! View with:");
          SPDLOG_INFO("  ffplay -video_size {}x{} -pixel_format nv12 {}",
                      frame->GetWidth(), frame->GetHeight(), filepath.string());
        }
      }

      error_count = 0;  // 重置错误计数
    } else {
      error_count++;
      SPDLOG_DEBUG("Failed to get frame, error count: {}", error_count);
      if (error_count >= max_errors) {
        SPDLOG_ERROR("Too many consecutive errors ({}), stopping...", error_count);
        break;
      }
    }

    // 短暂延时，避免 CPU 占用过高
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  SPDLOG_INFO("=== Capture Summary ===");
  SPDLOG_INFO("Warmup frames skipped: {}", skip_frames);
  SPDLOG_INFO("Total frames captured: {}", frame_count);
  SPDLOG_INFO("Frames saved to file: {}", saved_count);
  SPDLOG_INFO("Current FPS: {}", camera.GetCurrentFps());

  // Camera 析构时会自动释放资源
  SPDLOG_INFO("Exiting...");

  return 0;
}
