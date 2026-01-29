/**
 * @file main.cpp
 * @brief RV1106 RTSP 视频流示例程序
 *
 * 演示如何使用 VideoCapture 采集 YUV 帧，通过 VideoEncoder 编码为 H.264，
 * 并使用 RtspServer 推送 RTSP 流。
 *
 * 关键特性：
 * - VI -> VENC 硬件绑定（零拷贝）
 * - VENC -> RTSP 软件回调（数据传递）
 *
 * @author 好软，好温暖
 * @date 2026-01-30
 */

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "rmg.hpp"

#include <spdlog/spdlog.h>

// 全局运行标志
static std::atomic<bool> g_running{true};

// 统计信息
static std::atomic<uint64_t> g_frame_count{0};
static std::atomic<uint64_t> g_byte_count{0};

/**
 * @brief 信号处理函数
 */
static void SignalHandler(int sig) {
    (void) sig;
    SPDLOG_INFO("Received signal {}, stopping...", sig);
    g_running.store(false);
}

/**
 * @brief 打印使用帮助
 */
static void PrintUsage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -w <width>      Image width (default: 1920)\n");
    printf("  -h <height>     Image height (default: 1080)\n");
    printf("  -f <fps>        Frame rate (default: 30)\n");
    printf("  -b <bitrate>    Bitrate in kbps (default: 4000)\n");
    printf("  -g <gop>        GOP size (default: 60)\n");
    printf("  -p <port>       RTSP port (default: 554)\n");
    printf("  -s <path>       RTSP stream path (default: /live/0)\n");
    printf("  -c <codec>      Codec: h264, h265 (default: h264)\n");
    printf("  -v              Verbose mode\n");
    printf("  -?              Show this help\n");
    printf("\nExample:\n");
    printf("  %s -w 1920 -h 1080 -f 30 -b 4000\n", prog_name);
    printf("\nRTSP URL:\n");
    printf("  rtsp://<device_ip>:554/live/0\n");
}

/**
 * @brief 获取设备 IP 地址（简单实现）
 */
static std::string GetDeviceIp() {
    // 尝试从 eth0 或 wlan0 获取 IP
    FILE *fp = popen("ip -4 addr show scope global | grep inet | head -1 | awk '{print $2}' | cut -d'/' -f1", "r");
    if (fp) {
        char buf[64] = {0};
        if (fgets(buf, sizeof(buf), fp)) {
            // 移除换行符
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') {
                buf[len - 1] = '\0';
            }
            pclose(fp);
            if (strlen(buf) > 0) {
                return std::string(buf);
            }
        }
        pclose(fp);
    }
    return "<device_ip>";
}

