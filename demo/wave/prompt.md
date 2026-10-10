# Prompt

生成 `wave.mp4` 用的 prompt。可以直接丢给 AI agent，也可以自己照着改。

---

> 用 math-glsl 渲染一段视频。
>
> **画面**：二维坐标系，带向外淡出的网格和穿过原点的两条坐标轴。
> 在坐标系上画函数 `y = sin(x + t)` 的曲线，`t` 是时间，所以波形随时间向左移动。
> 曲线要抗锯齿、按高度从青绿渐变到紫色、带一层淡淡的光晕；在 `x = 0` 处放一个白色实心圆点
> 跟着波形上下移动，外面套一个不断扩散并淡出的圆环。
> 视野宽度 9 个数学单位，曲线半宽 1.6 像素。
>
> **规格**：3840×2160，60fps，10bit，时长 5 秒。

---

## 复现

```bash
git clone https://github.com/geoochi/math-glsl && cd math-glsl
make
# HWENC=0：这个仓库里存的演示视频用软件 H.264 编，兼容性最好，任何播放器都能直接打开。
# 去掉 HWENC=0 就会走默认的硬件编码（macOS 上是 HEVC Main 10，更快但兼容性略差）。
HWENC=0 SHADER=demo/wave/wave.glsl ./render_glsl demo/wave/wave.mp4 3840 2160 60 5
```

## 产出

| 文件 | 说明 |
|---|---|
| [`wave.glsl`](wave.glsl) | 着色器 |
| [`wave.mp4`](wave.mp4) | 渲染结果：4K 60fps 10bit，300 帧，5.000000 秒 |

## 这个 prompt 是怎么落到代码上的

坐标系不用自己写——`#include "../../shaders/lib.glsl"` 之后就有 `coordinateSystem()`、`strokeMask()`、`heightRamp()`、`vignette()` 可用。

1. **曲线用隐式方程**：`g = (p.y - f(p.x)) / sqrt(1 + f'(x)²)`。除以梯度模长，线宽才与斜率无关，陡的地方不会变细。
2. **抗锯齿交给导数**：`fwidth(g)` 把数学单位换算成像素，所以 4K 和 720p 共用同一份数字。
3. **光晕是第二层描边**：用更宽的 `smoothstep` 做一层低透明度加法混合。
4. **追踪点是另一个距离场**：白色圆点用 `smoothstep` 画实心，圆环用 `fract(t * 0.8)` 做周期扩散。
5. **时间来自 `uTime`**：虚拟时钟（第 i 帧恰好是 `i/fps` 秒），所以输出帧精确、不会丢帧。

改一行 `curveValue()` 就能换成波包、阻尼振荡、拍频或傅里叶方波——公式都写在文件注释里。
