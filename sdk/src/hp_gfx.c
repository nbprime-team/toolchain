/*
 * hp_gfx.c -- 用于 HP Prime G1 上 TCC 编译程序的图形库。
 *
 * 自包含的纯 C（可在计算器上由 TCC 编译）。唯一的
 * 固件依赖是 get_lcd (svc 0x1008d)，由 rt_svc.o 提供。
 *
 * Framebuffer 布局（已对照可用的 gbemu 移植版验证）：
 *   get_lcd() -> r0 = P;  Q = *(u32*)P;  w = *(u16*)(Q+2);
 *   h = *(u16*)(Q+4);  fb = *(u32*)(Q+16);  pixels 为 u32 0x00RRGGBB。
 *
 * 性能说明（技术取自 gbemu 的 lcd 渲染器）：
 *   - LCD 结构体由 hp_gfx_init() 只获取一次并缓存；没有
 *     每次绘制的系统调用，
 *   - 热点图元通过行指针直接写入 framebuffer，
 *     使用展开的 word 存储（gbemu 的 draw_filled_rect_ui / lcd_draw_line
 *     采用相同的后自增存储约定），而非逐像素调用
 *     hp_pixel，
 *   - 裁剪被提升到外层：hp_hline / hp_fill_rect / hp_line / hp_text 一次性
 *     裁剪整段，然后内层循环是纯存储，
 *   - hp_fill_circle 单调地移动半宽（总边缘
 *     工作量为 O(r)，而非逐行重新扫描的 O(r^2)）。
 */
#include "hp_gfx.h"

/* memory（由 hp_rt.c 提供，用户程序链接它） */
typedef unsigned int size_t;
void *malloc(size_t n);
void  free(void *p);

/* ---- 固件 get_lcd (rt_svc.o) ---- */
unsigned hp_svc_getlcd(unsigned dummy);

/* ---- 状态 ---- */
static unsigned int *g_fb;    /* LCD framebuffer 基址（来自 get_lcd） */
static int g_w;               /* LCD 宽度 */
static int g_h;               /* LCD 高度 */
static int g_ok;              /* 初始化完成 */
/* 当前绘制目标：默认为屏幕 fb，或选定的 grob */
static unsigned int *g_tgt;
static int g_tw;
static int g_th;

/* 内嵌的 5x8 font（定义在本文件末尾） */
static const unsigned char hp_font5x8[640];

/* ---- 底层填充：n 次展开的 word 存储 ---- */
static void fill_words(unsigned int *p, unsigned int v, int n)
{
    while (n >= 8) {
        p[0] = v; p[1] = v; p[2] = v; p[3] = v;
        p[4] = v; p[5] = v; p[6] = v; p[7] = v;
        p += 8; n -= 8;
    }
    while (n-- > 0)
        *p++ = v;
}

int hp_gfx_init(void)
{
    unsigned long p, q;
    if (g_ok)
        return 1;
    p = hp_svc_getlcd(0);
    if (!p)
        return 0;
    q = *(unsigned int *)p;          /* LCD 信息结构体 */
    if (!q)
        return 0;
    g_w = *(unsigned short *)(q + 2);
    g_h = *(unsigned short *)(q + 4);
    g_fb = (unsigned int *)(*(unsigned int *)(q + 16));
    if (!g_fb || g_w <= 0 || g_h <= 0)
        return 0;
    g_ok = 1;
    g_tgt = g_fb;
    g_tw = g_w;
    g_th = g_h;
    return 1;
}

int hp_gfx_w(void) { return g_w; }
int hp_gfx_h(void) { return g_h; }

/* 针对当前目标做带边界检查的存储；无符号比较技巧
 * 用一次判断即排除负值 */
static void px_chk(int x, int y, unsigned int v)
{
    if ((unsigned)x < (unsigned)g_tw && (unsigned)y < (unsigned)g_th)
        g_tgt[(unsigned)y * (unsigned)g_tw + (unsigned)x] = v;
}

void hp_pixel(int x, int y, hp_color c)
{
    if (!g_ok)
        return;
    px_chk(x, y, (unsigned int)c);
}