int main(int argc, char *argv[]) {
    // 默认配置
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 30;
    uint32_t bitrate = 4000;
    uint32_t gop = 60;
    uint16_t rtsp_port = 554;
    std::string rtsp_path = "/live/0";
    rmg::CodecType codec = rmg::CodecType::kH264;
    bool verbose = false;

    // 解析命令行参数
    int opt;
    while ((opt = getopt(argc, argv, "w:h:f:b:g:p:s:c:v?")) != -1) {
        switch (opt) {
            case 'w':
                width = static_cast<uint32_t>(atoi(optarg));
                break;
            case 'h':
                height = static_cast<uint32_t>(atoi(optarg));
                break;
            case 'f':
                fps = static_cast<uint32_t>(atoi(optarg));
                break;
            case 'b':
                bitrate = static_cast<uint32_t>(atoi(optarg));
                break;
            case 'g':
                gop = static_cast<uint32_t>(atoi(optarg));
                break;
            case 'p':
                rtsp_port = static_cast<uint16_t>(atoi(optarg));
                break;
            case 's':
                rtsp_path = optarg;
                break;
            case 'c':
                if (strcmp(optarg, "h265") == 0 || strcmp(optarg, "H265") == 0 ||
                    strcmp(optarg, "hevc") == 0 || strcmp(optarg, "HEVC") == 0) {
                    codec = rmg::CodecType::kH265;
                } else {
                    codec = rmg::CodecType::kH264;
                }
                break;
            case 'v':
                verbose = true;
                break;
            case '?':
            default:
                PrintUsage(argv[0]);
                return 0;
        }
    }

    // 设置日志级别
    if (verbose) {
        spdlog::set_level(spdlog::level::debug);
    } else {
        spdlog::set_level(spdlog::level::info);
    }

    // 注册信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    SPDLOG_INFO("=== RV1106 RTSP Streaming Example ===");
    SPDLOG_INFO("Configuration:");
    SPDLOG_INFO("  Resolution: {}x{}", width, height);
    SPDLOG_INFO("  FPS: {}", fps);
    SPDLOG_INFO("  Bitrate: {} kbps", bitrate);
    SPDLOG_INFO("  GOP: {}", gop);
    SPDLOG_INFO("  Codec: {}", codec == rmg::CodecType::kH265 ? "H.265" : "H.264");
    SPDLOG_INFO("  RTSP Port: {}", rtsp_port);
    SPDLOG_INFO("  RTSP Path: {}", rtsp_path);

    // 计算对齐尺寸 (16 字节对齐)
    uint32_t vir_width = (width + 15) & ~15;
    uint32_t vir_height = (height + 15) & ~15;

    // ========================================
    // 初始化系统
    // ========================================
    rmg::SystemManager &sys = rmg::SystemManager::GetInstance();
    if (!sys.Initialize()) {
        SPDLOG_ERROR("Failed to initialize system");
        return -1;
    }

    // ========================================
    // 创建视频采集模块 (VI)
    // ========================================
    rmg::VideoCapture::Config vi_config{};
    vi_config.cam_id = 0;
    vi_config.chn_id = 0;
    vi_config.pipe_id = 0;
    vi_config.width = width;
    vi_config.height = height;
    vi_config.pixel_format = RK_FMT_YUV420SP;  // NV12
    vi_config.buf_count = 4;

    auto vi = std::make_unique<rmg::VideoCapture>(vi_config);
    if (!vi->Initialize()) {
        SPDLOG_ERROR("Failed to initialize VideoCapture");
        sys.Deinitialize();
        return -1;
    }

    // ========================================
    // 创建视频编码器模块 (VENC)
    // ========================================
    rmg::VideoEncoder::Config enc_config{};
    enc_config.chn_id = 0;
    enc_config.width = width;
    enc_config.height = height;
    enc_config.vir_width = vir_width;
    enc_config.vir_height = vir_height;
    enc_config.pixel_format = RK_FMT_YUV420SP;
    enc_config.codec = codec;
    enc_config.fps = fps;
    enc_config.gop = gop;
    enc_config.bitrate = bitrate;
    enc_config.rc_mode = rmg::RateControlMode::kCBR;
    enc_config.profile = 100;  // High profile for H.264
    enc_config.buf_count = 4;

    auto encoder = std::make_unique<rmg::VideoEncoder>(enc_config);
    if (!encoder->Initialize()) {
        SPDLOG_ERROR("Failed to initialize VideoEncoder");
        sys.Deinitialize();
        return -1;
    }

    // ========================================
    // 创建 RTSP 服务器模块
    // ========================================
    rmg::RtspServer::Config rtsp_config{};
    rtsp_config.port = rtsp_port;
    rtsp_config.path = rtsp_path;
    rtsp_config.codec = (codec == rmg::CodecType::kH265) ? rmg::RtspCodecId::kH265 : rmg::RtspCodecId::kH264;

    auto rtsp = std::make_unique<rmg::RtspServer>(rtsp_config);
    if (!rtsp->Initialize()) {
        SPDLOG_ERROR("Failed to initialize RtspServer");
        sys.Deinitialize();
        return -1;
    }

    // ========================================
    // 设置编码数据回调 -> 推送到 RTSP
    // ========================================
    encoder->SetEncodedDataCallback([&rtsp](rmg::EncodedFrame frame) {
        // 推送到 RTSP 服务器
        if (rtsp->PushFrame(frame)) {
            g_frame_count.fetch_add(1, std::memory_order_relaxed);
            g_byte_count.fetch_add(frame.GetDataSize(), std::memory_order_relaxed);
        }
    });

    // ========================================
    // 建立 VI -> VENC 硬件绑定
    // ========================================
    rmg::ModuleEndpoint vi_endpoint = vi->GetEndpoint();
    rmg::ModuleEndpoint enc_endpoint = encoder->GetEndpoint();

    rmg::Pipeline pipeline;
    if (!pipeline.BindHardware(vi_endpoint, enc_endpoint)) {
        SPDLOG_ERROR("Failed to bind VI -> VENC");
        sys.Deinitialize();
        return -1;
    }
    SPDLOG_INFO("VI -> VENC hardware binding established");

    // ========================================
    // 启动所有模块
    // ========================================
    if (!rtsp->Start()) {
        SPDLOG_ERROR("Failed to start RtspServer");
        pipeline.UnbindAll();
        sys.Deinitialize();
        return -1;
    }

    if (!encoder->Start()) {
        SPDLOG_ERROR("Failed to start VideoEncoder");
        rtsp->Stop();
        pipeline.UnbindAll();
        sys.Deinitialize();
        return -1;
    }

    // 注意：在硬件绑定模式下，不需要调用 vi->Start()
    // 因为 VI 的数据会自动流向 VENC，无需通过 CaptureThread 拉取帧
    // 如果调用 vi->Start()，CaptureThread 会调用 GetFrame() 把帧"抢走"，
    // 导致 VENC 收不到数据

    // 打印 RTSP URL
    std::string device_ip = GetDeviceIp();
    SPDLOG_INFO("===========================================");
    SPDLOG_INFO("RTSP streaming started!");
    SPDLOG_INFO("Stream URL: rtsp://{}:{}{}", device_ip, rtsp_port, rtsp_path);
    SPDLOG_INFO("Use VLC or ffplay to view the stream:");
    SPDLOG_INFO("  ffplay -rtsp_transport tcp rtsp://{}:{}{}", device_ip, rtsp_port, rtsp_path);
    SPDLOG_INFO("Press Ctrl+C to stop...");
    SPDLOG_INFO("===========================================");

    // ========================================
    // 主循环 - 定期打印统计信息
    // ========================================
    auto start_time = std::chrono::steady_clock::now();
    uint64_t last_frame_count = 0;
    uint64_t last_byte_count = 0;

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        if (!g_running.load()) break;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

        uint64_t current_frames = g_frame_count.load();
        uint64_t current_bytes = g_byte_count.load();

        uint64_t delta_frames = current_frames - last_frame_count;
        uint64_t delta_bytes = current_bytes - last_byte_count;

        double avg_fps = (elapsed > 0) ? static_cast<double>(current_frames) / elapsed : 0;
        double current_fps = static_cast<double>(delta_frames) / 5.0;
        double bitrate_kbps = static_cast<double>(delta_bytes * 8) / (5.0 * 1000.0);

        SPDLOG_INFO("Stats: frames={}, avg_fps={:.1f}, current_fps={:.1f}, bitrate={:.0f}kbps",
                    current_frames, avg_fps, current_fps, bitrate_kbps);

        last_frame_count = current_frames;
        last_byte_count = current_bytes;
    }

    // ========================================
    // 停止并清理
    // ========================================
    SPDLOG_INFO("Stopping...");

    // 注意：在硬件绑定模式下，不需要调用 vi->Stop()
    encoder->Stop();
    rtsp->Stop();

    // 解绑
    pipeline.UnbindAll();

    // 关闭系统
    sys.Deinitialize();

    SPDLOG_INFO("Total frames streamed: {}", g_frame_count.load());
    SPDLOG_INFO("Total bytes sent: {} MB", g_byte_count.load() / (1024 * 1024));
    SPDLOG_INFO("Done.");

    return 0;
}
