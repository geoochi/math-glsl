# 平台层：三平台构建与各自的坑

## 构建

```bash
# macOS  — CGL，链接系统 OpenGL 框架
clang -O2 -Wall -Wextra -o render_glsl src/render_glsl.c -framework OpenGL

# Linux  — EGL，全无头，不需要 X / Wayland
cc -O2 -Wall -Wextra -D_GNU_SOURCE -o render_glsl src/render_glsl.c -lEGL -lGL
#   依赖：libegl1-mesa-dev / libgl1-mesa-dev（Debian 系）
#         mesa-libEGL-devel / mesa-libGL-devel（RPM 系）

# Windows — WGL，MSVC
cl /nologo /O2 /W3 /Fe:render_glsl.exe src\render_glsl.c opengl32.lib gdi32.lib user32.lib
# Windows — mingw
gcc -O2 -Wall -Wextra -o render_glsl.exe src\render_glsl.c -lopengl32 -lgdi32 -luser32
```

三平台的**平台层只有约 60 行**（`#if defined(__APPLE__) / _WIN32 / __linux__`），下面的 GL 代码完全共享——所以三个移植版本不会各自跑偏。

## macOS / CGL

- 没有 EGL/WGL，平台层是 CGL。
- **无 drawable 的上下文是合法的**，但不能渲染到默认帧缓冲，必须自己建 FBO。
- OpenGL 自 10.14 起被标记弃用：源码里已 `#define GL_SILENCE_DEPRECATION 1`。
- 驱动本身是 Metal 实现的（`glGetString(GL_VERSION)` 返回形如 `4.1 Metal - 91.7`），所以 GL 版本上限就是 4.1。
- 上下文创建不需要窗口，`CGLCreateContext` 之后直接 `CGLSetCurrentContext` 即可，适合 CI。
- **`kCGLPFAAccelerated` 不能强求**：虚拟机、CI 机器、无头服务器上常常没有硬件加速的 GL 设备，一旦要求加速，`CGLChoosePixelFormat` 会直接失败。源码里是先试加速、失败后去掉该属性重试，退到 Apple 的软件渲染器（同样支持 GL 4.1 core）。
  实测：GitHub 的 `macos-latest` runner 走的就是这条软件回退路径。

## Windows / WGL

两个必须知道的坑：

1. **`opengl32.dll` 只导出 GL 1.1。** `glCreateShader`、`glGenFramebuffers` 这些在 DLL 里根本不存在，必须在运行时用 `wglGetProcAddress` 一个个取。这就是 GLAD / GLEW 存在的全部原因。本文件不想引依赖，所以手写了一张 24 项的函数指针表（`GL_FUNCS` 宏）——想省掉这段就装 GLAD。
2. **必须走"两步舞"**：先用传统的 `ChoosePixelFormat` + `wglCreateContext` 建一个**临时**上下文并 `wglMakeCurrent`，才能解析到 `wglCreateContextAttribsARB`，再用它去建 4.1 core 上下文；然后把临时上下文删掉。

另外：`popen` 在 Windows 上是 `_popen`，且 **mode 必须带 `b`**（`"wb"`）——否则换行会被转换，视频流直接损坏。

## Linux / EGL

- EGL 可以完全无头：不需要 X server，也不需要 Wayland。
- 首选 **surfaceless 平台**（服务器和 CI 的常态）：
  `eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL)`
  取不到再退回 `eglGetDisplay(EGL_DEFAULT_DISPLAY)`。
- 然后 `eglBindAPI(EGL_OPENGL_API)` → 选 config → 请求 **4.1 core** 上下文 →
  `eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)`。
- 若 surfaceless 的 `eglMakeCurrent` 失败，退回 1×1 pbuffer 表面。
- 选 config 时也要注意：surfaceless 平台可能**完全没有 pbuffer 类型的 config**，所以 `eglChooseConfig` 要允许两种尝试（带/不带 `EGL_SURFACE_TYPE`）。
- 没有 GPU 的机器（云服务器、CI）靠 Mesa llvmpipe 软渲染：设 `LIBGL_ALWAYS_SOFTWARE=1`。llvmpipe 支持到 GL 4.5，跑 4.1 core 没问题，只是慢。
- 如果驱动只到 3.3，需同时降低源码里的请求版本和着色器头的 `#version`。

## 唯一的共同外部依赖：ffmpeg

```bash
brew install ffmpeg          # macOS
sudo apt install ffmpeg      # Linux
winget install ffmpeg        # Windows
```

管道是 `raw 帧 → ffmpeg -f rawvideo -pix_fmt <格式> -s WxH -r fps -i - -vf vflip -c:v ...`。
想摆脱 ffmpeg：macOS 用 AVFoundation，Windows 用 Media Foundation，Linux 用 libav* 直接调——但那就不再是"零依赖"了。

## 10bit 色彩管线（跨平台通用的坑）

```
GL_RGB10_A2 渲染缓冲
   ↓ glReadPixels(GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV)   每像素 4 字节
   ↓ 管道
ffmpeg -pix_fmt x2bgr10le -c:v libx264 -profile:v high10 -pix_fmt yuv420p10le
```

三个必须知道的点：

1. **`GL_UNSIGNED_INT_2_10_10_10_REV` 把红色放在低 10 位。** 所以那个 32 位字从高位往低位读是 **B、G、R、A**。对应的 ffmpeg 输入格式是 **`x2bgr10le`**。
   写成 `x2rgb10le` 会把红蓝对调——画面还能"看"，但颜色全错（青会变橙）。**这个只能靠采样像素值发现**：对着纯色背景量一点的 RGB，和设计值比对（本项目实测：设计背景 `(11,14,20)`，用错格式量到 `(18,13,10)`，改对后量到 `(10,13,18)`）。
2. **x264 必须显式指定 `-profile:v high10`**，否则它不接受 `yuv420p10le` 输入。硬件编码器（videotoolbox / nvenc / qsv）不听这套参数，所以要单独覆盖 `ENCODE_ARGS`。
3. **`GL_RGB10_A2` 是 core profile 里唯一保证可作颜色缓冲的 10bit 布局**（R10G10B10A2）。老驱动或软件渲染可能不支持，此时应退回 `GL_RGB8` + `rgb24` + `yuv420p`。本项目是自动降级的，并会打印一行提示。

另外 4:2:0 色度抽样会**明显削弱细线的颜色**（1–2 像素宽的亮线会被去饱和、看起来偏灰）。这是 H.264 的固有特性，不是通道顺序错了——判断通道顺序要看**大面积色块**，不要看细线。
