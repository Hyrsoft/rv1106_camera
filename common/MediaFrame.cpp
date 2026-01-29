/**
 * @file MediaFrame.cpp
 * @brief 媒体帧抽象 - 实现文件
 *
 * @author 好软，好温暖
 * @date 2026-01-29
 */

#include "MediaFrame.hpp"

#include <spdlog/spdlog.h>

namespace rmg {

    // ============================================================================
    // YuvFrame 实现
    // ============================================================================

    YuvFrame::YuvFrame(const VIDEO_FRAME_INFO_S &frame_info, ReleaseCallback release_cb) :
        frame_info_(frame_info), release_cb_(std::move(release_cb)) {
        is_valid_ = (frame_info_.stVFrame.pMbBlk != nullptr);
    }

    YuvFrame::~YuvFrame() {
        if (is_valid_ && release_cb_ && frame_info_.stVFrame.pMbBlk != nullptr) {
            release_cb_(&frame_info_);
        }
    }

    YuvFrame::YuvFrame(YuvFrame &&other) noexcept :
        frame_info_(other.frame_info_), release_cb_(std::move(other.release_cb_)), is_valid_(other.is_valid_) {
        other.is_valid_ = false;
        other.frame_info_.stVFrame.pMbBlk = nullptr;
        other.release_cb_ = nullptr;
    }

    YuvFrame &YuvFrame::operator=(YuvFrame &&other) noexcept {
        if (this != &other) {
            // 释放当前帧
            if (is_valid_ && release_cb_ && frame_info_.stVFrame.pMbBlk != nullptr) {
                release_cb_(&frame_info_);
            }

            // 转移所有权
            frame_info_ = other.frame_info_;
            release_cb_ = std::move(other.release_cb_);
            is_valid_ = other.is_valid_;

            other.is_valid_ = false;
            other.frame_info_.stVFrame.pMbBlk = nullptr;
            other.release_cb_ = nullptr;
        }
        return *this;
    }

    void *YuvFrame::GetVirAddr() const {
        if (!is_valid_ || frame_info_.stVFrame.pMbBlk == nullptr) {
            return nullptr;
        }

        void *vir_addr = frame_info_.stVFrame.pVirAddr[0];
        if (vir_addr == nullptr) {
            vir_addr = RK_MPI_MB_Handle2VirAddr(frame_info_.stVFrame.pMbBlk);
        }
        return vir_addr;
    }

    uint64_t YuvFrame::GetPhyAddr() const {
        if (!is_valid_ || frame_info_.stVFrame.pMbBlk == nullptr) {
            return 0;
        }
        return RK_MPI_MB_Handle2PhysAddr(frame_info_.stVFrame.pMbBlk);
    }

    size_t YuvFrame::GetDataSize() const {
        if (!is_valid_ || frame_info_.stVFrame.pMbBlk == nullptr) {
            return 0;
        }
        return static_cast<size_t>(RK_MPI_MB_GetSize(frame_info_.stVFrame.pMbBlk));
    }

    // ============================================================================
    // EncodedFrame 实现
    // ============================================================================

    EncodedFrame::EncodedFrame(const VENC_STREAM_S &stream, uint32_t chn_id, ReleaseCallback release_cb) :
        stream_(stream), chn_id_(chn_id), release_cb_(std::move(release_cb)) {
        is_valid_ = (stream_.u32PackCount > 0 && stream_.pstPack != nullptr);
    }

    EncodedFrame::~EncodedFrame() {
        if (is_valid_ && release_cb_) {
            release_cb_(&stream_);
        }
    }

    EncodedFrame::EncodedFrame(EncodedFrame &&other) noexcept :
        stream_(other.stream_), chn_id_(other.chn_id_), release_cb_(std::move(other.release_cb_)),
        is_valid_(other.is_valid_) {
        other.is_valid_ = false;
        other.stream_.pstPack = nullptr;
        other.release_cb_ = nullptr;
    }

    EncodedFrame &EncodedFrame::operator=(EncodedFrame &&other) noexcept {
        if (this != &other) {
            if (is_valid_ && release_cb_) {
                release_cb_(&stream_);
            }

            stream_ = other.stream_;
            chn_id_ = other.chn_id_;
            release_cb_ = std::move(other.release_cb_);
            is_valid_ = other.is_valid_;

            other.is_valid_ = false;
            other.stream_.pstPack = nullptr;
            other.release_cb_ = nullptr;
        }
        return *this;
    }

    void *EncodedFrame::GetVirAddr() const {
        if (!is_valid_ || stream_.pstPack == nullptr) {
            return nullptr;
        }
        // 通过 MB_BLK 句柄获取虚拟地址
        return RK_MPI_MB_Handle2VirAddr(stream_.pstPack[0].pMbBlk);
    }

    size_t EncodedFrame::GetDataSize() const {
        if (!is_valid_ || stream_.pstPack == nullptr) {
            return 0;
        }

        size_t total_size = 0;
        for (uint32_t i = 0; i < stream_.u32PackCount; ++i) {
            total_size += stream_.pstPack[i].u32Len;
        }
        return total_size;
    }

    uint64_t EncodedFrame::GetPts() const {
        if (!is_valid_ || stream_.pstPack == nullptr) {
            return 0;
        }
        return stream_.pstPack[0].u64PTS;
    }

    bool EncodedFrame::IsKeyFrame() const {
        if (!is_valid_ || stream_.pstPack == nullptr) {
            return false;
        }

        // 检查 NAL 类型判断是否为关键帧
        for (uint32_t i = 0; i < stream_.u32PackCount; ++i) {
            auto data_type = stream_.pstPack[i].DataType;
            // H.264 IDR 帧或 H.265 IDR 帧
            if (data_type.enH264EType == H264E_NALU_ISLICE || data_type.enH264EType == H264E_NALU_IDRSLICE ||
                data_type.enH265EType == H265E_NALU_ISLICE || data_type.enH265EType == H265E_NALU_IDRSLICE) {
                return true;
            }
        }
        return false;
    }

} // namespace rmg
