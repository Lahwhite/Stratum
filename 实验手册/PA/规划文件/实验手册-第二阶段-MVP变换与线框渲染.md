# 实验手册 · 第二阶段：MVP 变换与线框渲染

> **阶段目标：** 加载一个 `.obj` 3D模型文件，经过 MVP 矩阵变换后，在图片上画出它的线框（wireframe）。  
> **预计时间：** 2周，每天约1小时  
> **前置条件：** 第一阶段全部完成，`drawTriangle` 可以正确填充三角形

---

## 本阶段涉及的文件变动

```
game/
├── CMakeLists.txt          ← 需要追加新的 .cpp 文件
└── src/
    ├── main.cpp            ← 持续修改
    ├── math/
    │   ├── vec.h           ← 新增 Vec4（在原文件中追加，不要新建文件）
    │   └── mat.h           ← 本阶段新建
    └── core/
        ├── framebuffer.h   ← 新增 drawLine 函数声明
        ├── framebuffer.cpp ← 新增 drawLine 函数实现
        ├── model.h         ← 本阶段新建
        └── model.cpp       ← 本阶段新建
```

**本阶段不涉及：** `texture` / `pipeline` / `shader`，这些留给后续阶段，现在不要创建。

---

## 实验一：实现 Vec4 和 Mat4

### 目标
在 `vec.h` 中追加 `Vec4`，新建 `mat.h` 实现 4×4 矩阵，并支持矩阵与向量的乘法。

### 背景知识：为什么需要齐次坐标（Vec4）？

3D 变换（平移、旋转、缩放）理论上可以用 3×3 矩阵处理旋转和缩放，但**平移无法用 3×3 矩阵表示**。

解决方案：把坐标从 3D 升到 4D，加一个 `w` 分量：
```
Vec3(x, y, z)  →  Vec4(x, y, z, 1.0f)
```

这样所有变换（包括平移）都可以统一用 4×4 矩阵表示。这就是**齐次坐标**。

`w` 分量的含义：
- `w = 1`：表示一个**点**（有位置）
- `w = 0`：表示一个**方向向量**（无位置，平移对它无效）

### 步骤

**1. 在 `src/math/vec.h` 末尾追加 Vec4**

（不要新建文件，直接在 vec.h 已有内容后面追加）

需要实现：
```
struct Vec4 {
    float x, y, z, w;
    构造函数
    + - * 运算符
};
```

另外，补充两个辅助函数，后续会频繁用到：

```cpp
// Vec3 升到 Vec4（点，w=1）
// 用法：Vec4 v = toVec4Point(myVec3);
inline Vec4 toVec4Point(const Vec3& v) {
    return Vec4(v.x, v.y, v.z, 1.0f);
}

// Vec3 升到 Vec4（方向，w=0）
// 用法：Vec4 v = toVec4Dir(myVec3);
inline Vec4 toVec4Dir(const Vec3& v) {
    return Vec4(v.x, v.y, v.z, 0.0f);
}

// Vec4 降回 Vec3（透视除法，除以 w）
// 用法：Vec3 v = toVec3(myVec4);
inline Vec3 toVec3(const Vec4& v) {
    return Vec3(v.x / v.w, v.y / v.w, v.z / v.w);
}
```

> ⚠️ **注意：** `toVec3` 中除以 `w` 这一步叫做**透视除法**，是整个透视投影的关键步骤。现在先实现，后面会深刻理解它的含义。

**2. 新建 `src/math/mat.h`，实现 Mat4**

Mat4 是 4×4 的浮点矩阵。用一维数组存储，按**行优先**：
```
m[0]  m[1]  m[2]  m[3]   ← 第0行
m[4]  m[5]  m[6]  m[7]   ← 第1行
m[8]  m[9]  m[10] m[11]  ← 第2行
m[12] m[13] m[14] m[15]  ← 第3行
```

需要实现：