void hp_pixel_a(int x, int y, hp_color c, unsigned a)
{
    unsigned d, r, g, b;
    if (!g_ok || a == 0)
        return;
    if ((unsigned)x >= (unsigned)g_tw || (unsigned)y >= (unsigned)g_th)
        return;
    if (a >= 15) {
        g_tgt[(unsigned)y * (unsigned)g_tw + (unsigned)x] = (unsigned int)c;
        return;
    }
    d = g_tgt[(unsigned)y * (unsigned)g_tw + (unsigned)x];
    /* 对 R,G,B 各自进行 lerp：dst*(15-a)/15 + src*a/15（a 为 4-bit） */
    r = ((d >> 16 & 0xFF) * (15 - a) + (c >> 16 & 0xFF) * a + 7) / 15;
    g = ((d >>  8 & 0xFF) * (15 - a) + (c >>  8 & 0xFF) * a + 7) / 15;
    b = ((d       & 0xFF) * (15 - a) + (c       & 0xFF) * a + 7) / 15;
    g_tgt[(unsigned)y * (unsigned)g_tw + (unsigned)x] =
        0xFF000000u | (r << 16) | (g << 8) | b;
}

hp_color hp_get_pixel(int x, int y)
{
    if (!g_ok)
        return 0;
    if ((unsigned)x >= (unsigned)g_tw || (unsigned)y >= (unsigned)g_th)
        return 0;
    return (hp_color)g_tgt[(unsigned)y * (unsigned)g_tw + (unsigned)x];
}

void hp_clear(hp_color c)
{
    if (!g_ok)
        return;
    fill_words(g_tgt, (unsigned int)c, g_tw * g_th);
}

void hp_hline(int x, int y, int w, hp_color c)
{
    if (!g_ok || w <= 0)
        return;
    if ((unsigned)y >= (unsigned)g_th)
        return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > g_tw) w = g_tw - x;
    if (w <= 0)
        return;
    fill_words(g_tgt + (unsigned)y * (unsigned)g_tw + (unsigned)x,
               (unsigned int)c, w);
}

void hp_vline(int x, int y, int h, hp_color c)
{
    unsigned int *p;
    if (!g_ok || h <= 0)
        return;
    if ((unsigned)x >= (unsigned)g_tw)
        return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > g_th) h = g_th - y;
    if (h <= 0)
        return;
    p = g_tgt + (unsigned)y * (unsigned)g_tw + (unsigned)x;
    while (h-- > 0) {
        *p = (unsigned int)c;
        p += g_tw;
    }
}

void hp_fill_rect(int x, int y, int w, int h, hp_color c)
{
    unsigned int *p;
    if (!g_ok || w <= 0 || h <= 0)
        return;
    /* 一次性裁剪整个矩形，然后逐行 word 填充 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_tw) w = g_tw - x;
    if (y + h > g_th) h = g_th - y;
    if (w <= 0 || h <= 0)
        return;
    p = g_tgt + (unsigned)y * (unsigned)g_tw + (unsigned)x;
    while (h-- > 0) {
        fill_words(p, (unsigned int)c, w);
        p += g_tw;
    }
}

void hp_rect(int x, int y, int w, int h, hp_color c)
{
    if (!g_ok)
        return;
    if (w <= 0 || h <= 0)
        return;
    hp_hline(x, y, w, c);
    hp_hline(x, y + h - 1, w, c);
    hp_vline(x, y, h, c);
    hp_vline(x + w - 1, y, h, c);
}

/* ---- Bresenham 直线，使用 Cohen-Sutherland 对目标裁剪 ----
 * 完全可见的直线直接跳过裁剪循环；随后光栅化器
 * 以纯存储运行（端点保证在内部）。步进顺序
 * 与之前相同的 Bresenham 步序，只是没有逐像素调用。 */
#define OC_L 1
#define OC_R 2
#define OC_B 4
#define OC_T 8

static int outcode(int x, int y)
{
    int o = 0;
    if (x < 0) o |= OC_L;
    else if (x >= g_tw) o |= OC_R;
    if (y < 0) o |= OC_B;
    else if (y >= g_th) o |= OC_T;
    return o;
}

void hp_line(int x0, int y0, int x1, int y1, hp_color c)
{
    int dx, dy, sx, sy, err, e2;
    unsigned int v = (unsigned int)c;
    if (!g_ok)
        return;
    for (;;) {
        int co = outcode(x0, y0);
        int c1 = outcode(x1, y1);
        int c;
        if (!(co | c1))
            break;                    /* 两端都在内部 */
        if (co & c1)
            return;                   /* 平凡拒绝 */
        c = co ? co : c1;             /* 裁剪越界的那一端 */
        if (c & OC_T) {
            x0 = x0 + (x1 - x0) * (g_th - 1 - y0) / (y1 - y0);
            y0 = g_th - 1;
        } else if (c & OC_B) {
            x0 = x0 + (x1 - x0) * (0 - y0) / (y1 - y0);
            y0 = 0;
        } else if (c & OC_R) {
            y0 = y0 + (y1 - y0) * (g_tw - 1 - x0) / (x1 - x0);
            x0 = g_tw - 1;
        } else {
            y0 = y0 + (y1 - y0) * (0 - x0) / (x1 - x0);
            x0 = 0;
        }
        if (c != co) {                /* 我们裁剪的是 x1 而非 x0：换回来 */
            int t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
        }
    }
    dx = x1 > x0 ? x1 - x0 : x0 - x1;
    dy = y1 > y0 ? y1 - y0 : y0 - y1;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx - dy;
    {
        unsigned int *p = g_tgt + (unsigned)y0 * (unsigned)g_tw + (unsigned)x0;
        int st = sy > 0 ? g_tw : -g_tw;
        for (;;) {
            *p = v;
            if (x0 == x1 && y0 == y1)
                break;
            e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; p += sx; }
            if (e2 < dx)  { err += dx; y0 += sy; p += st; }
        }
    }
}

