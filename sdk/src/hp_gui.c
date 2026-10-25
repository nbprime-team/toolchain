/*
 * hp_gui.c -- 最小化的 widget 工具包（见 hp_gui.h）。
 *
 * 文本使用运行时字体：Montserrat（比例字体）用于标签和
 * 按钮标题，Unifont（等宽字体）用于 edit 文本。绘制通过
 * 一个惰性分配的全屏 GROB 进行双缓冲
 * （hp_gui_begin/end），因此画面永不闪烁。
 *
 * Prime scan-code -> 字符表（数字/运算符）位于此处；专用
 * 功能键（SIN/COS/...）留给 app 通过
 * hp_gui_edit_insert() 处理。
 */
#include "hp_gui.h"
#include "hp_icall.h"

/* ---- 后备缓冲（一个全屏 GROB，创建一次，重复使用） ---- */
static hp_grob *g_back;

void hp_gui_begin(void)
{
    if (!g_back)
        g_back = hp_grob_new(hp_gfx_w(), hp_gfx_h());
    if (g_back) {
        hp_grob_select(g_back);
        hp_clear(HP_BLACK);
    }
}

void hp_gui_end(void)
{
    if (g_back) {
        hp_grob_select(NULL);
        hp_grob_blit(g_back, 0, 0);
    }
}

/* ---- 绘制 ---- */
static const hp_font *gui_prop(void) { return hp_font_prop(16); }
static const hp_font *gui_mono(void) { return hp_font_mono(16); }

void hp_gui_draw(hp_gui_widget *ws, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        hp_gui_widget *w = &ws[i];
        if (w->type == HP_GUI_LABEL) {
            hp_font_draw(gui_prop(), w->x, w->y,
                         w->label ? w->label : "", HP_WHITE);
            continue;
        }
        if (w->type == HP_GUI_EDIT) {
            int fh = hp_font_h(gui_mono());
            hp_fill_rect(w->x, w->y, w->w, w->h, HP_BLACK);
            hp_rect(w->x, w->y, w->w, w->h,
                    w->state == 1 ? HP_CYAN : HP_WHITE);
            if (w->buf) {
                hp_font_draw(gui_mono(), w->x + 3, w->y + 2, w->buf, HP_WHITE);
                if (w->state == 1) {            /* 光标处的插入符 */
                    char tmp[64];
                    int cx, k, j = 0;
                    k = w->cur;
                    if (k > (int)sizeof tmp - 1)
                        k = (int)sizeof tmp - 1;
                    for (j = 0; j < k && w->buf[j]; j++)
                        tmp[j] = w->buf[j];
                    tmp[j] = 0;
                    cx = w->x + 3 + hp_font_w(gui_mono(), tmp);
                    hp_vline(cx, w->y + 3, fh - 6, HP_CYAN);
                }
            }
            continue;
        }
        /* button：填充矩形 + 边框（聚焦为 cyan，按下为深色） */
        if (w->state == 2) {
            hp_fill_rect(w->x, w->y, w->w, w->h, HP_GRAY);
            hp_rect(w->x, w->y, w->w, w->h, HP_WHITE);
        } else if (w->state == 1) {
            hp_fill_rect(w->x, w->y, w->w, w->h, HP_BLUE);
            hp_rect(w->x, w->y, w->w, w->h, HP_CYAN);
        } else {
            hp_fill_rect(w->x, w->y, w->w, w->h, HP_GRAY);
            hp_rect(w->x, w->y, w->w, w->h, HP_WHITE);
        }
        if (w->label) {
            int tw = hp_font_w(gui_prop(), w->label);
            int th = hp_font_h(gui_prop());
            int tx = w->x + (w->w - tw) / 2;
            int ty = w->y + (w->h - th) / 2;
            if (tx < w->x) tx = w->x + 2;
            if (ty < w->y) ty = w->y + 1;
            hp_font_draw(gui_prop(), tx, ty, w->label, HP_WHITE);
        }
    }
}

/* ---- 命中测试 / 焦点 ---- */
static int interactive(const hp_gui_widget *w)
{
    return w->type == HP_GUI_BUTTON || w->type == HP_GUI_EDIT;
}

static int in_rect(const hp_gui_widget *w, int x, int y)
{
    return x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h;
}

static int focus_index(hp_gui_widget *ws, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (interactive(&ws[i]) && ws[i].state == 1)
            return i;
    for (i = 0; i < n; i++)
        if (interactive(&ws[i]))
            return i;
    return -1;
}

static void set_focus(hp_gui_widget *ws, int n, int idx)
{
    int i;
    for (i = 0; i < n; i++)
        if (interactive(&ws[i]))
            ws[i].state = (i == idx) ? 1 : 0;
}

/* Prime key scan code -> 字符。
 * 同时接受纯 ASCII 码（数字/运算符）和来自 hp_input.h 的矩阵设备
 * id——某些固件构建上报其中一种，某些上报另一种，
 * 因此两种都映射（两种方案中冲突的码在数值上一致：
 * 例如 0x30 和 0x33 在两者中都是 '0'/'3'）。 */
