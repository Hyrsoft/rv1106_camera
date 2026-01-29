/**
 * @file main.cpp
 * @brief RV1106 JPEG 图像捕获示例程序
 *
 * 演示如何使用 VideoCapture 采集 YUV 帧，通过 VideoEncoder 编码为 JPEG，
 * 并保存到本地文件。支持单帧拍照和连续拍照模式。
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <thread>

#include "rmg.hpp"

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

// 全局运行标志
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_take_photo{false};

/**
 * @brief 信号处理函数
 */
static void SignalHandler(int sig) {
    (void) sig;
    SPDLOG_INFO("Received signal {}, stopping...", sig);
    g_running.store(false);
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
    return ".";
}

/**
 * @brief 生成带时间戳的文件名
 */
static std::string GenerateFilename(const std::string &output_dir, uint32_t width, uint32_t height) {
    time_t t = time(nullptr);
    struct tm tm = *localtime(&t);
    char filename[256];
    snprintf(filename, sizeof(filename), "%s/%d%02d%02d_%02d%02d%02d_%ux%u.jpg",
             output_dir.c_str(),
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             width, height);
    return std::string(filename);
}

/**
 * @brief 保存 JPEG 数据到文件
 */
static bool SaveJpegToFile(const rmg::EncodedFrame &frame, const std::string &filepath) {
    void *data = frame.GetVirAddr();
    if (data == nullptr) {
        SPDLOG_ERROR("Failed to get virtual address for encoded frame");
        return false;
    }

    size_t size = frame.GetDataSize();
    if (size == 0) {
        SPDLOG_ERROR("Encoded frame data size is 0");
        return false;
    }

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        SPDLOG_ERROR("Failed to open file for writing: {}", filepath);
        return false;
    }

    file.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    file.close();

    SPDLOG_INFO("Saved JPEG to {} ({} bytes)", filepath, size);
    return true;
}

/**
 * @brief 打印使用帮助
 */
static void PrintUsage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -w <width>     Set capture width (default: 1920)\n");
    printf("  -h <height>    Set capture height (default: 1080)\n");
    printf("  -n <count>     Number of JPEG images to capture (default: 1)\n");
    printf("  -i <interval>  Interval between captures in seconds (default: 2)\n");
    printf("  -q <quality>   JPEG quality 1-99 (default: 80)\n");
    printf("  -k <skip>      Number of warmup frames to skip (default: 30)\n");
    printf("  -d <delay>     Delay in seconds after init for AE (default: 1)\n");
    printf("  -o <path>      Output directory for JPEG files (default: executable dir)\n");
    printf("  -c             Continuous mode: capture until Ctrl+C\n");
    printf("  -v             Verbose mode (debug level logging)\n");
    printf("  --help         Show this help message\n");
    printf("\n");
    printf("Example:\n");
    printf("  %s -w 1920 -h 1080 -n 5 -i 2 -q 85\n", prog_name);
    printf("  %s -c -i 5  # Continuous capture every 5 seconds\n", prog_name);
}

