# Suika v4 使用的 PureDOOM 输入钩子 ABI

源自所提供的 `puredoom.elf` 反汇编。

## 固定的固件钩子目标

`install_input_hack()` 在下列地址保存 16 字节：

```text
0x307FBFA0
```

然后写入 8 字节：

```text
e51ff004
<hook address>
```

等价于：

```asm
ldr pc, [pc, #-4]
.word hook_address
```

该写入使用与原始 DOOM 二进制相同的特权字节拷贝/缓存刷新例程。

## 事件系统调用

钩子在 `R0` 收到事件指针，然后调用：

```asm
svc #0x1003f
```

相关的 `ui_event_prime_s` 布局为：

```text
+0x04  event_type          uint32
+0x18  available_events    uint16
+0x1C  event[0]            12 字节记录
```

每条记录：

```text
+0x00  action/type         uint16
+0x04  validity field      uint16  （DOOM 接受零）
+0x06  x                   uint16
+0x08  y                   uint16
```

DOOM 使用的触摸值：

```text
1 = begin（按下）
2 = move（移动）
8 = end（抬起）
```

对键盘事件，DOOM 识别的外层事件类型为：

```text
0x00100010
```

且 +0x1C 处的第一条记录包含按键动作。

Suika v4 故意在任意按键按下或抬起时退出。

## 实测记录（HP Prime G1，examples/keys 与 keymon）

| 键 | `+0x04` ty4 | `words[8]>>16`（扫描码） | 与 `hp_input.h` 对照 |
|---|---|---|---|
| ENTER | **按下 `0x00100010` / 抬起 `0x00000020`** | `0x0d` = 13 | `HP_KEY_ENTER` ✓ |
| 7/Q | — | `0x51` = 81 | `HP_KEY_Q` ✓ |
| LEFT / UP / RIGHT / DOWN | — | `0x02` / `0x03` / `0x04` / `0x05` | ✓ 全部一致 |
| ESC / ON | — | `0x01` / `0x83` | ✓（取事件与钩子卸载均正常） |

结论（真机实测）：

1. **扫描码 = `words[8] >> 16`**，与 `hp_input.h` 的 `HP_KEY_*` **完全一致**；
2. **`+0x04` 随按下/抬起变化**：按下 `0x00100010`、抬起 `0x00000020`
   —— 因此**判据不应只看 `+0x04`**；
3. **`words[7]`（= `+0x1C`）才是按键动作**：`16` 按下 / `0x100000` 抬起
   —— **判据宜以它为准**（触摸为 `1`/`2`/`8`）；
4. `words[8]` 的低 16 位在抬起时为 `0x00c3`（含义待查，暂不使用）；
5. 以上与 Upsilon 侧资料（`words[7]` / `words[8]>>16`）**在事实上一致** —— 说明两平台
   **共享事件结构**，差异只在取事件的**做法**（钩子 vs 事件线程）。
