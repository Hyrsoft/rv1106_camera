/**
 * @file main.cpp
 * @brief RV1106 H.264 视频录制示例程序
 *
 * 演示如何使用 VI + VENC 硬件绑定录制 H.264 原始码流。
 * 
 * 架构：
 *   Camera (VI) ══════════> VideoEncoder (VENC) ────────────> FileSaver
 *                硬件绑定                      软件回调
 *                (零拷贝)                    (文件写入)
 *
 * @author 好软，好温暖
 * @date 2026-01-29
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
    printf("  -t <seconds>    Recording duration in seconds (default: 10)\n");
    printf("  -o <path>       Output directory (default: current dir)\n");
    printf("  -n <filename>   Output filename (default: auto-generated)\n");
    printf("  -c <codec>      Codec: h264, h265 (default: h264)\n");
    printf("  -v              Verbose mode\n");
    printf("  -?              Show this help\n");
    printf("\nExample:\n");
    printf("  %s -w 1920 -h 1080 -f 30 -t 30\n", prog_name);
    printf("  %s -c h265 -b 2000 -t 60\n", prog_name);
}

/**
 * @brief 获取可执行文件所在目录
 */
static std::string GetExecutableDir() {
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        std::string exe_path(path);
        size_t pos = exe_path.find_last_of('/');
        if (pos != std::string::npos) {
            return exe_path.substr(0, pos);
        }
    }
    return ".";
}