```
struct Mat4 {
    float m[16];

    // 构造：单位矩阵
    Mat4();   → 对角线为1，其余为0

    // 矩阵乘法：Mat4 * Mat4
    Mat4 operator*(const Mat4& other) const;

    // 矩阵与向量乘法：Mat4 * Vec4 → Vec4
    Vec4 operator*(const Vec4& v) const;

    // 静态工厂函数（下面逐个实现）
    static Mat4 identity();
    static Mat4 translate(float tx, float ty, float tz);
    static Mat4 scale(float sx, float sy, float sz);
    static Mat4 rotateX(float angle);  // angle 单位：弧度
    static Mat4 rotateY(float angle);
    static Mat4 rotateZ(float angle);
    static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up);
    static Mat4 perspective(float fovy, float aspect, float near, float far);
};
```

**矩阵乘法规则：**

结果矩阵的第 i 行第 j 列 = 左矩阵第 i 行 × 右矩阵第 j 列（点积）：
```
result.m[i*4+j] = 左矩阵第i行的4个元素 与 右矩阵第j列的4个元素 对应相乘后求和
```

**各工厂函数的矩阵形式：**

单位矩阵（identity）：
```
1 0 0 0
0 1 0 0
0 0 1 0
0 0 0 1
```

平移矩阵（translate tx, ty, tz）：
```
1 0 0 tx
0 1 0 ty
0 0 1 tz
0 0 0  1
```

缩放矩阵（scale sx, sy, sz）：
```
sx  0  0  0
 0 sy  0  0
 0  0 sz  0
 0  0  0  1
```

绕 Y 轴旋转（angle 弧度，右手坐标系）：
```
 cos  0  sin  0
   0  1    0  0
-sin  0  cos  0
   0  0    0  1
```

> 先实现 rotateY，X 和 Z 的矩阵形式类似，自己推导或查阅。

**lookAt 矩阵（View 矩阵）：**

lookAt 的含义：摄像机在 `eye` 位置，看向 `center` 点，`up` 是摄像机的"上方向"。

推导步骤：
```
1. 计算摄像机的三个轴方向：
   z轴（朝后） = normalize(eye - center)
   x轴（朝右） = normalize(up × z轴)
   y轴（朝上） = z轴 × x轴

2. lookAt 矩阵 = 旋转矩阵 × 平移矩阵
   旋转矩阵（把世界轴对齐到摄像机轴）：
   | x.x  x.y  x.z  0 |
   | y.x  y.y  y.z  0 |
   | z.x  z.y  z.z  0 |
   |  0    0    0   1 |

   平移矩阵（把世界原点移到摄像机位置的反方向）：
   | 1  0  0  -eye.x |
   | 0  1  0  -eye.y |
   | 0  0  1  -eye.z |
   | 0  0  0    1    |

   最终：lookAt = 旋转矩阵 * 平移矩阵
```

**perspective 矩阵（投影矩阵）：**

参数含义：
- `fovy`：垂直视野角（Field of View，单位弧度，通常用 45°~60°，换算成弧度）
- `aspect`：宽高比（width / height）
- `near`：近裁剪面距离（正数，通常 0.1）
- `far`：远裁剪面距离（正数，通常 100）

矩阵形式（OpenGL 约定，NDC 空间 z 范围 [-1, 1]）：
```
f = 1 / tan(fovy / 2)

| f/aspect    0         0              0       |
|    0        f         0              0       |
|    0        0   (far+near)/(near-far)  (2*far*near)/(near-far) |
|    0        0        -1              0       |
```

> **记忆要点：** 最后一行是 `0 0 -1 0`，这一行会把 z 值复制到 w 分量，透视除法（÷w）正是利用这一点实现了"近大远小"的效果。

### 检查点
- [ ] `Vec4` 实现完毕，`toVec4Point`、`toVec4Dir`、`toVec3` 函数可用
- [ ] `Mat4` 乘法（矩阵×矩阵、矩阵×向量）实现正确
- [ ] `identity()` 乘以任意向量结果不变（手算验证一下）
- [ ] 编译通过

