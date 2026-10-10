---
name: math-shader-video
description: 用一个零依赖的原生 C 程序把数学/物理函数用 GLSL 着色器渲染成帧精确 MP4 视频（默认 4K 60fps 10bit），支持 macOS、Linux、Windows 三个平台（CGL / EGL / WGL 三种上下文后端在同一份源码里）。默认输出是一个空的二维坐标系，在其上加一段 GLSL 逻辑即可变成用户想要的视频。触发词：GLSL、着色器、shader 出片、数学函数动画、波函数、GPU 渲染视频、GPU 出片、场可视化、offscreen render、坐标系统动画、f(x,t) 可视化、4K 10bit 渲染。
agent_created: true
---

# 数学函数 GPU 着色器视频（math-shader-video）

## 何时用

用户想把**一个数学/物理函数渲染成视频**：`y = f(x, t)` 的波形、场、涟漪、干涉、分形。
典型说法："用 GPU 渲染 y=sin(x+t) 的动画""把波包做成视频""4K 60fps 10bit 输出流体模拟""GLSL 出片"。

**不要用于**：需要精确文字/公式排版（matplotlib 更合适）、需要 scipy 数值求解的仿真、只出一张静态图。

**本 skill 的特色：纯 C，零第三方库，零 Python 依赖。** 唯一外部依赖是 `ffmpeg`。

---

## 一、30 秒跑起来

```bash
git clone https://github.com/geoochi/math-glsl && cd math-glsl
make                       # Windows 用 build.bat
./render_glsl              # → ./out.mp4：4K 60fps 10bit，2 秒，一个空的坐标系
```

**默认输出就是一块"空画布"**（网格 + 坐标轴）。用户要的视频 = 在这块画布上加一段 GLSL。

---

## 二、加用户逻辑的标准流程

1. 复制 `shaders/axes.glsl` 成新的着色器文件（例如 `shaders/mine.glsl`）。
2. 在 `// ===== YOUR DRAWING GOES HERE =====` 与 `// ===== END OF YOUR DRAWING =====` 之间写数学。
3. `SHADER=shaders/mine.glsl ./render_glsl out/mine.mp4 3840 2160 60 5`

`shaders/lib.glsl` 提供了现成件，用 `#include "lib.glsl"` 引入（渲染器自己实现了 `#include`，因为 core GLSL 没有该指令；路径按**被包含文件**所在目录解析）：

| 函数 | 作用 |
|---|---|
| `toMath(fragCoord, res, unitsWide)` | 像素坐标 → 数学坐标（原点居中、y 向上、等比） |
| `strokeMask(g, halfWidth, aa)` | 由隐式方程 `g` 得到抗锯齿描边 |
| `gridMask(p)` / `axisMask(d, pixel)` / `coordinateSystem(p, pixel)` | 网格与坐标轴 |
| `heightRamp(y, amp)` | 按高度做青绿→紫色渐变 |
| `vignette(col, fragCoord, res)` | 四角压暗 |

着色器里可用的 uniform 只有三个：`uResolution`（vec2 像素尺寸）、`uTime`（float 秒）、输出 `fragColor`。

**完整范例**：`demo/wave/`（prompt + 着色器 + 视频），或把 `shaders/axes.glsl` 里的 `#define DRAW_EXAMPLE` 取消注释。

---

## 三、七条硬性规范（违反任何一条都会出 bug）

1. **虚拟时钟**：第 i 帧的时间必须是 `t = i / fps`，**绝不读真实时钟**。输出才帧精确、永不丢帧。
2. **着色器正文不含 `#version`**。宿主会加头：
   `#version 410 core` + `uniform vec2 uResolution; uniform float uTime; out vec4 fragColor;`
3. **上下方向只翻一次**。渲染器按 GL 约定产出（行 0 = 底部），只在 ffmpeg 那侧 `-vf vflip` 翻一次。
   翻两次 = 画面上下颠倒，而**亮度统计完全正常**，只看数值查不出来，必须抽帧看图。
4. **10bit 的像素格式是 `x2bgr10le`，不是 `x2rgb10le`**。`GL_UNSIGNED_INT_2_10_10_10_REV` 把红放**低** 10 位，从高位往下读是 BGR。写成 x2rgb10le 会让红蓝对调——这个坑只能靠**采样像素值**发现（对着纯色背景量一个点的 RGB，和设计值比对）。
5. **`GL_RGB10_A2` 存储要配 `GL_RGBA` + `UNSIGNED_INT_2_10_10_10_REV` 回读**；8bit 模式则是 `GL_RGB8` + `GL_RGB` + `UNSIGNED_BYTE`。两者不能混搭（会被驱动做格式转换，慢约 30%）。
6. **不要在 10bit 模式下用硬件 H.264。** Apple 的硬件 H.264 编码器只出 8bit，且会**静默降级**——实测 `h264_videotoolbox` 把 `yuv420p10le` 变成 `yuv420p`、profile 从 High 10 变成 High，画面不报错但 10bit 保证没了。要用硬件就选 HEVC（`hevc_videotoolbox` / `hevc_nvenc` / `hevc_qsv`），并在 MP4 里加 `-tag:v hvc1` 否则 QuickTime 不认。
7. **core profile 即使不用顶点属性也必须绑 VAO**，否则 `glDrawArrays` 静默无输出。
8. **回读后必须验帧**：`ffprobe` 确认 `nb_frames == fps * duration`、`duration` 精确、`profile` 与 `pix_fmt` 符合预期（10bit 应为 `Main 10`/`High 10` + `yuv420p10le`）；再抽一帧看图（顺便验证通道顺序）。**编码器是否真的保住了位深，只能靠 ffprobe 验，不能靠"没报错"判断。**

