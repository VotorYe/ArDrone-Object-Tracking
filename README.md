### <span id="jump">项目概述</span>
本项目是对四轴飞行机器人`AR.Drone2.0`的二次开发。目标是实现飞行器对物体的自动跟踪。
`AR.Drone2.0`是`Parrot`公司开发的一款四轴飞行器，提供一套可用于二次开发的SDK供开发者。基础的功能包括：
 - 自飞行器向控制端发送压缩视频流 （参考官方文档7.2）
 - 自飞行器向控制端发送飞行器状态数据 （参考官方文档7.1）
 - 自控制端向飞行器发送控制指令 （参考官方文档4.1）

`AR.Drone2.0`的[首页](http://www.parrot.com/zh/products/ardrone-2/)。

### 项目目标
使用跟随算法识别视频流中预先选定的物体，根据物体相对飞行器的位置，自动发送控制指令控制飞行器，保持物体在飞行器视野中心。

### 库和插件
- [`AR.Drone2.0 SDK`][1] 官方套件，功能见[项目概述](#jump)。
- [`GTK`][2] 图形工具包，用于为这个软件添加简单的控制按钮。
- [`opencv`][3] 计算机视觉库，提供图形相关的大量接口，极大提高我们工作效率，无需重复造轮子。

### 算法和编程
- [`Camshift`][4] 检测算法，用于跟踪预先选定的物体。
- `Linux进程间通信`，由于官方套件和opencv一同编译时出现多个难以解决的问题，我们决定分开编译，运行两个进程。进程1发送控制指令，进程2分析图像返回结果。因此需要用到进程间通信，传递图像和结果相关的数据。使用到：
  - 信号量
  - 内存共享
- 一个Naive的<span id="jump2">跟踪方案</span>
  - 目标：保持物体在视野中心
  - ![实例图片][5]
  - 参数
    - `x`： 盒子中心宽度上的坐标（以左起为0）
    - `y`： 盒子中心高度上的坐标（以上起为0）
    - `width`，`height`：盒子的宽高
    - `area` ： 盒子的面积
  - 当前物体的盒子的参数： 
    `[x_bounding, y_bounding, width_bounding_box, height_bounding_box]` （图中绿色线和框）
  - 理想状态的参数：
   （这里假定理想面积为总面积30%，实际可调）
    ```
    x_ideal = image_width / 2 //红色线
    y_ideal = image_height / 2 //红色线
    area_ideal = image_width * image_height * 0.3 //粉框
    ```
  - 根据当前状态和理想状态计算差值：
    ```
    x_err = x_bounding - x_ideal; //橙色线
    y_err = y_bounding - y_ideal; //橙色线
    z_err = area_ideal - (width_bounding_box * height_bounding_box)
    ```
  - 根据差值调整飞行器方向和速度。

### 开发步骤概要：
下面的步骤基于我们小组思考的解决方案，请结合自己实际参考。
- 添加`takeOff`，`land`，`tracking`，`stop`的按钮方便调试。
- 从视频流中获取图像：
    这里官方套件提供了接口，请参考官方示例代码`video_demo`。
- 将图像数据传送到处理图像的进程中。用到[进程间通信][6]。
- 将图像转换为Mat格式以使用opencv的接口。这里感谢另一个小组提供的方案。
- 分析图像。用到Camshift算法识别到目标物体，并用到上面的[追踪算法](#jump2)获取到`差值`，返回数据给控制进程。
- `差值`映射为控制指令的数据，发送给飞行器。根据实际效果调整映射的参数。

### 部署教程
以下是Linux下的部署教程：
- 依赖
  - 确保你的电脑安装了[`AR.Drone2.0 SDk2`][7]（[编译错误怎么办][8]）
  - 确保你的电脑安装了GTK，cmake，make
- 编译
  - 进入`imageProcess`文件夹，执行：
  ```
  cmake .
  make
  ```
  - 进入`Control`文件夹，执行：
  ```
  make
  ```
- 执行
  - 进入`Build`文件夹，执行：
  ```
  ./tracking
  ```
  - 进入`imageProcess`，执行：
  ```
  ./imageProcess
  ```
  - 在出现的面板上点击`takeOff`。
  - 确保你在视频上选择了目标后（被追踪的目标被红框框住），点击`tracking`，飞行器开始自动跟踪。
  - 点击`stop`，飞行器会处于悬浮状态，点击`land`以使飞行器降落。也可直接点击`land`。
- 参数`ini_area`，表示目标在返回图像中占据的面积（像素）。

### 经验和总结
- 安装`AR.Drone2.0 SDk`出现编译错误。这个套件是为linux 32bit开发，在64bit上需要安装一些依赖。[参考][9]。同时我们的一位组员的ubuntu 15.xx在安装依赖后依然无法编译套件，建议使用ubuntu 14.xx做二次开发。
- 将图像数据转化为`IplImage`格式：
```
if (src == NULL)
   src = cvCreateImage(cvSize(width,height), IPL_DEPTH_8U, 3);
if (dst == NULL)
   dst = cvCreateImage(cvSize(width,height), IPL_DEPTH_8U, 3);
src->imageData = (char*)realdata; // realdata 解码后的图像数据
cvCvtColor(src, dst, CV_RGB2BGR);
image = cv::cvarrToMat(src);
```
这里需要注意realdata应当为rgb888格式，解码后的数据格式依据设置而定，如果为其他格式需要转为rgb888格式才能应用以上代码。比如我们发送过来的是rgb565格式，我们自己写了一段代码进行转换。
- 参考过`video_deom`里面的并行代码后。我们觉得在自己的代码中应用线程的知识很有价值。在处理图像的步骤中，我们使用一个线程独立拷贝共享内存中的数据，另一个线程负责处理图像。这样避免了拷贝和处理串行时间较长而导致整体的处理速度下降。
- 为了提供简单的控制，我们使用GTK写了几个基础按钮，有兴趣的朋友可以添加更多的控制按钮。
- 进程间通信也是很有趣的内容。

### 参考文献
以下是上图中提供的链接的列表：
  Parrot官网: http://developer.parrot.com/products.html
  [GTK官网]: http://www.gtk.org/
  [opencv官网]: http://opencv.org/
  [camshift算法]: http://docs.opencv.org/3.1.0/db/df8/tutorial_py_meanshift.html#gsc.tab=0
  [进程间通讯wikiPedia]: https://zh.wikipedia.org/wiki/%E8%A1%8C%E7%A8%8B%E9%96%93%E9%80%9A%E8%A8%8A
  [SDk编译问题]: http://forum.developer.parrot.com/t/installing-sdk-2-0-1-problem/1483/7

  [1]: http://developer.parrot.com/products.html
  [2]: http://www.gtk.org/
  [3]: http://opencv.org/
  [4]: http://docs.opencv.org/3.1.0/db/df8/tutorial_py_meanshift.html#gsc.tab=0
  [5]: http://i.stack.imgur.com/4nmex.jpg "实例图片"
  [6]: https://zh.wikipedia.org/wiki/%E8%A1%8C%E7%A8%8B%E9%96%93%E9%80%9A%E8%A8%8A
  [7]: http://developer.parrot.com/products.html
  [8]: http://forum.developer.parrot.com/t/installing-sdk-2-0-1-problem/1483/7
  [9]: http://forum.developer.parrot.com/t/installing-sdk-2-0-1-problem/1483
