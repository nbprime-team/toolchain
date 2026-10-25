# HP Prime 自制二进制程序开发套件 (DevKit)

在 HP Prime 上运行**你自己的原生 ARM 程序**的完整思路步骤与可复用脚本。
本套件从 DOOM 移植项目（`DOOM.hpappdir/main.py`）中抽象而来：把"DOOM 专用"的部分
（参数、键位、日志大小）抽成配置，把"加载二进制"的部分做成通用引导器。

```
devkit/
├── README.md            ← 本文档：思路与步骤
├── generic_loader.py    ← 通用引导脚本（在计算器上运行，你只需改配置）
├── app_template.c       ← C 端应用骨架（照此写你的程序）
└── Makefile             ← 交叉编译示例
```

---

## 0. 原理回顾：你在利用什么？

HP Prime 固件内置一个**内存调试后门**（以 `debug` 特殊文件形式暴露），提供三个能力：

| 能力 | 说明 |
|---|---|
| 读内存 | 任意地址读任意长度 |
| 写内存 | 任意地址写 32 位值 |
| 任意函数调用 | 以 ARM R0-R3 传参调用任意地址，返回值从 R0 取回 |

配合固件内部一张**导出函数表**（`BASE = 0x307FBCAC`，每项 12 字节，ID 从 0x10000 起），
可以动态定位 `malloc`/`free` 等系统函数。这就是"在 Python 里运行 C 程序"的立足点：

```
MicroPython 脚本 ──PrimeDebug──▶ 固件内存/函数
      │                              │
      └── 注入 ARM shellcode ────────▶ 读文件 → 拷段 → 清 bss → 跳入口
                                            │
                                            ▼
                                      你的程序 (ELF)
```

**关键结论**：后门只负责"把二进制弄进内存并跳过去"。程序本身的逻辑（游戏、工具、
演示）全部在 C/汇编侧实现，Python 侧只做编排、日志回显、资源清理。

---

## 1. 前置条件

| 项目 | 要求 | 说明 |
|---|---|---|
| 计算器 | HP Prime G1 / G2 | G1 为 ARM9 (ARMv5TE)；G2 为 Cortex-A7 (ARMv7-A)，均兼容 ARM 指令 |
| 固件 | 含 PrimeDebug 后门的版本 | 需先确认你的固件里 `BASE=0x307FBCAC` 是否命中 |
| 工具链 | `arm-none-eabi-gcc` 或 `arm-linux-gnueabi-gcc` | 交叉编译 C → ELF |
| 文件 | 计算器上已装好 MicroPython 应用 | `.hpappdir` 结构，参照 DOOM 项目 |

> **第一步永远是验证后门**，而不是写程序。先把 `generic_loader.py` 原样跑一遍
> （里面自带 DOOM 配置作 demo），能加载 `puredoom.elf` 就说明整条链路是通的。

---

## 2. 约定（ABI）——你的 C 程序必须遵守这些

这些约定来自对 DOOM 项目的逆向推断，**你的程序按同样规则写，引导器才能驱动它**：

### 2.1 ELF 形态

| 项 | 约定 | 原因 |
|---|---|---|
| 机器 | EM_ARM (0x28) | ARM 处理器 |
| 类型 | ET_DYN (PIC, `-fPIC`) | 加载到任意 malloc 地址都能跑 |
| 段 | vaddr 从 0 开始 | shellcode 按 `基址 + p_vaddr` 拷段 |
| bss | 允许 `memsz > filesz` | shellcode 负责清零差额 |
| 入口 | `基址 + e_entry` | 引导器从 shellcode 拿到绝对入口 |

> 验证：`readelf -h app.elf` 应看到 `Type: DYN`、`Entry` 为偏移值；
> `readelf -l app.elf` 应看到首个 `LOAD` 段 `vaddr 0x00000000`。
> `puredoom.elf` 实测：单 LOAD 段 `vaddr=0`、`filesz=0x6bbf0`、`memsz=0xeb5d0`（含 bss）。

### 2.2 入口签名

