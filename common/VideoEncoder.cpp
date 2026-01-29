/**
 * @file VideoEncoder.cpp
 * @brief 视频编码器模块 - 实现文件
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#include "VideoEncoder.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

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

    bool VideoEncoder::PushJpegFrame(const YuvFrame &frame) {
        if (!IsRunning()) {
            return false;
        }

        if (!frame.IsValid()) {
            SPDLOG_WARN("PushJpegFrame: invalid frame");
            return false;
        }

        if (config_.codec != CodecType::kJPEG) {
            SPDLOG_WARN("PushJpegFrame only works for JPEG encoder");
            return false;
        }

        // 对于 JPEG 单帧模式，需要动态调整编码器属性以匹配帧的实际分辨率
        uint32_t frame_width = frame.GetWidth();
        uint32_t frame_height = frame.GetHeight();
        uint32_t frame_vir_width = frame.GetVirWidth();
        uint32_t frame_vir_height = frame.GetVirHeight();

        // 获取当前编码器属性
        VENC_CHN_ATTR_S chn_attr;
        std::memset(&chn_attr, 0, sizeof(chn_attr));
        RK_S32 ret = RK_MPI_VENC_GetChnAttr(config_.chn_id, &chn_attr);
        if (ret != RK_SUCCESS) {
            SPDLOG_WARN("RK_MPI_VENC_GetChnAttr failed: 0x{:08X}", ret);
            return false;
        }

        // 更新编码器分辨率以匹配当前帧
        chn_attr.stVencAttr.u32PicWidth = frame_width;
        chn_attr.stVencAttr.u32PicHeight = frame_height;
        chn_attr.stVencAttr.u32VirWidth = frame_vir_width;
        chn_attr.stVencAttr.u32VirHeight = frame_vir_height;

        ret = RK_MPI_VENC_SetChnAttr(config_.chn_id, &chn_attr);
        if (ret != RK_SUCCESS) {
            SPDLOG_WARN("RK_MPI_VENC_SetChnAttr failed: 0x{:08X}", ret);
            return false;
        }

        // 发送帧到编码器
        const auto &frame_info = frame.GetFrameInfo();
        ret = RK_MPI_VENC_SendFrame(config_.chn_id, const_cast<VIDEO_FRAME_INFO_S *>(&frame_info), 1000);
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

        constexpr RK_S32 RK_ERR_VENC_HW_NOT_CREATE = 0xA0048010;

        while (running_.load()) {
            // 为包元数据分配堆内存（关键修复）
            // RV1106 JPEG 编码通常只有 1 个包，H.264/H.265 可能有多个
            VENC_PACK_S* pack_buffer = new VENC_PACK_S[1];

            VENC_STREAM_S stream;
            std::memset(&stream, 0, sizeof(stream));
            stream.pstPack = pack_buffer;
            stream.u32PackCount = 1;

            int32_t timeout_ms = (config_.codec == CodecType::kJPEG) ? 200 : 100;
            RK_S32 ret = RK_MPI_VENC_GetStream(config_.chn_id, &stream, timeout_ms);

            if (ret == RK_SUCCESS && stream.pstPack != nullptr && stream.u32PackCount > 0) {
                // 创建 EncodedFrame，使用 Lambda 捕获 pack_buffer 指针进行释放
                uint32_t chn_id = config_.chn_id;

                // 定义 ReleaseCallback：既释放 MPI 数据，也释放我们分配的元数据
                auto release_cb = [chn_id, pack_buffer](VENC_STREAM_S *s) {
                    // 1. 告诉 MPI 释放底层的数据 buffer (pMbBlk 等)
                    RK_MPI_VENC_ReleaseStream(chn_id, s);
                    // 2. 释放我们自己分配的元数据 struct
                    delete[] pack_buffer;
                };

                // EncodedFrame 会接管 stream 结构体（浅拷贝），
                // 它的 pstPack 成员指向堆上的 pack_buffer，这是安全的。
                EncodedFrame encoded_frame(stream, config_.chn_id, release_cb);

                if (encoded_callback_) {
                    encoded_callback_(std::move(encoded_frame));
                }
            } else {
                // 获取失败，必须立即手动释放 pack_buffer
                delete[] pack_buffer;

                if (ret == RK_ERR_VENC_HW_NOT_CREATE) {
                    // JPEG 模式下，等待第一帧数据到来
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                } else if (ret != RK_ERR_VENC_BUF_EMPTY && ret != RK_SUCCESS) {
                    SPDLOG_DEBUG("RK_MPI_VENC_GetStream: 0x{:08X}", ret);
                }
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
            case CodecType::kJPEG:
                chn_attr.stVencAttr.enType = RK_VIDEO_ID_JPEG;
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

        // 为 JPEG 模式设置最大分辨率（以支持运行时调整）
        if (config_.codec == CodecType::kJPEG || config_.codec == CodecType::kMJPEG) {
            chn_attr.stVencAttr.u32MaxPicWidth = 2560;
            chn_attr.stVencAttr.u32MaxPicHeight = 1440;
            // 增加 buf 大小以支持高分辨率 JPEG
            chn_attr.stVencAttr.u32BufSize = std::max(chn_attr.stVencAttr.u32BufSize, 204800U);
        }

        // 配置码率控制
        ConfigureRateControl(chn_attr);

        // 设置 GOP (仅针对 H.264/H.265，JPEG 不需要)
        if (config_.codec == CodecType::kJPEG || config_.codec == CodecType::kMJPEG) {
            // JPEG 是单帧编码，不有 GOP 概念，清零 GOP 属性
            std::memset(&chn_attr.stGopAttr, 0, sizeof(VENC_GOP_ATTR_S));
        } else {
            // H.264/H.265 设置 GOP
            chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
            chn_attr.stGopAttr.s32VirIdrLen = config_.gop;
        }

        // 创建通道
        RK_S32 ret = RK_MPI_VENC_CreateChn(config_.chn_id, &chn_attr);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VENC_CreateChn failed: 0x{:08X}", ret);
            return false;
        }

        // 如果是 JPEG/MJPEG，设置质量参数
        if (config_.codec == CodecType::kJPEG || config_.codec == CodecType::kMJPEG) {
            VENC_JPEG_PARAM_S stJpegParam;
            std::memset(&stJpegParam, 0, sizeof(stJpegParam));
            stJpegParam.u32Qfactor = config_.jpeg_quality;
            ret = RK_MPI_VENC_SetJpegParam(config_.chn_id, &stJpegParam);
            if (ret != RK_SUCCESS) {
                SPDLOG_WARN("RK_MPI_VENC_SetJpegParam failed: 0x{:08X}", ret);
            }
        }

        // 对于非 JPEG 单帧模式，默认启动接收帧
        if (config_.codec != CodecType::kJPEG) {
            VENC_RECV_PIC_PARAM_S stRecvParam;
            std::memset(&stRecvParam, 0, sizeof(stRecvParam));
            stRecvParam.s32RecvPicNum = -1; // 持续接收
            ret = RK_MPI_VENC_StartRecvFrame(config_.chn_id, &stRecvParam);
            if (ret != RK_SUCCESS) {
                SPDLOG_WARN("RK_MPI_VENC_StartRecvFrame failed: 0x{:08X}", ret);
            }
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

            case CodecType::kJPEG:
                // JPEG 单帧编码不需要码率控制
                break;
        }
    }

    bool VideoEncoder::SetJpegQuality(uint32_t quality) {
        if (config_.codec != CodecType::kJPEG && config_.codec != CodecType::kMJPEG) {
            SPDLOG_WARN("SetJpegQuality only works for JPEG/MJPEG encoder");
            return false;
        }

        if (quality < 1 || quality > 99) {
            SPDLOG_ERROR("JPEG quality must be between 1 and 99, got {}", quality);
            return false;
        }

        VENC_JPEG_PARAM_S stJpegParam;
        std::memset(&stJpegParam, 0, sizeof(stJpegParam));
        stJpegParam.u32Qfactor = quality;

        RK_S32 ret = RK_MPI_VENC_SetJpegParam(config_.chn_id, &stJpegParam);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VENC_SetJpegParam failed: 0x{:08X}", ret);
            return false;
        }

        config_.jpeg_quality = quality;
        SPDLOG_INFO("JPEG quality set to {}", quality);
        return true;
    }

    bool VideoEncoder::StartRecvFrame(int32_t recv_count) {
        VENC_RECV_PIC_PARAM_S stRecvParam;
        std::memset(&stRecvParam, 0, sizeof(stRecvParam));
        stRecvParam.s32RecvPicNum = recv_count;

        RK_S32 ret = RK_MPI_VENC_StartRecvFrame(config_.chn_id, &stRecvParam);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VENC_StartRecvFrame failed: 0x{:08X}", ret);
            return false;
        }

        SPDLOG_DEBUG("Started receiving {} frame(s)", recv_count);
        return true;
    }

    bool VideoEncoder::StopRecvFrame() {
        RK_S32 ret = RK_MPI_VENC_StopRecvFrame(config_.chn_id);
        if (ret != RK_SUCCESS) {
            SPDLOG_ERROR("RK_MPI_VENC_StopRecvFrame failed: 0x{:08X}", ret);
            return false;
        }

        SPDLOG_DEBUG("Stopped receiving frames");
        return true;
    }

} // namespace rmg
