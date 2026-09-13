/*
 * prime_hook.h —— 固件输入钩子（SDK）
 *
 * ⚠️ AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
 *
 * 为什么需要它：
 *   取事件用的 SVC #0x1003f（`prime_sys_get_event`）**不能在普通循环里轮询**
 *   —— 它只在固件的事件分发路径中有效。正确做法与 PureDOOM / suika 一致：
 *   把 `0x307FBFA0` 处的固件入口换成指向**用户回调**的 trampoline，固件每次
 *   分发事件都会直接调用该回调；回调**内部**再调用 SVC #0x1003f 取事件。
 *
 *   直接轮询的表现：程序能加载、入口被调用，但随即卡死（SVC 不返回）。
 *
 * 用法（suika、cube3d 均如此）：
 *     static void on_event(void *event) {
 *         prime_sys_get_event(event);   // 取事件（SVC #0x1003f）
 *         ...解析 event...
 *     }
 *     prime_hook_install(on_event);
 *     while (!quit) { render(); prime_sys_sleep(20); }
 *     prime_hook_remove();              // 退出前务必恢复
 *
 * 注意：
 *   - 适用固件版本与 puredoom.elf 相同（钩子地址 0x307FBFA0 取自该二进制）；
 *   - `prime_hook_install` 会保存 `0x307FBFA0` 处原始 16 字节，`remove` 时还原；
 *   - 回调在**固件上下文**中执行，应尽量短小（置标志位，重活留给主循环）；
 *   - trampoline 直接指向回调、**不经过中间层**（与原 suika 的实现同构），
 *     因此回调必须自己调用 `prime_sys_get_event(event)`。
 */
#ifndef PRIME_HOOK_H
#define PRIME_HOOK_H

/* 由 prime_input.S 提供：SVC #0x1003f 的包装（在回调内调用） */
int prime_sys_get_event(void *event);

/* 事件回调：event 指向固件的 ui_event_prime_s 缓冲；回调需先调用
 * prime_sys_get_event(event) 填充它。 */
typedef void (*prime_hook_fn)(void *event);

/* 安装钩子。成功返回 1；已安装或指针为空返回 0。 */
int  prime_hook_install(prime_hook_fn cb);

/* 卸载并还原固件原始代码。未安装时为无操作。 */
void prime_hook_remove(void);

/* 是否已安装 */
int  prime_hook_active(void);

#endif /* PRIME_HOOK_H */