int main(int argc, char *argv[]) {
    // 默认配置
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 30;
    uint32_t bitrate = 4000;
    uint32_t gop = 60;
    uint32_t duration_sec = 10;
    std::string output_dir;
    std::string output_filename;
    rmg::CodecType codec = rmg::CodecType::kH264;
    bool verbose = false;

    // 解析命令行参数
    int opt;
    while ((opt = getopt(argc, argv, "w:h:f:b:g:t:o:n:c:v?")) != -1) {
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
            case 't':
                duration_sec = static_cast<uint32_t>(atoi(optarg));
                break;
            case 'o':
                output_dir = optarg;
                break;
            case 'n':
                output_filename = optarg;
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

    // 设置输出目录
    if (output_dir.empty()) {
        output_dir = GetExecutableDir();
    }

    // 设置日志级别
    if (verbose) {
        spdlog::set_level(spdlog::level::debug);
    } else {
        spdlog::set_level(spdlog::level::info);
    }
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    // 注册信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    SPDLOG_INFO("=== RV1106 H.264/HEVC Recording Example ===");
    SPDLOG_INFO("Configuration:");
    SPDLOG_INFO("  Resolution: {}x{}", width, height);
    SPDLOG_INFO("  FPS: {}", fps);
    SPDLOG_INFO("  Bitrate: {} kbps", bitrate);
    SPDLOG_INFO("  GOP: {}", gop);
    SPDLOG_INFO("  Codec: {}", codec == rmg::CodecType::kH265 ? "H.265/HEVC" : "H.264/AVC");
    SPDLOG_INFO("  Duration: {} seconds", duration_sec);
    SPDLOG_INFO("  Output dir: {}", output_dir);

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
    // [FIX] 硬件绑定模式下，必须将 depth 设为 0！
    // 否则 VI 会等待用户 GetFrame，导致管道阻塞。
    // depth > 0 时，VI 保留帧等待用户取走，如果不取，队列满后不再向 VENC 发送数据
    vi_config.depth = 0;

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
    // 创建文件保存模块 (FileSaver)
    // ========================================
    rmg::FileSaver::Config saver_config{};
    saver_config.output_dir = output_dir;
    saver_config.filename_prefix = output_filename;
    saver_config.format = (codec == rmg::CodecType::kH265) ? rmg::FileFormat::kHEVC : rmg::FileFormat::kH264;
    saver_config.width = width;
    saver_config.height = height;
    saver_config.append_timestamp = output_filename.empty();  // 如果没指定文件名，则添加时间戳
    saver_config.max_frames = duration_sec * fps;  // 根据时长和帧率计算最大帧数

    auto saver = std::make_unique<rmg::FileSaver>(saver_config);
    if (!saver->Initialize()) {
        SPDLOG_ERROR("Failed to initialize FileSaver");
        sys.Deinitialize();
        return -1;
    }

    // ========================================
    // 设置编码数据回调 -> 保存到文件
    // ========================================
    encoder->SetEncodedDataCallback([&saver](rmg::EncodedFrame frame) {
        if (saver->SaveFrame(frame)) {
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
    if (!saver->Start()) {
        SPDLOG_ERROR("Failed to start FileSaver");
        pipeline.UnbindAll();
        sys.Deinitialize();
        return -1;
    }

    // 开始录制
    if (!saver->StartRecording()) {
        SPDLOG_ERROR("Failed to start recording");
        saver->Stop();
        pipeline.UnbindAll();
        sys.Deinitialize();
        return -1;
    }

    if (!encoder->Start()) {
        SPDLOG_ERROR("Failed to start VideoEncoder");
        saver->Stop();
        pipeline.UnbindAll();
        sys.Deinitialize();
        return -1;
    }

    // 注意：在硬件绑定模式下，不需要调用 vi->Start()
    // 因为 VI 的数据会自动流向 VENC，无需通过 CaptureThread 拉取帧
    // 如果调用 vi->Start()，CaptureThread 会调用 GetFrame() 把帧"抢走"，
    // 导致 VENC 收不到数据

    SPDLOG_INFO("===========================================");
    SPDLOG_INFO("Recording started!");
    SPDLOG_INFO("Output file: {}", saver->GetCurrentFilePath());
    SPDLOG_INFO("Press Ctrl+C to stop early...");
    SPDLOG_INFO("===========================================");

    // ========================================
    // 主循环 - 等待录制完成或用户中断
    // ========================================
    auto start_time = std::chrono::steady_clock::now();
    uint64_t last_frame_count = 0;

    while (g_running.load() && saver->IsRecording()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

        uint64_t current_frames = g_frame_count.load();
        uint64_t current_bytes = g_byte_count.load();

        uint64_t delta_frames = current_frames - last_frame_count;
        double current_fps = static_cast<double>(delta_frames);
        double bitrate_kbps = static_cast<double>(current_bytes * 8) / (elapsed * 1000.0);

        SPDLOG_INFO("Recording: {}s / {}s | frames={} | fps={:.1f} | size={:.1f}MB",
                    elapsed, duration_sec, current_frames, current_fps,
                    static_cast<double>(current_bytes) / (1024 * 1024));

        last_frame_count = current_frames;

        // 检查是否达到时长限制
        if (static_cast<uint32_t>(elapsed) >= duration_sec) {
            SPDLOG_INFO("Recording duration reached");
            break;
        }
    }

    // ========================================
    // 停止并清理
    // ========================================
    SPDLOG_INFO("Stopping...");

    // 注意：在硬件绑定模式下，不需要调用 vi->Stop()
    encoder->Stop();
    
    // 停止录制并获取文件路径
    std::string output_file = saver->StopRecording();
    saver->Stop();

    // 解绑
    pipeline.UnbindAll();

    // 关闭系统
    sys.Deinitialize();

    // 打印统计信息
    SPDLOG_INFO("===========================================");
    SPDLOG_INFO("Recording completed!");
    SPDLOG_INFO("Output file: {}", output_file);
    SPDLOG_INFO("Total frames: {}", g_frame_count.load());
    SPDLOG_INFO("Total size: {:.2f} MB", static_cast<double>(g_byte_count.load()) / (1024 * 1024));
    SPDLOG_INFO("===========================================");
    SPDLOG_INFO("To play the video:");
    if (codec == rmg::CodecType::kH265) {
        SPDLOG_INFO("  ffplay -f hevc {}", output_file);
    } else {
        SPDLOG_INFO("  ffplay -f h264 {}", output_file);
    }
    SPDLOG_INFO("To convert to MP4:");
    if (codec == rmg::CodecType::kH265) {
        SPDLOG_INFO("  ffmpeg -f hevc -i {} -c copy output.mp4", output_file);
    } else {
        SPDLOG_INFO("  ffmpeg -f h264 -i {} -c copy output.mp4", output_file);
    }

    return 0;
}
