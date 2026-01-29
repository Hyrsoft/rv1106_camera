/**
 * @file VideoCapture.hpp
 * @brief 视频采集模块 - VI/ISP 封装
 *
 * 封装 RV1106 的 VI 和 ISP 模块，继承自 MediaModule。
 * 使用 SystemManager 管理 MPI 系统生命周期，解决多模块共享问题。
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "MediaModule.hpp"
#include "Pipeline.hpp"
#include "SystemManager.hpp"

// Rockchip MPI headers
#include "rk_comm_vi.h"
#include "rk_mpi_vi.h"

// ISP headers
#include "sample_comm_isp.h"

namespace rmg {

    /**
     * @brief 视频采集模块
     *
     * 封装 VI/ISP 接口，作为数据源模块提供 YUV 帧。
     * 支持两种工作模式：
     * 1. 轮询模式：主动调用 GetFrame() 获取帧
     * 2. 回调模式：启动后自动采集并触发回调
     */
    class VideoCapture : public MediaModule {
    public:
        /**
         * @brief 采集配置
         */
        struct Config {
            int cam_id = 0; ///< 相机 ID
            uint32_t width = 1920; ///< 输出图像宽度
            uint32_t height = 1080; ///< 输出图像高度
            std::string iq_path = "/etc/iqfiles"; ///< IQ 文件路径
            std::string dev_name = "/dev/video11"; ///< V4L2 设备名称
            PIXEL_FORMAT_E pixel_format = RK_FMT_YUV420SP; ///< 像素格式 (NV12)
            uint32_t buf_count = 3; ///< VI 缓冲区数量
            uint32_t depth = 2; ///< 用户获取帧深度
            rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL; ///< HDR 模式
            bool multi_cam = false; ///< 是否多相机模式
            uint32_t pipe_id = 0; ///< VI Pipe ID
            uint32_t chn_id = 0; ///< VI 通道 ID
        };

        /**
         * @brief 构造函数
         * @param config 采集配置
         */
        explicit VideoCapture(const Config &config);

        ~VideoCapture() override;

        // MediaModule 接口实现
        [[nodiscard]] bool Initialize() override;
        [[nodiscard]] bool Start() override;
        void Stop() override;

        /**
         * @brief 获取一帧 YUV 图像（轮询模式，值语义）
         *
         * 返回 std::optional<YuvFrame>，避免堆分配：
         * - 成功时返回包含 YuvFrame 的 optional
         * - 失败时返回 std::nullopt
         *
         * @param timeout_ms 超时时间（毫秒），-1 表示阻塞等待
         * @return 成功返回 OptionalYuvFrame，失败返回 std::nullopt
         *
         * @code
         * if (auto frame = capture.GetFrame(1000)) {
         *     void* data = frame->GetVirAddr();
         *     // 使用帧数据...
         * }  // frame 离开作用域时自动释放 MPI 资源
         * @endcode
         */
        [[nodiscard]] OptionalYuvFrame GetFrame(int timeout_ms = 1000);

        /**
         * @brief 设置 YUV 帧回调（回调模式，值语义）
         *
         * @param callback 回调函数，接收 YuvFrame 的右值引用
         */
        void SetYuvFrameCallback(YuvFrameCallback callback) { yuv_callback_ = std::move(callback); }

        /**
         * @brief 获取模块端点（用于硬件绑定）
         */
        [[nodiscard]] ModuleEndpoint GetEndpoint() const;

        /**
         * @brief 获取当前帧率
         */
        [[nodiscard]] uint32_t GetCurrentFps() const;

        /**
         * @brief 设置帧率
         */
        [[nodiscard]] bool SetFrameRate(uint32_t fps);

        /**
         * @brief 设置镜像翻转
         */
        [[nodiscard]] bool SetMirrorFlip(bool mirror, bool flip);

        /**
         * @brief 获取配置
         */
        [[nodiscard]] const Config &GetConfig() const { return config_; }

    private:
        /**
         * @brief 初始化 ISP
         */
        [[nodiscard]] bool InitIsp();

        /**
         * @brief 初始化 VI 设备和通道
         */
        [[nodiscard]] bool InitVi();

        /**
         * @brief 反初始化 VI
         */
        void DeinitVi();

        /**
         * @brief 反初始化 ISP
         */
        void DeinitIsp();

        /**
         * @brief 采集线程函数（回调模式）
         */
        void CaptureThread();

        Config config_;
        std::unique_ptr<SystemGuard> sys_guard_; ///< 系统管理器守卫
        bool isp_initialized_ = false;
        bool vi_initialized_ = false;

        // 回调模式相关
        std::thread capture_thread_;
        std::atomic<bool> running_{false};
        YuvFrameCallback yuv_callback_; ///< YUV 帧回调（值语义）
    };

} // namespace rmg
