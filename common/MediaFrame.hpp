/**
 * @file MediaFrame.hpp
 * @brief 媒体帧抽象 - 基于 std::variant 的零拷贝数据载体
 *
 * 使用 C++17 std::variant 实现静态多态，避免虚函数开销。
 * 内部持有 RK MB_BLK 句柄，实现硬件级零拷贝。
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <variant>

// Rockchip MPI headers
#include "rk_comm_mb.h"
#include "rk_comm_venc.h"
#include "rk_comm_video.h"
#include "rk_mpi_mb.h"

namespace rmg { // RV1106 MediaGraph

    /**
     * @brief 帧类型枚举
     */
    enum class FrameType {
        kYuv, ///< YUV 原始帧
        kEncoded, ///< 编码后的数据包 (H.264/H.265)
    };

    // ============================================================================
    // overloaded 辅助模板 - 用于 std::visit
    // ============================================================================

    /**
     * @brief overloaded 模式辅助类
     *
     * 用于 std::visit 时组合多个 lambda 表达式
     * @code
     * std::visit(overloaded{
     *     [](const YuvFrame& f) { ... },
     *     [](const EncodedFrame& f) { ... }
     * }, frame);
     * @endcode
     */
    template<class... Ts>
    struct overloaded : Ts... {
        using Ts::operator()...;
    };

    // C++17 推导指引
    template<class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

    // ============================================================================
    // YuvFrame - YUV 原始帧
    // ============================================================================

    /**
     * @brief YUV 帧类型
     *
     * 封装 VIDEO_FRAME_INFO_S，支持从 VI/VPSS 获取的 YUV 数据。
     * 使用 RAII 确保帧资源在对象析构时正确释放。
     */
    class YuvFrame final {
    public:
        /**
         * @brief 帧释放回调类型
         *
         * 用于自定义帧的释放方式（VI/VPSS/RGA 等不同来源的帧释放方式不同）
         */
        using ReleaseCallback = std::function<void(VIDEO_FRAME_INFO_S *)>;

        /**
         * @brief 默认构造函数（创建无效帧）
         */
        YuvFrame() = default;

        /**
         * @brief 构造函数
         * @param frame_info 帧信息结构体
         * @param release_cb 释放回调函数
         */
        YuvFrame(const VIDEO_FRAME_INFO_S &frame_info, ReleaseCallback release_cb);

        /**
         * @brief 析构函数 - 自动释放帧资源
         */
        ~YuvFrame();

        // 禁用拷贝
        YuvFrame(const YuvFrame &) = delete;
        YuvFrame &operator=(const YuvFrame &) = delete;

        // 移动语义
        YuvFrame(YuvFrame &&other) noexcept;
        YuvFrame &operator=(YuvFrame &&other) noexcept;

        /**
         * @brief 获取帧类型
         */
        [[nodiscard]] static constexpr FrameType GetType() { return FrameType::kYuv; }

        /**
         * @brief 获取帧的虚拟地址（CPU 可访问）
         * @return 虚拟地址指针，失败返回 nullptr
         */
        [[nodiscard]] void *GetVirAddr() const;

        /**
         * @brief 获取帧的物理地址（硬件加速器使用）
         * @return 物理地址
         */
        [[nodiscard]] uint64_t GetPhyAddr() const;

        /**
         * @brief 获取帧数据大小
         * @return 数据大小（字节）
         */
        [[nodiscard]] size_t GetDataSize() const;

        /**
         * @brief 获取时间戳
         */
        [[nodiscard]] uint64_t GetPts() const { return frame_info_.stVFrame.u64PTS; }

        /**
         * @brief 检查帧是否有效
         */
        [[nodiscard]] bool IsValid() const { return is_valid_; }

        /**
         * @brief 获取帧宽度
         */
        [[nodiscard]] uint32_t GetWidth() const { return frame_info_.stVFrame.u32Width; }

        /**
         * @brief 获取帧高度
         */
        [[nodiscard]] uint32_t GetHeight() const { return frame_info_.stVFrame.u32Height; }

        /**
         * @brief 获取虚拟宽度（对齐后的宽度）
         */
        [[nodiscard]] uint32_t GetVirWidth() const { return frame_info_.stVFrame.u32VirWidth; }

        /**
         * @brief 获取虚拟高度（对齐后的高度）
         */
        [[nodiscard]] uint32_t GetVirHeight() const { return frame_info_.stVFrame.u32VirHeight; }

        /**
         * @brief 获取像素格式
         */
        [[nodiscard]] PIXEL_FORMAT_E GetPixelFormat() const { return frame_info_.stVFrame.enPixelFormat; }

        /**
         * @brief 获取原始帧信息结构体引用（用于硬件绑定等场景）
         */
        [[nodiscard]] const VIDEO_FRAME_INFO_S &GetFrameInfo() const { return frame_info_; }

    private:
        VIDEO_FRAME_INFO_S frame_info_{};
        ReleaseCallback release_cb_;
        bool is_valid_ = false;
    };

    // ============================================================================
    // EncodedFrame - 编码后的数据包
    // ============================================================================

    /**
     * @brief 编码数据包帧类型
     *
     * 封装 VENC_STREAM_S，支持 H.264/H.265 编码后的数据包。
     */
    class EncodedFrame final {
    public:
        /**
         * @brief 数据包释放回调类型
         */
        using ReleaseCallback = std::function<void(VENC_STREAM_S *)>;

        /**
         * @brief 默认构造函数（创建无效帧）
         */
        EncodedFrame() = default;

        /**
         * @brief 构造函数
         * @param stream 编码流结构体
         * @param chn_id 编码通道 ID
         * @param release_cb 释放回调函数
         */
        EncodedFrame(const VENC_STREAM_S &stream, uint32_t chn_id, ReleaseCallback release_cb);

        /**
         * @brief 析构函数 - 自动释放流资源
         */
        ~EncodedFrame();

        // 禁用拷贝
        EncodedFrame(const EncodedFrame &) = delete;
        EncodedFrame &operator=(const EncodedFrame &) = delete;

        // 移动语义
        EncodedFrame(EncodedFrame &&other) noexcept;
        EncodedFrame &operator=(EncodedFrame &&other) noexcept;

        /**
         * @brief 获取帧类型
         */
        [[nodiscard]] static constexpr FrameType GetType() { return FrameType::kEncoded; }

        /**
         * @brief 获取帧的虚拟地址（第一个包的地址）
         */
        [[nodiscard]] void *GetVirAddr() const;

        /**
         * @brief 获取帧的物理地址（编码数据通常只能通过虚拟地址访问）
         * @return 始终返回 0
         */
        [[nodiscard]] uint64_t GetPhyAddr() const { return 0; }

        /**
         * @brief 获取帧数据总大小（所有包的大小之和）
         */
        [[nodiscard]] size_t GetDataSize() const;

        /**
         * @brief 获取时间戳
         */
        [[nodiscard]] uint64_t GetPts() const;

        /**
         * @brief 检查帧是否有效
         */
        [[nodiscard]] bool IsValid() const { return is_valid_; }

        /**
         * @brief 获取数据包数量
         */
        [[nodiscard]] uint32_t GetPacketCount() const { return stream_.u32PackCount; }

        /**
         * @brief 获取原始流结构体引用
         */
        [[nodiscard]] const VENC_STREAM_S &GetStream() const { return stream_; }

        /**
         * @brief 检查是否为关键帧
         */
        [[nodiscard]] bool IsKeyFrame() const;

    private:
        VENC_STREAM_S stream_{};
        uint32_t chn_id_ = 0;
        ReleaseCallback release_cb_;
        bool is_valid_ = false;
    };

    // ============================================================================
    // MediaFrame - 基于 std::variant 的统一帧类型
    // ============================================================================

    /**
     * @brief 媒体帧类型 - 使用 std::variant 实现静态多态
     *
     * 相比抽象基类的优势：
     * - 值语义：可直接存放在栈或连续容器中
     * - 无虚函数开销：编译时类型分发
     * - 缓存友好：内存连续分布
     *
     * @code
     * MediaFrame frame = capture.GetFrame();
     * std::visit(overloaded{
     *     [](YuvFrame& yuv) { ... },
     *     [](EncodedFrame& enc) { ... }
     * }, frame);
     * @endcode
     */
    using MediaFrame = std::variant<YuvFrame, EncodedFrame>;

    /**
     * @brief 可选媒体帧（用于表示可能失败的帧获取）
     */
    using OptionalFrame = std::optional<MediaFrame>;

    /**
     * @brief 可选 YUV 帧
     */
    using OptionalYuvFrame = std::optional<YuvFrame>;

    /**
     * @brief 可选编码帧
     */
    using OptionalEncodedFrame = std::optional<EncodedFrame>;

    // ============================================================================
    // 辅助函数
    // ============================================================================

    /**
     * @brief 获取 MediaFrame 的帧类型
     * @param frame 媒体帧
     * @return 帧类型枚举
     */
    [[nodiscard]] inline FrameType GetFrameType(const MediaFrame &frame) {
        return std::visit(
                [](const auto &f) -> FrameType {
                    using T = std::decay_t<decltype(f)>;
                    if constexpr (std::is_same_v<T, YuvFrame>) {
                        return FrameType::kYuv;
                    } else {
                        return FrameType::kEncoded;
                    }
                },
                frame);
    }

    /**
     * @brief 检查 MediaFrame 是否有效
     * @param frame 媒体帧
     * @return 是否有效
     */
    [[nodiscard]] inline bool IsFrameValid(const MediaFrame &frame) {
        return std::visit([](const auto &f) { return f.IsValid(); }, frame);
    }

    /**
     * @brief 获取 MediaFrame 的时间戳
     * @param frame 媒体帧
     * @return 时间戳
     */
    [[nodiscard]] inline uint64_t GetFramePts(const MediaFrame &frame) {
        return std::visit([](const auto &f) { return f.GetPts(); }, frame);
    }

    /**
     * @brief 获取 MediaFrame 的数据大小
     * @param frame 媒体帧
     * @return 数据大小
     */
    [[nodiscard]] inline size_t GetFrameDataSize(const MediaFrame &frame) {
        return std::visit([](const auto &f) { return f.GetDataSize(); }, frame);
    }

    /**
     * @brief 尝试获取 YuvFrame 引用
     * @param frame 媒体帧
     * @return YuvFrame 指针，如果不是 YuvFrame 则返回 nullptr
     */
    [[nodiscard]] inline YuvFrame *GetYuvFrame(MediaFrame &frame) { return std::get_if<YuvFrame>(&frame); }

    [[nodiscard]] inline const YuvFrame *GetYuvFrame(const MediaFrame &frame) { return std::get_if<YuvFrame>(&frame); }

    /**
     * @brief 尝试获取 EncodedFrame 引用
     * @param frame 媒体帧
     * @return EncodedFrame 指针，如果不是 EncodedFrame 则返回 nullptr
     */
    [[nodiscard]] inline EncodedFrame *GetEncodedFrame(MediaFrame &frame) { return std::get_if<EncodedFrame>(&frame); }

    [[nodiscard]] inline const EncodedFrame *GetEncodedFrame(const MediaFrame &frame) {
        return std::get_if<EncodedFrame>(&frame);
    }

    // ============================================================================
    // 向后兼容的智能指针类型（用于需要共享所有权的场景）
    // ============================================================================

    using FramePtr = std::shared_ptr<MediaFrame>;
    using YuvFramePtr = std::shared_ptr<YuvFrame>;
    using EncodedFramePtr = std::shared_ptr<EncodedFrame>;

} // namespace rmg
