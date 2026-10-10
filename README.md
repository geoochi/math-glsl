# math-glsl

[![build](https://github.com/geoochi/math-glsl/actions/workflows/build.yml/badge.svg)](https://github.com/geoochi/math-glsl/actions/workflows/build.yml)

把数学函数渲染成帧精确视频。**一个 C 文件，零第三方库，零 Python 依赖**，macOS / Linux / Windows 都能直接跑。

```bash
git clone https://github.com/geoochi/math-glsl && cd math-glsl
make            # Windows: build.bat
./render_glsl   # → ./out.mp4：4K 60fps 10bit，2 秒，一个空的坐标系
```

默认输出就是一个**空的二维坐标系**（网格 + 坐标轴），在这上面加上你想要的那段 GLSL 逻辑，它就成了你的视频。

## 加上你的逻辑

默认着色器是 [`shaders/axes.glsl`](shaders/axes.glsl)，里面有一个标记清楚的区段：

```glsl
void main() {
    vec2  p     = toMath(gl_FragCoord.xy, res, UNITS_X);  // 本像素的数学坐标
    float pixel = UNITS_X / res.x;                        // 一个像素有多宽（数学单位）
    vec3  col   = coordinateSystem(p, pixel);             // 已画好的空坐标系

    // ================== YOUR DRAWING GOES HERE ==================
    // 把数学写进 col 就行，例如把 p.x 换成 length(p - 中心) 就得到二维涟漪
    // ============================================================

    fragColor = vec4(vignette(col, gl_FragCoord.xy, res), 1.0);
}
```

`shaders/lib.glsl` 里已经备好了常用件，任何着色器都能 `#include "lib.glsl"`（渲染器自己实现了 `#include`，因为 core GLSL 没有这个指令）：

| 函数 | 作用 |
|---|---|
| `toMath(fragCoord, res, unitsWide)` | 像素坐标 → 数学坐标（原点居中、y 向上、等比） |
| `strokeMask(g, halfWidth, aa)` | 由隐式方程 `g` 得到抗锯齿描边 |
| `gridMask` / `axisMask` / `coordinateSystem` | 网格与坐标轴 |
| `heightRamp(y, amp)` | 按高度做青绿→紫色渐变 |
| `vignette(col, fragCoord, res)` | 四角压暗 |

想直接看一个完整例子：把 `shaders/axes.glsl` 里的 `#define DRAW_EXAMPLE` 取消注释，或者看 [`demo/wave/`](demo/wave/)。

## 演示

[`demo/wave/`](demo/wave/) 是一次完整的「prompt → 着色器 → 视频」：

| 文件 | 说明 |
|---|---|
| [`prompt.md`](demo/wave/prompt.md) | 生成这段视频用的 prompt |
| [`wave.glsl`](demo/wave/wave.glsl) | 它产出的着色器 |
| [`wave.mp4`](demo/wave/wave.mp4) | 渲染结果：4K 60fps 10bit，5 秒 300 帧 |

```bash
SHADER=demo/wave/wave.glsl ./render_glsl demo/wave/wave.mp4 3840 2160 60 5
```

## 命令行与环境变量

```bash
./render_glsl [输出] [宽] [高] [fps] [秒数]     # 默认 out.mp4 3840 2160 60 2
```

| 变量 | 作用 |
|---|---|
| `SHADER` | 换着色器，例如 `SHADER=demo/wave/wave.glsl` |
| `BITDEPTH=8` | 强制 8bit（默认 10bit；驱动不支持 R10G10B10A2 时会自动降级） |
| `NO_ENCODE=1` | 管道接 `/dev/null`，用来判断耗时里有多少是编码器反压 |
| `FFMPEG` | ffmpeg 可执行文件路径 |
| `ENCODER` | 编码器，默认 `libx264` |
| `ENCODE_ARGS` | 质量参数，默认 `-crf 16 -preset slow`；硬件编码器改成 `-q:v 60` |

硬件编码示例（快很多，10bit 需要编码器支持）：

```bash
ENCODER=hevc_videotoolbox ENCODE_ARGS="-q:v 60" ./render_glsl out.mp4 3840 2160 60 2
```

## 环境要求

| 需要 | 说明 |
|---|---|
| C 编译器 | macOS 自带 clang；Linux 装 gcc；Windows 装 MSVC 或 mingw |
| 图形头文件 | macOS 自带；Linux 装 `libegl1-mesa-dev libgl1-mesa-dev`；Windows 自带（Windows SDK） |
| **ffmpeg** | 唯一的外部依赖，需在 PATH 上：`brew install ffmpeg` / `apt install ffmpeg` / `winget install ffmpeg` |

## 它是怎么工作的

```
每帧：
  glUniform1f(uTime, i / fps)        ← 虚拟时钟：第 i 帧永远是 i/fps 秒
  glDrawArrays(全屏三角形 3 个顶点)    ← 没有顶点缓冲，靠 gl_VertexID 生成
  glReadPixels                       ← 从离屏 FBO 读回 10bit 打包像素
  fwrite 到 ffmpeg 的 stdin           ← 管道编码
```

**时间由 `i / fps` 给出，与真实时钟无关**，所以输出帧精确、绝不丢帧，机器快慢不影响结果。

**色彩管线（10bit）**：`R10G10B10A2` 渲染缓冲 → `glReadPixels` 取 `GL_UNSIGNED_INT_2_10_10_10_REV` 的打包 32 位字 → ffmpeg 当作 `x2bgr10le` → `libx264 -profile:v high10 / yuv420p10le`。
注意是 **bgr** 那个格式：该 GL 类型把红色放在**低** 10 位，所以从高位往下读是 BGR。写成 `x2rgb10le` 会把红蓝对调——这条是采样像素量出来的，不是照着 spec 猜的。

三平台的上下文后端在同一份源码里，只有约 60 行不同：

| 平台 | 上下文 | 链接参数 |
|---|---|---|
| macOS | CGL | `-framework OpenGL` |
| Linux | EGL（全无头，不需要 X/Wayland） | `-lEGL -lGL` |
| Windows | WGL | `opengl32.lib gdi32.lib user32.lib` |

## 性能

4K 60fps 一帧（Apple M1 Pro，10bit）：GPU 着色 **3.4 ms**、`glReadPixels` **3.3 ms**、H.264 编码 **14.7 ms**，整帧约 **25 ms**。
注意 **4K 下编码成了绝对瓶颈（约 60%）**——想快就换硬件编码，而不是动着色器。

## 作为 AI skill 使用

仓库根目录就是一份可加载的 skill（[`SKILL.md`](SKILL.md)），包含硬性规范、环境变量速查、排错表，以及两篇参考：

- [`references/platforms.md`](references/platforms.md) — 三平台构建参数与平台层的具体坑（`opengl32.dll` 只导出 GL 1.1、EGL 的 surfaceless 平台、CGL 的软件渲染回退）
- [`references/shader-writing.md`](references/shader-writing.md) — GLSL 写作与抗锯齿手法，以及移植到 WGSL/MSL 的对照表

## CI

GitHub Actions 在三个 runner 上编译三个平台后端（编译是**硬性要求**），并各自尝试跑一条 1 秒冒烟视频：

| runner | 编译 | 渲染 |
|---|---|---|
| `ubuntu-latest` | ✅ EGL | ✅ llvmpipe 软件渲染（Mesa 4.5 core） |
| `macos-latest` | ✅ CGL | ✅ Apple 软件渲染器（GL 4.1 core） |
| `windows-latest` | ✅ WGL / MSVC | ❌ 云 VM 只有 GDI Generic 渲染器（GL 1.1），跑不了 4.1 core |

渲染成功的 runner 会把视频传到 Artifacts。

## License

MIT
