# RV1106 MediaGraph (rmg)

> 基于 RV1106 的高性能音视频处理库

一个为 Luckfox Pico (RV1106) 设计的现代化 C++17 音视频处理框架，采用**组件化管道模型**和**零拷贝架构**，提供简洁易用的 API。

## 🎯 设计目标

- **零拷贝**：利用 RK MPI 的硬件绑定机制，实现数据在硬件模块间直接传输
- **值语义**：使用 `std::variant` 和 `std::optional` 避免堆分配，缓存友好
- **现代 C++**：充分利用 C++17 特性（`std::variant`、`std::optional`、`[[nodiscard]]` 等）
- **RAII**：资源自动管理，无需手动释放
- **高性能**：静态多态取代虚函数，减少运行时开销

---

## 📐 架构设计

### 核心概念

```
┌─────────────────────────────────────────────────────────────────┐
│                        Pipeline (管道管理器)                      │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────┐    Hardware     ┌──────────┐    Callback    ┌────────┐
│  │ VideoCapture │───Bind────▶│ VideoEncoder │─────────────▶│  Sink  │
│  │   (VI/ISP)   │   (Zero-Copy)  │   (VENC)   │             │ (RTSP) │
│  └──────────┘                └──────────┘               └────────┘
│       │                           │                          │
│       │  GetFrame()               │  EncodedCallback         │
│       ▼                           ▼                          ▼
│  ┌──────────┐               ┌──────────┐              ┌──────────┐
│  │ YuvFrame │               │EncodedFrame│             │  Network │
│  │(值语义)  │               │ (值语义)   │              └──────────┘
│  └──────────┘               └──────────┘                        
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
                              │
                    ┌─────────┴─────────┐
                    │   SystemManager   │
                    │     (单例)        │
                    │  引用计数管理 MPI   │
                    └───────────────────┘
```

### 数据类型设计

```
MediaFrame = std::variant<YuvFrame, EncodedFrame>

YuvFrame        - YUV 原始帧（封装 VIDEO_FRAME_INFO_S）
EncodedFrame    - 编码后的数据包（封装 VENC_STREAM_S）

OptionalYuvFrame     = std::optional<YuvFrame>
OptionalEncodedFrame = std::optional<EncodedFrame>
```

### 模块类型

```
MediaModule (抽象基类)
├── VideoCapture    - VI/ISP 视频采集
├── VideoEncoder    - VENC 视频编码
├── VideoDecoder    - VDEC 视频解码 (计划中)
├── RgaProcessor    - RGA 图形加速 (计划中)
└── RtspSink        - RTSP 推流 (计划中)
```

---

## 🔧 核心组件

### 1. MediaFrame - 基于 std::variant 的数据载体

使用 `std::variant` 实现静态多态，避免虚函数开销：

```cpp
// YUV 帧 - 值语义，自动释放
if (auto frame = capture.GetFrame(1000)) {
    void* data = frame->GetVirAddr();      // CPU 访问
    uint64_t phy = frame->GetPhyAddr();    // 硬件加速器访问
    
    // 零拷贝与 OpenCV 联动
    cv::Mat yuv(frame->GetVirHeight() * 3 / 2, 
                frame->GetVirWidth(), 
                CV_8UC1, 
                frame->GetVirAddr());
}  // frame 离开作用域，自动释放 MPI 资源

// 使用 std::visit 处理多态帧
MediaFrame frame = ...;
std::visit(overloaded{
    [](YuvFrame& yuv) { /* 处理 YUV */ },
    [](EncodedFrame& enc) { /* 处理编码数据 */ }
}, frame);
```

### 2. MediaModule - 组件基类

所有处理模块的统一接口：

```cpp
class MediaModule {
public:
    [[nodiscard]] virtual bool Initialize() = 0;
    [[nodiscard]] virtual bool Start() = 0;
    virtual void Stop() = 0;
};
```

### 3. SystemManager - 系统管理

单例模式管理 MPI 系统生命周期，解决多模块共享问题：

```cpp
// 自动管理方式（推荐）
{
    rmg::SystemGuard guard;  // 构造时初始化
    // ... 使用 MPI
}  // 析构时自动反初始化（引用计数归零时）

// 手动管理方式
rmg::SystemManager::GetInstance().Initialize();
// ...
rmg::SystemManager::GetInstance().Deinitialize();
```

### 4. Pipeline - 管道管理

