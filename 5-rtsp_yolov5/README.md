运行时加载模型
`./model/yolov5.rknn`

编译后要执行安装
```bash
cmake --intall build/Debug
```
把得到的`rtsp_yolov5/`传输到设备上，以相对位置执行
```bash
cd rtsp_yolov5
./rtsp_yolov5
```