/**
 * @file SystemManager.cpp
 * @brief MPI 系统管理器 - 实现文件
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#include "SystemManager.hpp"

#include <stdexcept>

#include <spdlog/spdlog.h>

#include "rk_mpi_sys.h"

namespace rmg {

    SystemManager &SystemManager::GetInstance() {
        static SystemManager instance;
        return instance;
    }

    SystemManager::~SystemManager() {
        if (sys_initialized_) {
            SPDLOG_WARN("SystemManager destroyed with active references: {}", ref_count_.load());
            RK_MPI_SYS_Exit();
        }
    }

    bool SystemManager::Initialize() {
        std::lock_guard<std::mutex> lock(mutex_);

        // 引用计数增加
        int prev_count = ref_count_.fetch_add(1);

        // 如果已经初始化，直接返回成功
        if (prev_count > 0) {
            SPDLOG_DEBUG("MPI system already initialized, ref_count: {}", ref_count_.load());
            return true;
        }

        // 首次初始化
        SPDLOG_INFO("Initializing MPI system...");

        RK_S32 ret = RK_MPI_SYS_Init();
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_SYS_Init failed: 0x{:08X}", ret);
            ref_count_.fetch_sub(1); // 回滚引用计数
            return false;
        }

        sys_initialized_ = true;
        SPDLOG_INFO("MPI system initialized successfully");
        return true;
    }

    void SystemManager::Deinitialize() {
        std::lock_guard<std::mutex> lock(mutex_);

        int prev_count = ref_count_.fetch_sub(1);

        if (prev_count <= 0) {
            SPDLOG_WARN("Deinitialize called with ref_count <= 0");
            ref_count_.store(0);
            return;
        }

        if (prev_count == 1 && sys_initialized_) {
            // 最后一个引用，执行反初始化
            SPDLOG_INFO("Deinitializing MPI system...");
            RK_MPI_SYS_Exit();
            sys_initialized_ = false;
            SPDLOG_INFO("MPI system deinitialized");
        } else {
            SPDLOG_DEBUG("MPI system ref_count decreased to: {}", ref_count_.load());
        }
    }

    // ============================================================================
    // SystemGuard 实现
    // ============================================================================

    SystemGuard::SystemGuard() {
        is_valid_ = SystemManager::GetInstance().Initialize();
        if (!is_valid_) {
            SPDLOG_ERROR("SystemGuard: Failed to initialize MPI system");
        }
    }

    SystemGuard::~SystemGuard() {
        if (is_valid_) {
            SystemManager::GetInstance().Deinitialize();
        }
    }

} // namespace rmg
