/**
 * @file RtspServer.cpp
 * @brief RTSP 服务器模块实现
 */

#include "RtspServer.hpp"

#include <spdlog/spdlog.h>

// RTSP library header
extern "C" {
#include "rtsp_demo.h"
}

namespace rmg {

    RtspServer::RtspServer(const Config &config)
        : MediaModule("RtspServer", ModuleType::kSink), config_(config) {}

    RtspServer::~RtspServer() {
        Stop();
    }

    bool RtspServer::Initialize() {
        if (GetState() != ModuleState::kUninitialized) {
            spdlog::warn("[RtspServer] Already initialized");
            return true;
        }

        // Create RTSP demo (server)
        demo_ = create_rtsp_demo(config_.port);
        if (!demo_) {
            spdlog::error("[RtspServer] Failed to create RTSP demo on port {}", config_.port);
            SetState(ModuleState::kError);
            return false;
        }

        // Create session
        session_ = rtsp_new_session(demo_, config_.path.c_str());
        if (!session_) {
            spdlog::error("[RtspServer] Failed to create session: {}", config_.path);
            rtsp_del_demo(demo_);
            demo_ = nullptr;
            SetState(ModuleState::kError);
            return false;
        }

        // Set video codec
        int codec_id = static_cast<int>(config_.codec);
        if (rtsp_set_video(session_, codec_id, nullptr, 0) < 0) {
            spdlog::error("[RtspServer] Failed to set video codec");
            rtsp_del_session(session_);
            rtsp_del_demo(demo_);
            session_ = nullptr;
            demo_ = nullptr;
            SetState(ModuleState::kError);
            return false;
        }

        // Sync timestamps
        rtsp_sync_video_ts(session_, rtsp_get_reltime(), rtsp_get_ntptime());

        spdlog::info("[RtspServer] Initialized - URL: rtsp://<ip>:{}{}", config_.port, config_.path);
        SetState(ModuleState::kInitialized);
        return true;
    }

    bool RtspServer::Start() {
        if (GetState() != ModuleState::kInitialized) {
            spdlog::warn("[RtspServer] Cannot start: invalid state");
            return false;
        }

        spdlog::info("[RtspServer] Started");
        SetState(ModuleState::kRunning);
        return true;
    }

    void RtspServer::Stop() {
        if (GetState() == ModuleState::kUninitialized) {
            return;
        }

        if (session_) {
            rtsp_del_session(session_);
            session_ = nullptr;
        }

        if (demo_) {
            rtsp_del_demo(demo_);
            demo_ = nullptr;
        }

        spdlog::info("[RtspServer] Stopped");
        SetState(ModuleState::kStopped);
    }

    bool RtspServer::PushFrame(const EncodedFrame &frame) {
        void *data = frame.GetVirAddr();
        if (!data) return false;
        return PushData(static_cast<const uint8_t *>(data), frame.GetDataSize(), frame.GetPts());
    }

    bool RtspServer::PushData(const uint8_t *data, size_t len, uint64_t pts) {
        if (GetState() != ModuleState::kRunning) {
            return false;
        }

        if (!demo_ || !session_ || !data || len == 0) {
            return false;
        }

        // Send video frame
        int ret = rtsp_tx_video(session_, data, static_cast<int>(len), pts);
        if (ret < 0) {
            spdlog::warn("[RtspServer] Failed to send video frame");
            return false;
        }

        // Process RTSP events
        rtsp_do_event(demo_);

        return true;
    }

    int RtspServer::DoEvent() {
        if (!demo_) {
            return -1;
        }
        return rtsp_do_event(demo_);
    }

    std::string RtspServer::GetUrl() const {
        return "rtsp://<ip>:" + std::to_string(config_.port) + config_.path;
    }

} // namespace rmg
