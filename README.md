# math-glsl

[![build](https://github.com/geoochi/math-glsl/actions/workflows/build.yml/badge.svg)](https://github.com/geoochi/math-glsl/actions/workflows/build.yml)

把数学函数渲染成帧精确视频。**一个 C 文件，零第三方库，零 Python 依赖**，macOS / Linux / Windows 都能直接跑。

```bash
git clone https://github.com/geoochi/math-glsl && cd math-glsl
make            # Windows: build.bat
./render_glsl   # → ./wave.mp4（1920x1080 @ 60fps，5 秒，300 帧）
```

默认渲染 `y = sin(x + t)`——带二维坐标系、网格、抗锯齿曲线和沿波形移动的追踪点。

[**▶ demo/wave.mp4**](demo/wave.mp4) ← 本程序自己渲染出来的，点开即可播放（1920×1080 @ 60fps，5 秒 300 帧）。

## 换成你要的函数

只改一处——`shaders/wave.glsl` 里的 `curveValue()`：

```glsl
float curveValue(float x, float t) {
    return AMPLITUDE * sin(FREQ * x + SPEED * t);      // ← 换成你的 f(x, t)
}
```

坐标系、网格、抗锯齿、追踪点会自动适配。文件顶部注释里给了波包、阻尼振荡、傅里叶方波、拍频的现成公式。

```bash
./render_glsl out/4k.mp4 3840 2160 120 10           # 4K 120fps 10 秒
SHADER=shaders/ripple.glsl ./render_glsl ripple.mp4 # 换着色器
ENCODER=h264_videotoolbox ENCODE_ARGS="-q:v 60" ./render_glsl   # 硬件编码
```

## 环境要求

| 需要 | 说明 |
|---|---|
| C 编译器 | macOS 自带 clang；Linux 装 gcc；Windows 装 MSVC 或 mingw |
| 图形头文件 | macOS 自带；Linux 装 `libegl1-mesa-dev libgl1-mesa-dev`；Windows 自带（Windows SDK） |
| **ffmpeg** | 唯一的外部依赖，需要在 PATH 上：`brew install ffmpeg` / `apt install ffmpeg` / `winget install ffmpeg` |

## 它是怎么工作的

```
每帧：
  glUniform1f(uTime, i / fps)      ← 虚拟时钟：第 i 帧永远是 i/fps 秒
  glDrawArrays(全屏三角形 3 个顶点)  ← 没有顶点缓冲，靠 gl_VertexID 生成
  glReadPixels                     ← 从离屏 FBO 读回 RGB
  fwrite 到 ffmpeg 的 stdin         ← 管道编码成 H.264
```

**时间由 `i / fps` 给出，与真实时钟无关**，所以输出帧精确、绝不丢帧，跑的机器快慢不影响结果。

三平台的上下文后端在同一份源码里，只有约 60 行不同：

| 平台 | 上下文 | 链接参数 |
|---|---|---|
| macOS | CGL | `-framework OpenGL` |
| Linux | EGL（全无头，不需要 X/Wayland） | `-lEGL -lGL` |
| Windows | WGL | `opengl32.lib gdi32.lib user32.lib` |

## 性能

1920×1080 一帧（Apple M1 Pro）：GPU 着色 **2.3 ms**、`glReadPixels` 回读 **2.3 ms**、H.264 编码 **2.4 ms**，整帧约 **7.8 ms**（128 fps 含编码）。
着色只占约 30%——想更快先换硬件编码，再动回读，最后才动着色器。

## 作为 AI skill 使用

仓库根目录就是一份可加载的 skill（[`SKILL.md`](SKILL.md)），包含六条硬性规范、环境变量速查、排错表，以及两篇参考：

- [`references/platforms.md`](references/platforms.md) — 三平台构建参数与平台层的具体坑（`opengl32.dll` 只导出 GL 1.1、EGL 的 surfaceless 平台、CGL 的无窗口上下文）
- [`references/shader-writing.md`](references/shader-writing.md) — GLSL 写作与抗锯齿手法，以及移植到 WGSL/MSL 的对照表

## CI

GitHub Actions 在三个 runner 上编译三个平台后端（编译是**硬性要求**），并各自尝试跑一条 1 秒冒烟视频。实测结果：

| runner | 编译 | 渲染 |
|---|---|---|
| `ubuntu-latest` | ✅ EGL | ✅ llvmpipe 软件渲染（Mesa 4.5 core） |
| `macos-latest` | ✅ CGL | ✅ Apple 软件渲染器（GL 4.1 core） |
| `windows-latest` | ✅ WGL / MSVC | ❌ 云 VM 只有 GDI Generic 渲染器（GL 1.1），跑不了 4.1 core |

渲染成功的 runner 会把视频传到 Artifacts，可以直接下载来验。

CI 已经抓到过一个真实缺陷：macOS runner 上 `CGLChoosePixelFormat` 直接失败，因为代码里强制要求了 `kCGLPFAAccelerated`，而 VM 没有硬件加速的 GL 设备。现在改成先试加速、失败则退到软件渲染器——这个修复对无 GPU 的虚拟机和无头机器同样有用。

> 小提示：`actions/checkout@v4` 与 `actions/upload-artifact@v4` 目前会打印 Node.js 20 弃用警告（会自动跑在 Node 24 上）。等它们各自发布 v5 后升级即可，不影响功能。

## License

MIT