char hp_gui_key_char(int k)
{
    if ((k >= '0' && k <= '9') || k == '+' || k == '-' || k == '*' ||
        k == '/' || k == '.' || k == '(' || k == ')' || k == ',' ||
        k == '^' || k == ' ')
        return (char)k;
    switch (k) {
    case 0x30: return '0';   /* 矩阵 id（hp_input.h） */
    case 0x59: return '1';
    case 0x5A: return '2';
    case 0x33: return '3';
    case 0x55: return '4';
    case 0x56: return '5';
    case 0x57: return '6';
    case 0x51: return '7';
    case 0x52: return '8';
    case 0x53: return '9';
    case 0xB9: return '+';
    case 0xB7: return '-';
    case 0x58: return '*';
    case 0x54: return '/';
    case 0xB8: return '.';
    case 0x4E: return '(';
    case 0x4D: return ')';
    case 0x4F: return ',';
    case 0x46: return '^';
    case HP_KEY_SPACE: return ' ';
    }
    return 0;
}

int hp_gui_edit_insert(hp_gui_widget *w, const char *s)
{
    int len = 0, n;
    if (!w || w->type != HP_GUI_EDIT || !w->buf || !s)
        return 0;
    while (s[len])
        len++;
    if (w->cur + len >= w->buflen)
        len = w->buflen - 1 - w->cur;     /* 为 NUL 留出空间 */
    if (len <= 0)
        return 0;
    for (n = 0; w->buf[w->cur + n]; n++)
        ;
    for (; n >= 0; n--)
        w->buf[w->cur + len + n] = w->buf[w->cur + n];
    for (n = 0; n < len; n++)
        w->buf[w->cur + n] = s[n];
    w->cur += len;
    return len;
}

int hp_gui_handle(hp_gui_widget *ws, int n, int etype, const hp_event *ev)
{
    if (etype == HP_EV_TOUCH) {
        int x, y, i;
        if (hp_event_touch_count(ev) > 0 &&
            hp_event_touch(ev, 0, &x, &y)) {
            hp_gui_widget *w = 0;
            for (i = 0; i < n; i++) {
                if (interactive(&ws[i]) && in_rect(&ws[i], x, y)) {
                    w = &ws[i];
                    break;
                }
            }
            for (i = 0; i < n; i++)
                if (ws[i].state == 2)
                    ws[i].state = 0;
            if (w) {
                if (w->type == HP_GUI_EDIT) {
                    set_focus(ws, n, (int)(w - ws));
                    w->cur = 0;
                    while (w->buf[w->cur])
                        w->cur++;
                } else {
                    /* 先设置焦点，再标记按下：set_focus 会给每个可交互
                     * widget 赋 state = 0/1，过去会覆盖刚设置的
                     * 按下标记，导致释放步骤（触发 state == 2 的
                     * widget）永远找不到目标——屏幕上的按钮
                     * 无法被点击 */
                    set_focus(ws, n, (int)(w - ws));
                    w->state = 2;
                }
            }
        } else {
            /* 释放：触发被按下的 button */
            hp_gui_widget *hit = 0;
            for (i = 0; i < n; i++)
                if (ws[i].state == 2) {
                    hit = &ws[i];
                    ws[i].state = 1;
                }
            if (hit && hit->on_click) {
                hp_icall2((void *)hit->on_click,
                          (unsigned)hit, (unsigned)hit->udata);
                return hit->id;
            }
        }
        return 0;
    }
    if (etype == HP_EV_KEY && hp_event_key_down(ev)) {
        int k = hp_event_key(ev);
        int f = focus_index(ws, n);
        hp_gui_widget *fw = (f >= 0) ? &ws[f] : 0;
        int nxt, i, cnt;

        if (fw && fw->type == HP_GUI_EDIT) {
            /* 获得焦点的 edit 拥有文本按键 */
            if (k == HP_KEY_BACKSPACE) {
                if (fw->cur > 0) {
                    for (i = fw->cur; fw->buf[i]; i++)
                        fw->buf[i - 1] = fw->buf[i];
                    fw->buf[i - 1] = 0;
                    fw->cur--;
                }
                return 0;
            }
            if (k == HP_KEY_LEFT) {
                if (fw->cur > 0) fw->cur--;
                return 0;
            }
            if (k == HP_KEY_RIGHT) {
                if (fw->buf[fw->cur]) fw->cur++;
                return 0;
            }
            if (k == HP_KEY_ENTER) {
                if (fw->on_click) {
                    hp_icall2((void *)fw->on_click,
                              (unsigned)fw, (unsigned)fw->udata);
                    return fw->id;
                }
                return 0;
            }
            if (k != HP_KEY_UP && k != HP_KEY_DOWN) {
                char c = hp_gui_key_char(k);
                if (c)
                    hp_gui_edit_insert(fw, &c);
                return 0;   /* 已消费（含功能键：由 app 处理） */
            }
        }

        /* 焦点导航 */
        if (k == HP_KEY_UP || k == HP_KEY_LEFT) {
            nxt = f;
            cnt = 0;
            for (i = f - 1; cnt < n; i--, cnt++) {
                int idx = (i + n) % n;
                if (interactive(&ws[idx])) { nxt = idx; break; }
            }
            set_focus(ws, n, nxt);
        } else if (k == HP_KEY_RIGHT || k == HP_KEY_DOWN) {
            nxt = f;
            cnt = 0;
            for (i = f + 1; cnt < n; i++, cnt++) {
                int idx = i % n;
                if (interactive(&ws[idx])) { nxt = idx; break; }
            }
            set_focus(ws, n, nxt);
        } else if (k == HP_KEY_ENTER && fw) {
            if (fw->type == HP_GUI_BUTTON && fw->on_click) {
                hp_icall2((void *)fw->on_click,
                          (unsigned)fw, (unsigned)fw->udata);
                return fw->id;
            }
        }
        return 0;
    }
    return 0;
}
