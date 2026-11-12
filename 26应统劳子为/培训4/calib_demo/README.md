# 培训4相机标定

OpenCV C++ 棋盘格相机标定示例。程序按 7×10 内角点检测棋盘，亚像素细化后用全部成功图像估计内参和畸变系数；随后对目录内每张可读且尺寸一致的图像执行矫正。标定角点图和矫正图分别保存在 `results/corners/` 与 `results/corrected/`，参数及矫正前后示例也写入 `results/`。原始数据目录作为命令行参数传入，不要把原始图像提交到仓库。

```bash
cmake -S . -B build
cmake --build build -j
./build/calib_demo /path/to/cam_mix_img
# 可选：显示矫正窗口
./build/calib_demo /path/to/cam_mix_img --show
```

本次数据：113 张 640×480 图像，96 张检测到棋盘角点并用于标定；RMS 重投影误差约 1.4278 px。标定输出单位取决于方格边长；因未提供实体格长，程序采用相对单位。程序会对可读且分辨率一致的图像逐张矫正。
