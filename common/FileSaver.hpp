/**
 * @file FileSaver.hpp
 * @brief 文件保存模块 - 支持 JPEG、H.264、HEVC 原始码流保存
 *
 * 作为 Sink 模块，接收编码后的数据并保存到文件。
 * 支持的格式：
 * - JPEG 单帧图片
 * - H.264 原始码流 (.h264)
 * - HEVC 原始码流 (.hevc/.h265)
 *
 * @note MP4 封装由专门的 Muxer 模块负责
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "MediaModule.hpp"

namespace rmg {

    /**
     * @brief 文件保存格式
     */
    enum class FileFormat {
        kJPEG,    ///< JPEG 图片（单帧）
        kH264,    ///< H.264 原始码流 (Annex B)
        kHEVC,    ///< HEVC/H.265 原始码流 (Annex B)
        kAuto,    ///< 根据编码器类型自动判断
    };

    /**
     * @brief 文件保存模块
     *
     * 支持连续录制和单帧保存两种模式：
     * - 录制模式：持续写入同一文件（H.264/HEVC）
     * - 单帧模式：每次保存创建新文件（JPEG）
     */
    class FileSaver : public MediaModule {
    public:
        /**
         * @brief 文件保存配置
         */
        struct Config {
            std::string output_dir = ".";       ///< 输出目录
            std::string filename_prefix = "";   ///< 文件名前缀（空则用时间戳）
            FileFormat format = FileFormat::kAuto;  ///< 文件格式
            uint32_t width = 0;                 ///< 视频宽度（用于文件名）
            uint32_t height = 0;                ///< 视频高度（用于文件名）
            bool append_timestamp = true;       ///< 文件名是否包含时间戳
            uint64_t max_file_size = 0;         ///< 最大文件大小（字节），0 表示不限制
            uint32_t max_frames = 0;            ///< 最大帧数，0 表示不限制
        };

        /**
         * @brief 保存完成回调
         */
        using SaveCallback = std::function<void(const std::string &filepath, size_t bytes)>;

        /**
         * @brief 构造函数
         * @param config 保存配置
         */
        explicit FileSaver(const Config &config);

        ~FileSaver() override;

        // MediaModule 接口实现
        [[nodiscard]] bool Initialize() override;
        [[nodiscard]] bool Start() override;
        void Stop() override;

        /**
         * @brief 保存编码帧
         *
         * 根据配置的格式保存数据：
         * - JPEG：每帧生成一个新文件
         * - H.264/HEVC：追加到当前录制文件
         *
         * @param frame 编码帧
         * @return true 保存成功
         */
        bool SaveFrame(const EncodedFrame &frame);

        /**
         * @brief 保存 JPEG 图片（单帧）
         * @param frame 编码帧
         * @param custom_filename 自定义文件名（可选）
         * @return 保存的文件路径，失败返回空字符串
         */
        [[nodiscard]] std::string SaveJpeg(const EncodedFrame &frame, 
                                           const std::string &custom_filename = "");

        /**
         * @brief 开始录制（H.264/HEVC）
         * @param custom_filename 自定义文件名（可选）
         * @return true 开始成功
         */
        [[nodiscard]] bool StartRecording(const std::string &custom_filename = "");

        /**
         * @brief 停止录制
         * @return 录制的文件路径
         */
        [[nodiscard]] std::string StopRecording();

        /**
         * @brief 检查是否正在录制
         */
        [[nodiscard]] bool IsRecording() const { return is_recording_.load(); }

        /**
         * @brief 设置保存完成回调
         */
        void SetSaveCallback(SaveCallback callback) { save_callback_ = std::move(callback); }

        /**
         * @brief 获取当前录制文件路径
         */
        [[nodiscard]] std::string GetCurrentFilePath() const;

        /**
         * @brief 获取已保存的帧数
         */
        [[nodiscard]] uint64_t GetSavedFrameCount() const { return frame_count_.load(); }

        /**
         * @brief 获取已保存的字节数
         */
        [[nodiscard]] uint64_t GetSavedBytes() const { return byte_count_.load(); }

        /**
         * @brief 获取配置
         */
        [[nodiscard]] const Config &GetConfig() const { return config_; }

        /**
         * @brief 设置文件格式
         * @param format 文件格式
         */
        void SetFormat(FileFormat format) { config_.format = format; }

    private:
        /**
         * @brief 生成文件名
         * @param extension 文件扩展名
         * @return 完整文件路径
         */
        [[nodiscard]] std::string GenerateFilename(const std::string &extension) const;

        /**
         * @brief 获取格式对应的文件扩展名
         */
        [[nodiscard]] static std::string GetExtension(FileFormat format);

        /**
         * @brief 写入数据到文件
         */
        bool WriteToFile(const void *data, size_t size);

        Config config_;
        std::ofstream file_;
        std::string current_filepath_;
        mutable std::mutex file_mutex_;
        std::atomic<bool> is_recording_{false};
        std::atomic<uint64_t> frame_count_{0};
        std::atomic<uint64_t> byte_count_{0};
        SaveCallback save_callback_;
    };

} // namespace rmg
