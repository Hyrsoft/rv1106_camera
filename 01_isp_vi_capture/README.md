该例程实现：
驱动CSI摄像头，利用RK的库，实现经过ISP采集原始YUV帧

```bash
# 采集 5 帧并保存第一帧
./CameraCapture -n 5 -s

# 指定输出目录
./CameraCapture -n 5 -s -o /tmp

# 启用详细日志
./CameraCapture -n 5 -s -v

# 查看生成的 YUV 文件
ffplay -video_size 1920x1080 -pixel_format nv12 frame_1920x1080.yuv
```