/*
 * hp_fonts.c -- HP Prime 图形库的 font 渲染器。
 *
 * 自包含的纯 C 实现（可在计算器上用 TCC 编译）。  glyph
 * 表位于 hp_fonts_data.c（由 rt/tools/fontgen2.py 生成）；
 * 绘制通过 hp_pixel 完成，因此文本会自动落到屏幕或
 * 当前选中的 GROB 上。
 *
 * 每个 glyph 的布局：画笔从 (x, y) = 行框左上角开始；
 * 墨水 bitmap 绘制在 (x + g.xoff, y + g.yoff)，随后画笔按
 * g.adv 前进。  hp_font_w 累加 advance，hp_font_h 是行
 * 高（ascent + descent）。
 */
#include "hp_gfx.h"
#include "hp_fonts.h"

/* ---- 由 fontgen2.py 生成的表（rt/hp_fonts_data.c） ---- */
#define HP_FONT_N 4

static const hp_font *pick(int mono, int size)
{
    int i, best = 0, bd = 1 << 30;
    for (i = 0; i < HP_FONT_N; i++) {
        const hp_font *f = mono ? hp_font_mono_data(i) : hp_font_prop_data(i);
        int d = f->size - size;
        if (d < 0)
            d = -d;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return mono ? hp_font_mono_data(best) : hp_font_prop_data(best);
}

hp_font *hp_font_mono(int size) { return (hp_font *)pick(1, size); }
hp_font *hp_font_prop(int size) { return (hp_font *)pick(0, size); }

int hp_font_h(const hp_font *f)
{
    return f ? f->line_h : 0;
}

int hp_font_advance(const hp_font *f, int ch)
{
    if (!f)
        return 0;
    if (ch < f->first || ch > f->last)
        return f->line_h / 2;               /* 未知字符：半行 */
    return f->glyphs[ch - f->first].adv;
}

int hp_font_w(const hp_font *f, const char *s)
{
    int w = 0;
    if (!f || !s)
        return 0;
    while (*s)
        w += hp_font_advance(f, (unsigned char)*s++);
    return w;
}

void hp_font_draw(const hp_font *f, int x, int y, const char *s, hp_color c)
{
    unsigned char ch;
    if (!f || !s)
        return;
    while (*s) {
        ch = (unsigned char)*s++;
        if (ch >= f->first && ch <= f->last) {
            const hp_glyph *g = &f->glyphs[ch - f->first];
            int rowbytes = (g->w + 1) >> 1;   /* 每 byte 2 个 pixel */
            int j, i;
            for (j = 0; j < g->h; j++) {
                const unsigned char *rp =
                    f->bits + g->bits + (unsigned)j * rowbytes;
                for (i = 0; i < g->w; i++) {
                    /* 高 nibble = 左侧 pixel。  使用掩码形式读取，
                     * 而不要用 (byte >> shift)：在 ARM 上，对寄存器按 0
                     * 位移位是 LSR #32（结果为 0），会静默丢弃每个 byte
                     * 最左侧的 pixel（ARM926 行为，经模拟器
                     * 验证）。 */
                    unsigned a = rp[i >> 1] & (i & 1 ? 0x0Fu : 0xF0u);
                    if (!a)
                        continue;
                    a = (i & 1) ? a : (a >> 4);
                    if (a == 15)
                        hp_pixel(x + g->xoff + i, y + g->yoff + j, c);
                    else
                        hp_pixel_a(x + g->xoff + i, y + g->yoff + j,
                                   c, a);
                }
            }
        }
        x += hp_font_advance(f, ch);
    }
}

void hp_font_draw_bg(const hp_font *f, int x, int y, const char *s,
                     hp_color fg, hp_color bg)
{
    int w;
    if (!f || !s)
        return;
    w = hp_font_w(f, s);
    if (w > 0)
        hp_fill_rect(x, y, w, f->line_h, bg);
    hp_font_draw(f, x, y, s, fg);
}