---

## 四、命令行与环境变量

```bash
./render_glsl [输出] [宽] [高] [fps] [秒数]     # 默认 out.mp4 3840 2160 60 2
```

| 变量 | 作用 |
|---|---|
| `SHADER` | 换着色器：`SHADER=demo/wave/wave.glsl ./render_glsl w.mp4` |
| `BITDEPTH=8` | 强制 8bit（默认 10bit；驱动不支持 `R10G10B10A2` 时自动降级并提示） |
| `HWENC=0` | 关掉硬件编码。启动探测会花 0.2–0.3 秒，短片段可跳过 |
| `NO_ENCODE=1` | 管道接 `/dev/null`，用来判断耗时里有多少是编码器反压 |
| `FFMPEG` | ffmpeg **可执行文件**路径（不是编码器） |
| `ENCODER` | 指定编码器（跳过自动探测） |
| `ENCODE_ARGS` | 质量参数，默认软件 `-crf 16 -preset slow`、硬件 `-q:v 60` |

---

## 五、排错表

| 现象 | 原因 | 解决 |
|---|---|---|
| **红蓝对调**（该蓝的地方发红） | 10bit 用了 `x2rgb10le` | 改成 `x2bgr10le`；用采样像素值验证 |
| 画面上下颠倒 | 翻转做了两次 | 检查只保留一处 `-vf vflip` |
| 全黑/只见背景 | 没绑 VAO；或 `glUseProgram` 没调用；或 uniform 名拼错（`glGetUniformLocation` 返回 -1 且 `glUniform*` 静默失败） | 绑一个空 VAO；打印 uniform 位置 |
| `fragment shader failed: undeclared identifier` | 把 `const` 改成 uniform 时改了名字 | **必须保留标识符名**：`const float X = 1.0;` → `uniform float X;` |
| 报错找不到着色器 | 不在仓库根目录运行 | 用 `SHADER=` 指定绝对路径，或在根目录运行 |
| `GL_RGB10_A2 is not renderable here` | 老驱动/软件渲染不支持 | 程序自动降 8bit；也可主动 `BITDEPTH=8` |
| `eglCreateContext failed`（Linux） | Mesa 太老 | 装 `libegl1-mesa-dev`，无 GPU 时加 `LIBGL_ALWAYS_SOFTWARE=1` |
| `CGLChoosePixelFormat failed`（macOS） | 强制要求了硬件加速 | 源码已处理：会自动退到软件渲染器 |
| `WGL_ARB_create_context missing`（Windows） | 显卡驱动太旧 | 更新驱动 |
| 慢（4K 尤其明显） | 编码占了 60% | 换 `ENCODER=hevc_videotoolbox` 等硬件编码 |

---

## 六、性能常识（避免错误优化）

| 场景 | 着色 | 回读 | 编码 | 整帧 |
|---|---|---|---|---|
| 1920×1080 8bit | ~2.3 ms | ~2.3 ms | ~2.4 ms | ~7.8 ms |
| 4K 10bit + libx264（静止画面） | 3.4 ms | 3.3 ms | 12.2 ms | ~25 ms |
| 4K 10bit + libx264（有运动） | 6.3 ms | 2.6 ms | 16.1 ms | ~30 ms |
| 4K 10bit + hevc_videotoolbox（有运动） | 5.1 ms | 1.7 ms | 11.4 ms | **~22 ms** |

**关键结论**

- **4K 下真正的瓶颈是"把 33 MB 一帧喂进编码器"本身**，不是编码器算法。纯管道容量测试（`NO_ENCODE=1` → `cat > /dev/null`）量到光写就要 **5.6 ms/帧**（约 5.9 GB/s 上限）。硬件编码器的价值主要是**省 CPU**（同内容 39.6 s → 1.6 s CPU 时间）和在有运动时快约 29%，而不是数量级的提速。
- **内容决定一切**：默认的空坐标系几乎静止，软件编码只要 12.2 ms/帧，硬件毫无优势；换成有运动的画面，软件涨到 16.1 ms 而硬件 11.4 ms。
- **优化顺序**：换硬件编码（自动，已默认）→ 少读字节（非必要不上 10bit）→ 绕开管道（进程内编码 / 共享内存）。**动手前先用 `NO_ENCODE=1` 量一下管道容量上限**，否则容易优化错地方。
- **别为了性能换宿主语言**：实测 C 与 Python 驱动同一着色器的差距是**每帧 0.9 微秒**（一帧只发 4 个 GL 调用）。

---

## 七、参考资料

| 文件 | 什么时候看 |
|---|---|
| `references/platforms.md` | 三平台构建/链接参数、平台层的具体坑（WGL 的函数加载、EGL 的 surfaceless、CGL 的软件渲染回退） |
| `references/shader-writing.md` | 写 GLSL 时的类型/精度/坐标系注意事项、抗锯齿手法，以及和 WGSL、MSL 的对照 |
| `demo/wave/prompt.md` | 一次完整的 prompt → 着色器 → 视频示例 |

## 八、验收清单

- [ ] `ffprobe`：`nb_frames == fps * duration`、`duration` 精确、`profile=High 10`、`pix_fmt=yuv420p10le`
- [ ] 抽帧看图：方向正确、通道顺序正确、坐标轴/网格正常、描边平滑
- [ ] 三平台 CI 全绿（`.github/workflows/build.yml`）
