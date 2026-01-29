# 03_h264_record - H.264/HEVC 视频录制示例

## 概述

本示例演示如何在 RV1106 (Luckfox Pico) 上录制 H.264/HEVC 原始码流视频。

### 架构说明

```
┌────────────┐     硬件绑定      ┌─────────────┐     软件回调      ┌─────────────┐
│   Camera   │ ═══════════════> │  VideoEncoder│ ───────────────> │  FileSaver  │
│    (VI)    │   (零拷贝)       │   (VENC)     │   (文件写入)     │   (.h264)   │
└────────────┘                  └─────────────┘                  └─────────────┘
```

- **VI -> VENC**: 硬件绑定，实现零拷贝数据传输
- **VENC -> FileSaver**: 软件回调，将编码后的帧写入文件

## 编译

```bash
# 在项目根目录执行
cmake --preset Debug
cmake --build build/Debug
```

生成的可执行文件位于 `build/Debug/03_h264_record/H264Record`

## 部署

```bash
scp build/Debug/03_h264_record/H264Record root@<device_ip>:/root/
```

## 使用方法

### 基本用法

```bash
# 在设备上运行（默认录制 10 秒）
./H264Record
```

默认配置：
- 分辨率：1920x1080
- 帧率：30fps
- 码率：4000kbps
- 时长：10 秒

### 命令行参数

```
Usage: H264Record [options]
Options:
  -w <width>      图像宽度 (默认: 1920)
  -h <height>     图像高度 (默认: 1080)
  -f <fps>        帧率 (默认: 30)
  -b <bitrate>    码率 kbps (默认: 4000)
  -g <gop>        GOP 大小 (默认: 60)
  -t <seconds>    录制时长秒数 (默认: 10)
  -o <path>       输出目录 (默认: 当前目录)
  -n <filename>   输出文件名 (默认: 自动生成)
  -c <codec>      编码器: h264, h265 (默认: h264)
  -v              详细输出模式
  -?              显示帮助
```

### 示例

```bash
# 录制 30 秒 1080p H.264 视频
./H264Record -t 30

# 录制 720p H.265 视频，2Mbps 码率
./H264Record -w 1280 -h 720 -c h265 -b 2000 -t 60

# 指定输出文件名
./H264Record -n my_video -t 20
```

## 播放录制的视频

### 使用 ffplay

```bash
# H.264 格式
ffplay -f h264 output.h264

# H.265 格式
ffplay -f hevc output.hevc
```

### 转换为 MP4

```bash
# H.264 -> MP4
ffmpeg -f h264 -i output.h264 -c copy output.mp4

# H.265 -> MP4
ffmpeg -f hevc -i output.hevc -c copy output.mp4

# 带帧率信息
ffmpeg -f h264 -framerate 30 -i output.h264 -c copy output.mp4
```

## 输出文件格式

- **H.264**: `.h264` - Annex B 格式原始码流
- **H.265**: `.hevc` - Annex B 格式原始码流

文件命名格式（自动生成）：
```
YYYYMMDD_HHMMSS_mmm_WIDTHxHEIGHT.h264
```

示例：`20260129_143052_123_1920x1080.h264`

## 性能指标

| 分辨率 | 帧率 | 码率 | CPU 占用 | 文件大小/分钟 |
|--------|------|------|----------|---------------|
| 1920x1080 | 30fps | 4Mbps | ~15% | ~30MB |
| 1280x720 | 30fps | 2Mbps | ~10% | ~15MB |
| 640x480 | 30fps | 1Mbps | ~5% | ~7.5MB |

## 故障排除

### 录制文件无法播放

1. 确保使用正确的格式参数：
   ```bash
   ffplay -f h264 file.h264   # 对于 H.264
   ffplay -f hevc file.hevc   # 对于 H.265
   ```

2. 检查文件是否完整（使用 Ctrl+C 正常停止录制）

### 录制卡顿或丢帧

- 降低分辨率或码率
- 检查存储设备写入速度
- 使用 SSD 或高速 SD 卡

## 许可证

MIT License