---

## 实验二：实现 drawLine，补全 Framebuffer

### 目标
在 `framebuffer` 中新增画线函数，后续线框渲染会用到。

### 步骤

**在 `framebuffer.h` 中新增声明：**
```cpp
void drawLine(Vec2 p0, Vec2 p1, const Color& color);
```

**在 `framebuffer.cpp` 中实现：**

用 Bresenham 直线算法（你熟悉这类算法）。如果觉得麻烦，用最简单的参数化方法也可以：
```
步长 = max(|p1.x-p0.x|, |p1.y-p0.y|)
t 从 0 到 1，共 步长 步
每步：point = p0 + (p1-p0)*t，调用 setPixel
```

> 注意：传入的坐标是浮点数，调用 `setPixel` 时需要转为整数（用 `(int)` 或 `std::round`）。

### 检查点
- [ ] 在 `main.cpp` 里测试 `drawLine`，能看到一条正确的线段

---

## 实验三：解析 .obj 文件，加载 3D 模型

### 目标
新建 `Model` 类，能读取 `.obj` 文件，获取顶点列表和三角面列表。

### 背景知识：.obj 文件格式

.obj 是最简单的3D模型格式，纯文本，结构如下：

```
# 注释行，忽略

v 0.123 0.456 0.789     ← 顶点坐标（x y z），以 v 开头
v -0.456 0.789 0.123
v ...

vt 0.5 0.3              ← UV坐标（本阶段暂时忽略）
vn 0.0 1.0 0.0          ← 法线（本阶段暂时忽略）

f 1 2 3                 ← 面，三个数字是顶点索引（从1开始！）
f 1/1/1 2/2/2 3/3/3     ← 面（带UV和法线索引，格式：顶点/UV/法线）
```

**本阶段只需要解析 `v` 和 `f` 行**，`vt`、`vn` 留到第四、五阶段处理。

`f` 行的索引从 **1** 开始，不是0，使用时记得减1。

`f` 行可能有多种格式：
- `f 1 2 3`（只有顶点索引）
- `f 1/1/1 2/2/2 3/3/3`（顶点/UV/法线）

解析时，用 `/` 切割每个字段，取第一个数字（顶点索引）即可。

### 步骤

**1. 新建 `src/core/model.h`**

```
class Model {
public:
    Model(const std::string& filename);

    int numFaces() const;                        // 面的数量
    Vec3 vert(int faceIdx, int vertIdx) const;   // 第 faceIdx 个面的第 vertIdx 个顶点坐标

private:
    std::vector<Vec3> verts;                     // 所有顶点
    std::vector<std::array<int, 3>> faces;       // 每个面存3个顶点索引
};
```

**2. 新建 `src/core/model.cpp`**

在构造函数中：
- 用 `std::ifstream` 逐行读取文件
- 遇到 `v ` 开头的行：解析三个浮点数，存入 `verts`
- 遇到 `f ` 开头的行：解析三个顶点索引（处理 `/` 分隔的情况），**减1后**存入 `faces`
- 其他行：跳过

`vert(faceIdx, vertIdx)` 的实现：
```cpp
return verts[faces[faceIdx][vertIdx]];
```

**3. 更新 `CMakeLists.txt`**

把 `src/core/model.cpp` 加进 `SOURCE` 列表。

**4. 下载测试模型**

推荐使用 `tinyrenderer` 的配套模型资源：

在 `game/assets/` 目录下放置 `african_head.obj`，可以从以下地址获取：  
`https://github.com/ssloy/tinyrenderer/tree/master/obj/african_head`

下载 `african_head.obj`（暂时不需要纹理文件，第四阶段再下载）。

