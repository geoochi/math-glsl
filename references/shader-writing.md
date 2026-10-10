# 写着色器时的注意事项

## 组合方式：基础 + 你的逻辑

本仓库的着色器分两层，这是推荐的使用姿势：

- **`shaders/axes.glsl`** — 默认着色器，只画一个空的坐标系，并留出 `YOUR DRAWING GOES HERE` 区段。
- **`shaders/lib.glsl`** — 可复用件（`toMath` / `strokeMask` / `coordinateSystem` / `heightRamp` / `vignette`）。纯函数，无全局状态，任何着色器都能包含。

复制 `axes.glsl` → 改区段里的数学 → `SHADER=你的文件 ./render_glsl`。

**渲染器自己实现了 `#include "..."`**（core GLSL 没有这个指令）：路径按**被包含文件**所在目录解析，所以 `demo/wave/wave.glsl` 里写 `#include "../../shaders/lib.glsl"`。嵌套深度上限 8 层。

注意 GLSL 要求"先声明后使用"，且 `#include` 是**文本替换**，所以被包含的函数不能引用包含方稍后才定义的常量——需要的话通过参数传进去（`lib.glsl` 就是这么写的）。

## 正文契约

着色器**不含 `#version`**，只写正文和 `main()`。宿主负责加头：

```glsl
#version 410 core
uniform vec2  uResolution;   // 帧缓冲尺寸（像素）
uniform float uTime;         // 秒，由虚拟时钟给出
out vec4 fragColor;
```

改宿主时保持这个契约，着色器就能不加修改地复用。

## 坐标与像素

- `gl_FragCoord.xy` 的原点是**左下角**（OpenGL 约定），像素中心在 `+0.5` 处。
- 转成"以中心为原点、y 向上"的数学坐标：
  `vec2 p = (gl_FragCoord.xy - 0.5 * uResolution) * (UNITS_X / uResolution.x);`
  这个写法**等比**（x、y 同一比例），所以圆是圆、正方形是正方形。
- 想要非等比缩放（纵向拉伸波形）就分别给 x、y 两个比例。

## 抗锯齿：用导数，不要手调线宽

```glsl
float g  = (p.y - f(p.x)) / sqrt(1.0 + fp * fp);  // 隐式方程，除以梯度模长
float gw = fwidth(g);                              // 每像素 g 的变化量
float hw = LINE_W * pixel;                         // 目标半宽（像素）
float a  = 1.0 - smoothstep(hw - gw, hw + gw, abs(g));
```

- 除以 `sqrt(1+f'^2)` 是把**垂直距离**换算成**垂直距离**，线宽才与斜率无关；不除的话陡峭段会变细、水平段会变粗。
- `fwidth` 让线宽在任何分辨率下都自动正确（2K/4K 不用改代码），这是着色器路线相对 `cv2.polylines` 的主要画质优势。
- 网格同理，但用像素空间的 `abs(fract(p-0.5)-0.5) / fwidth(p)`，得到的是"到最近格线的像素距离"。

## 常量、精度、限制

- 数值字面量尽量写 `1.0` 而不是 `1`，避免隐式转换（GLSL ES 对隐式转换更严格）。
- `fract` 对负数返回正的小数部分，做周期性图案时很好用。
- 想省算力就在 CPU 侧预计算并做 uniform 传入，不要在着色器里重复算常数。
- **没有跨像素通信**：着色器里写不了"累加上一帧"的循环。要做反馈（粒子拖尾、流体）需要 ping-pong 两张纹理，而本仓库的单 pass 渲染器不支持——那时改用三缓冲/多 pass。

## 常见报错对照

| 报错 | 原因 |
|---|---|
| `undeclared identifier 'AMPLITUDE'` | 把 `const float X = 1.0;` 改成 uniform 时改了名字。**必须保留标识符名**：`const float X = 1.0;` → `uniform float X;` |
| `'XXX' : cannot be used as a function` / `no matching overloaded function` | 类型不匹配（如 `vec2` 传给了期望 `float` 的参数） |
| `#version must be the first line` | 正文里混进了 `#version`，或宿主加头后有空白行 |
| 编译通过但画面全黑 | 没绑 VAO（core profile 必须）；或 `glUseProgram` 没调用；或 uniform 名拼错（`glGetUniformLocation` 返回 -1 且 `glUniform*` 静默失败） |

**排查手法**：`glGetShaderInfoLog` / `glGetProgramInfoLog` 一定要打印（本仓库已打印）；uniform 位置返回 -1 时要提示而不是静默忽略。

## 将来要移植到 WGSL / MSL 时的对照

数学函数名几乎不变（`fract / fwidth / smoothstep / mix / clamp / length / dot` 三家同名），差别只在类型名、绑定方式和坐标系原点：

| 概念 | GLSL | WGSL | MSL |
|---|---|---|---|
| 类型 | `vec2/vec3/vec4` | `vec2<f32>` … | `float2/float3/float4` |
| 像素坐标原点 | **左下** | **左上** | **左上** |
| 文件作用域常量 | `const float X = 1.0;` | `const X: f32 = 1.0;` | `constant float X = 1.0;`（`constexpr` 会报 "program scope variable must reside in constant address space"） |
| 参数传递 | `uniform` | `var<uniform>` + `@group/@binding` | `constant T&` + `[[buffer(n)]]` |
| 入口 | `void main()` | `@vertex` / `@fragment` | `vertex` / `fragment` |

移植到左上角原点的 API 时，在着色器里翻一次 y（`0.5*res.y - fc.y`），然后 ffmpeg 那侧**不要**再加 `vflip`。
