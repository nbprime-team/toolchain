/*
 * hp_image.c -- PNG 解码器 + 图像绘制辅助函数 (见 hp_image.h)。
 *
 * 内置 zlib inflate (RFC 1951): stored 块、固定与动态
 * Huffman、32KB LZ 窗口。各行数据流经 inflater，
 * 反滤波 (None/Sub/Up/Average/Paeth) 并即时转换为 ARGB，
 * 因此从不分配原始图像中间缓冲。
 *
 * 注意: interval/count 变量故意使用 u32 —— 计算器上的
 * TCC 会错误地传递 u64 函数参数 (与 hp_gfx.c 规则相同)。
 */
#include "hp_sys.h"
#include "hp_gfx.h"
#include "hp_image.h"

/* 运行时提供的符号 (hp_rt.c 编译进同一个 rt_core.o) */
void *malloc(size_t n);
void free(void *p);
void prints(const char *s);
#ifndef NULL
#define NULL ((void *)0)
#endif

#define IMG_WMAX 4096
#define IMG_HMAX 4096

/* ---- bit 读取器 (deflate: 字节内 LSB 优先; Huffman 码为
 * MSB 优先, 但 STREAM 每个字节内按 LSB 优先打包每一位) ---- */
typedef struct {
    const unsigned char *d;
    unsigned pos;
    unsigned bitpos;
} img_br;

static unsigned img_bit(img_br *br)
{
    unsigned b = (br->d[br->pos] >> br->bitpos) & 1u;
    if (++br->bitpos == 8) {
        br->bitpos = 0;
        br->pos++;
    }
    return b;
}

/* ---- 规范 (canonical) Huffman ---- */
#define IMS 288                 /* 最大字母表大小 (lit/len) */

typedef struct {
    unsigned char len[IMS];
    unsigned short code[IMS];
    unsigned char bl[16];
    unsigned short order[IMS];  /* 符号最大到 285 > 255: 不能用 u8! */
} img_huff;

static void img_huff_build(img_huff *hf, const unsigned char *len, int n)
{
    unsigned next[16] = {0};
    unsigned first = 0;
    int i, l, o = 0;
    for (i = 0; i < 16; i++)
        hf->bl[i] = 0;
    for (i = 0; i < n; i++) {
        hf->len[i] = len[i];
        if (len[i])
            hf->bl[len[i]]++;
    }
    for (l = 1; l <= 15; l++) {
        next[l] = first;
        first = (first + hf->bl[l]) << 1;
    }
    for (l = 1; l <= 15; l++)
        for (i = 0; i < n; i++)
            if (len[i] == l) {
                hf->code[i] = next[l]++;
                hf->order[o++] = (unsigned short)i;
            }
}

static int img_huff_decode(img_huff *hf, img_br *br)
{
    unsigned cur = 0, first = 0, index = 0;
    int l;
    for (l = 1; l <= 15; l++) {
        cur = (cur << 1) | img_bit(br);
        if (cur - first < hf->bl[l])
            return hf->order[index + (cur - first)];
        index += hf->bl[l];
        first = (first + hf->bl[l]) << 1;
    }
    return -1;
}

/* ---- 解码器状态: 各行流经 32KB LZ 窗口 ---- */
typedef struct {
    unsigned w, h, bpp, ct, row_len;
    const unsigned char *plte;
    unsigned char *cur;         /* 当前行: [filter][w*bpp] */
    unsigned char *prev;        /* 上一行 (已反滤波) */
    unsigned int *argb;         /* 输出 w*h ARGB */
    unsigned row_pos, row;
    unsigned char hist[32768];  /* LZ 窗口 (环形) */
    unsigned hist_idx, total_out;
} img_dec;

