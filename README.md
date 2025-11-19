# 简介

基于opencv-mobile和Luckfox-pico开发板，提供一些功能验证或者工程模板

| 序号 | 名称                       | 描述                       | 备注                                                         |
| :--: | :------------------------- | -------------------------- | ------------------------------------------------------------ |
|  1   | base_template              | 捕获9张图片并进行处理      | 取自[luckfox wiki](https://wiki.luckfox.com/zh/Luckfox-Pico/Luckfox-Pico-RV1106/Luckfox-Pico-Ultra-W/Luckfox-Pico-opencv-mobile) |
|  2   | rtsp_opencv_mobile         | rtsp推流                   | 取自[幸狐提供的例程](https://github.com/LuckfoxTECH/luckfox_pico_rkmpi_example.git) |
|  3   | rtsp_opencv_mobile_capture | rtsp推流+帧率标注          | 取自[幸狐提供的例程](https://github.com/LuckfoxTECH/luckfox_pico_rkmpi_example.git) |
|  4   | fb_show                    | 捕获视频实时显示在fb设备上 | 默认分辨率为720x720                                          |
|. 5.  |rtsp_yolov5                 | 部署yolov5推理并推流.      | 取自[幸狐提供的例程](https://github.com/LuckfoxTECH/luckfox_pico_rkmpi_example.git) |

## 一、操作指南

### 获取opencv-mobile预编译包
获取4.10.0版本的适用于luckfox-pico的预编译包
```bash
./common/setup.sh
```
### 使用命令行编译
进入具体项目路径
```bash
# 调用预设的配置 (例如 Debug)
cmake --preset Debug

# 使用预设进行构建
cmake --build --preset Debug
```

### 使用scp传输文件
设置文件名和开发板ip地址
```bash
scp opencv-mobile-test root@192.168.31.91:/root
```
### 开发板
- luckfox-pico-max
- luckfox-pico-ultra-w

## 二、引用与致谢
- [nihui/opencv-mobile](https://github.com/nihui/opencv-mobile)
- [LuckfoxTECH/luckfox_pico_rkmpi_example.git](https://github.com/LuckfoxTECH/luckfox_pico_rkmpi_example.git)