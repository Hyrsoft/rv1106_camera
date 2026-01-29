/**
 * @file Pipeline.hpp
 * @brief 媒体管道管理器
 *
 * 负责管理模块间的绑定关系，支持硬件级绑定（RK_MPI_SYS_Bind）
 * 和软件级回调链接，实现零拷贝数据流。
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "MediaModule.hpp"

// Rockchip MPI headers
#include "rk_comm_sys.h"
#include "rk_mpi_sys.h"

namespace rmg {

    /**
     * @brief 模块端点标识
     *
     * 用于标识模块的输入/输出端点，对应 RK MPI 的 MPP_CHN_S
     */
    struct ModuleEndpoint {
        int32_t mod_id; ///< 模块 ID (如 RK_ID_VI, RK_ID_VENC)
        int32_t dev_id; ///< 设备 ID
        int32_t chn_id; ///< 通道 ID

        /**
         * @brief 转换为 MPP_CHN_S 结构
         */
        [[nodiscard]] MPP_CHN_S ToMppChn() const {
            MPP_CHN_S chn;
            chn.enModId = static_cast<MOD_ID_E>(mod_id);
            chn.s32DevId = dev_id;
            chn.s32ChnId = chn_id;
            return chn;
        }
    };

    /**
     * @brief 绑定类型枚举
     */
    enum class BindType {
        kHardware, ///< 硬件绑定（零拷贝，通过 RK_MPI_SYS_Bind）
        kSoftware, ///< 软件绑定（回调方式）
    };

    /**
     * @brief 绑定信息
     */
    struct BindInfo {
        ModulePtr src_module;
        ModulePtr dst_module;
        ModuleEndpoint src_endpoint;
        ModuleEndpoint dst_endpoint;
        BindType bind_type;
    };

    /**
     * @brief 媒体管道管理器
     *
     * 管理模块的注册、绑定和生命周期
     */
    class Pipeline {
    public:
        Pipeline() = default;
        ~Pipeline();

        // 禁用拷贝
        Pipeline(const Pipeline &) = delete;
        Pipeline &operator=(const Pipeline &) = delete;

        /**
         * @brief 注册模块
         * @param name 模块名称
         * @param module 模块实例
         * @return true 注册成功
         */
        bool RegisterModule(const std::string &name, ModulePtr module);

        /**
         * @brief 获取已注册的模块
         * @param name 模块名称
         * @return 模块指针，未找到返回 nullptr
         */
        [[nodiscard]] ModulePtr GetModule(const std::string &name) const;

        /**
         * @brief 硬件绑定两个模块（零拷贝）
         *
         * 使用 RK_MPI_SYS_Bind 实现硬件级数据传输
         *
         * @param src_endpoint 源端点
         * @param dst_endpoint 目标端点
         * @return true 绑定成功
         */
        [[nodiscard]] bool BindHardware(const ModuleEndpoint &src_endpoint, const ModuleEndpoint &dst_endpoint);

        /**
         * @brief 软件绑定两个模块（回调方式）
         *
         * 将源模块的输出回调连接到目标模块的 PushFrame
         *
         * @param src_module 源模块
         * @param dst_module 目标模块
         * @return true 绑定成功
         */
        bool BindSoftware(ModulePtr src_module, ModulePtr dst_module);

        /**
         * @brief 初始化所有模块
         * @return true 全部初始化成功
         */
        [[nodiscard]] bool InitializeAll();

        /**
         * @brief 启动所有模块
         * @return true 全部启动成功
         */
        [[nodiscard]] bool StartAll();

        /**
         * @brief 停止所有模块
         */
        void StopAll();

        /**
         * @brief 解绑所有硬件绑定
         */
        void UnbindAll();

    private:
        std::unordered_map<std::string, ModulePtr> modules_;
        std::vector<BindInfo> bindings_;
    };

} // namespace rmg
