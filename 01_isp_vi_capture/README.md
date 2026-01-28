该例程实现：
驱动CSI摄像头，利用RK的库，实现经过ISP采集原始YUV帧

初始化后延时，等待AE收敛
跳过前面的热身帧，只保留后面的正常帧

1. 初始化相机
2. 等待 1 秒（-d 参数）让 ISP 启动
3. 跳过 30 帧（-k 参数）让 AE 收敛
4. 开始真正采集 50 帧（-n 参数）
5. 保存第一帧（-s 参数）到 YUV 文件

```bash
# 采集 5 帧并保存第一帧
./CameraCapture -n 5 -s

# 指定输出目录
./CameraCapture -n 5 -s -o /tmp

# 启用详细日志
./CameraCapture -n 5 -s -v

# 自定义参数
./CameraCapture -w 1920 -h 1080 -n 50 -k 30 -d 1 -s

./CameraCapture -w 3840 -h 2160 -n 10 -k 5 -d 1 -s

# 查看生成的 YUV 文件
ffplay -video_size 1920x1080 -pixel_format nv12 frame_1920x1080.yuv

ffplay -video_size 3840x2160 -pixel_format nv12 frame_3840x2160.yuv
```