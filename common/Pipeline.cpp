/**
 * @file Pipeline.cpp
 * @brief 媒体管道管理器 - 实现文件
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#include "Pipeline.hpp"

#include <spdlog/spdlog.h>

namespace rmg {

    Pipeline::~Pipeline() {
        StopAll();
        UnbindAll();
    }

    bool Pipeline::RegisterModule(const std::string &name, ModulePtr module) {
        if (!module) {
            SPDLOG_ERROR("Cannot register null module: {}", name);
            return false;
        }

        if (modules_.find(name) != modules_.end()) {
            SPDLOG_WARN("Module '{}' already registered, replacing", name);
        }

        modules_[name] = std::move(module);
        SPDLOG_DEBUG("Module '{}' registered", name);
        return true;
    }

    ModulePtr Pipeline::GetModule(const std::string &name) const {
        auto it = modules_.find(name);
        if (it != modules_.end()) {
            return it->second;
        }
        return nullptr;
    }

    bool Pipeline::BindHardware(const ModuleEndpoint &src_endpoint, const ModuleEndpoint &dst_endpoint) {
        MPP_CHN_S src_chn = src_endpoint.ToMppChn();
        MPP_CHN_S dst_chn = dst_endpoint.ToMppChn();

        SPDLOG_INFO("Hardware binding: ({}, {}, {}) -> ({}, {}, {})", static_cast<int>(src_chn.enModId),
                    src_chn.s32DevId, src_chn.s32ChnId, static_cast<int>(dst_chn.enModId), dst_chn.s32DevId,
                    dst_chn.s32ChnId);

        RK_S32 ret = RK_MPI_SYS_Bind(&src_chn, &dst_chn);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_SYS_Bind failed: 0x{:08X}", ret);
            return false;
        }

        // 记录绑定信息
        BindInfo info{};
        info.src_endpoint = src_endpoint;
        info.dst_endpoint = dst_endpoint;
        info.bind_type = BindType::kHardware;
        bindings_.push_back(info);

        SPDLOG_INFO("Hardware binding established");
        return true;
    }

    bool Pipeline::BindSoftware(ModulePtr src_module, ModulePtr dst_module) {
        if (!src_module || !dst_module) {
            SPDLOG_ERROR("Cannot bind null modules");
            return false;
        }

        SPDLOG_INFO("Software binding: {} -> {}", src_module->GetName(), dst_module->GetName());

        // 设置源模块的输出回调，将帧推送到目标模块
        std::weak_ptr<MediaModule> weak_dst = dst_module;
        src_module->SetOutputCallback([weak_dst](FramePtr frame) {
            if (auto dst = weak_dst.lock()) {
                dst->PushFrame(std::move(frame));
            }
        });

        // 记录绑定信息
        BindInfo info{};
        info.src_module = src_module;
        info.dst_module = dst_module;
        info.bind_type = BindType::kSoftware;
        bindings_.push_back(info);

        SPDLOG_INFO("Software binding established");
        return true;
    }

    bool Pipeline::InitializeAll() {
        SPDLOG_INFO("Initializing all modules...");

        for (auto &[name, module]: modules_) {
            if (!module->Initialize()) {
                SPDLOG_ERROR("Failed to initialize module: {}", name);
                return false;
            }
            SPDLOG_DEBUG("Module '{}' initialized", name);
        }

        SPDLOG_INFO("All modules initialized");
        return true;
    }

    bool Pipeline::StartAll() {
        SPDLOG_INFO("Starting all modules...");

        for (auto &[name, module]: modules_) {
            if (!module->Start()) {
                SPDLOG_ERROR("Failed to start module: {}", name);
                return false;
            }
            SPDLOG_DEBUG("Module '{}' started", name);
        }

        SPDLOG_INFO("All modules started");
        return true;
    }

    void Pipeline::StopAll() {
        SPDLOG_INFO("Stopping all modules...");

        for (auto &[name, module]: modules_) {
            module->Stop();
            SPDLOG_DEBUG("Module '{}' stopped", name);
        }

        SPDLOG_INFO("All modules stopped");
    }

    void Pipeline::UnbindAll() {
        SPDLOG_INFO("Unbinding all connections...");

        for (const auto &binding: bindings_) {
            if (binding.bind_type == BindType::kHardware) {
                MPP_CHN_S src_chn = binding.src_endpoint.ToMppChn();
                MPP_CHN_S dst_chn = binding.dst_endpoint.ToMppChn();

                RK_S32 ret = RK_MPI_SYS_UnBind(&src_chn, &dst_chn);
                if (ret != RK_SUCCESS) {
                    SPDLOG_WARN("RK_MPI_SYS_UnBind failed: 0x{:08X}", ret);
                }
            }
        }

        bindings_.clear();
        SPDLOG_INFO("All bindings removed");
    }

} // namespace rmg