管理模块注册和绑定关系：

```cpp
rmg::Pipeline pipeline;

// 注册模块
pipeline.RegisterModule("capture", capture);
pipeline.RegisterModule("encoder", encoder);

// 硬件绑定（零拷贝）
pipeline.BindHardware(capture->GetEndpoint(), encoder->GetEndpoint());

// 统一生命周期管理
pipeline.InitializeAll();
pipeline.StartAll();
// ...
pipeline.StopAll();
```

---

## 📖 使用示例

### 基础采集

```cpp
#include "rmg.hpp"

int main() {
    // 配置
    rmg::VideoCapture::Config config;
    config.width = 1920;
    config.height = 1080;
    
    // 创建采集模块
    rmg::VideoCapture capture(config);
    
    if (!capture.Initialize()) {
        return -1;
    }
    
    // 轮询模式获取帧（返回 std::optional<YuvFrame>）
    if (auto frame = capture.GetFrame(1000)) {
        if (frame->IsValid()) {
            void* data = frame->GetVirAddr();
            size_t size = frame->GetDataSize();
            // 处理帧...
        }
    }  // frame 自动释放
    
    return 0;
}
```

### VI → VENC 硬件绑定（零拷贝编码）

```cpp
#include "rmg.hpp"

int main() {
    // 创建采集模块
    rmg::VideoCapture::Config cap_cfg;
    cap_cfg.width = 1920;
    cap_cfg.height = 1080;
    auto capture = std::make_shared<rmg::VideoCapture>(cap_cfg);
    
    // 创建编码模块
    rmg::VideoEncoder::Config enc_cfg;
    enc_cfg.width = 1920;
    enc_cfg.height = 1080;
    enc_cfg.codec = rmg::CodecType::kH264;
    enc_cfg.bitrate = 4000;
    auto encoder = std::make_shared<rmg::VideoEncoder>(enc_cfg);
    
    // 设置编码回调（值语义，接收 EncodedFrame）
    encoder->SetEncodedDataCallback([](rmg::EncodedFrame frame) {
        if (frame.IsKeyFrame()) {
            SPDLOG_INFO("Got IDR frame, size: {}", frame.GetDataSize());
        }
        // 推送到 RTSP/WebRTC...
    });  // frame 移动进入回调，离开时自动释放
    
    // 创建管道
    rmg::Pipeline pipeline;
    pipeline.RegisterModule("capture", capture);
    pipeline.RegisterModule("encoder", encoder);
    
    // 硬件绑定（数据不经过 CPU）
    pipeline.BindHardware(capture->GetEndpoint(), encoder->GetEndpoint());
    
    // 启动
    pipeline.InitializeAll();
    pipeline.StartAll();
    
    // 运行...
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // 停止（自动解绑和资源释放）
    pipeline.StopAll();
    
    return 0;
}
```

---

## 🏗️ 项目结构

```
luckfox-pico-ipc-examples/
├── CMakeLists.txt              # 根 CMake 配置
├── toolchain-luckfox-pico.cmake # 交叉编译工具链
├── README.md                   # 本文档
│
├── common/                     # RV1106 MediaGraph 核心库
│   ├── CMakeLists.txt
│   ├── rmg.hpp                # 统一头文件
│   │
│   ├── MediaFrame.hpp/cpp     # 媒体帧（std::variant 实现）
│   ├── MediaModule.hpp        # 模块基类
│   ├── SystemManager.hpp/cpp  # 系统管理单例
│   ├── Pipeline.hpp/cpp       # 管道管理器
│   │
│   ├── VideoCapture.hpp/cpp   # VI/ISP 采集模块
│   └── VideoEncoder.hpp/cpp   # VENC 编码模块
│
├── 01_isp_vi_capture/         # 示例：基础采集
├── 02_vi_venc_rtsp/           # 示例：编码推流 (计划中)
├── 03_opencv_detection/       # 示例：OpenCV 目标检测 (计划中)
│
└── third_party/               # 第三方依赖
    ├── luckfox_pico_rkmpi_example/
    ├── spdlog/
    └── libdatachannel/
```

---

## 🔑 设计亮点

### 1. std::variant 静态多态

**为什么选择 `std::variant` 而非抽象基类？**

| 特性 | 抽象基类 | `std::variant` |
|------|----------|----------------|
| 多态类型 | 运行时（虚函数表） | 编译时（std::visit） |
| 内存布局 | 堆分配，碎片化 | 栈分配，连续 |
| 性能 | 虚函数调用开销 | 无虚函数，查表分发 |
| 扩展性 | 开放（可派生） | 封闭（需修改定义） |

