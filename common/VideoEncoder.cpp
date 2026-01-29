/**
 * @file VideoEncoder.cpp
 * @brief 视频编码器模块 - 实现文件
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#include "VideoEncoder.hpp"

#include <cstring>

#include <spdlog/spdlog.h>

namespace rmg {

    VideoEncoder::VideoEncoder(const Config &config) :
        MediaModule("VideoEncoder", ModuleType::kEncoder), config_(config) {}

    VideoEncoder::~VideoEncoder() {
        Stop();
        DestroyChannel();
    }

    bool VideoEncoder::Initialize() {
        if (GetState() != ModuleState::kUninitialized) {
            SPDLOG_WARN("VideoEncoder already initialized");
            return true;
        }

        SPDLOG_INFO("Initializing VideoEncoder ({}x{}, codec: {}, bitrate: {} kbps)", config_.width, config_.height,
                    static_cast<int>(config_.codec), config_.bitrate);

        if (!CreateChannel()) {
            SPDLOG_ERROR("Failed to create VENC channel");
            SetState(ModuleState::kError);
            return false;
        }

        SetState(ModuleState::kInitialized);
        SPDLOG_INFO("VideoEncoder initialized successfully");
        return true;
    }

    bool VideoEncoder::Start() {
        if (GetState() != ModuleState::kInitialized && GetState() != ModuleState::kStopped) {
            SPDLOG_ERROR("VideoEncoder not in valid state to start");
            return false;
        }

        SPDLOG_INFO("Starting VideoEncoder...");

        running_.store(true);
        stream_thread_ = std::thread(&VideoEncoder::GetStreamThread, this);

        SetState(ModuleState::kRunning);
        SPDLOG_INFO("VideoEncoder started");
        return true;
    }

    void VideoEncoder::Stop() {
        if (!running_.load()) {
            return;
        }

        SPDLOG_INFO("Stopping VideoEncoder...");

        running_.store(false);

        if (stream_thread_.joinable()) {
            stream_thread_.join();
        }

        SetState(ModuleState::kStopped);
        SPDLOG_INFO("VideoEncoder stopped");
    }

    bool VideoEncoder::PushYuvFrame(const YuvFrame &frame) {
        if (!IsRunning()) {
            return false;
        }

        if (!frame.IsValid()) {
            SPDLOG_WARN("PushYuvFrame: invalid frame");
            return false;
        }

        // 发送帧到编码器
        const auto &frame_info = frame.GetFrameInfo();
        RK_S32 ret = RK_MPI_VENC_SendFrame(config_.chn_id, const_cast<VIDEO_FRAME_INFO_S *>(&frame_info), 1000);
        if (ret != RK_SUCCESS) {
            SPDLOG_WARN("RK_MPI_VENC_SendFrame failed: 0x{:08X}", ret);
            return false;
        }

        return true;
    }

    ModuleEndpoint VideoEncoder::GetEndpoint() const {
        return ModuleEndpoint{
                .mod_id = RK_ID_VENC,
                .dev_id = 0,
                .chn_id = static_cast<int32_t>(config_.chn_id),
        };
    }

    bool VideoEncoder::RequestIdr() {
        RK_S32 ret = RK_MPI_VENC_RequestIDR(config_.chn_id, RK_FALSE);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VENC_RequestIDR failed: 0x{:08X}", ret);
            return false;
        }
        SPDLOG_DEBUG("IDR frame requested");
        return true;
    }

    bool VideoEncoder::SetBitrate(uint32_t bitrate_kbps) {
        VENC_CHN_ATTR_S chn_attr;
        std::memset(&chn_attr, 0, sizeof(chn_attr));

        RK_S32 ret = RK_MPI_VENC_GetChnAttr(config_.chn_id, &chn_attr);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VENC_GetChnAttr failed: 0x{:08X}", ret);
            return false;
        }

        // 更新码率
        auto &rc_attr = chn_attr.stRcAttr;
        switch (config_.codec) {
            case CodecType::kH264:
                if (config_.rc_mode == RateControlMode::kCBR) {
                    rc_attr.stH264Cbr.u32BitRate = bitrate_kbps;
                } else {
                    rc_attr.stH264Vbr.u32BitRate = bitrate_kbps;
                }
                break;
            case CodecType::kH265:
                if (config_.rc_mode == RateControlMode::kCBR) {
                    rc_attr.stH265Cbr.u32BitRate = bitrate_kbps;
                } else {
                    rc_attr.stH265Vbr.u32BitRate = bitrate_kbps;
                }
                break;
            default:
                break;
        }

        ret = RK_MPI_VENC_SetChnAttr(config_.chn_id, &chn_attr);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VENC_SetChnAttr failed: 0x{:08X}", ret);
            return false;
        }

        config_.bitrate = bitrate_kbps;
        SPDLOG_INFO("Bitrate set to {} kbps", bitrate_kbps);
        return true;
    }

    bool VideoEncoder::SetFrameRate(uint32_t fps) {
        VENC_CHN_ATTR_S chn_attr;
        std::memset(&chn_attr, 0, sizeof(chn_attr));

        RK_S32 ret = RK_MPI_VENC_GetChnAttr(config_.chn_id, &chn_attr);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VENC_GetChnAttr failed: 0x{:08X}", ret);
            return false;
        }

        // 更新帧率
        auto &rc_attr = chn_attr.stRcAttr;
        switch (config_.codec) {
            case CodecType::kH264:
                if (config_.rc_mode == RateControlMode::kCBR) {
                    rc_attr.stH264Cbr.u32SrcFrameRateNum = fps;
                    rc_attr.stH264Cbr.u32SrcFrameRateDen = 1;
                    rc_attr.stH264Cbr.fr32DstFrameRateNum = fps;
                    rc_attr.stH264Cbr.fr32DstFrameRateDen = 1;
                }
                break;
            case CodecType::kH265:
                if (config_.rc_mode == RateControlMode::kCBR) {
                    rc_attr.stH265Cbr.u32SrcFrameRateNum = fps;
                    rc_attr.stH265Cbr.u32SrcFrameRateDen = 1;
                    rc_attr.stH265Cbr.fr32DstFrameRateNum = fps;
                    rc_attr.stH265Cbr.fr32DstFrameRateDen = 1;
                }
                break;
            default:
                break;
        }

        ret = RK_MPI_VENC_SetChnAttr(config_.chn_id, &chn_attr);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VENC_SetChnAttr failed: 0x{:08X}", ret);
            return false;
        }

        config_.fps = fps;
        SPDLOG_INFO("Frame rate set to {} fps", fps);
        return true;
    }

    void VideoEncoder::GetStreamThread() {
        SPDLOG_DEBUG("GetStreamThread started");

        VENC_STREAM_S stream;
        std::memset(&stream, 0, sizeof(stream));

        while (running_.load()) {
            RK_S32 ret = RK_MPI_VENC_GetStream(config_.chn_id, &stream, 100);

            if (ret == RK_SUCCESS) {
                // 创建 EncodedFrame，带自定义释放器（值语义）
                uint32_t chn_id = config_.chn_id;
                auto release_cb = [chn_id](VENC_STREAM_S *s) { RK_MPI_VENC_ReleaseStream(chn_id, s); };

                EncodedFrame encoded_frame(stream, config_.chn_id, release_cb);

                // 调用回调（移动传递）
                if (encoded_callback_) {
                    encoded_callback_(std::move(encoded_frame));
                }

            } else if (ret != RK_ERR_VENC_BUF_EMPTY) {
                SPDLOG_WARN("RK_MPI_VENC_GetStream failed: 0x{:08X}", ret);
            }
        }

        SPDLOG_DEBUG("GetStreamThread exited");
    }

    bool VideoEncoder::CreateChannel() {
        VENC_CHN_ATTR_S chn_attr;
        std::memset(&chn_attr, 0, sizeof(chn_attr));

        // 设置编码类型
        switch (config_.codec) {
            case CodecType::kH264:
                chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
                chn_attr.stVencAttr.u32Profile = config_.profile;
                break;
            case CodecType::kH265:
                chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
                chn_attr.stVencAttr.u32Profile = 0; // Main profile
                break;
            case CodecType::kMJPEG:
                chn_attr.stVencAttr.enType = RK_VIDEO_ID_MJPEG;
                break;
        }

        // 设置基本属性
        chn_attr.stVencAttr.enPixelFormat = config_.pixel_format;
        chn_attr.stVencAttr.u32PicWidth = config_.width;
        chn_attr.stVencAttr.u32PicHeight = config_.height;
        chn_attr.stVencAttr.u32VirWidth = config_.vir_width;
        chn_attr.stVencAttr.u32VirHeight = config_.vir_height;
        chn_attr.stVencAttr.u32StreamBufCnt = config_.buf_count;
        chn_attr.stVencAttr.u32BufSize = config_.vir_width * config_.vir_height * 3 / 2;

        // 配置码率控制
        ConfigureRateControl(chn_attr);

        // 设置 GOP
        chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
        chn_attr.stGopAttr.s32VirIdrLen = config_.gop;

        // 创建通道
        RK_S32 ret = RK_MPI_VENC_CreateChn(config_.chn_id, &chn_attr);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VENC_CreateChn failed: 0x{:08X}", ret);
            return false;
        }

        channel_created_ = true;
        SPDLOG_INFO("VENC channel {} created", config_.chn_id);
        return true;
    }

    void VideoEncoder::DestroyChannel() {
        if (!channel_created_) {
            return;
        }

        RK_S32 ret = RK_MPI_VENC_DestroyChn(config_.chn_id);
        if (ret != RK_SUCCESS) {
            SPDLOG_WARN("RK_MPI_VENC_DestroyChn failed: 0x{:08X}", ret);
        }

        channel_created_ = false;
        SPDLOG_INFO("VENC channel {} destroyed", config_.chn_id);
    }

    void VideoEncoder::ConfigureRateControl(VENC_CHN_ATTR_S &chn_attr) {
        auto &rc_attr = chn_attr.stRcAttr;

        switch (config_.codec) {
            case CodecType::kH264:
                if (config_.rc_mode == RateControlMode::kCBR) {
                    rc_attr.enRcMode = VENC_RC_MODE_H264CBR;
                    rc_attr.stH264Cbr.u32BitRate = config_.bitrate;
                    rc_attr.stH264Cbr.u32Gop = config_.gop;
                    rc_attr.stH264Cbr.u32SrcFrameRateNum = config_.fps;
                    rc_attr.stH264Cbr.u32SrcFrameRateDen = 1;
                    rc_attr.stH264Cbr.fr32DstFrameRateNum = config_.fps;
                    rc_attr.stH264Cbr.fr32DstFrameRateDen = 1;
                } else {
                    rc_attr.enRcMode = VENC_RC_MODE_H264VBR;
                    rc_attr.stH264Vbr.u32BitRate = config_.bitrate;
                    rc_attr.stH264Vbr.u32Gop = config_.gop;
                    rc_attr.stH264Vbr.u32SrcFrameRateNum = config_.fps;
                    rc_attr.stH264Vbr.u32SrcFrameRateDen = 1;
                    rc_attr.stH264Vbr.fr32DstFrameRateNum = config_.fps;
                    rc_attr.stH264Vbr.fr32DstFrameRateDen = 1;
                    rc_attr.stH264Vbr.u32MaxBitRate = config_.bitrate * 2;
                }
                break;

            case CodecType::kH265:
                if (config_.rc_mode == RateControlMode::kCBR) {
                    rc_attr.enRcMode = VENC_RC_MODE_H265CBR;
                    rc_attr.stH265Cbr.u32BitRate = config_.bitrate;
                    rc_attr.stH265Cbr.u32Gop = config_.gop;
                    rc_attr.stH265Cbr.u32SrcFrameRateNum = config_.fps;
                    rc_attr.stH265Cbr.u32SrcFrameRateDen = 1;
                    rc_attr.stH265Cbr.fr32DstFrameRateNum = config_.fps;
                    rc_attr.stH265Cbr.fr32DstFrameRateDen = 1;
                } else {
                    rc_attr.enRcMode = VENC_RC_MODE_H265VBR;
                    rc_attr.stH265Vbr.u32BitRate = config_.bitrate;
                    rc_attr.stH265Vbr.u32Gop = config_.gop;
                    rc_attr.stH265Vbr.u32SrcFrameRateNum = config_.fps;
                    rc_attr.stH265Vbr.u32SrcFrameRateDen = 1;
                    rc_attr.stH265Vbr.fr32DstFrameRateNum = config_.fps;
                    rc_attr.stH265Vbr.fr32DstFrameRateDen = 1;
                    rc_attr.stH265Vbr.u32MaxBitRate = config_.bitrate * 2;
                }
                break;

            case CodecType::kMJPEG:
                rc_attr.enRcMode = VENC_RC_MODE_MJPEGCBR;
                rc_attr.stMjpegCbr.u32BitRate = config_.bitrate;
                rc_attr.stMjpegCbr.u32SrcFrameRateNum = config_.fps;
                rc_attr.stMjpegCbr.u32SrcFrameRateDen = 1;
                rc_attr.stMjpegCbr.fr32DstFrameRateNum = config_.fps;
                rc_attr.stMjpegCbr.fr32DstFrameRateDen = 1;
                break;
        }
    }

} // namespace rmg
