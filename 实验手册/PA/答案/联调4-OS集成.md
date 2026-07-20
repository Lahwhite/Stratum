# 联调4 — OS 集成
> 双人共同完成 · 前置：PA4-A 和 PA4-B 均已完成 · 预计工时：2～3 天

---

## 目标

将 A 的 SCore 中断/陷入机制与 B 的 SOS 操作系统对接，逐步验证 ecall→syscall→用户程序→多进程调度的完整链路，最终打 tag `v4.0-pa4`，完成整个 Stratum 项目。

---

## 一、合并到 dev

```bash
git checkout dev && git pull origin dev
git merge --no-ff feat/pa4-trap -m "merge: pa4-a trap and CSR mechanism"
git merge --no-ff feat/pa4-os   -m "merge: pa4-b SOS kernel"
```

冲突处理原则：
- `csr.h`、`trap.h`：以 A 的为准
- `syscall.h`：以 B 的为准（B 定义调用号，若 A 有单独的编号参考文件，合并统一）
- `mmio.h`：以 A 的为准（新增了 `CLINT_*` 地址）

---

## 二、构建 Ramdisk

B 的 SOS 依赖一个 ramdisk 镜像和对应的文件表。需要先构建它。

### 2.1 准备用户程序

编译最简单的用户程序 `stratum-apps/apps/hello/hello_user.c`：

```c
// 用户程序通过系统调用与 OS 交互
// 不能直接用 printf（printf 依赖 write 系统调用）
// 这里使用内联 ecall 实现 write

static inline void sys_write(int fd, const char *buf, int len) {
    register int    a7 asm("a7") = 2;    // SYS_write
    register int    a0 asm("a0") = fd;
    register long   a1 asm("a1") = (long)buf;
    register int    a2 asm("a2") = len;
    asm volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2));
}

static inline void sys_exit(int code) {
    register int a7 asm("a7") = 1;      // SYS_exit
    register int a0 asm("a0") = code;
    asm volatile("ecall" : : "r"(a7), "r"(a0));
}

void _start() {
    const char *msg = "Hello from user space!\n";
    sys_write(1, msg, 23);
    sys_exit(0);
}
```

编译：

```bash
riscv32-unknown-elf-gcc -march=rv32im -mabi=ilp32 -nostdlib \
    -T stratum-apps/cpu-tests/link.ld \
    stratum-apps/apps/hello/hello_user.c \
    -o stratum-apps/apps/hello/hello_user.elf
```

### 2.2 生成 Ramdisk 镜像

创建构建脚本 `sos/tools/gen_ramdisk.py`：

```python
#!/usr/bin/env python3
"""
将多个文件打包成 ramdisk 镜像，并生成 C 头文件 ramdisk_files.h
用法：python3 gen_ramdisk.py output.img file1:path1 file2:path2 ...
"""
import sys, os, struct

if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} output.img [name:file ...]")
    sys.exit(1)

output = sys.argv[1]
entries = []
data = bytearray()

for arg in sys.argv[2:]:
    name, path = arg.split(':', 1)
    with open(path, 'rb') as f:
        content = f.read()
    entries.append((name, len(data), len(content)))
    data.extend(content)

with open(output, 'wb') as f:
    f.write(data)

# 生成 ramdisk_files.h
header_path = os.path.join(os.path.dirname(output), 'ramdisk_files.h')
with open(header_path, 'w') as f:
    f.write("// 自动生成，勿手动修改\n")
    f.write("static FileEntry file_table[] = {\n")
    for name, offset, size in entries:
        f.write(f'    {{"{name}", {offset}, {size}}},\n')
    f.write("};\n")

print(f"ramdisk: {len(data)} bytes, {len(entries)} files")
```

运行：

```bash
python3 sos/tools/gen_ramdisk.py sos/ramdisk.img \
    /bin/hello:stratum-apps/apps/hello/hello_user.elf
```

---

## 三、逐步对接

### Step 1：验证 trap 向量设置

在 SCore 上单步跟踪 OS 启动过程，确认 `trap_init()` 后 `mtvec` 被正确设置：

```
(sdb) si 200
(sdb) info r      # 查看所有寄存器
(sdb) p $mtvec    # 若 SDB 支持 CSR 引用
```

若 A 的 SDB 尚未支持 `$mtvec`，可通过 `x 1 <trap_handler地址>` 间接验证。

**验证标准**：OS 启动后 `mtvec` 不为零，值为 `__am_asm_trap` 的地址。

---

### Step 2：验证 ecall 触发

在 OS 中加入一个在 `main()` 最开始的 ecall 测试，不依赖文件系统：

```cpp
// 在 sos/src/main.cpp 的 main() 最开头加
// 手动触发一次 ecall，检查 trap handler 是否被调用
static const char *test_msg = "[SOS] trap OK\n";
sys_write(1, test_msg, 15);   // 若 trap 正常，终端会打印这行
```