RV1106 场景下的优势：
- **避免内存碎片**：高帧率下频繁 `new/delete` 导致碎片
- **缓存友好**：连续内存布局提高 CPU 缓存命中率
- **类型固定**：硬件输出类型明确（VI→YUV，VENC→H264）

```cpp
// std::variant 定义
using MediaFrame = std::variant<YuvFrame, EncodedFrame>;

// 使用 overloaded 模式处理
std::visit(overloaded{
    [](YuvFrame& yuv) { /* ... */ },
    [](EncodedFrame& enc) { /* ... */ }
}, frame);
```

### 2. 零拷贝实现

**硬件级零拷贝**：通过 `RK_MPI_SYS_Bind` 实现模块间数据直传

```cpp
// VI 直接绑定到 VENC，数据不经过 CPU
pipeline.BindHardware(capture->GetEndpoint(), encoder->GetEndpoint());
```

**软件级零拷贝**：通过移动语义和自定义删除器管理帧生命周期

```cpp
// YuvFrame 使用回调函数释放，值语义移动传递
auto release_cb = [pipe, chn](VIDEO_FRAME_INFO_S* frame) {
    RK_MPI_VI_ReleaseChnFrame(pipe, chn, frame);
};
return YuvFrame(frame_info, release_cb);  // 移动返回
```

### 3. C++17 现代特性

| 特性 | 应用场景 |
|------|----------|
| `std::variant` | 静态多态帧容器 |
| `std::optional` | 可失败的帧获取 |
| `[[nodiscard]]` | 强制检查返回值 |
| `std::string_view` | 零拷贝字符串传递 |
| 结构化绑定 | 简化配置解析 |
| `overloaded` 模式 | std::visit 辅助 |

### 4. RAII 资源管理

```cpp
// SystemGuard 自动管理 MPI 系统
class SystemGuard {
public:
    SystemGuard() { SystemManager::GetInstance().Initialize(); }
    ~SystemGuard() { SystemManager::GetInstance().Deinitialize(); }
};

// YuvFrame 析构时自动释放
~YuvFrame() {
    if (is_valid_ && release_cb_) {
        release_cb_(&frame_info_);
    }
}
```

---

## 🚀 构建方法

### 交叉编译

```bash
# 配置
cmake --preset debug  # 或 release

# 编译
cmake --build --preset debug
```

### 编译选项

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `SPDLOG_ACTIVE_LEVEL` | 编译时日志级别 (0-6) | 2 (INFO) |

---

## 📋 模块开发指南

### 添加新模块

1. 继承 `MediaModule` 基类
2. 实现 `Initialize()`、`Start()`、`Stop()` 接口
3. 定义 `GetEndpoint()` 用于硬件绑定
4. 使用值语义回调传递帧数据

```cpp
class MyProcessor : public MediaModule {
public:
    explicit MyProcessor(const Config& config)
        : MediaModule("MyProcessor", ModuleType::kProcessor) {}
    
    [[nodiscard]] bool Initialize() override { /* ... */ }
    [[nodiscard]] bool Start() override { /* ... */ }
    void Stop() override { /* ... */ }
    
    // 值语义回调
    void SetYuvFrameCallback(YuvFrameCallback callback) {
        yuv_callback_ = std::move(callback);
    }
    
private:
    YuvFrameCallback yuv_callback_;
};
```

---

## 🗺️ 路线图

- [x] 核心框架（MediaFrame、MediaModule、Pipeline）
- [x] std::variant 静态多态
- [x] VideoCapture（VI/ISP 采集）
- [x] VideoEncoder（VENC 编码）
- [ ] RgaProcessor（RGA 缩放/格式转换）
- [ ] VideoDecoder（VDEC 解码）
- [ ] RtspSink（RTSP 推流）
- [ ] WebRtcSink（WebRTC 推流）
- [ ] OpenCV 集成示例
- [ ] JSON 配置文件支持

---

## 📄 许可证

MIT License

## 🙏 致谢

- [Rockchip](https://www.rock-chips.com/) - RV1106 SDK
- [Luckfox](https://www.luckfox.com/) - Luckfox Pico 开发板
- [spdlog](https://github.com/gabime/spdlog) - 高性能日志库