/* 中点画圆（轮廓） */
void hp_circle(int cx, int cy, int r, hp_color c)
{
    int x = r, y = 0, err = 1 - r;
    unsigned int v = (unsigned int)c;
    if (!g_ok || r < 0)
        return;
    while (x >= y) {
        px_chk(cx + x, cy + y, v);
        px_chk(cx - x, cy + y, v);
        px_chk(cx + x, cy - y, v);
        px_chk(cx - x, cy - y, v);
        px_chk(cx + y, cy + x, v);
        px_chk(cx - y, cy + x, v);
        px_chk(cx + y, cy - x, v);
        px_chk(cx - y, cy - x, v);
        y++;
        if (err < 0)
            err += 2 * y + 1;
        else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

int hp_text_w(const char *s)
{
    int n = 0;
    if (!s)
        return 0;
    /* 每个字符前进 6 px（5 glyph + 1 间隔）；文本占据
     * [x, x + n)，因此下一个元素从 x + n 开始（无重叠） */
    while (*s) { n += 6; s++; }
    return n;
}

void hp_text_bg(int x, int y, const char *s, hp_color fg, hp_color bg)
{
    int w;
    if (!g_ok || !s)
        return;
    w = hp_text_w(s);
    if (w > 0)
        hp_fill_rect(x, y, w, 8, bg);
    hp_text(x, y, s, fg);
}

void hp_text(int x, int y, const char *s, hp_color c)
{
    unsigned char ch;
    unsigned int v = (unsigned int)c;
    if (!g_ok || !s)
        return;
    while (*s) {
        ch = (unsigned char)*s++;
        if (ch >= 0x20 && ch < 0x80) {
            /* 5x8 font，128 个 glyph，按原始字符码索引 */
            const unsigned char *gl = hp_font5x8 + ch * 5;
            if (x >= 0 && y >= 0 && x + HP_FONT_W <= g_tw &&
                y + HP_FONT_H <= g_th) {
                /* 快速路径：glyph 框完全位于目标内 —— 每个置位
                 * 直接存储，不做逐像素裁剪 */
                unsigned int *p =
                    g_tgt + (unsigned)y * (unsigned)g_tw + (unsigned)x;
                int i;
                for (i = 0; i < HP_FONT_W; i++) {
                    unsigned char bits = gl[i];
                    unsigned int *q;
                    if (!bits)
                        continue;
                    q = p + i;
                    if (bits & 0x01) q[0] = v;
                    if (bits & 0x02) q[g_tw] = v;
                    if (bits & 0x04) q[g_tw * 2] = v;
                    if (bits & 0x08) q[g_tw * 3] = v;
                    if (bits & 0x10) q[g_tw * 4] = v;
                    if (bits & 0x20) q[g_tw * 5] = v;
                    if (bits & 0x40) q[g_tw * 6] = v;
                    if (bits & 0x80) q[g_tw * 7] = v;
                }
            } else {
                /* 慢速路径：glyph 靠近/越过边缘，逐像素裁剪 */
                int i, j;
                for (i = 0; i < HP_FONT_W; i++) {
                    unsigned char bits = gl[i];
                    if (!bits)
                        continue;
                    for (j = 0; j < HP_FONT_H; j++)
                        if (bits & (1u << j))
                            hp_pixel(x + i, y + j, v);
                }
            }
        }
        x += 6;
    }
}

/* ---- 三角形 ----
 * hp_triangle：三条 Bresenham 边。hp_fill_triangle：扫描线填充，
 * 顶点按 y 排序，左/右 x 用整数运算插值，
 * 沿长边 (p0->p2) 和短边 (p0->p1, p1->p2) 进行；
 * 退化三角形坍缩为一条水平线段或单个点。 */
void hp_triangle(int x0, int y0, int x1, int y1, int x2, int y2, hp_color c)
{
    if (!g_ok)
        return;
    hp_line(x0, y0, x1, y1, c);
    hp_line(x1, y1, x2, y2, c);
    hp_line(x2, y2, x0, y0, c);
}

void hp_fill_triangle(int x0, int y0, int x1, int y1,
                      int x2, int y2, hp_color c)
{
    int t, y;
    if (!g_ok)
        return;
    /* 按 y 排序顶点：y0 <= y1 <= y2（x 随之移动） */
    if (y0 > y1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (y1 > y2) {
        t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t;
        if (y0 > y1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    }
    if (y0 == y2) {
        /* 三个顶点都在同一条扫描线上 */
        int xm = x0, xM = x0;
        if (x1 < xm) xm = x1;
        if (x2 < xm) xm = x2;
        if (x1 > xM) xM = x1;
        if (x2 > xM) xM = x2;
        hp_hline(xm, y0, xM - xm + 1, c);
        return;
    }
    for (y = y0; y <= y2; y++) {
        int xa, xb;
        /* 纯 int 运算：只要坐标在
         * +-46340 之内，|dx*dy| < 2^31 就成立 —— 远超本库可寻址的任何 LCD/GROB ——
         * 因此从不需要 64-bit 乘/除辅助函数 */
        xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);        /* 长边 */
        if (y < y1 || y1 == y2) {         /* 位于中间顶点之上 / 处 */
            xb = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        } else {                          /* 在中间顶点之下 */
            xb = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        }
        if (xa > xb) { t = xa; xa = xb; xb = t; }
        hp_hline(xa, y, xb - xa + 1, c);
    }
}

/* ---- 实心圆（扫描线，O(r) 边缘步进）----
 * |y| 处的半宽 dx 向中间增大、越过中间后减小；
 * 在行间保持它，把逐行重新扫描（原来的 O(r^2)）变成
 * 每行少量自增。 */
void hp_fill_circle(int cx, int cy, int r, hp_color c)
{
    int y, dx, rsq;
    if (!g_ok || r < 0)
        return;
    rsq = r * r;
    dx = 0;
    for (y = -r; y <= r; y++) {
        int t = rsq - y * y;
        while (dx > 0 && dx * dx > t) dx--;
        while ((dx + 1) * (dx + 1) <= t) dx++;
        hp_hline(cx - dx, cy + y, 2 * dx + 1, c);
    }
}

/* ---- GROB 图层缓冲区 ---- */
hp_grob *hp_grob_new(int w, int h)
{
    hp_grob *g;
    unsigned *px;
    int n;
    if (w <= 0 || h <= 0)
        return 0;
    n = w * h;
    g = (hp_grob *)malloc(sizeof(hp_grob) + (size_t)n * 4);
    if (!g)
        return 0;
    px = (unsigned *)((char *)g + sizeof(hp_grob));
    g->w = w;
    g->h = h;
    g->px = px;
    for (n--; n >= 0; n--)
        px[n] = 0;
    return g;
}

void hp_grob_free(hp_grob *g)
{
    if (g)
        free(g);
}

int hp_grob_w(const hp_grob *g) { return g ? g->w : 0; }
int hp_grob_h(const hp_grob *g) { return g ? g->h : 0; }

int hp_target_w(void) { return g_ok ? g_tw : 0; }
int hp_target_h(void) { return g_ok ? g_th : 0; }

void hp_grob_select(hp_grob *g)
{
    if (!g_ok)
        return;
    if (g) {
        g_tgt = g->px;
        g_tw = g->w;
        g_th = g->h;
    } else {
        g_tgt = g_fb;
        g_tw = g_w;
        g_th = g_h;
    }
}

void hp_grob_blit(hp_grob *g, int x, int y)
{
    int sy, n, sx0, sx1, ncopy;
    unsigned int *dst;
    unsigned int *src;
    if (!g_ok || !g)
        return;
    /* 与其他所有图元一样，blit 作用于当前绘制目标：
     * 默认为屏幕，或选定的 GROB（grob 到 grob 的拷贝）。
     * 因此裁剪使用目标的尺寸，这样小的 GROB
     * 目标绝不会被越界写入。不支持把 GROB blit 到
     * 其自身。 */
    if (y >= g_th || y + g->h <= 0 || x >= g_tw || x + g->w <= 0)
        return;
    if (y < 0) {
        sy = -y;
        n = g->h - sy;
        y = 0;
    } else {
        sy = 0;
        n = g->h;
        if (y + n > g_th)
            n = g_th - y;
    }
    sx0 = x < 0 ? -x : 0;
    sx1 = g->w;
    if (x + sx1 > g_tw)
        sx1 = g_tw - x;
    if (sx1 <= sx0)
        return;
    ncopy = sx1 - sx0;
    dst = g_tgt + (unsigned)y * g_tw + x + sx0;
    src = g->px + (unsigned)sy * g->w + sx0;
    while (n-- > 0) {
        int k = ncopy;
        unsigned int *d = dst;
        const unsigned int *sp = src;
        while (k >= 4) {
            d[0] = sp[0]; d[1] = sp[1]; d[2] = sp[2]; d[3] = sp[3];
            d += 4; sp += 4; k -= 4;
        }
        while (k-- > 0)
            *d++ = *sp++;
        dst += g_tw;
        src += g->w;
    }
}

/* ---- 原始 pixel 数组 blit（见 hp_gfx.h）-------------------------------
 * 三者都作用于当前绘制目标，并像其他所有图元一样裁剪。
 * hp_blit/hp_blit_key 用逐行循环拷贝一个矩形；
 * hp_blit_scaled 使用可分离的面积滤波重采样（放大时
 * 用线性插值，缩小时用 box 平均）。 */

void hp_blit(const unsigned *pix, int w, int h, int x, int y)
{
    const unsigned *src;
    unsigned int *dst;
    int sy, sx, ny, nx, ty;
    if (!g_ok || !pix || w <= 0 || h <= 0)
        return;
    if (y >= g_th || y + h <= 0 || x >= g_tw || x + w <= 0)
        return;
    if (y < 0) { sy = -y; ny = h + y; y = 0; }
    else       { sy = 0;  ny = h; if (y + ny > g_th) ny = g_th - y; }
    if (x < 0) { sx = -x; nx = w + x; x = 0; }
    else       { sx = 0;  nx = w; if (x + nx > g_tw) nx = g_tw - x; }
    if (ny <= 0 || nx <= 0)
        return;
    src = pix + (unsigned)sy * (unsigned)w + (unsigned)sx;
    dst = g_tgt + (unsigned)y * (unsigned)g_tw + (unsigned)x;
    for (ty = 0; ty < ny; ty++) {
        int tx;
        for (tx = 0; tx < nx; tx++)
            dst[tx] = src[tx];
        src += w;
        dst += g_tw;
    }
}

void hp_blit_key(const unsigned *pix, int w, int h, int x, int y,
                 hp_color key)
{
    const unsigned *src;
    unsigned int *dst;
    unsigned k = (unsigned)key;
    int sy, sx, ny, nx, ty;
    if (!g_ok || !pix || w <= 0 || h <= 0)
        return;
    if (y >= g_th || y + h <= 0 || x >= g_tw || x + w <= 0)
        return;
    if (y < 0) { sy = -y; ny = h + y; y = 0; }
    else       { sy = 0;  ny = h; if (y + ny > g_th) ny = g_th - y; }
    if (x < 0) { sx = -x; nx = w + x; x = 0; }
    else       { sx = 0;  nx = w; if (x + nx > g_tw) nx = g_tw - x; }
    if (ny <= 0 || nx <= 0)
        return;
    src = pix + (unsigned)sy * (unsigned)w + (unsigned)sx;
    dst = g_tgt + (unsigned)y * (unsigned)g_tw + (unsigned)x;
    for (ty = 0; ty < ny; ty++) {
        int tx;
        for (tx = 0; tx < nx; tx++)
            if (src[tx] != k)
                dst[tx] = src[tx];
        src += w;
        dst += g_tw;
    }
}

/* ---- hp_blit_scaled 的重采样核心 --------------------------------
 * 对源区间 [a,b) 内的 src[j*stride] 做加权平均
 * （Q16.16，u32：支持最大 8192 px 的源尺寸）。一维
 * 且可分离：
 *   - 目标比源宽  -> 区间短于一个 pixel：
 *     两个重叠的 pixel 按其覆盖率混合 = 线性
 *     插值（平滑放大）
 *   - 目标更窄               -> 区间跨越多个 pixel
 *     且每一个都按其覆盖率贡献 = box 平均
 *     （缩小时无锯齿）
 * 24-bit RGB 等于 key 的 pixel 权重为零；一个单元中
 * 每个源 pixel 都是 key 时返回 key 本身（保持透明）。
 * 三个逐通道累加器 —— 打包的 RGB 累加会在通道间泄漏
 * 进位（红/蓝边缘处蓝色渗入绿色）。
 * NOTE：区间参数故意用 u32 —— 此 TCC 会错误传递 u64
 * 函数参数，而 u64 局部累加器编译正常。
 * 输出 alpha 始终为 0xFF。 */
static unsigned blit_resample1d(const unsigned *src, int stride,
                                unsigned a, unsigned b, hp_color key)
{
    unsigned long long sr = 0, sg = 0, sb = 0, tw = 0;
    unsigned p = a >> 16, e = (b - 1) >> 16;
    unsigned krgb = (unsigned)key & 0xFFFFFFu;
    for (;;) {
        unsigned lo = p << 16, hi = lo + 65536;
        unsigned cov;
        unsigned c;
        if (lo < a) lo = a;
        if (hi > b) hi = b;
        cov = hi - lo;
        c = src[p * (unsigned)stride] & 0xFFFFFFu;
        if (cov && c != krgb) {
            sr += (unsigned long long)(c >> 16) * cov;
            sg += (unsigned long long)((c >> 8) & 0xFF) * cov;
            sb += (unsigned long long)(c & 0xFF) * cov;
            tw += cov;
        }
        if (p == e)
            break;
        p++;
    }
    if (!tw)
        return key;
    return 0xFF000000u |
           ((unsigned)(sr / tw) << 16) |
           ((unsigned)(sg / tw) << 8) |
           (unsigned)(sb / tw);
}

void hp_blit_scaled(const unsigned *pix, int w, int h,
                    int x, int y, int dw, int dh, hp_color key)
{
    unsigned *tmp;
    int tx, ty, dx1, dy1;
    if (!g_ok || !pix || w <= 0 || h <= 0 || dw <= 0 || dh <= 0)
        return;
    dx1 = x + dw;
    dy1 = y + dh;
    if (dx1 <= 0 || dy1 <= 0 || x >= g_tw || y >= g_th)
        return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (dx1 > g_tw) dx1 = g_tw;
    if (dy1 > g_th) dy1 = g_th;

    /* 通过 dw x h 临时缓冲区进行可分离的两遍重采样；
     * 分配失败时回退到普通的最近邻 */
    tmp = (unsigned *)malloc((size_t)dw * (size_t)h * 4);
    if (tmp) {
        unsigned stepx = (unsigned)(((unsigned long long)w << 16) / dw);
        unsigned stepy = (unsigned)(((unsigned long long)h << 16) / dh);
        /* 第 1 遍：水平方向 pix(w x h) -> tmp(dw x h) */
        for (ty = 0; ty < h; ty++) {
            const unsigned *row = pix + (unsigned)ty * (unsigned)w;
            unsigned *out = tmp + (unsigned)ty * (unsigned)dw;
            for (tx = 0; tx < dw; tx++)
                out[tx] = blit_resample1d(row, 1,
                                          (unsigned)tx * stepx,
                                          (unsigned)(tx + 1) * stepx,
                                          key);
        }
        /* 第 2 遍：垂直方向 tmp(dw x h) -> 目标（裁剪后的矩形） */
        for (ty = y; ty < dy1; ty++) {
            unsigned int *dst = g_tgt + (unsigned)ty * (unsigned)g_tw;
            unsigned a = (unsigned)(ty - y) * stepy;
            unsigned b = (unsigned)(ty - y + 1) * stepy;
            for (tx = x; tx < dx1; tx++) {
                /* tmp 的列索引是相对目标 (tx - x) 的，绝不是
                 * 屏幕坐标 */
                unsigned p = blit_resample1d(tmp + (unsigned)(tx - x), dw,
                                             a, b, key);
                if (p != (unsigned)key)
                    dst[tx] = p;
            }
        }
        free(tmp);
        return;
    }

    /* 回退：最近邻（无需临时缓冲区） */
    for (ty = y; ty < dy1; ty++) {
        const unsigned *src = pix +
            (unsigned)((ty - y) * h / dh) * (unsigned)w;
        unsigned int *dst = g_tgt + (unsigned)ty * (unsigned)g_tw;
        for (tx = x; tx < dx1; tx++) {
            unsigned p = src[(unsigned)((tx - x) * w / dw)];
            if (p != (unsigned)key)
                dst[tx] = p;
        }
    }
}

/* HP Prime 5x8 font：128 个 glyph x 5 列字节，bit0 = 顶部 pixel。 */
static const unsigned char hp_font5x8[640] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x5B, 0x4F, 0x5B, 0x3E, 0x3E, 0x6B,
    0x4F, 0x6B, 0x3E, 0x1C, 0x3E, 0x7C, 0x3E, 0x1C, 0x18, 0x3C, 0x7E, 0x3C,
    0x18, 0x1C, 0x57, 0x7D, 0x57, 0x1C, 0x1C, 0x5E, 0x7F, 0x5E, 0x1C, 0x00,
    0x18, 0x3C, 0x18, 0x00, 0xFF, 0xE7, 0xC3, 0xE7, 0xFF, 0x00, 0x18, 0x24,
    0x18, 0x00, 0xFF, 0xE7, 0xDB, 0xE7, 0xFF, 0x30, 0x48, 0x3A, 0x06, 0x0E,
    0x26, 0x29, 0x79, 0x29, 0x26, 0x40, 0x7F, 0x05, 0x05, 0x07, 0x40, 0x7F,
    0x05, 0x25, 0x3F, 0x5A, 0x3C, 0xE7, 0x3C, 0x5A, 0x7F, 0x3E, 0x1C, 0x1C,
    0x08, 0x08, 0x1C, 0x1C, 0x3E, 0x7F, 0x14, 0x22, 0x7F, 0x22, 0x14, 0x5F,
    0x5F, 0x00, 0x5F, 0x5F, 0x06, 0x09, 0x7F, 0x01, 0x7F, 0x00, 0x66, 0x89,
    0x95, 0x6A, 0x60, 0x60, 0x60, 0x60, 0x60, 0x94, 0xA2, 0xFF, 0xA2, 0x94,
    0x08, 0x04, 0x7E, 0x04, 0x08, 0x10, 0x20, 0x7E, 0x20, 0x10, 0x08, 0x08,
    0x2A, 0x1C, 0x08, 0x08, 0x1C, 0x2A, 0x08, 0x08, 0x1E, 0x10, 0x10, 0x10,
    0x10, 0x0C, 0x1E, 0x0C, 0x1E, 0x0C, 0x30, 0x38, 0x3E, 0x38, 0x30, 0x06,
    0x0E, 0x3E, 0x0E, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F,
    0x00, 0x00, 0x00, 0x07, 0x00, 0x07, 0x00, 0x14, 0x7F, 0x14, 0x7F, 0x14,
    0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x23, 0x13, 0x08, 0x64, 0x62, 0x36, 0x49,
    0x56, 0x20, 0x50, 0x00, 0x08, 0x07, 0x03, 0x00, 0x00, 0x1C, 0x22, 0x41,
    0x00, 0x00, 0x41, 0x22, 0x1C, 0x00, 0x2A, 0x1C, 0x7F, 0x1C, 0x2A, 0x08,
    0x08, 0x3E, 0x08, 0x08, 0x00, 0x80, 0x70, 0x30, 0x00, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x00, 0x00, 0x60, 0x60, 0x00, 0x20, 0x10, 0x08, 0x04, 0x02,
    0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, 0x42, 0x7F, 0x40, 0x00, 0x72, 0x49,
    0x49, 0x49, 0x46, 0x21, 0x41, 0x49, 0x4D, 0x33, 0x18, 0x14, 0x12, 0x7F,
    0x10, 0x27, 0x45, 0x45, 0x45, 0x39, 0x3C, 0x4A, 0x49, 0x49, 0x31, 0x41,
    0x21, 0x11, 0x09, 0x07, 0x36, 0x49, 0x49, 0x49, 0x36, 0x46, 0x49, 0x49,
    0x29, 0x1E, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x40, 0x34, 0x00, 0x00,
    0x00, 0x08, 0x14, 0x22, 0x41, 0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x41,
    0x22, 0x14, 0x08, 0x02, 0x01, 0x59, 0x09, 0x06, 0x3E, 0x41, 0x5D, 0x59,
    0x4E, 0x7C, 0x12, 0x11, 0x12, 0x7C, 0x7F, 0x49, 0x49, 0x49, 0x36, 0x3E,
    0x41, 0x41, 0x41, 0x22, 0x7F, 0x41, 0x41, 0x41, 0x3E, 0x7F, 0x49, 0x49,
    0x49, 0x41, 0x7F, 0x09, 0x09, 0x09, 0x01, 0x3E, 0x41, 0x41, 0x51, 0x73,
    0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00, 0x41, 0x7F, 0x41, 0x00, 0x20, 0x40,
    0x41, 0x3F, 0x01, 0x7F, 0x08, 0x14, 0x22, 0x41, 0x7F, 0x40, 0x40, 0x40,
    0x40, 0x7F, 0x02, 0x1C, 0x02, 0x7F, 0x7F, 0x04, 0x08, 0x10, 0x7F, 0x3E,
    0x41, 0x41, 0x41, 0x3E, 0x7F, 0x09, 0x09, 0x09, 0x06, 0x3E, 0x41, 0x51,
    0x21, 0x5E, 0x7F, 0x09, 0x19, 0x29, 0x46, 0x26, 0x49, 0x49, 0x49, 0x32,
    0x03, 0x01, 0x7F, 0x01, 0x03, 0x3F, 0x40, 0x40, 0x40, 0x3F, 0x1F, 0x20,
    0x40, 0x20, 0x1F, 0x3F, 0x40, 0x38, 0x40, 0x3F, 0x63, 0x14, 0x08, 0x14,
    0x63, 0x03, 0x04, 0x78, 0x04, 0x03, 0x61, 0x59, 0x49, 0x4D, 0x43, 0x00,
    0x7F, 0x41, 0x41, 0x41, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x41, 0x41,
    0x41, 0x7F, 0x04, 0x02, 0x01, 0x02, 0x04, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x00, 0x03, 0x07, 0x08, 0x00, 0x20, 0x54, 0x54, 0x78, 0x40, 0x7F, 0x28,
    0x44, 0x44, 0x38, 0x38, 0x44, 0x44, 0x44, 0x28, 0x38, 0x44, 0x44, 0x28,
    0x7F, 0x38, 0x54, 0x54, 0x54, 0x18, 0x00, 0x08, 0x7E, 0x09, 0x02, 0x18,
    0xA4, 0xA4, 0x9C, 0x78, 0x7F, 0x08, 0x04, 0x04, 0x78, 0x00, 0x44, 0x7D,
    0x40, 0x00, 0x20, 0x40, 0x40, 0x3D, 0x00, 0x7F, 0x10, 0x28, 0x44, 0x00,
    0x00, 0x41, 0x7F, 0x40, 0x00, 0x7C, 0x04, 0x78, 0x04, 0x78, 0x7C, 0x08,
    0x04, 0x04, 0x78, 0x38, 0x44, 0x44, 0x44, 0x38, 0xFC, 0x18, 0x24, 0x24,
    0x18, 0x18, 0x24, 0x24, 0x18, 0xFC, 0x7C, 0x08, 0x04, 0x04, 0x08, 0x48,
    0x54, 0x54, 0x54, 0x24, 0x04, 0x04, 0x3F, 0x44, 0x24, 0x3C, 0x40, 0x40,
    0x20, 0x7C, 0x1C, 0x20, 0x40, 0x20, 0x1C, 0x3C, 0x40, 0x30, 0x40, 0x3C,
    0x44, 0x28, 0x10, 0x28, 0x44, 0x4C, 0x90, 0x90, 0x90, 0x7C, 0x44, 0x64,
    0x54, 0x4C, 0x44, 0x00, 0x08, 0x36, 0x41, 0x00, 0x00, 0x00, 0x77, 0x00,
    0x00, 0x00, 0x41, 0x36, 0x08, 0x00, 0x02, 0x01, 0x02, 0x04, 0x02, 0x3C,
    0x26, 0x23, 0x26, 0x3C,
};
