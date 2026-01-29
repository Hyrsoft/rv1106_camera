/**
 * @file VideoEncoder.hpp
 * @brief 视频编码器模块 - VENC 封装
 *
 * 封装 RK VENC 接口，支持 H.264/H.265 编码。
 * 继承自 MediaModule，支持管道模型。
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>

#include "MediaModule.hpp"
#include "Pipeline.hpp"

// Rockchip MPI headers
#include "rk_comm_venc.h"
#include "rk_mpi_venc.h"

namespace rmg {

    /**
     * @brief 编码器类型
     */
    enum class CodecType {
        kH264, ///< H.264/AVC
        kH265, ///< H.265/HEVC
        kMJPEG, ///< Motion JPEG
        kJPEG, ///< JPEG 单帧编码
    };

    /**
     * @brief 码率控制模式
     */
    enum class RateControlMode {
        kCBR, ///< 恒定码率
        kVBR, ///< 可变码率
        kAVBR, ///< 自适应可变码率
    };

    /**
     * @brief 视频编码器模块
     *
     * 支持硬件加速的视频编码，可与 VI/VPSS 进行硬件绑定实现零拷贝。
     */
    class VideoEncoder : public MediaModule {
    public:
        /**
         * @brief 编码器配置
         */
        struct Config {
            uint32_t chn_id = 0; ///< 编码通道 ID
            uint32_t width = 1920; ///< 输入图像宽度
            uint32_t height = 1080; ///< 输入图像高度
            uint32_t vir_width = 1920; ///< 虚拟宽度（对齐）
            uint32_t vir_height = 1080; ///< 虚拟高度（对齐）
            PIXEL_FORMAT_E pixel_format = RK_FMT_YUV420SP; ///< 输入像素格式
            CodecType codec = CodecType::kH264; ///< 编码类型
            uint32_t fps = 30; ///< 帧率
            uint32_t gop = 60; ///< GOP 大小
            uint32_t bitrate = 4000; ///< 码率 (kbps)
            RateControlMode rc_mode = RateControlMode::kCBR; ///< 码率控制模式
            uint32_t profile = 100; ///< 编码 profile (100=High for H.264)
            uint32_t buf_count = 4; ///< 输出缓冲区数量
            uint32_t jpeg_quality = 80; ///< JPEG 质量 (1-99, 仅 JPEG/MJPEG 有效)
        };

        /**
         * @brief 编码数据回调类型（值语义，支持移动）
         */
        using EncodedDataCallback = std::function<void(EncodedFrame)>;

        /**
         * @brief 构造函数
         * @param config 编码器配置
         */
        explicit VideoEncoder(const Config &config);

        ~VideoEncoder() override;

        // MediaModule 接口实现
        [[nodiscard]] bool Initialize() override;
        [[nodiscard]] bool Start() override;
        void Stop() override;

        /**
         * @brief 推送 YUV 帧进行编码（软件级数据传递）
         * @param frame YUV 帧引用
         * @return true 推送成功
         */
        bool PushYuvFrame(const YuvFrame &frame);

        /**
         * @brief 设置编码数据回调
         * @param callback 回调函数
         */
        void SetEncodedDataCallback(EncodedDataCallback callback) { encoded_callback_ = std::move(callback); }

        /**
         * @brief 获取模块端点（用于硬件绑定）
         */
        [[nodiscard]] ModuleEndpoint GetEndpoint() const;

        /**
         * @brief 请求 IDR 帧
         * @return true 请求成功
         */
        [[nodiscard]] bool RequestIdr();

        /**
         * @brief 动态设置码率
         * @param bitrate_kbps 码率 (kbps)
         * @return true 设置成功
         */
        [[nodiscard]] bool SetBitrate(uint32_t bitrate_kbps);

        /**
         * @brief 动态设置帧率
         * @param fps 帧率
         * @return true 设置成功
         */
        [[nodiscard]] bool SetFrameRate(uint32_t fps);

        /**
         * @brief 设置 JPEG 质量
         * @param quality JPEG 质量 (1-99)
         * @return true 设置成功
         */
        [[nodiscard]] bool SetJpegQuality(uint32_t quality);

        /**
         * @brief 启动接收帧（用于 JPEG 单帧编码模式）
         * @param recv_count 接收帧数量，-1 表示持续接收
         * @return true 启动成功
         */
        [[nodiscard]] bool StartRecvFrame(int32_t recv_count = -1);

        /**
         * @brief 停止接收帧
         * @return true 停止成功
         */
        [[nodiscard]] bool StopRecvFrame();

        /**
         * @brief 检查是否为 JPEG/MJPEG 编码器
         */
        [[nodiscard]] bool IsJpegEncoder() const {
            return config_.codec == CodecType::kJPEG || config_.codec == CodecType::kMJPEG;
        }

        /**
         * @brief 获取配置
         */
        [[nodiscard]] const Config &GetConfig() const { return config_; }

    private:
        /**
         * @brief 获取编码数据线程函数
         */
        void GetStreamThread();

        /**
         * @brief 创建编码通道
         */
        [[nodiscard]] bool CreateChannel();

        /**
         * @brief 销毁编码通道
         */
        void DestroyChannel();

        /**
         * @brief 配置码率控制
         */
        void ConfigureRateControl(VENC_CHN_ATTR_S &chn_attr);

        Config config_;
        std::thread stream_thread_;
        std::atomic<bool> running_{false};
        EncodedDataCallback encoded_callback_;
        bool channel_created_ = false;
    };

} // namespace rmg
