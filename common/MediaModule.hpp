/**
 * @file MediaModule.hpp
 * @brief 媒体模块基类 - 组件化设计的核心
 *
 * 所有硬件/软件单元（VI, VENC, RGA, RTSP Sink 等）都继承自此类，
 * 定义统一的接口实现管道模型。
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "MediaFrame.hpp"

namespace rmg {

    /**
     * @brief 模块状态枚举
     */
    enum class ModuleState {
        kUninitialized, ///< 未初始化
        kInitialized, ///< 已初始化
        kRunning, ///< 运行中
        kStopped, ///< 已停止
        kError, ///< 错误状态
    };

    /**
     * @brief 模块类型枚举
     */
    enum class ModuleType {
        kSource, ///< 数据源（如 VI）
        kProcessor, ///< 处理器（如 RGA、VPSS）
        kEncoder, ///< 编码器（如 VENC）
        kDecoder, ///< 解码器（如 VDEC）
        kSink, ///< 数据汇（如 RTSP、文件写入）
    };

    /**
     * @brief YUV 帧回调类型（值语义，支持移动）
     */
    using YuvFrameCallback = std::function<void(YuvFrame)>;

    /**
     * @brief 编码帧回调类型（值语义，支持移动）
     */
    using EncodedFrameCallback = std::function<void(EncodedFrame)>;

    /**
     * @brief 通用帧回调类型（使用 shared_ptr 用于需要共享所有权的场景）
     */
    using FrameCallback = std::function<void(FramePtr)>;

    /**
     * @brief 媒体模块基类
     *
     * 定义所有媒体处理模块的统一接口，支持：
     * - 生命周期管理（Initialize/Start/Stop）
     * - 帧推送（PushFrame）
     * - 回调机制（SetCallback）
     */
    class MediaModule {
    public:
        /**
         * @brief 构造函数
         * @param name 模块名称（用于日志和调试）
         * @param type 模块类型
         */
        explicit MediaModule(std::string_view name, ModuleType type) : name_(name), type_(type) {}

        virtual ~MediaModule() = default;

        // 禁用拷贝
        MediaModule(const MediaModule &) = delete;
        MediaModule &operator=(const MediaModule &) = delete;

        // 允许移动
        MediaModule(MediaModule &&) noexcept = default;
        MediaModule &operator=(MediaModule &&) noexcept = default;

        /**
         * @brief 初始化模块
         * @return true 初始化成功
         * @return false 初始化失败
         */
        [[nodiscard]] virtual bool Initialize() = 0;

        /**
         * @brief 启动模块
         * @return true 启动成功
         */
        [[nodiscard]] virtual bool Start() = 0;

        /**
         * @brief 停止模块
         */
        virtual void Stop() = 0;

        /**
         * @brief 推送帧到模块（用于软件级数据传递）
         * @param frame 帧数据
         * @return true 推送成功
         */
        virtual bool PushFrame([[maybe_unused]] FramePtr frame) { return false; }

        /**
         * @brief 设置输出帧回调
         * @param callback 回调函数
         */
        void SetOutputCallback(FrameCallback callback) { output_callback_ = std::move(callback); }

        /**
         * @brief 获取模块名称
         */
        [[nodiscard]] std::string_view GetName() const { return name_; }

        /**
         * @brief 获取模块类型
         */
        [[nodiscard]] ModuleType GetType() const { return type_; }

        /**
         * @brief 获取模块状态
         */
        [[nodiscard]] ModuleState GetState() const { return state_.load(); }

        /**
         * @brief 检查模块是否正在运行
         */
        [[nodiscard]] bool IsRunning() const { return state_.load() == ModuleState::kRunning; }

    protected:
        /**
         * @brief 设置模块状态
         */
        void SetState(ModuleState state) { state_.store(state); }

        /**
         * @brief 调用输出回调
         */
        void InvokeOutputCallback(FramePtr frame) {
            if (output_callback_) {
                output_callback_(std::move(frame));
            }
        }

        std::string name_;
        ModuleType type_;
        std::atomic<ModuleState> state_{ModuleState::kUninitialized};
        FrameCallback output_callback_;
    };

    /**
     * @brief 模块智能指针类型
     */
    using ModulePtr = std::shared_ptr<MediaModule>;

} // namespace rmg
