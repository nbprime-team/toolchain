/* AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
 *
 * hp_input_hook.h —— 路线 A（MicroPython/调试接口）的标准输入组件。
 *
 * 真机验证过的四件事（详见 nb-markdown/MIGRATION-NOTES.md）：
 *   1. 事件在入口线程分发，主循环必须在该线程；
 *   2. 调用上下文栈很小，需自备大栈（本组件不负责，调用者用静态栈）；
 *   3. 钩子用 prime_privileged_memcpy 写 0x307FBFA0 槽；
 *   4. 事件判据：w7@+28==16 为按下，scan=w8>>16。
 *
 * 用法：
 *   hp_input_start();
 *   for (;;) { hp_event e; if (hp_input_poll(&e)) { ... } }
 *   hp_input_stop();
 */
#ifndef HP_INPUT_HOOK_H
#define HP_INPUT_HOOK_H

#define HP_EV_NONE  0
#define HP_EV_KEY   1
#define HP_EV_TOUCH 2

struct hp_event {
    unsigned type;   /* HP_EV_* */
    unsigned code;   /* 按键扫描码；触摸为 0 */
};

int  hp_input_start(void);
void hp_input_stop(void);
int  hp_input_poll(struct hp_event *out);   /* 1=有事件, 0=无 */

#endif
