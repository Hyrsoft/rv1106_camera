# 03_vi_venc_rtsp - RTSP 视频流示例

## 概述

本示例演示如何在 RV1106 (Luckfox Pico) 上实现 RTSP 实时视频流。

### 架构说明

```
┌────────────┐     硬件绑定      ┌─────────────┐     软件回调      ┌─────────────┐
│   Camera   │ ═══════════════> │  VideoEncoder│ ───────────────> │  RtspServer │
│    (VI)    │   (零拷贝)       │   (VENC)     │   (数据传递)     │   (RTSP)    │
└────────────┘                  └─────────────┘                  └─────────────┘
                                     │
                                     ▼
                              H.264/H.265 流
                                     │
                                     ▼
                              ┌─────────────┐
                              │ RTSP Client │
                              │ (VLC/ffplay)│
                              └─────────────┘
```

- **VI -> VENC**: 硬件绑定，实现零拷贝数据传输
- **VENC -> RTSP**: 软件回调，将编码后的帧推送到 RTSP 服务器

## 编译

```bash
# 在项目根目录执行
cmake --preset Debug
cmake --build build/Debug
```

生成的可执行文件位于 `build/Debug/03_vi_venc_rtsp/ViVencRtsp`

## 部署

需要将以下文件复制到设备：

### 可执行文件
```bash
scp build/Debug/03_vi_venc_rtsp/ViVencRtsp root@<device_ip>:/root/
```

### 动态库（如果使用动态链接）
```bash
# 从 third_party/luckfox_pico_rkmpi_example/lib/uclibc/ 复制
scp librga.so root@<device_ip>:/usr/lib/
scp librkaiq.so root@<device_ip>:/usr/lib/
scp librockit.so root@<device_ip>:/usr/lib/
scp librockchip_mpp.so* root@<device_ip>:/usr/lib/
```

> 注意：librtsp.a 是静态链接的，无需单独部署

## 使用方法

### 基本用法

```bash
# 在设备上运行
./ViVencRtsp
```

默认配置：
- 分辨率：1920x1080
- 帧率：30fps
- 码率：4000kbps
- RTSP 端口：554
- 流路径：/live/0

### 命令行参数

```
Usage: ViVencRtsp [options]
Options:
  -w <width>      图像宽度 (默认: 1920)
  -h <height>     图像高度 (默认: 1080)
  -f <fps>        帧率 (默认: 30)
  -b <bitrate>    码率 kbps (默认: 4000)
  -g <gop>        GOP 大小 (默认: 60)
  -p <port>       RTSP 端口 (默认: 554)
  -s <path>       RTSP 流路径 (默认: /live/0)
  -c <codec>      编码器: h264, h265 (默认: h264)
  -v              详细输出模式
  -?              显示帮助
```

### 示例

```bash
# 720p H.264 流，2Mbps 码率
./ViVencRtsp -w 1280 -h 720 -b 2000

# 1080p H.265 流
./ViVencRtsp -c h265

# 自定义端口和路径
./ViVencRtsp -p 8554 -s /camera/main
```

## 观看直播

### 使用 ffplay

```bash
# TCP 传输（推荐）
ffplay -rtsp_transport tcp rtsp://<device_ip>:554/live/0

# UDP 传输
ffplay rtsp://<device_ip>:554/live/0
```

### 使用 VLC

1. 打开 VLC
2. 媒体 -> 打开网络串流
3. 输入 URL：`rtsp://<device_ip>:554/live/0`
4. 点击播放

### 使用 FFmpeg 录制

```bash
# 录制为 MP4 文件
ffmpeg -rtsp_transport tcp -i rtsp://<device_ip>:554/live/0 \
       -c copy -t 60 output.mp4
```

## 性能指标

在 RV1106 上的典型性能：

| 分辨率 | 帧率 | 码率 | CPU 占用 |
|--------|------|------|----------|
| 1920x1080 | 30fps | 4Mbps | ~15% |
| 1280x720 | 30fps | 2Mbps | ~10% |
| 640x480 | 30fps | 1Mbps | ~5% |

## 故障排除

### 端口被占用

```bash
# 检查端口使用情况
netstat -tlnp | grep 554

# 杀死占用端口的进程
kill -9 <pid>
```

### 无法连接

1. 确保设备和客户端在同一网络
2. 检查防火墙设置
3. 确认 RTSP 服务已启动

### 延迟过高

- 在 ffplay 中添加参数减少延迟：
  ```bash
  ffplay -fflags nobuffer -flags low_delay -rtsp_transport tcp \
         rtsp://<device_ip>:554/live/0
  ```

## 代码说明

### 关键步骤

1. **初始化系统** - `SystemManager::Initialize()`
2. **创建 VI 模块** - 配置摄像头采集参数
3. **创建 VENC 模块** - 配置 H.264/H.265 编码器
4. **创建 RTSP 模块** - 初始化 RTSP 服务器
5. **设置回调** - VENC 编码完成后推送到 RTSP
6. **硬件绑定** - VI -> VENC 零拷贝传输
7. **启动所有模块** - 开始推流

### 数据流

```
Camera Sensor
     │
     ▼ (ISP 处理)
VI 采集 (NV12)
     │
     ▼ (硬件绑定, 零拷贝)
VENC 编码 (H.264)
     │
     ▼ (回调函数)
RTSP 推送
     │
     ▼ (网络传输)
客户端播放
```

## 许可证

MIT License
