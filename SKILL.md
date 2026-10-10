---
name: math-shader-video
description: 用一个零依赖的原生 C 程序把数学/物理函数用 GLSL 着色器渲染成帧精确 MP4 视频，支持 macOS、Linux、Windows 三个平台（CGL / EGL / WGL 三种上下文后端在同一份源码里）。触发词：GLSL、着色器、shader 出片、数学函数动画、波函数、GPU 渲染视频、GPU 出片、场可视化、offscreen render、坐标系统动画、f(x,t) 可视化。
agent_created: true
---

# 数学函数 GPU 着色器视频（math-shader-video）

## 何时用

用户想把**一个数学/物理函数渲染成视频**：`y = f(x, t)` 的波形、场、涟漪、分形、粒子。
典型说法："用 GPU 渲染 y=sin(x+t) 的动画""把波包做成视频""4K 60fps 输出流体模拟""GLSL 出片"。

**不要用于**：需要精确文字/公式排版（matplotlib 更合适）、需要 scipy 数值求解的仿真、只出一张静态图。

**本 skill 的特色：纯 C，零第三方库，零 Python 依赖。** 唯一外部依赖是 `ffmpeg`。

---

## 一、30 秒跑起来

```bash
git clone https://github.com/geoochi/math-glsl && cd math-glsl
make            # macOS/Linux；Windows 用 build.bat
./render_glsl   # → ./wave.mp4，1920x1080@60，5 秒
```

换分辨率和时长：`./render_glsl out/4k.mp4 3840 2160 120 10`

**改数学只需要改一处**：`shaders/wave.glsl` 里的 `waveValue(x, t)`。坐标系、网格、抗锯齿、追踪点全部自适应。
骨架模板见 `shaders/wave.glsl` 顶部的注释：波包、阻尼振荡、傅里叶方波都给了公式。

---

## 二、六条核心规范（违反任何一条都会出 bug）

1. **虚拟时钟**：第 i 帧的时间必须是 `t = i / fps`，**绝不读真实时钟**。这样输出与机器快慢无关、帧精确、永不丢帧。实时循环（游戏/网页）才用真实时钟。
2. **着色器正文不含 `#version`**。`shaders/wave.glsl` 只写正文（含 `main()`），由宿主加头：
   ```
   #version 410 core
   uniform vec2  uResolution;
   uniform float uTime;
   out vec4 fragColor;
   ```
   这样同一份正文可以喂给任何宿主，不用分叉。
3. **上下方向只翻一次**。渲染器内部按 GL 约定产出（行 0 = 底部），只在 ffmpeg 那侧 `-vf vflip` 翻一次。
   翻两次 = 画面上下颠倒，而**亮度统计完全正常**，只看数值查不出来，必须抽帧看图。
4. **`GL_RGB8` 存储必须配 `GL_RGB` 回读**。不配套（如 RGBA8 存储 + `GL_RGB` 回读）会触发驱动格式转换，慢约 30%。
5. **core profile 即使不用顶点属性也必须绑定 VAO**（`glGenVertexArrays` + `glBindVertexArray`），否则 `glDrawArrays` 静默无输出。
6. **回读后必须验帧**：`ffprobe` 确认 `nb_frames == fps * duration`、`duration` 精确、分辨率正确；再抽一帧看图。

---

## 三、环境变量速查

| 变量 | 作用 | 例子 |
|---|---|---|
| `SHADER` | 换着色器正文 | `SHADER=shaders/ripple.glsl ./render_glsl` |
| `NO_ENCODE=1` | 管道接 `/dev/null`，用来确认"管道耗时"里有多少是编码器反压 | `NO_ENCODE=1 ./render_glsl x.mp4` |
| `FFMPEG` | ffmpeg **可执行文件**路径 | `FFMPEG=/opt/homebrew/bin/ffmpeg ./render_glsl` |
| `ENCODER` | 编码器（默认 `libx264`） | `ENCODER=h264_videotoolbox` |
| `ENCODE_ARGS` | 质量参数（硬件编码器不支持 CRF） | `ENCODE_ARGS="-q:v 60"` |

命令行位置参数：`./render_glsl [输出] [宽] [高] [fps] [秒数]`。

---

## 四、改数学的心法

**不要写"循环每个像素"，要写"一个像素怎么算"。**

- 曲线抗锯齿用**屏幕空间导数**，不要手调线宽：
  `float gw = fwidth(g); alpha = 1.0 - smoothstep(hw-gw, hw+gw, abs(g));`
  其中 `g` 是曲线的隐式方程（曲线上恰为 0）。
- 线宽要**除以梯度模长**，否则陡峭处会变细：
  `float g = (p.y - f(p.x)) / sqrt(1.0 + fp*fp);`
- 网格/坐标轴同理：`vec2 gr = abs(fract(p - 0.5) - 0.5) / fwidth(p);` 得到"到最近整数格线的像素距离"，天然 1px 宽，任意分辨率下都锐利。
- 二维场（涟漪、波包）把 `p.x` 换成 `length(p)` 或 `length(p - 中心)` 即可，其余照旧。

---

## 五、排错表

| 现象 | 原因 | 解决 |
|---|---|---|
| 画面上下颠倒 | 翻转做了两次 | 检查 ffmpeg 只保留一处 `-vf vflip` |
| 画布/视频全黑或全背景色 | 没绑 VAO；或 `glDrawArrays` 前没 `glUseProgram` | 绑一个空 VAO 即可 |
| `fragment shader failed: undeclared identifier` | 把 `const` 改成 uniform 时改了名字 | **必须保留标识符名**：`const float X = 1.0;` → `uniform float X;` |
| 报错找不到着色器 | 不在仓库根目录运行 | 用 `SHADER=` 指定，或在根目录运行 |
| `eglCreateContext failed`（Linux） | Mesa 太老，或没有软件渲染兜底 | 装 `libegl1-mesa-dev`，加 `LIBGL_ALWAYS_SOFTWARE=1` |
| `WGL_ARB_create_context missing`（Windows） | 显卡驱动太旧 | 更新显卡驱动 |
| 视频比预期短/长 | 时长是浮点，取整方式 | `nb_frames == fps*duration` 向上取整 |
| 慢 | 见下节 | 先砍编码，再砍回读 |

---

## 六、性能常识（避免错误优化）

1920×1080 一帧的成本（Apple M1 Pro 实测）：GPU 着色 **7–10%**、`glReadPixels` 回读 **33%**、H.264 编码 **25–35%**，其余是内存拷贝。

**优化顺序**：先用硬件编码（`ENCODER` + `ENCODE_ARGS`）→ 再考虑异步回读（PBO）/ 对齐像素格式 → 最后才动着色器。
**别为了性能重写宿主语言**：实测同配置下 C 与 Python 驱动同一着色器的差距是**每帧 0.9 微秒**（一帧只发 4 个 GL 调用）。

---

## 七、参考资料

| 文件 | 什么时候看 |
|---|---|
| `references/platforms.md` | 三平台构建/链接参数、平台层的具体坑（WGL 的函数加载、EGL 的 surfaceless、CGL 的无窗口） |
| `references/shader-writing.md` | 写 GLSL 时的类型/精度/常量/坐标系注意事项，以及和 WGSL、MSL 的对照（如果以后要移植） |

## 八、验收清单

- [ ] `ffprobe` 的 `nb_frames == fps * duration`，`duration` 精确（如 5.000000）
- [ ] 抽帧看图：方向正确、坐标轴/网格正常、曲线抗锯齿平滑
- [ ] 三平台 CI 全绿（`.github/workflows/build.yml`）
