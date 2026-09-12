/*
 * API/ABI 探针 —— 接口示例
 *
 * ⚠️ AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
 *
 * 目的：用**同一份源码**验证 `prime-tcc` 的用户侧 C API 在两种环境下都成立：
 *   * 目标环境：HP Prime G1 上的 TCC（本仓库用 arm-none-eabi-gcc 代验）
 *   * 主机环境：宿主 gcc（PC）
 *
 * 本文件做两件事：
 *   1) 编译期断言（C99 数组技巧，TCC 不支持 _Static_assert）检查 ABI 假设；
 *   2) 调用全部 13 个公开头文件中的 API，确保声明在两个平台都能解析。
 *
 * 注意：**不链接**。hp_* 的已就位实现位于 toolchain/sdk/，其余仍缺失
 * （见 ../../../prime-tcc/STATUS.md），因此只做 `-c` 编译验证。
 * 契约细节见 ../../../prime-tcc/API_ABI_CONTRACT.md。
 */

#include "prime.h"
#include "hp_gfx.h"
#include "hp_input.h"
#include "hp_math.h"
#include "hp_string.h"
#include "hp_fonts.h"
#include "hp_gui.h"
#include "hp_fixmath.h"
#include "hp_random.h"
#include "hp_codec.h"
#include "hp_sys.h"
#include "hp_icall.h"
#include "hp_image.h"

/* ---- 编译期断言 --------------------------------------------------------
 * 条件为假时数组长度为负，编译失败并报出 abi_assert_<name> 便于定位。 */
#define ABI_ASSERT(name, cond) \
    typedef char abi_assert_##name[(cond) ? 1 : -1]

ABI_ASSERT(int_is_32, sizeof(int) == 4);
ABI_ASSERT(bool_is_1, sizeof(bool) == 1);
ABI_ASSERT(hp_color_is_32, sizeof(hp_color) == 4);
ABI_ASSERT(hp_fx_is_32, sizeof(hp_fx) == 4);
ABI_ASSERT(hp_grob_px_is_pointer, sizeof(((struct hp_grob *)0)->px) == sizeof(void *));
ABI_ASSERT(hp_event_words, sizeof(((struct hp_event *)0)->data) == 32 * sizeof(unsigned));

#if defined(__arm__)
/* 目标环境：ILP32 + 软浮点（见 API_ABI_CONTRACT.md §2/§3） */
ABI_ASSERT(arm_size_t_is_32, sizeof(size_t) == 4);
ABI_ASSERT(arm_pointer_is_32, sizeof(void *) == 4);
ABI_ASSERT(arm_long_is_32, sizeof(long) == 4);
#else
/* 主机环境：size_t 必须与指针同宽（LP64 下为 8 字节） */
ABI_ASSERT(host_size_t_matches_pointer, sizeof(size_t) == sizeof(void *));
#endif

/* ---- API 覆盖：每个头文件都要被用到 ---------------------------------- */

/* prime.h / stdbool.h */
static int probe_prime(void)
{
    char b[64];
    unsigned long n;
    bool flag = true;                     /* stdbool.h -> _Bool */

    printf("probe: %d %08x\n", 42, 0xBEEF);      /* 变参宏 -> hp_printf8 */
    sprintf(b, "%s=%d", "k", 7);                  /* 变参宏 -> hp_sprintf8 */
    puts("probe");
    prints("probe");
    printd(1);
    printx(1);
    n = (unsigned long)strlen(b);
    memcpy(b, "x", 1);
    memset(b, 0, sizeof b);
    return (int)n + abs(-1) + (flag ? 1 : 0);
}

/* hp_gfx.h */
static int probe_gfx(void)
{
    static const unsigned pix[4] = { 0xFFFFFFFFu, 0, 0, 0xFFFFFFFFu };
    hp_color c = HP_RGB(0x12, 0x34, 0x56);

    hp_clear(c);
    hp_pixel(0, 0, HP_WHITE);
    hp_fill_rect(0, 0, 2, 2, c);
    hp_line(0, 0, 1, 1, c);
    hp_blit(pix, 2, 2, 0, 0);                    /* 上游新增 */
    hp_blit_key(pix, 2, 2, 0, 0, c);
    hp_blit_scaled(pix, 2, 2, 0, 0, 4, 4, c);
    return hp_gfx_w() + hp_gfx_h();
}

/* hp_input.h */
static int probe_input(void)
{
    struct hp_event ev;
    int i;

    ev.data[0] = HP_EV_KEY;                       /* 事件类型常量 */
    for (i = 0; i < 32; ++i) {
        ev.data[i] = 0;
    }
    return (int)(ev.data[0] != 0);
}

/* hp_math.h */
static double probe_math(double x)
{
    return hp_sqrt(x) + hp_sin(x);
}

/* hp_string.h */
static int probe_string(void)
{
    char *p = strchr("abc", 'b');                 /* 由 hp_string.h 声明 */

    return p ? (int)(p - "abc") : -1;
}

/* hp_fonts.h */
static int probe_fonts(void)
{
    hp_font *f = hp_font_mono(16);

    return hp_font_h(f) + hp_font_w(f, "abc");
}

/* hp_gui.h */
static int probe_gui(void)
{
    return (int)hp_gui_key_char(0);      /* 仅检查声明可用 */
}

/* hp_fixmath.h */
static int probe_fixmath(void)
{
    hp_fx a = hp_fx_int(3);
    hp_fx b = hp_fx_mul(a, HP_FX_ONE);

    return hp_fx_trunc(hp_fx_add(a, b));
}

/* hp_random.h */
static unsigned probe_random(void)
{
    hp_rng r;

    hp_rng_seed(&r, 1u);
    return hp_rng_u32(&r) ^ hp_noise2_hash(1, 2, 3u);
}

/* hp_codec.h */
static unsigned probe_codec(void)
{
    const char *s = "abc";

    return hp_crc32(s, 3u) ^ hp_adler32(s, 3u);
}

/* hp_sys.h */
static void *probe_sys(void)
{
    void *p = hp_sys_malloc(16u);

    hp_sys_free(p);
    return p;
}

/* hp_icall.h —— TCC 生成的代码不保留 r4-r11，从 GCC 运行时回调 TCC 函数
 * 必须走 hp_icall2（见 API_ABI_CONTRACT.md §2）。 */
static int probe_icall(void (*cb)(int), int arg)
{
    return hp_icall2((void *)cb, (unsigned)arg, 0u);
}

/* hp_image.h */
static int probe_image(const unsigned char *png, unsigned len)
{
    hp_image *img = hp_image_load_png_mem(png, len);
    int w;

    if (!img) {
        return -1;
    }
    w = hp_image_w(img);
    hp_image_draw(img, 0, 0);
    hp_image_draw_key(img, 0, 0, HP_BLACK);
    hp_image_draw_scaled(img, 0, 0, 32, 24);
    hp_image_free(img);
    return w;
}

/* 汇总入口（不叫 main，避免与用户程序入口冲突） */
int probe_run(void (*cb)(int), const unsigned char *png, unsigned png_len)
{
    return probe_prime() + probe_gfx() + probe_input() +
           (int)probe_math(2.0) + probe_string() + probe_fonts() +
           probe_gui() + probe_fixmath() + (int)probe_random() +
           (int)probe_codec() + (probe_sys() != 0) +
           probe_icall(cb, 1) + probe_image(png, png_len);
}