static void img_emit_byte(img_dec *st, unsigned char b)
{
    st->cur[st->row_pos++] = b;
    st->hist[st->hist_idx] = b;
    st->hist_idx = (st->hist_idx + 1) & 32767;
    st->total_out++;
    if (st->row_pos == st->row_len) {
        unsigned int *dst = st->argb + (size_t)st->row * st->w;
        const unsigned char *src;
        unsigned i;
        switch (st->cur[0]) {
        case 1:
            for (i = st->bpp; i < st->w * st->bpp; i++)
                st->cur[i] = (unsigned char)(st->cur[i] + st->cur[i - st->bpp]);
            break;
        case 2:
            for (i = 0; i < st->w * st->bpp; i++)
                st->cur[i] = (unsigned char)(st->cur[i] + st->prev[i]);
            break;
        case 3:
            for (i = 0; i < st->w * st->bpp; i++) {
                unsigned a = i >= st->bpp ? st->cur[i - st->bpp] : 0;
                st->cur[i] = (unsigned char)(st->cur[i]
                                             + ((a + st->prev[i]) >> 1));
            }
            break;
        case 4:
            for (i = 0; i < st->w * st->bpp; i++) {
                unsigned a = i >= st->bpp ? st->cur[i - st->bpp] : 0;
                unsigned b2 = st->prev[i];
                unsigned c = i >= st->bpp ? st->prev[i - st->bpp] : 0;
                int p = (int)a + (int)b2 - (int)c;
                unsigned pa = p > (int)a ? (unsigned)(p - a)
                                         : (unsigned)(a - p);
                unsigned pb = p > (int)b2 ? (unsigned)(p - b2)
                                          : (unsigned)(b2 - p);
                unsigned pc = p > (int)c ? (unsigned)(p - c)
                                         : (unsigned)(c - p);
                unsigned pr;
                if (pa <= pb && pa <= pc) pr = a;
                else if (pb <= pc) pr = b2;
                else pr = c;
                st->cur[i] = (unsigned char)(st->cur[i] + pr);
            }
            break;
        default:
            break;              /* 0 = 无 */
        }
        src = st->cur + 1;
        for (i = 0; i < st->w; i++) {
            unsigned r, g, b;
            const unsigned char *p = src + (size_t)i * st->bpp;
            if (st->ct == 0) r = g = b = p[0];
            else if (st->ct == 2) { r = p[0]; g = p[1]; b = p[2]; }
            else if (st->ct == 3) {
                unsigned idx = p[0] * 3;
                r = st->plte[idx];
                g = st->plte[idx + 1];
                b = st->plte[idx + 2];
            } else if (st->ct == 4) r = g = b = p[0];
            else { r = p[0]; g = p[1]; b = p[2]; }
            dst[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
        for (i = 0; i < st->w * st->bpp; i++)
            st->prev[i] = st->cur[i + 1];
        st->row++;
        st->row_pos = 0;
    }
}

/* ---- deflate (RFC 1951) ---- */
static const unsigned char img_cl_order[19] =
    {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
static const unsigned char lx[29] =
    {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const unsigned short lb[29] =
    {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,
     131,163,195,227,258};
static const unsigned char dx[30] =
    {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static const unsigned short db[30] =
    {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,
     2049,3073,4097,6145,8193,12289,16385,24577};

static int img_inflate(const unsigned char *in, unsigned inlen, img_dec *st)
{
    img_br br;
    img_huff hf;
    unsigned char clen[320];
    int final = 0;
    br.d = in;
    br.pos = 2;                 /* 跳过 zlib 头 */
    br.bitpos = 0;
    if (inlen < 6 || (in[0] & 0x0F) != 8 || (in[1] & 0x20)
        || (((in[0] << 8) | in[1]) % 31) != 0)
        return 1;
    while (!final) {
        unsigned btype;
        final = (int)img_bit(&br);
        btype = img_bit(&br) | (img_bit(&br) << 1);
        if (btype == 0) {
            unsigned len, i;
            if (br.bitpos) {
                br.bitpos = 0;
                br.pos++;
            }
            if (br.pos + 4 > inlen)
                return 1;
            len = (unsigned)in[br.pos] | ((unsigned)in[br.pos + 1] << 8);
            br.pos += 4;
            if (br.pos + len > inlen)
                return 1;
            for (i = 0; i < len; i++)
                img_emit_byte(st, in[br.pos + i]);
            br.pos += len;
        } else if (btype == 1 || btype == 2) {
            int i;
            img_huff dhh;
            if (btype == 1) {
                unsigned char fxd[288];
                for (i = 0; i < 288; i++)
                    fxd[i] = (i < 144) ? 8 : (i < 256) ? 9 : (i < 280) ? 7 : 8;
                img_huff_build(&hf, fxd, 288);
            } else {
                unsigned hlit, hdist, hclen, k;
                unsigned char cll[19];
                img_huff chf;
                hlit = img_bit(&br) | (img_bit(&br) << 1) | (img_bit(&br) << 2)
                     | (img_bit(&br) << 3) | (img_bit(&br) << 4);
                hdist = img_bit(&br) | (img_bit(&br) << 1) | (img_bit(&br) << 2)
                      | (img_bit(&br) << 3) | (img_bit(&br) << 4);
                hclen = img_bit(&br) | (img_bit(&br) << 1) | (img_bit(&br) << 2)
                      | (img_bit(&br) << 3);
                hlit += 257;
                hdist += 1;
                hclen += 4;
                if (hlit > 286 || hdist > 30)
                    return 1;
                for (i = 0; i < 19; i++)
                    cll[i] = 0;
                for (i = 0; i < (int)hclen; i++) {
                    unsigned v = 0;
                    for (k = 0; k < 3; k++)
                        v |= img_bit(&br) << k;
                    cll[img_cl_order[i]] = (unsigned char)v;
                }
                img_huff_build(&chf, cll, 19);
                i = 0;
                while (i < (int)(hlit + hdist)) {
                    int s = img_huff_decode(&chf, &br);
                    unsigned v, rep;
                    if (s < 0)
                        return 1;
                    if (s < 16) {
                        clen[i++] = (unsigned char)s;
                    } else {
                        if (s == 16) {
                            rep = 3 + (img_bit(&br) | (img_bit(&br) << 1));
                            if (i == 0) return 1;
                            v = clen[i - 1];
                        } else if (s == 17) {
                            rep = 3 + (img_bit(&br) | (img_bit(&br) << 1)
                                     | (img_bit(&br) << 2));
                            v = 0;
                        } else {
                            rep = 11 + (img_bit(&br) | (img_bit(&br) << 1)
                                      | (img_bit(&br) << 2) | (img_bit(&br) << 3)
                                      | (img_bit(&br) << 4) | (img_bit(&br) << 5)
                                      | (img_bit(&br) << 6));
                            v = 0;
                        }
                        while (rep-- && i < (int)(hlit + hdist))
                            clen[i++] = (unsigned char)v;
                    }
                }
                if (i < (int)(hlit + hdist))
                    return 1;
                img_huff_build(&hf, clen, hlit);
                img_huff_build(&dhh, clen + hlit, hdist);
            }
            for (;;) {
                int s = img_huff_decode(&hf, &br);
                if (s < 0)
                    return 1;
                if (s < 256) {
                    img_emit_byte(st, (unsigned char)s);
                } else if (s == 256) {
                    break;
                } else if (s <= 285) {
                    unsigned sym = (unsigned)(s - 257);
                    unsigned len, dist, i, es;
                    int ds;
                    if (sym >= 29)
                        return 1;
                    len = lb[sym];
                    es = lx[sym];
                    for (i = 0; i < (int)es; i++)
                        len += img_bit(&br) << i;   /* 额外位: LSB 优先 */
                    if (btype == 1) {
                        unsigned dcode = 0;
                        for (i = 0; i < 5; i++)
                            dcode |= img_bit(&br) << i;
                        if (dcode >= 30) return 1;
                        dist = db[dcode];
                        es = dx[dcode];
                        for (i = 0; i < (int)es; i++)
                            dist += img_bit(&br) << i;   /* LSB 优先 */
                    } else {
                        ds = img_huff_decode(&dhh, &br);
                        if (ds < 0 || (unsigned)ds >= 30)
                            return 1;
                        dist = db[ds];
                        es = dx[ds];
                        for (i = 0; i < (int)es; i++)
                            dist += img_bit(&br) << i;   /* LSB 优先 */
                    }
                    if (dist == 0 || dist > st->total_out)
                        return 1;
                    for (i = 0; i < len; i++)
                        img_emit_byte(st,
                                      st->hist[(st->hist_idx - dist) & 32767]);
                } else {
                    return 1;
                }
            }
        } else {
            return 1;
        }
    }
    return st->row == st->h ? 0 : 1;
}

/* ---- PNG chunk 遍历 ---- */
static unsigned img_be32(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16)
         | ((unsigned)p[2] << 8) | (unsigned)p[3];
}

/* 返回数据指针, 设置 *len, 将 *p 前进越过 (len,type,data,crc) */
static const unsigned char *img_next(const unsigned char **p,
                                     const unsigned char *end, unsigned *len)
{
    const unsigned char *q = *p;
    unsigned l;
    if (q + 12 > end)
        return NULL;
    l = img_be32(q);
    if (q + 12 + l > end)
        return NULL;
    *len = l;
    *p = q + 12 + l;
    return q + 8;
}

/* ---- 解码原始 PNG 字节 ---- */
hp_image *hp_image_load_png_mem(const unsigned char *data, unsigned len)
{
    const unsigned char *p, *end;
    unsigned w = 0, h = 0, ct = 0, bpp = 0, plte_n = 0, got_idat = 0;
    unsigned idat_total = 0, idat_off = 0;
    unsigned char plte[768];
    unsigned char *idat = NULL;
    unsigned int *px = NULL;
    img_dec *st = NULL;
    hp_image *img = NULL;
    int rc = 2;                 /* 0 成功, 1 坏文件, 2 解码错误 */

    if (!data || len < 33 || data[0] != 0x89 || data[1] != 'P'
        || data[2] != 'N' || data[3] != 'G' || data[4] != 0x0D
        || data[5] != 0x0A || data[6] != 0x1A || data[7] != 0x0A) {
        prints("IMG: bad signature\n");
        return NULL;
    }
    p = data + 8;
    end = data + len;
    while (p + 12 <= end) {
        unsigned l;
        const unsigned char *d = img_next(&p, end, &l);
        if (!d)
            break;
        if (l == 13 && d[-4] == 'I' && d[-3] == 'H' && d[-2] == 'D'
            && d[-1] == 'R') {
            w = img_be32(d);
            h = img_be32(d + 4);
            ct = d[9];
            if (d[8] != 8 || d[10] != 0 || d[11] != 0 || d[12] != 0
                || w == 0 || h == 0 || w > IMG_WMAX || h > IMG_HMAX
                || (ct != 0 && ct != 2 && ct != 3 && ct != 4 && ct != 6)) {
                prints("IMG: unsupported IHDR\n");
                goto done;
            }
            bpp = (ct == 0 || ct == 3) ? 1 : (ct == 4 ? 2 : (ct == 2 ? 3 : 4));
        } else if (d[-4] == 'P' && d[-3] == 'L' && d[-2] == 'T'
                   && d[-1] == 'E') {
            if (l > 768)
                goto done;
            plte_n = l;
            {
                unsigned i;
                for (i = 0; i < l; i++)
                    plte[i] = d[i];
            }
        } else if (d[-4] == 'I' && d[-3] == 'D' && d[-2] == 'A'
                   && d[-1] == 'T') {
            idat_total += l;
            got_idat = 1;
        } else if (d[-4] == 'I' && d[-3] == 'E' && d[-2] == 'N'
                   && d[-1] == 'D') {
            break;
        }
    }
    if (!w || !got_idat)
        goto done;

    idat = (unsigned char *)malloc(idat_total ? idat_total : 1);
    px = (unsigned int *)malloc((size_t)w * h * 4);
    st = (img_dec *)malloc(sizeof(img_dec));
    if (!idat || !px || !st) {
        prints("IMG: no mem\n");
        goto done;
    }
    p = data + 8;
    idat_off = 0;
    while (p + 12 <= end) {
        unsigned l;
        const unsigned char *d = img_next(&p, end, &l);
        unsigned i;
        if (!d)
            break;
        if (d[-4] == 'I' && d[-3] == 'D' && d[-2] == 'A' && d[-1] == 'T') {
            for (i = 0; i < l; i++)
                idat[idat_off + i] = d[i];
            idat_off += l;
        }
    }

    st->w = w; st->h = h; st->bpp = bpp; st->ct = ct;
    st->row_len = w * bpp + 1;
    st->plte = plte_n ? plte : NULL;
    st->cur = (unsigned char *)malloc(st->row_len);
    st->prev = (unsigned char *)malloc(w * bpp);
    st->argb = px;
    st->row_pos = 0; st->row = 0;
    st->hist_idx = 0; st->total_out = 0;
    if (!st->cur || !st->prev) {
        prints("IMG: no mem\n");
        goto done;
    }
    if (img_inflate(idat, idat_off, st) != 0) {
        prints("IMG: inflate failed\n");
        goto done;
    }
    img = (hp_image *)malloc(sizeof(hp_image));
    if (!img) {
        prints("IMG: no mem\n");
        goto done;
    }
    img->w = (int)w;
    img->h = (int)h;
    img->px = px;
    rc = 0;
done:
    free(idat);
    if (st) {
        free(st->cur);
        free(st->prev);
        free(st);
    }
    if (rc != 0) {
        if (rc == 2)
            prints("IMG: decode error\n");
        free(px);
        free(img);
        return NULL;
    }
    return img;
}

/* ---- 从文件加载 (ASCII 路径 -> 内部 UTF-16) ---- */
hp_image *hp_image_load_png(const char *ascii_path)
{
    unsigned fd, sz;
    unsigned char *buf;
    hp_image *img;
    if (!ascii_path)
        return NULL;
    fd = hp_sys_fopen(ascii_path, "rb");
    if (!fd) {
        prints("IMG: open failed\n");
        return NULL;
    }
    sz = hp_sys_filesize(fd);
    if (!sz) {
        prints("IMG: empty file\n");
        hp_sys_fclose(fd);
        return NULL;
    }
    buf = (unsigned char *)malloc(sz);
    if (!buf) {
        prints("IMG: no mem\n");
        hp_sys_fclose(fd);
        return NULL;
    }
    if (hp_sys_fread((unsigned)(unsigned long)buf, 1, sz, fd) != sz) {
        prints("IMG: read failed\n");
        free(buf);
        hp_sys_fclose(fd);
        return NULL;
    }
    hp_sys_fclose(fd);
    img = hp_image_load_png_mem(buf, sz);
    free(buf);
    return img;
}

void hp_image_free(hp_image *img)
{
    if (!img)
        return;
    free(img->px);
    free(img);
}

int hp_image_w(const hp_image *img) { return img ? img->w : 0; }
int hp_image_h(const hp_image *img) { return img ? img->h : 0; }

/* ---- 绘制辅助函数 (blit API 的薄封装) ---- */
void hp_image_draw(hp_image *img, int x, int y)
{
    if (!img)
        return;
    hp_blit(img->px, img->w, img->h, x, y);
}

void hp_image_draw_key(hp_image *img, int x, int y, hp_color key)
{
    if (!img)
        return;
    hp_blit_key(img->px, img->w, img->h, x, y, key);
}

void hp_image_draw_scaled(hp_image *img, int x, int y, int dw, int dh)
{
    if (!img)
        return;
    hp_blit_scaled(img->px, img->w, img->h, x, y, dw, dh, 0);
}