int main(int argc, char *argv[]) {
    // 设置信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    signal(SIGPIPE, SignalHandler);

    // 默认日志级别
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    // 解析命令行参数
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t capture_count = 1;
    uint32_t interval_sec = 2;
    uint32_t jpeg_quality = 80;
    uint32_t skip_frames = 30;
    uint32_t init_delay_sec = 1;
    bool continuous_mode = false;
    std::string output_dir;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            width = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            height = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            capture_count = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            interval_sec = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) {
            jpeg_quality = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            skip_frames = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            init_delay_sec = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0) {
            continuous_mode = true;
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

    // 确保输出目录存在
    if (!fs::exists(output_dir)) {
        fs::create_directories(output_dir);
    }

    SPDLOG_INFO("=== RV1106 JPEG Capture Demo ===");
    SPDLOG_INFO("Configuration: {}x{}, quality: {}", width, height, jpeg_quality);
    SPDLOG_INFO("Output directory: {}", output_dir);
    if (continuous_mode) {
        SPDLOG_INFO("Mode: Continuous capture every {} second(s)", interval_sec);
    } else {
        SPDLOG_INFO("Mode: Capture {} image(s) with {} second interval", capture_count, interval_sec);
    }

    // ========================================
    // 配置视频采集 (VI)
    // ========================================
    rmg::VideoCapture::Config vi_config;
    vi_config.width = width;
    vi_config.height = height;
    vi_config.iq_path = "/etc/iqfiles";
    vi_config.dev_name = "/dev/video11";
    vi_config.pixel_format = RK_FMT_YUV420SP;
    vi_config.buf_count = 3;
    vi_config.depth = 2;

    rmg::VideoCapture capture(vi_config);

    if (!capture.Initialize()) {
        SPDLOG_ERROR("Failed to initialize VideoCapture!");
        return -1;
    }
    SPDLOG_INFO("VideoCapture initialized");

    // ========================================
    // 配置 JPEG 编码器 (VENC)
    // ========================================
    rmg::VideoEncoder::Config venc_config;
    venc_config.chn_id = 0;
    venc_config.width = width;
    venc_config.height = height;
    venc_config.vir_width = width;
    venc_config.vir_height = height;
    venc_config.pixel_format = RK_FMT_YUV420SP;
    venc_config.codec = rmg::CodecType::kJPEG;
    venc_config.jpeg_quality = jpeg_quality;
    venc_config.buf_count = 2;

    rmg::VideoEncoder encoder(venc_config);

    if (!encoder.Initialize()) {
        SPDLOG_ERROR("Failed to initialize VideoEncoder!");
        return -1;
    }
    SPDLOG_INFO("JPEG Encoder initialized");

    // ========================================
    // 设置编码回调 - 保存 JPEG 文件
    // ========================================
    std::atomic<uint32_t> saved_count{0};

    encoder.SetEncodedDataCallback([&](rmg::EncodedFrame frame) {
        if (!frame.IsValid()) {
            SPDLOG_WARN("Received invalid encoded frame");
            return;
        }

        std::string filepath = GenerateFilename(output_dir, width, height);
        if (SaveJpegToFile(frame, filepath)) {
            saved_count++;
        }
    });

    // 启动编码器（获取流线程）
    if (!encoder.Start()) {
        SPDLOG_ERROR("Failed to start VideoEncoder!");
        return -1;
    }

    // ========================================
    // 等待 AE 收敛
    // ========================================
    if (init_delay_sec > 0) {
        SPDLOG_INFO("Waiting {} second(s) for AE to stabilize...", init_delay_sec);
        std::this_thread::sleep_for(std::chrono::seconds(init_delay_sec));
    }

    // 跳过热身帧
    if (skip_frames > 0) {
        SPDLOG_INFO("Skipping {} warmup frames...", skip_frames);
        for (uint32_t i = 0; i < skip_frames && g_running.load(); ++i) {
            auto frame = capture.GetFrame(1000);
            if (frame && frame->IsValid()) {
                SPDLOG_DEBUG("Warmup frame #{} skipped", i + 1);
            }
        }
        SPDLOG_INFO("Warmup complete");
    }

    // ========================================
    // 主循环 - 拍照
    // ========================================
    SPDLOG_INFO("Starting JPEG capture...");
    SPDLOG_INFO("Press Ctrl+C to stop");

    uint32_t capture_attempt = 0;
    auto last_capture_time = std::chrono::steady_clock::now() - std::chrono::seconds(interval_sec);

    while (g_running.load()) {
        // 检查是否该拍照了
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_capture_time).count();

        if (elapsed >= static_cast<int64_t>(interval_sec)) {
            // 获取一帧 YUV
            auto yuv_frame = capture.GetFrame(1000);
            if (!yuv_frame || !yuv_frame->IsValid()) {
                SPDLOG_WARN("Failed to get YUV frame");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // 启动 JPEG 编码器接收一帧
            if (!encoder.StartRecvFrame(1)) {
                SPDLOG_ERROR("Failed to start receiving frame");
                continue;
            }

            // 将 YUV 帧发送到编码器
            if (!encoder.PushYuvFrame(*yuv_frame)) {
                SPDLOG_ERROR("Failed to push YUV frame to encoder");
                continue;
            }

            capture_attempt++;
            last_capture_time = now;
            SPDLOG_INFO("Capture #{} triggered", capture_attempt);

            // 等待编码完成（回调会保存文件）
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            // 非连续模式下检查是否已达到目标数量
            if (!continuous_mode && capture_attempt >= capture_count) {
                SPDLOG_INFO("Target capture count reached");
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // ========================================
    // 清理
    // ========================================
    SPDLOG_INFO("Stopping...");
    encoder.Stop();

    SPDLOG_INFO("=== Capture Summary ===");
    SPDLOG_INFO("Capture attempts: {}", capture_attempt);
    SPDLOG_INFO("JPEG files saved: {}", saved_count.load());
    SPDLOG_INFO("Output directory: {}", output_dir);

    return 0;
}
