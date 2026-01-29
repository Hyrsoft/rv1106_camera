/**
 * @file RtspServer.hpp
 * @brief RTSP 服务器模块 - rtsp_demo 封装
 *
 * 封装 Rockchip rtsp_demo 接口，支持 H.264/H.265 流的 RTSP 推送。
 * 继承自 MediaModule，支持管道模型。
 *
 * @author 好软，好温暖
 * @date 2026-01-30
 */

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "MediaModule.hpp"

// Forward declarations for rtsp_demo types
typedef void *rtsp_demo_handle;
typedef void *rtsp_session_handle;

namespace rmg {

    /**
     * @brief RTSP 视频编码类型
     */
    enum class RtspCodecId {
        kH264 = 0x0001,  ///< H.264/AVC
        kH265 = 0x0002,  ///< H.265/HEVC
    };

    /**
     * @brief RTSP 服务器模块
     *
     * 支持创建 RTSP 服务器和会话，接收编码后的视频流并推送给客户端。
     */
    class RtspServer : public MediaModule {
    public:
        /**
         * @brief 服务器配置
         */
        struct Config {
            uint16_t port = 554;              ///< RTSP 服务端口
            std::string path = "/live/0";     ///< 流路径
            RtspCodecId codec = RtspCodecId::kH264;  ///< 视频编码类型
        };

        /**
         * @brief 构造函数
         * @param config 服务器配置
         */
        explicit RtspServer(const Config &config);

        ~RtspServer() override;

        // MediaModule 接口实现
        [[nodiscard]] bool Initialize() override;
        [[nodiscard]] bool Start() override;
        void Stop() override;

        /**
         * @brief 推送编码帧（EncodedFrame 方式）
         *
         * 支持从 VideoEncoder 回调直接接收 EncodedFrame
         *
         * @param frame 编码帧
         * @return true 推送成功
         */
        bool PushFrame(const EncodedFrame &frame);

        /**
         * @brief 推送原始数据
         * @param data 数据指针
         * @param len 数据长度
         * @param pts 时间戳 (microseconds)
         * @return true 推送成功
         */
        bool PushData(const uint8_t *data, size_t len, uint64_t pts);

        /**
         * @brief 处理 RTSP 事件
         *
         * 需要定期调用以处理客户端请求
         *
         * @return 0 成功, 负数失败
         */
        int DoEvent();

        /**
         * @brief 获取配置
         */
        [[nodiscard]] const Config &GetConfig() const { return config_; }

        /**
         * @brief 获取 RTSP URL
         */
        [[nodiscard]] std::string GetUrl() const;

    private:
        Config config_;
        rtsp_demo_handle demo_{nullptr};
        rtsp_session_handle session_{nullptr};
    };

} // namespace rmg
