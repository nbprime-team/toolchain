# 固件 SVC 一览（路线 A / B 通用内核）

> ⚠️ AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）

调用约定：`push {r0}; push {lr}; svc <id>`，参数在 r0–r3，结果在 r0。

| SVC | 名称 | 说明 |
|---|---|---|
| 0x10000 | OSCreateThread | 建线程（栈=0 用系统默认） |
| 0x10008 | OSSleep | 毫秒睡眠 |
| 0x10037 | lmalloc | 固件堆分配 |
| 0x1003A | lfree | 释放 |
| 0x1003F | GetEvent | 取一个事件（阻塞语义，需在入口/事件线程） |
| 0x10040 | GetPendEvent | 非阻塞取挂起事件 |
| 0x1004A | ClearAllEvents | 清空事件队列 |
| 0x1004C | SetSystemVariable | 系统变量；**85=电源管理，写 0 会关机** |
| 0x1008D | GetActiveLCD | 帧缓冲信息 |
| 0x1026F | W_FOPEN | 宽字符打开（固件 fopen 需绝对路径） |
| 0x10274/75/76/77/78 | remove/rename/copy/mkdir/rmdir | 宽字符路径 |
| 0x10270/71/72 | find_first/find_next/get_attr | 目录与属性 |

## 输入（路线 A 标准做法）

1. 用 `prime_privileged_memcpy` 把 `0x307FBFA0` 处 16 字节换成
   `{ ldr pc,[pc,#-4]; &hook }`（保存原值，退出恢复）；
2. 钩子在**入口线程**被调用，内部 `prime_sys_get_event(ev)`；
3. 判据：`w7=*(u32*)(ev+28)`，`16`=按下、`0x100000`=抬起；
   `scan = (*(u32*)(ev+32)) >> 16`；触摸 `w7` 为 1/2/8。

公共实现见 `sdk/src/hp_input_hook.c`（`hp_input_start/stop/poll`）。