```c
// 入口被这样调用: R0 = BootConfig* (见下), R1 = 0
// 返回值: 日志结构体地址 (可被 LogReader 轮询)，失败返回 NULL
void* app_entry(BootConfig* cfg);
```

对应 `generic_loader.py` 中的：
```python
log_addr = dbg.call(entry_point, config_addr, 0)
```

### 2.3 BootConfig 结构体（32 字节，由 Python 端构造）

```c
typedef struct {
    uint32_t magic;        // 0xD00BC760，供 C 端校验
    uint32_t argc;
    char**   argv;         // 指向 Python 分配的字符串数组
    uint32_t env_count;
    EnvPair* envs;         // {char* name; char* val;} x env_count
    uint32_t key_count;
    KeyMap*  keys;         // {uint16_t hp_scan; uint16_t doom_key;} x key_count
    uint32_t services;     // PrimeServices* (系统服务表地址), 0 = 未提供
} BootConfig;
```

`services` 指向一张 **PrimeServices 系统服务表**（8 个 uint32，由引导器
在 `APP["framebuffer"]` 配置后构造）：固件后门 `malloc`/`free` 函数指针、
LCD framebuffer 地址与宽/高/行字节、时钟与键盘回调（可 0）。这是 ELF 获得
系统能力的唯一通道——参考移植工程 `hp-prime-nmarkdown/port/prime_sys.h`。
不需要服务表的程序可忽略该字段（保持 0）。

C 端用它读取程序参数、环境变量、键位映射。**键位映射是给"需要键盘输入"的程序用的**
（HP 物理扫描码 → 你的程序内部键 ID），不需要键盘的程序可以传 0。

### 2.4 日志结构体（跨语言通信通道）

```c
#define LOG_BUFFER_SIZE 4096   // 必须与 Python 端 log_buffer_size 一致

typedef struct {
    uint32_t head;             // 写指针（单调递增，% LOG_BUFFER_SIZE 为下标）
    char     buffer[LOG_BUFFER_SIZE];
} LogStruct;
```

Python 端轮询 `head`，读到新增字节就打印。C 端用 `log_printf()` 往里写（见模板）。
这是你的程序"往终端输出"的唯一通道——**不要直接写 GPIO/调试口，走这个环形缓冲**。

### 2.5 键盘输入

DOOM 的做法是 C 端通过后门/固件 API 直接读物理键盘（键位表在 BootConfig 里）。
你开发自己的程序时两种路线任选：

- **C 端读键盘**（推荐，实时性好）：在 C 里查固件导出表找键盘 API，或用你已知的
  地址调用；键位经 BootConfig.keys 映射。
- **Python 端轮询后写入共享内存**：Python 用 `hpprime` 模块能力轮询，把扫描码写进
  一个由 C/Python 共用的内存区，C 端轮询该区。适合不需要精确实时的程序。

模板 `app_template.c` 默认走 C 端读键盘路线（在 `poll_keys()` 处留占位）。

---

## 3. 开发流程（8 步）

### 步骤 1：验证后门与基线

1. 把整个 `hp-prime-devkit/` 拷到计算器（或与 DOOM 同盘方便测试）。
2. 跑 `generic_loader.py`（默认 DOOM 配置）——能加载 `puredoom.elf` 即链路通。
3. 若 `get_addr` 出来的 `malloc` 地址可疑（malloc 返回 0），说明你的固件
   `BASE` 不同，改用你固件的导出表基址。

### 步骤 2：搭交叉编译环境

```bash
# Debian/Ubuntu
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi
# 或通用目标
sudo apt install gcc-arm-linux-gnueabi binutils-arm-linux-gnueabi
```

### 步骤 3：写你的程序（C 骨架）

复制 `app_template.c`，替换 `demo_main()` 为你的逻辑，保留：

- `app_entry(BootConfig*)` 入口（名字无所谓，入口地址由链接决定）
- 日志环形缓冲与 `log_printf`
- 结束时返回 `&log`（或 NULL）