### 检查点
- [ ] `Model` 类编译通过
- [ ] 在 `main.cpp` 里加载模型，打印 `numFaces()` 的值（african_head 应该有 2000+ 个面）
- [ ] 打印前几个顶点坐标，确认数值在 [-1, 1] 范围内（african_head 的顶点是归一化的）

---

## 实验四：MVP 变换，画出线框

### 目标
把3D模型的顶点经过 MVP 矩阵变换，投影到屏幕坐标，用 `drawLine` 画出所有边。

### 背景知识：MVP 是什么

MVP 是三个矩阵变换的组合：

```
Model（模型矩阵）   → 把模型从"局部坐标"变换到"世界坐标"
                       作用：控制模型的位置、旋转、缩放

View（观察矩阵）    → 把"世界坐标"变换到"摄像机坐标"
                       作用：模拟摄像机的位置和朝向（即 lookAt）

Projection（投影矩阵）→ 把"摄像机坐标"变换到"裁剪坐标"
                       作用：实现透视效果（近大远小）
```

变换顺序（**从右到左**）：

```
裁剪坐标 = Projection * View * Model * 顶点坐标(Vec4)
```

然后做**透视除法**（÷w），得到 NDC 坐标（Normalized Device Coordinates）：
```
NDC.x = clipCoord.x / clipCoord.w    范围 [-1, 1]
NDC.y = clipCoord.y / clipCoord.w    范围 [-1, 1]
NDC.z = clipCoord.z / clipCoord.w    范围 [-1, 1]
```

最后把 NDC 坐标映射到屏幕坐标：
```
screenX = (NDC.x + 1.0) / 2.0 * width
screenY = (NDC.y + 1.0) / 2.0 * height
```

> **注意坐标系：** NDC 的 y 轴向上，你的屏幕坐标（TGA）原点在左下角，y 轴也向上，所以这里 y 方向不需要翻转。如果你发现模型上下颠倒，说明坐标系不一致，检查这一步的 y 方向。

### 步骤

**1. 在 `main.cpp` 中设置 MVP 矩阵**

```cpp
// 模型矩阵：暂时用单位矩阵（模型已经在世界坐标原点）
Mat4 model = Mat4::identity();

// 观察矩阵：摄像机在 (0, 0, 3)，看向原点，上方向为 y 轴
Mat4 view = Mat4::lookAt(
    Vec3(0.0f, 0.0f, 3.0f),   // eye
    Vec3(0.0f, 0.0f, 0.0f),   // center
    Vec3(0.0f, 1.0f, 0.0f)    // up
);

// 投影矩阵：60度视野，宽高比 1:1，近面0.1，远面100
float fovy = 60.0f * 3.14159f / 180.0f;   // 60度转弧度
Mat4 proj = Mat4::perspective(fovy, 1.0f, 0.1f, 100.0f);

// 合并：MVP = proj * view * model
Mat4 mvp = proj * view * model;
```

**2. 实现顶点变换函数（可以写在 main.cpp 里，也可以后续移到 pipeline 里）**

```
输入：Vec3 世界坐标，Mat4 mvp，int screenWidth，int screenHeight
输出：Vec2 屏幕坐标

步骤：
1. Vec4 clip = mvp * toVec4Point(worldPos)  // 变换到裁剪空间
2. Vec3 ndc  = toVec3(clip)                 // 透视除法，得到NDC
3. Vec2 screen:
   screen.x = (ndc.x + 1.0f) / 2.0f * screenWidth
   screen.y = (ndc.y + 1.0f) / 2.0f * screenHeight
4. return screen
```

**3. 遍历所有三角面，画线框**

```
for 每个面 i in [0, model.numFaces()):
    for 每条边 j in [0, 1, 2]:
        v0 = model.vert(i, j)
        v1 = model.vert(i, (j+1) % 3)   // 下一个顶点，mod 3 形成环
        
        s0 = transform(v0, mvp, width, height)   // 变换到屏幕坐标
        s1 = transform(v1, mvp, width, height)
        
        fb.drawLine(s0, s1, Color{255, 255, 255}) // 白色线框
```

