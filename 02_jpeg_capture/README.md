# 02_jpeg_capture - JPEG 图像捕获示例

## 功能说明

本示例演示如何在 RV1106 平台上：
1. 使用 `VideoCapture` 从摄像头采集 YUV 帧
2. 使用 `VideoEncoder` 将 YUV 帧编码为 JPEG 图像
3. 将 JPEG 图像保存到本地文件

支持两种拍照模式：
- **单次/多次拍照**：拍摄指定数量的照片后退出
- **连续拍照**：按指定间隔持续拍照直到用户中断

## 编译

```bash
# 在项目根目录下
mkdir -p build && cd build
cmake --preset debug
cmake --build build/Debug --target JpegCapture
```

## 运行

### 基本用法

```bash
# 单次拍照（默认 1920x1080，质量 80）
./JpegCapture

# 拍摄 5 张照片，每 2 秒一张
./JpegCapture -n 5 -i 2

# 连续拍照模式，每 5 秒一张
./JpegCapture -c -i 5

# 自定义分辨率和质量
./JpegCapture -w 1280 -h 720 -q 90

# 指定输出目录
./JpegCapture -o /tmp/photos
```

### 命令行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-w <width>` | 图像宽度 | 1920 |
| `-h <height>` | 图像高度 | 1080 |
| `-n <count>` | 拍摄数量 | 1 |
| `-i <interval>` | 拍照间隔（秒） | 2 |
| `-q <quality>` | JPEG 质量（1-99） | 80 |
| `-k <skip>` | 跳过的热身帧数量 | 30 |
| `-d <delay>` | 初始化后延时（秒） | 1 |
| `-o <path>` | 输出目录 | 可执行文件所在目录 |
| `-c` | 连续拍照模式 | 否 |
| `-v` | 详细日志输出 | 否 |
| `--help` | 显示帮助信息 | - |

## 输出文件

JPEG 文件命名格式：`YYYYMMDD_HHMMSS_WIDTHxHEIGHT.jpg`

例如：`20260129_143052_1920x1080.jpg`

## 技术细节

### 工作流程

```
摄像头 (VI) → YUV 帧 → JPEG 编码器 (VENC) → JPEG 文件
```

### 与 H.264/H.265 编码的区别

- JPEG 编码使用 `CodecType::kJPEG`，为单帧编码模式
- 需要通过 `StartRecvFrame(1)` 显式启动每次编码
- 不需要码率控制，仅设置质量参数

### 关于 AE（自动曝光）

摄像头启动后需要一定时间进行自动曝光调整，初始帧可能较暗。程序默认：
- 延时 1 秒等待 AE 初始化
- 跳过前 30 帧热身帧

可通过 `-d` 和 `-k` 参数调整。

## 查看 JPEG 文件

```bash
# 使用 ImageMagick
display photo.jpg

# 使用 feh
feh photo.jpg

# 使用 eog (GNOME)
eog photo.jpg
```

## 注意事项

1. 确保 `/etc/iqfiles` 目录存在且包含正确的 IQ 文件
2. 确保 `/dev/video11` 设备存在
3. 输出目录需要有写入权限
4. JPEG 质量越高，文件越大，编码时间越长
