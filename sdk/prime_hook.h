/*
 * prime_hook.h —— 固件输入钩子（SDK）
 *
 * ⚠️ AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
 *
 * 为什么需要它：
 *   取事件用的 SVC #0x1003f（`prime_sys_get_event`）**不能在普通循环里轮询**
 *   —— 它只在固件的事件分发路径中有效。正确做法与 PureDOOM/原 suika 一致：
 *   把 `0x307FBFA0` 处的固件入口换成指向本模块的 trampoline，固件每次分发
 *   事件都会回调我们；**在回调内部**再调用 SVC #0x1003f 取事件。
 *
 *   直接轮询的表现：程序能加载、入口被调用，但随即卡死（SVC 不返回）。
 *
 * 用法：
 *     static void on_event(void *event) { ...解析事件... }
 *     prime_hook_install(on_event);
 *     while (!quit) { render(); prime_sys_sleep(20); }
 *     prime_hook_remove();          // 退出前务必恢复
 *
 * 注意：
 *   - 适用固件版本与 puredoom.elf 相同（钩子地址 0x307FBFA0 取自该二进制）；
 *   - `prime_hook_install` 会保存 `0x307FBFA0` 处原始 16 字节，`remove` 时还原；
 *   - 回调在固件上下文中执行，应尽量短小（置标志位，重活留给主循环）。
 */
#ifndef PRIME_HOOK_H
#define PRIME_HOOK_H

/* 事件回调：event 指向固件的 ui_event_prime_s 缓冲（本模块已调用过 SVC） */
typedef void (*prime_hook_fn)(void *event);

/* 安装钩子。成功返回 1；已安装或指针为空返回 0。 */
int  prime_hook_install(prime_hook_fn cb);

/* 卸载并还原固件原始代码。未安装时为无操作。 */
void prime_hook_remove(void);

/* 是否已安装 */
int  prime_hook_active(void);

#endif /* PRIME_HOOK_H */