在 SCore 上运行 SOS：

```bash
./score/score -e sos/sos.elf
```

**期望**：终端打印 `[SOS] trap OK`

若无输出，排查顺序：
1. A：用 SDB 单步到 ecall 指令附近，确认 trap handler 被调用（PC 跳到 mtvec）
2. A：确认 `mcause` 值为 `0xb`（CAUSE_ECALL_FROM_M）
3. B：确认 `do_syscall` 中 `SYS_write` 分支被命中，`A0` 中的 fd = 1

---

### Step 3：验证用户程序加载

```bash
# OS 启动后加载 hello_user.elf
```

观察终端输出：

```
[SOS] 初始化文件系统...
[SOS] 创建进程 0：/bin/hello 入口 0x80000000
Hello from user space!
[SOS] 进程 0 退出，返回值 0
[SOS] 所有进程结束，系统停机
```

常见问题排查：

| 现象 | 先查谁 | 具体排查 |
|------|--------|----------|
| `[SOS] 找不到文件` | B | ramdisk 构建脚本是否生成了 `/bin/hello`；链接时 `_ramdisk_start` 是否正确 |
| 加载后 PC 跑到奇怪地址 | B | ELF 加载器中 `phdr.p_paddr` 是否正确；SCore 内存范围是否覆盖了用户程序地址 |
| `SYS_write` 无输出 | B | 系统调用分发中 fd=1 分支是否走到 UART 写操作 |
| 进程退出后死循环 | B | `proc_exit` → `schedule` → 无可用进程 → `ebreak` 路径是否正确 |

---

### Step 4：验证时钟中断

开启两个进程，观察是否交替执行：

```cpp
// 在 sos/src/main.cpp 中加载两个程序
proc_create("/bin/hello");
proc_create("/bin/hello");
```

**验证标准**：两个进程的输出交错出现（中间夹杂着各自的 `Hello from user space!`），说明 Round-Robin 调度生效。

若两个进程串行输出（先全部输出进程0，再输出进程1）：
- A：检查时钟中断是否正常触发（在 `check_interrupt()` 中加调试输出）
- B：检查 `mtimecmp` 写入逻辑，确认每次中断后都更新了下一次触发时间

---

### Step 5：用 SDB 调试一次上下文切换

在 `schedule()` 入口设置 watchpoint，观察进程切换时的上下文变化：

```
(sdb) w $pc       # 监视 PC 变化
(sdb) si 5000     # 执行 5000 条指令
```

期望：watchpoint 多次触发，每次 PC 值对应不同进程的代码地址。

---

## 四、最终演示

两人一起运行最终 Demo，确认所有功能：

```bash
./score/score -e sos/sos.elf
```

**期望终端输出**：

```
Stratum-Core (SCore) - RISC-V 32-bit Simulator
Build: ...

加载 ELF：sos/sos.elf  入口 = 0x80000000
[SOS] 初始化文件系统... OK
[SOS] 初始化进程管理... OK
[SOS] 设置 trap 向量，开启中断... OK
[SOS] 创建进程 0：/bin/hello 入口 0x80010000
Hello from user space!
[SOS] 进程 0 退出，返回值 0
[SOS] 所有进程结束，系统停机

(sdb)
```

**期望 SDL2 窗口**（若加载了打字游戏）：显示游戏界面，能响应键盘。

---

## 五、联调完成检查清单

- [ ] `mtvec` 设置正确，trap handler 被调用
- [ ] `ecall` 触发系统调用，`SYS_write` 在终端打印字符
- [ ] `SYS_exit` 正常终止进程
- [ ] 文件系统：`/bin/hello` 能被加载
- [ ] 用户程序：`Hello from user space!` 能打印
- [ ] 多进程：两个 hello 进程能交替执行
- [ ] SDB 依然可用（打断并调试 OS 运行）
- [ ] 无内存踩踏，无死循环（SDB 的 watchpoint 可验证）

---

## 六、合入 main 并打最终 tag

```bash
git checkout dev
make clean && make
# 发 PR dev → main，对方 Review
git checkout main && git pull origin main
git tag v4.0-pa4
git push origin v4.0-pa4
```

---

## 七、项目总结

至此，**Stratum** 项目全部完成：

```
v0.1-pa0  环境搭建，框架搭好
v1.0-pa1  SDB 调试器全功能（表达式、watchpoint）
v2.0-pa2  RV32IM CPU 核心，能运行 C 程序
v3.0-pa3  外设全通（串口/定时器/键盘/VGA），能跑打字游戏
v4.0-pa4  SOS 操作系统，系统调用、文件系统、多进程调度全部就位
```

在自己写的模拟器上，跑着自己写的操作系统，加载着自己写的用户程序。

---

*恭喜完成 Stratum 全部实验！*
