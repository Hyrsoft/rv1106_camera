/**
 * @file SystemManager.hpp
 * @brief MPI 系统管理器 - 单例模式
 *
 * 负责 RK MPI 系统的初始化和反初始化，确保在多模块场景下
 * 系统资源的正确管理。使用引用计数机制支持多个模块共享系统资源。
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#pragma once

#include <atomic>
#include <mutex>

namespace rmg {

    /**
     * @brief MPI 系统管理器（单例）
     *
     * 解决多组件共享 MPI 系统资源的问题：
     * - 确保 RK_MPI_SYS_Init 只被调用一次
     * - 使用引用计数，只有当所有组件都释放后才调用 RK_MPI_SYS_Exit
     */
    class SystemManager {
    public:
        /**
         * @brief 获取单例实例
         */
        static SystemManager &GetInstance();

        // 禁用拷贝和移动
        SystemManager(const SystemManager &) = delete;
        SystemManager &operator=(const SystemManager &) = delete;
        SystemManager(SystemManager &&) = delete;
        SystemManager &operator=(SystemManager &&) = delete;

        /**
         * @brief 初始化 MPI 系统（引用计数 +1）
         * @return true 初始化成功
         * @return false 初始化失败
         */
        [[nodiscard]] bool Initialize();

        /**
         * @brief 反初始化 MPI 系统（引用计数 -1）
         *
         * 当引用计数归零时，自动调用 RK_MPI_SYS_Exit
         */
        void Deinitialize();

        /**
         * @brief 检查系统是否已初始化
         */
        [[nodiscard]] bool IsInitialized() const { return ref_count_.load() > 0; }

        /**
         * @brief 获取当前引用计数
         */
        [[nodiscard]] int GetRefCount() const { return ref_count_.load(); }

    private:
        SystemManager() = default;
        ~SystemManager();

        mutable std::mutex mutex_;
        std::atomic<int> ref_count_{0};
        bool sys_initialized_ = false;
    };

    /**
     * @brief 系统管理器 RAII 守卫
     *
     * 使用 RAII 自动管理系统初始化/反初始化
     */
    class SystemGuard {
    public:
        /**
         * @brief 构造函数 - 初始化系统
         * @throws std::runtime_error 如果初始化失败
         */
        SystemGuard();

        /**
         * @brief 析构函数 - 反初始化系统
         */
        ~SystemGuard();

        // 禁用拷贝和移动
        SystemGuard(const SystemGuard &) = delete;
        SystemGuard &operator=(const SystemGuard &) = delete;
        SystemGuard(SystemGuard &&) = delete;
        SystemGuard &operator=(SystemGuard &&) = delete;

        /**
         * @brief 检查初始化是否成功
         */
        [[nodiscard]] bool IsValid() const { return is_valid_; }

    private:
        bool is_valid_ = false;
    };

} // namespace rmg
