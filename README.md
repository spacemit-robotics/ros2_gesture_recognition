# gesture_recognition

## 项目简介

ROS2 手势识别节点，使用 YOLOv5 手势模型，基于 `vision_service.h`（`model_zoo/vision`）实现实时手势识别功能。

## 功能特性

- 支持实时手势识别
- 基于 YOLOv5 模型
- 输出手势类别和位置
- 可视化调试图像
- 不支持：动态手势序列识别

## 快速开始

### 环境准备

- ROS2 Humble 或更高版本
- 已编译的 `components/model_zoo/vision` 组件
- 对应的模型文件

### 构建编译

```bash
colcon build --packages-select gesture_recognition
source install/setup.bash
```

### 运行示例

```bash
ros2 launch gesture_recognition gesture_recognition.launch.py
```

## 详细使用


### 依赖

- `components/model_zoo/vision`：提供 `libvision.so` 与 `vision_service.h`
- 手势识别模型：默认由 `config/yolov5_gesture.yaml` 指定

### 话题

| 类型 | 话题（默认） | 说明 |
|------|--------------|------|
| 订阅/发布 | `/camera/image_raw` | use_camera=true 时发布，否则订阅 |
| 发布 | `/perception/gestures` | Detection2DArray（需 vision_msgs） |
| 发布 | `/gesture_recognition/boxes` | Float32MultiArray，每手势 7 个数：x1,y1,x2,y2,score,label,track_id |
| 发布 | `/gesture_recognition/debug_image` | 带框与标签的可视化图 |

### 配置

- 默认配置：`config/yolov5_gesture.yaml`
- 默认模型路径：`~/.cache/models/vision/yolov5/yolov5_gesture.q.onnx`
- 若 model_zoo 中手势模型类名不同，请在 yaml 中修改 `class`
- 常用参数：`use_camera`、`camera_id`、`camera_fps`、`image_topic`


## 常见问题

- 若结果为空，先检查输入图像与模型路径是否正常
- 若从外部话题送图，请确认 `use_camera:=false`

参数 `use_camera`、`camera_id`、`camera_fps`、`image_topic` 同其他 perception 节点。

## 版本与发布

当前版本：1.0.0

变更记录：
- 初始版本发布

## 贡献方式

欢迎提交 Issue 和 Pull Request。

贡献者与维护者名单见：`CONTRIBUTORS.md`（如有）

## License

本组件源码文件头声明为 Apache-2.0，最终以本目录 `LICENSE` 文件为准。