模板已含：`cfg 校验`、`日志`、`按键轮询占位`、`无限主循环示例`。

### 步骤 4：编译为符合约定的 ELF

```bash
make                 # 产出 app.elf
make check           # 自动校验: 机器类型 / PIC / vaddr 从 0 开始
```

关键编译选项（详见 Makefile）：

```
-fPIC -nostdlib -ffreestanding -mcpu=arm926ej-s -mthumb
链接: -Ttext=0 或自定义 linker script 让首 LOAD 段 vaddr=0
```

> **不要**用 libc/启动文件：固件里没有 Linux ABI，你的程序是裸机/裸 ELF，
> 需要的系统功能自己通过后门导出表调（或直接操作硬件寄存器）。
> `-nostdlib -ffreestanding` 可保证这一点。

### 步骤 5：配置引导器

编辑 `generic_loader.py` 末尾的 `APP` 字典（也可新建一个 `myapp.py` 把它 import 后覆盖配置）：

```python
APP = {
    "name":        "myapp",                       # 显示名
    "elf":         "myapp.elf",                   # 编译产物，放到 .hpappdir 里
    "dir":         "C:\\DATA\\MYAPP.hpappdir",    # 部署目录（路径转义！）
    "args":        ["myapp", "--option", "value"],
    "envs":        {"HOME": "C:\\DATA\\MYAPP.hpappdir"},
    "keys":        [ (0x03, 0xAD), (0x0D, 0x9D), ... ],  # (HP 扫描码, 程序键 ID)
    "entry_returns_log": True,   # 入口返回 LogStruct* 则开日志轮询；False 则直接退出
    "log_buffer_size": 4096,     # 与 C 端 LOG_BUFFER_SIZE 一致
    "magic":       0xD00BC760,   # 与 C 端校验魔数一致
}
```

### 步骤 6：部署

1. 在计算器上建 `C:\DATA\MYAPP.hpappdir\`（或复用任一 hpappdir 目录）。
2. 把 `generic_loader.py` + `myapp.elf` 放进去。
3. 在 HP Prime 上运行该 Python 应用。

### 步骤 7：调试

- 引导器会打印：加载器分配地址、ELF 所需内存、入口点、日志流。
- **最小验证**：先让 `demo_main` 只 `log_printf("hello\n")` 后返回，确认
  "加载 → 入口 → 日志回显"整条链路；再加业务逻辑。
- 入口返回 NULL / 入口点 < 0x30000000 → 加载或入口约定有问题，
  对照 README 第 2 节逐条检查 ELF（`make check`）。
- 日志空白 → `log_buffer_size` 与 C 端不一致，或 head 语义不同。

### 步骤 8：收尾与发布

- 确认 `Ctrl+C` 后引导器 `finally` 块能 free 掉所有内存（ELF、shellcode、config）。
- 发布时把 `generic_loader.py` 里 `APP` 改成你的程序，重命名应用即可。

---

## 4. 常见坑

| 坑 | 现象 | 解决 |
|---|---|---|
| 固件导出表基址不对 | `malloc` 返回 0 | 按固件版本换 `BASE` |
| ELF 不是 PIC / vaddr 非 0 | 入口点乱跳、崩溃 | `-fPIC`、`-Ttext=0`，用 `make check` |
| 用了 libc | 入口直接崩溃（无 ABI） | `-nostdlib -ffreestanding` |
| 日志大小不一致 | 日志乱码/丢失 | 对齐 `LOG_BUFFER_SIZE` 与 `log_buffer_size` |
| 路径写错 | 加载器报打不开文件 | 目录是 `C:\DATA\` 下，双反斜杠转义 |
| 入口签名不匹配 | 返回值异常/NULL | R0=cfg, R1=0, 返回 LogStruct* |

---

## 5. 参考

- 本套件各文件头注释
- 原始 DOOM 项目：`DOOM.hpappdir/main.py`（已验证的完整链路）
- `puredoom.elf`：可直接作为"标准 ELF 形态"的样本（`readelf -h/-l` 查看）
