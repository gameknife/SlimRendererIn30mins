# SlimRender

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3+-red.svg)](https://vulkan.lunarg.com/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

基于现代 **Vulkan 1.3 硬件光线追踪 (Ray Query + Buffer Device Address)** 与 **C++20** 实现的极简路径追踪渲染器，配备类似 **Blender 的 3D 编辑器交互界面**。

整个渲染器约 **3000 行** 自有代码（另有约 900 行注释），刻意保持在"一个下午读得完"的规模：每个文件只负责一件事，关键决策都在注释里说明了**为什么**这么写。

> 💡 **项目起源与讨论**：
> - 灵感讨论：[知乎 - 如何实现一个现代的 Vulkan 光线追踪渲染器？](https://www.zhihu.com/question/9874420979)
> - 架构参考：[gkNextEngine](https://github.com/gameknife/gkNextEngine)（参考其硬件光线追踪、Compute Shader 内联 Ray Query 与 Buffer Device Address 架构设计）

---

## 📸 编辑器与渲染预览

### 🎨 Blender 风格 3D 交互视口编辑器
![SlimRender Editor UI](docs/images/editor_ui.png)
*(支持 3D 视口三轴 Gizmo 变换、大纲视图 Outliner、属性面板 Transform/Material/Render 实时调节与 SPP 累积进度指示)*

### 🖼️ 物理真实感路径追踪样张

| 室内复杂会议室场景 (`conf_room.glb`) | 户外游乐场场景 (`playground.glb`) |
| :---: | :---: |
| ![conf_room](docs/images/conf_room.png) | ![playground](docs/images/playground.png) |

| PBR 材质球渐变测试 (`pbr.glb`) |
| :---: |
| ![pbr](docs/images/pbr.png) |

---

## 🔍 一帧是怎么画出来的

想读懂这个项目，从 [`src/main.cpp`](src/main.cpp) 的主循环开始，只有五步：

```
1. EditorUI::Draw          UI 先跑：它可能改场景，并决定这一帧相机是否响应鼠标
2. Camera::Update          WASD / 鼠标 → 视图矩阵；视角一变就清空累积
3. PathTracer::RenderCompute   一次 Dispatch = 每像素追一条路径，累加进 rgba32f 图像
4. PathTracer::BlitToSwapchain 把色调映射后的 rgba8 结果 Blit 到交换链图像
5. EditorUI::EndFrameAndRender ImGui 通过 Dynamic Rendering 叠加在画面之上并转 Present
```

第 3 步是渲染器的全部核心，落在 [`assets/shaders/pathtrace.comp`](assets/shaders/pathtrace.comp) 里：

```
每像素：
  生成相机射线（亚像素抖动 → 渐进抗锯齿）
  循环 maxBounces 次：
    rayQueryEXT 求交 ─ 未命中 → 采样解析天空，结束
                    └ 命中   → 按 BDA 指针取顶点/材质，重建法线与 UV
                               NEE：朝太阳打一条阴影射线，累加直接光
                               按 metallic 概率二选一：GGX 高光叶 或 余弦漫反射叶
                               俄罗斯轮盘赌决定是否继续
  与历史累积求平均 → ACES 色调映射 → sRGB 编码 → 写入输出图像
```

**累积是这个渲染器的关键设计**：`frameIndex` 既是已累积样本数，也是随机数种子。相机、材质、光照、物体变换任一改变都会把它清零，画面因此重新收敛——所有交互功能都建立在这一条规则上。

---

## ✨ 特色与设计理念

### 1. 现代 Vulkan 硬件光追架构

- **内联光线追踪 (Inline Ray Query)**：
  在普通 Compute Shader 中用 `rayQueryEXT` 调度多次反弹。**不需要 Ray Tracing Pipeline，也不需要 Shader Binding Table (SBT)**，管线与生命周期管理因此简化了一大截，同时仍然吃满硬件 BVH 求交单元。
- **Buffer Device Address (BDA) 近乎无绑定**：
  顶点、索引、材质、实例矩阵全部以 64 位 GPU 虚拟地址放进 **88 字节的 Push Constants**（Vulkan 保证至少 128 字节）直接传给着色器。描述符集里只剩下 TLAS、两张图像和纹理数组四项。
  配合 `scalarBlockLayout`，GLSL 结构体与 [`SceneTypes.hpp`](src/Scene/SceneTypes.hpp) 中的 C++ 结构体逐字节一致，不需要为 std140/std430 的对齐规则做手工 padding。
- **两层加速结构 (BLAS + TLAS)**：
  每个 mesh primitive 建一个物体空间的 BLAS；TLAS 用 3x4 实例矩阵把它们摆进世界。交互拖动物体时只需重写实例矩阵并重建（很便宜的）TLAS，昂贵的三角形 BVH 完全复用。
- **物理着色与重要性采样**：
  - **Microfacet GGX**：按粗糙度对半程向量重要性采样，模拟金属与高光反射；
  - **Cosine-weighted Hemisphere**：半球余弦加权漫反射采样，cos 项与 pdf 相消；
  - **Next-Event Estimation (NEE)**：每次反弹朝太阳打一条阴影射线求直接光；
  - **避免太阳重复计数**：既然 NEE 已经显式采样了太阳，漫反射反弹再撞上太阳圆盘就会重复计算一次——因此只有相机射线与镜面反弹能"看见"太阳圆盘（严格做法是 MIS 权重，代码注释里标注了这一点）；
  - **正确的色彩管线**：基础色贴图按 `R8G8B8A8_SRGB` 上传，由采样器负责线性化；着色全程在线性空间进行，最后统一做 ACES 色调映射与 sRGB 编码；
  - **俄罗斯轮盘赌 (Russian Roulette)**：无偏地终止低贡献路径。

### 2. Blender 风格 DCC 交互编辑器 (Dear ImGui + ImGuizmo)

- **Blender 经典深色主题**：石板灰基调与标志性橙色高亮（`#E87D0D`）；
- **视口 3D 操纵手柄 (ImGuizmo)**：直接在视口里平移、旋转、缩放选中的模型实例；
- **大纲视图 (Outliner)**：树状列出场景中所有物体实例，点击选中；
- **属性面板 (Properties)**：
  - **Transform**：位置 / 欧拉角旋转 / 缩放的数值微调，支持一键重置；
  - **Material**：BaseColor 拾色器、Metallic、Roughness、Emissive 实时调节；
  - **Render**：Max Bounces（1~16）、太阳方向 / 强度 / 颜色、天空强度；
- **高质量离线出图**：设定目标采样数（128 ~ 2048 SPP），累积完成后自动经 `stb_image_write` 导出 PNG 到 `renders/` 目录。

### 3. 极简依赖（无需包管理器）

- **原生 Win32 窗口**：不依赖 SDL / GLFW / Qt，直接封装轻量 Win32 窗口与输入；
- 数学库 `glm` 由 CMake `FetchContent` 自动拉取；
- `cgltf.h`、`stb_image.h`、`stb_image_write.h`、`imgui`、`ImGuizmo` 以源码内嵌于 `third_party/`；
- **只需安装 Vulkan SDK**，无需配置 vcpkg / conan。

### 4. glTF 2.0 资产与相机支持

- 支持 `.gltf` 与 `.glb`（二进制）格式，解析场景层级、顶点属性、PBR 材质因子与基础色贴图；
- **文件拖拽 (Drag & Drop)**：把任意 `.glb` / `.gltf` 拖进视口即可加载，太阳与天空设置会保留；
- **智能相机就位**：文件内嵌 Camera 节点时直接沿用其位姿；否则按包围盒外接球自动构图；
- **明确的格式告警**：遇到未解压的 Draco 网格或 WebP 贴图会打印清晰警告，而不是静默黑屏。

---

## 📁 项目目录结构

```
SlimRender/
├── CMakeLists.txt                      # CMake 配置（含 glslc 编译着色器到输出目录）
├── LICENSE                             # MIT
├── docs/images/                        # 预览截图
├── assets/
│   ├── models/                         # 测试模型 (conf_room.glb, playground.glb, pbr.glb)
│   └── shaders/pathtrace.comp          # 核心路径追踪着色器 (RayQuery + BDA + GGX)
├── third_party/                        # cgltf / stb / imgui / ImGuizmo
└── src/
    ├── main.cpp                        # 入口、主渲染循环、场景热加载
    ├── Core/
    │   ├── Common.hpp                  # 平台与 Vulkan/GLM 头文件、VK_CHECK
    │   └── Window.hpp / .cpp           # Win32 窗口、消息循环、输入与拖放
    ├── Scene/
    │   ├── SceneTypes.hpp              # 与着色器一一对应的 GPU 结构体
    │   ├── Camera.hpp / .cpp           # 自由飞行观察相机
    │   └── GltfLoader.hpp / .cpp       # glTF 加载、层级展平、BLAS/TLAS 构建
    ├── UI/
    │   └── EditorUI.hpp / .cpp         # Blender 风格编辑器 (Outliner / Properties / Gizmo)
    └── Vulkan/
        ├── VulkanContext.hpp / .cpp    # 实例、物理设备筛选、扩展与特性
        ├── VulkanBuffer.hpp / .cpp     # 缓冲分配、Staging 上传、DeviceAddress
        ├── VulkanImage.hpp / .cpp      # 图像、采样器与布局转换
        ├── VulkanSwapchain.hpp / .cpp  # 交换链与多帧同步对象
        ├── AccelerationStructure.hpp/.cpp # BLAS / TLAS 构建（共用同一套构建流程）
        └── PathTracer.hpp / .cpp       # 计算管线、描述符与多帧累积
```

---

## 🛠️ 构建与运行

### 环境要求
- **操作系统**：Windows 10 / 11 64-bit
- **显卡**：支持 `VK_KHR_ray_query` 的 GPU（NVIDIA RTX 20 系及以上，或 AMD RX 6000 系及以上）
  程序启动时会显式检查该扩展，不支持时给出明确报错而不是崩溃。
- **开发工具**：
  - [Vulkan SDK](https://vulkan.lunarg.com/) 1.3+（确保 `glslc` 在 PATH 中或已设置 `VULKAN_SDK`）
  - Visual Studio 2022 (MSVC C++20)
  - CMake 3.22+

### 1. 配置与编译

```powershell
cmake -B build -S .
cmake --build build --config Release
```

着色器会在每次链接后由 `glslc` 编译到输出目录（`build/Release/assets/shaders/`），SPIR-V 属于构建产物，不进版本库。

> Debug 配置会自动启用 Vulkan Validation Layer，Release 配置则关闭它。改动渲染代码后，建议至少用 Debug 跑一次确认没有 API 误用。

### 2. 运行渲染器

```powershell
# 默认材质球场景
.\build\Release\SlimRender.exe assets/models/pbr.glb

# 室内会议室 / 户外游乐场
.\build\Release\SlimRender.exe assets/models/conf_room.glb
.\build\Release\SlimRender.exe assets/models/playground.glb

# 任意外部模型（也可直接拖入窗口）
.\build\Release\SlimRender.exe "D:/path/to/your/model.glb"
```

### 3. 无人值守出图（回归自测）

累积到指定 SPP 后写出 PNG 并退出，方便改完渲染代码比对结果：

```powershell
.\build\Release\SlimRender.exe assets/models/pbr.glb --test-render out.png 256
```

---

## 🎮 交互控制说明

| 操作按键 | 对应功能 |
| :--- | :--- |
| **鼠标右键拖动** | 旋转观察视角 (Yaw / Pitch) |
| **鼠标中键拖动** 或 **Shift + 右键拖动** | 平移摄像机 (Pan) |
| **鼠标滚轮** | 摄像机前后推拉 (Zoom / Dolly) |
| **W / A / S / D** | 摄像机平移（前 / 左 / 后 / 右） |
| **Q / E** | 摄像机垂直升降（下移 / 上移） |
| **Shift 键 (按住)** | 3x 快速飞行移动 |
| **Ctrl 键 (按住)** | 0.3x 精细微调移动 |
| **W / E / R 键** | 切换 Gizmo 操作模式（平移 / 旋转 / 缩放） |
| **Ctrl + O** | 打开 glTF / GLB 文件对话框 |
| **文件拖入窗口** | 直接加载外部 `.gltf` / `.glb` 场景 |
| **F1 键** | 切换隐藏 / 显示 UI 界面 |
| **ESC 键** | 退出程序 |

---

## 🚧 有意省略的部分

为了让核心逻辑保持可读，下面这些"真实引擎必备"的东西被刻意留白，也正好是很好的练习题：

- **显存分配器**：每个 buffer / image 各自 `vkAllocateMemory`，真实项目应使用 VMA 之类的子分配器；
- **多队列**：全部工作跑在同一个 graphics+compute+present 队列上，没有异步计算与专用传输队列；
- **GGX 的遮蔽项**：高光叶把 masking-shadowing (G) 项近似为 1，这是它与参考渲染器的主要差距；
- **MIS**：太阳采样用的是"镜面反弹才看得见太阳圆盘"的简化规则，而非严格的多重重要性采样；
- **区域光 / IBL**：光照仅有解析太阳 + 天空渐变，不支持 HDRI 环境贴图与自发光物体的显式采样；
- **法线贴图 / 金属粗糙度贴图**：材质只采样 base color 贴图，其余走标量因子；
- **降噪与时域复用**：纯靠 SPP 累积收敛，没有 SVGF / ReSTIR / DLSS 之类的加速。

---

## 📚 参考与致谢

- [gkNextEngine](https://github.com/gameknife/gkNextEngine) - 现代 Vulkan 硬件光追设计与实现参考
- [知乎讨论问题](https://www.zhihu.com/question/9874420979) - 架构设计与极简实现的灵感来源
- [Dear ImGui](https://github.com/ocornut/imgui) - 即时模式图形界面库
- [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) - 3D 变换操纵手柄
- [cgltf](https://github.com/jkuhlmann/cgltf) - 极简快速的 C99 glTF 2.0 解析器
- [stb](https://github.com/nothings/stb) - 单头文件图像读写库

本项目以 [MIT License](LICENSE) 发布。