**4. 运行，看到线框模型**

如果一切正确，你应该能看到 african_head 的白色线框图像。

### 检查点
- [ ] 能看到人头的线框
- [ ] 模型比例正常（不是扁的或拉伸的）
- [ ] 模型方向正确（不是上下颠倒）

---

## 实验五：让模型旋转（可选，加分体验）

### 目标
修改 `main.cpp`，每次运行用不同的旋转角度，观察从不同角度看模型。

### 步骤

在 model 矩阵上加一个 Y 轴旋转：

```cpp
float angle = 30.0f * 3.14159f / 180.0f;   // 旋转30度
Mat4 model = Mat4::rotateY(angle);
```

多改几个角度，运行，观察变化。理解 Model 矩阵的作用。

> **进阶：** 如果你想让模型实时旋转（而不是每次改代码重新编译），可以用命令行参数传入角度：
> ```cpp
> float angle = atof(argv[1]) * 3.14159f / 180.0f;
> ```
> 然后运行：`./SoftRender 45`、`./SoftRender 90` 等。

---

## 阶段总结

完成本阶段后，新增的内容：

| 文件 | 新增内容 |
|------|------|
| `src/math/vec.h` | 追加了 Vec4 和三个转换函数 |
| `src/math/mat.h` | Mat4 完整实现，含 MVP 所需的所有工厂函数 |
| `src/core/framebuffer.h/cpp` | 新增 drawLine |
| `src/core/model.h/cpp` | .obj 文件解析，顶点和面的数据结构 |
| `src/main.cpp` | MVP 变换 + 线框渲染主流程 |

**你现在能回答：**
- MVP 矩阵各代表什么？变换顺序是什么？
- 齐次坐标的 w 分量是什么含义？
- 透视除法是什么，为什么要除以 w？
- NDC 坐标是什么，范围是多少？
- lookAt 矩阵是怎么构造的？

---

## 进入第三阶段的前提

满足以下条件后，再看第三阶段手册：

- [ ] 能看到正确的 african_head 线框图
- [ ] 理解每一步坐标变换的含义（能说清楚 Model/View/Projection 各做了什么）
- [ ] `Mat4` 的矩阵乘法结果验证过（identity * 任意矩阵 = 原矩阵）
- [ ] 修改摄像机位置（`lookAt` 的 eye 参数），观察图像变化，确认 View 矩阵理解正确

---

## 常见问题

**Q：模型显示为空白或看不到？**  
A：按以下顺序排查：
1. 先检查 `numFaces()` 是否大于 0（模型是否加载成功）
2. 打印几个变换后的屏幕坐标，看数值是否在 [0, 512] 范围内
3. 如果坐标全是 NaN，检查 `toVec3` 中是否除以了 0（w 为 0 的情况）
4. 摄像机距离是否太近或太远（把 eye 的 z 值改大试试）

**Q：模型显示但比例奇怪（很扁或很窄）？**  
A：检查 `perspective` 的 `aspect` 参数，应该等于 `width / height`。如果你的图是 512×512，aspect = 1.0。

**Q：模型上下颠倒？**  
A：检查屏幕坐标映射时 y 轴方向。NDC 的 y 向上，如果 TGA 的坐标系 y 也向上（你在第一阶段确定的），则不需要翻转。

**Q：线框有很多奇怪的线穿过模型中心？**  
A：这是正常的，因为线框渲染没有深度测试，背面的边也被画出来了。第三阶段加入 Z-buffer 后会解决这个问题（届时会改为三角面填充，不再画线框）。

**Q：Mat4 的矩阵乘法顺序？**  
A：数学上矩阵乘法不满足交换律，`A*B ≠ B*A`。MVP 的正确顺序是：`proj * view * model`，变换时：`result = mvp * vertex`。记忆方式：从右到左读，先 model 再 view 再 proj。
