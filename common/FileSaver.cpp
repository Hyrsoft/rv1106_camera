/**
 * @file FileSaver.cpp
 * @brief 文件保存模块实现
 */

#include "FileSaver.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace rmg {

    FileSaver::FileSaver(const Config &config)
        : MediaModule("FileSaver", ModuleType::kSink), config_(config) {}

    FileSaver::~FileSaver() {
        Stop();
    }

    bool FileSaver::Initialize() {
        if (GetState() != ModuleState::kUninitialized) {
            spdlog::warn("[FileSaver] Already initialized");
            return true;
        }

        // 确保输出目录存在
        try {
            if (!config_.output_dir.empty() && !fs::exists(config_.output_dir)) {
                fs::create_directories(config_.output_dir);
                spdlog::info("[FileSaver] Created output directory: {}", config_.output_dir);
            }
        } catch (const fs::filesystem_error &e) {
            spdlog::error("[FileSaver] Failed to create output directory: {}", e.what());
            SetState(ModuleState::kError);
            return false;
        }

        spdlog::info("[FileSaver] Initialized - output_dir: {}", config_.output_dir);
        SetState(ModuleState::kInitialized);
        return true;
    }

    bool FileSaver::Start() {
        if (GetState() != ModuleState::kInitialized && GetState() != ModuleState::kStopped) {
            spdlog::warn("[FileSaver] Cannot start: invalid state");
            return false;
        }

        frame_count_.store(0);
        byte_count_.store(0);

        spdlog::info("[FileSaver] Started");
        SetState(ModuleState::kRunning);
        return true;
    }

    void FileSaver::Stop() {
        if (GetState() == ModuleState::kUninitialized) {
            return;
        }

        // 停止录制
        if (is_recording_.load()) {
            (void)StopRecording();
        }

        spdlog::info("[FileSaver] Stopped - saved {} frames, {} bytes",
                     frame_count_.load(), byte_count_.load());
        SetState(ModuleState::kStopped);
    }

    bool FileSaver::SaveFrame(const EncodedFrame &frame) {
        if (GetState() != ModuleState::kRunning) {
            return false;
        }

        if (!frame.IsValid()) {
            spdlog::warn("[FileSaver] Invalid frame");
            return false;
        }

        // 根据格式决定保存方式
        FileFormat format = config_.format;

        // JPEG 模式：每帧一个文件
        if (format == FileFormat::kJPEG) {
            return !SaveJpeg(frame).empty();
        }

        // H.264/HEVC 模式：追加到录制文件
        if (!is_recording_.load()) {
            // 如果没有开始录制，自动开始
            if (!StartRecording()) {
                return false;
            }
        }

        void *data = frame.GetVirAddr();
        size_t size = frame.GetDataSize();

        if (!data || size == 0) {
            spdlog::warn("[FileSaver] Empty frame data");
            return false;
        }

        if (!WriteToFile(data, size)) {
            return false;
        }

        frame_count_.fetch_add(1, std::memory_order_relaxed);
        byte_count_.fetch_add(size, std::memory_order_relaxed);

        // 检查是否达到限制
        if (config_.max_frames > 0 && frame_count_.load() >= config_.max_frames) {
            spdlog::info("[FileSaver] Reached max frames limit: {}", config_.max_frames);
            (void)StopRecording();
        }

        if (config_.max_file_size > 0 && byte_count_.load() >= config_.max_file_size) {
            spdlog::info("[FileSaver] Reached max file size limit: {} bytes", config_.max_file_size);
            (void)StopRecording();
        }

        return true;
    }

    std::string FileSaver::SaveJpeg(const EncodedFrame &frame, const std::string &custom_filename) {
        if (!frame.IsValid()) {
            spdlog::error("[FileSaver] Invalid frame for JPEG save");
            return "";
        }

        void *data = frame.GetVirAddr();
        size_t size = frame.GetDataSize();

        if (!data || size == 0) {
            spdlog::error("[FileSaver] Empty JPEG data");
            return "";
        }

        // 生成文件名
        std::string filepath;
        if (!custom_filename.empty()) {
            filepath = config_.output_dir + "/" + custom_filename;
            if (filepath.find(".jpg") == std::string::npos &&
                filepath.find(".jpeg") == std::string::npos) {
                filepath += ".jpg";
            }
        } else {
            filepath = GenerateFilename(".jpg");
        }

        // 写入文件
        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            spdlog::error("[FileSaver] Failed to open file: {}", filepath);
            return "";
        }

        file.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
        file.close();

        frame_count_.fetch_add(1, std::memory_order_relaxed);
        byte_count_.fetch_add(size, std::memory_order_relaxed);

        spdlog::info("[FileSaver] Saved JPEG: {} ({} bytes)", filepath, size);

        // 调用回调
        if (save_callback_) {
            save_callback_(filepath, size);
        }

        return filepath;
    }

    bool FileSaver::StartRecording(const std::string &custom_filename) {
        std::lock_guard<std::mutex> lock(file_mutex_);

        if (is_recording_.load()) {
            spdlog::warn("[FileSaver] Already recording");
            return true;
        }

        // 确定文件扩展名
        std::string extension = GetExtension(config_.format);

        // 生成文件名
        if (!custom_filename.empty()) {
            current_filepath_ = config_.output_dir + "/" + custom_filename;
            if (current_filepath_.find(extension) == std::string::npos) {
                current_filepath_ += extension;
            }
        } else {
            current_filepath_ = GenerateFilename(extension);
        }

        // 打开文件
        file_.open(current_filepath_, std::ios::binary);
        if (!file_.is_open()) {
            spdlog::error("[FileSaver] Failed to open file for recording: {}", current_filepath_);
            return false;
        }

        is_recording_.store(true);
        frame_count_.store(0);
        byte_count_.store(0);

        spdlog::info("[FileSaver] Started recording: {}", current_filepath_);
        return true;
    }

    std::string FileSaver::StopRecording() {
        std::lock_guard<std::mutex> lock(file_mutex_);

        if (!is_recording_.load()) {
            return "";
        }

        is_recording_.store(false);

        if (file_.is_open()) {
            file_.close();
        }

        std::string filepath = current_filepath_;
        uint64_t frames = frame_count_.load();
        uint64_t bytes = byte_count_.load();

        spdlog::info("[FileSaver] Stopped recording: {} ({} frames, {} bytes)",
                     filepath, frames, bytes);

        // 调用回调
        if (save_callback_) {
            save_callback_(filepath, bytes);
        }

        return filepath;
    }

    std::string FileSaver::GetCurrentFilePath() const {
        std::lock_guard<std::mutex> lock(file_mutex_);
        return current_filepath_;
    }

    std::string FileSaver::GenerateFilename(const std::string &extension) const {
        std::ostringstream oss;
        oss << config_.output_dir << "/";

        // 前缀
        if (!config_.filename_prefix.empty()) {
            oss << config_.filename_prefix;
            if (config_.append_timestamp) {
                oss << "_";
            }
        }

        // 时间戳
        if (config_.append_timestamp || config_.filename_prefix.empty()) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now.time_since_epoch()) % 1000;

            struct tm tm_buf{};
            localtime_r(&time, &tm_buf);

            oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
            oss << "_" << std::setfill('0') << std::setw(3) << ms.count();
        }

        // 分辨率
        if (config_.width > 0 && config_.height > 0) {
            oss << "_" << config_.width << "x" << config_.height;
        }

        oss << extension;
        return oss.str();
    }

    std::string FileSaver::GetExtension(FileFormat format) {
        switch (format) {
            case FileFormat::kJPEG:
                return ".jpg";
            case FileFormat::kH264:
                return ".h264";
            case FileFormat::kHEVC:
                return ".hevc";
            case FileFormat::kAuto:
            default:
                return ".h264";  // 默认 H.264
        }
    }

    bool FileSaver::WriteToFile(const void *data, size_t size) {
        std::lock_guard<std::mutex> lock(file_mutex_);

        if (!file_.is_open()) {
            spdlog::error("[FileSaver] File not open");
            return false;
        }

        file_.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));

        if (file_.fail()) {
            spdlog::error("[FileSaver] Failed to write to file");
            return false;
        }

        return true;
    }

} // namespace rmg
