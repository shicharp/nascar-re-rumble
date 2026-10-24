#include <math.h>
#include <string.h>
#include "nr.h"
#include "exe.h"
#include "mdec.h"

/* Codigos VLC (como el original, Mdec_DecodeVlc 8009905c): los patrones de bits son fijos (800abf10: 0x60 codigos
 * de hasta 13 bits; 800ac090: 0x80 codigos que empiezan por 8 ceros, de los que se miran 9 bits mas) y su valor,
 * una palabra del MDEC (run << 10 | nivel de 10 bits), sale de una tabla de 0xE0 valores: la de por defecto
 * (800ac290, la del bitstream de Sony) o la que trae cada video en su bloque VLC0 (80098ee0 / 80098f28).
 * Valores especiales: 0xFE00 fin del bloque, 0x7C1F escape (16 bits sin codificar). */
#define VLC_EOB 0xfe00
#define VLC_ESC 0x7c1f
static uint8_t  s_len1[0x2000];   /* por los 13 bits siguientes: longitud del codigo */
static uint16_t s_val1[0x2000];
static uint8_t  s_len2[0x200];    /* despues de 8 ceros, por los 9 siguientes */
static uint16_t s_val2[0x200];
static int      s_tableOk;

static const uint8_t s_zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

/* Matriz de cuantizacion por defecto de la PS1 (la de DecDCTReset) */
static const uint8_t s_quant[64] = {
    2, 16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34, 34, 38, 22, 22, 26, 27, 29, 34, 37, 40,
    22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58, 26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83 };

static float s_cos[8][8];
static int   s_init;

/* 80098ee0 / 80098f28: values = 0xE0 u16 (NULL = la tabla por defecto del ejecutable) */
void Mdec_SetVlcTable(const uint8_t* values)
{
    if (!values)
        values = Exe_Ptr(0x800ac290u);
    const uint8_t* codes1 = Exe_Ptr(0x800abf10u);
    const uint8_t* codes2 = Exe_Ptr(0x800ac090u);
    if (!values || !codes1 || !codes2)
    {
        s_tableOk = 0;
        return;
    }
    memset(s_len1, 0, sizeof(s_len1));
    memset(s_len2, 0, sizeof(s_len2));
    for (int i = 0; i < 0x60; i++)
    {
        int len = codes1[i * 4], code = NR_Read16(codes1 + i * 4 + 2);
        uint16_t v = (uint16_t)NR_Read16(values + i * 2);
        for (int k = 0; k < 1 << (13 - len); k++)
        {
            s_len1[(code >> 3) | k] = (uint8_t)len;
            s_val1[(code >> 3) | k] = v;
        }
    }
    for (int i = 0; i < 0x80; i++)
    {
        int len = codes2[i * 4], code = NR_Read16(codes2 + i * 4 + 2);
        uint16_t v = (uint16_t)NR_Read16(values + (0x60 + i) * 2);
        for (int k = 0; k < 1 << (9 - len); k++)
        {
            s_len2[(code >> 7) | k] = (uint8_t)len;
            s_val2[(code >> 7) | k] = v;
        }
    }
    s_tableOk = 1;
}

static void Mdec_Init(void)
{
    for (int x = 0; x < 8; x++)
        for (int u = 0; u < 8; u++)
            s_cos[x][u] = (float)((u == 0 ? sqrt(0.5) : 1.0) * cos((2 * x + 1) * u * 3.14159265358979 / 16) / 2);
    if (!s_tableOk)
        Mdec_SetVlcTable(NULL);
    s_init = 1;
}

typedef struct
{
    const uint8_t* data;
    int            size;
    int            pos;    /* en bits; palabras u16 LE leidas MSB primero */
} BitReader;

static int Bit(BitReader* br)
{
    int word = br->pos >> 4;
    if (word * 2 + 1 >= br->size)
        return 0;
    int b = 15 - (br->pos & 15);
    br->pos++;
    return ((br->data[word * 2 + 1] << 8 | br->data[word * 2]) >> b) & 1;
}

static int Bits(BitReader* br, int n)
{
    int v = 0;
    while (n--)
        v = (v << 1) | Bit(br);
    return v;
}

static int Peek(BitReader* br, int n)
{
    int pos = br->pos, v = Bits(br, n);
    br->pos = pos;
    return v;
}

static int Signed10(int v)
{
    return (v & 0x200) ? v - 0x400 : v;
}

static int ReadBlock(BitReader* br, int qscale, float* out)
{
    int coef[64] = { 0 };
    coef[0] = Signed10(Bits(br, 10)) * s_quant[0];
    int k = 0;
    for (;;)
    {
        int len, v, t = Peek(br, 13);
        if (t < 0x20)
        {
            br->pos += 8;
            t = Peek(br, 9);
            len = s_len2[t];
            v = s_val2[t];
        }
        else
        {
            len = s_len1[t];
            v = s_val1[t];
        }
        if (!len)
            return 0;                                   /* codigo invalido */
        br->pos += len;
        if (v == VLC_EOB)
            break;
        if (v == VLC_ESC)
            v = Bits(br, 16);
        int run = (v >> 10) & 0x3f, level = Signed10(v & 0x3ff);
        k += run + 1;
        if (k > 63)
            break;
        int z = s_zigzag[k];
        int mag = (level < 0 ? -level : level) * s_quant[z] * qscale;
        coef[z] = level < 0 ? -((mag + 4) >> 3) : (mag + 4) >> 3;
    }

    {
        float tmp[64];
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
            {
                float s = 0;
                for (int u = 0; u < 8; u++)
                    s += s_cos[x][u] * coef[y * 8 + u];
                tmp[y * 8 + x] = s;
            }
        for (int x = 0; x < 8; x++)
            for (int y = 0; y < 8; y++)
            {
                float s = 0;
                for (int v = 0; v < 8; v++)
                    s += s_cos[y][v] * tmp[v * 8 + x];
                out[y * 8 + x] = s;
            }
    }
    return 1;
}

static int Clamp255(float v)
{
    int i = (int)(v + 128.5f);
    return i < 0 ? 0 : (i > 255 ? 255 : i);
}

int Mdec_DecodeFrame(const uint8_t* bs, int size, int w, int h, uint16_t* out)
{
    if (!s_init)
        Mdec_Init();
    int magic = NR_Read16(bs + 2), qscale = NR_Read16(bs + 4), version = NR_Read16(bs + 6);
    if (!s_tableOk || magic != 0x3800 || version != 2 || (w & 15) || (h & 15))
    {
        NR_Log("Mdec: imagen no soportada (%dx%d magic %04x v%d)\n", w, h, magic, version);
        return 0;
    }

    BitReader br = { bs + 8, size - 8, 0 };
    float blk[6][64];
    for (int mx = 0; mx < w / 16; mx++)          /* macrobloques en columnas, como el MDEC */
        for (int my = 0; my < h / 16; my++)
        {
            for (int b = 0; b < 6; b++)           /* Cr, Cb, Y0, Y1, Y2, Y3 */
                if (!ReadBlock(&br, qscale, blk[b]))
                {
                    NR_Log("Mdec: VLC invalido en el macrobloque %d,%d\n", mx, my);
                    return 0;
                }
            for (int py = 0; py < 16; py++)
                for (int px = 0; px < 16; px++)
                {
                    float Y = blk[2 + (py >> 3) * 2 + (px >> 3)][(py & 7) * 8 + (px & 7)];
                    int c = (py >> 1) * 8 + (px >> 1);
                    float cr = blk[0][c], cb = blk[1][c];
                    int r = Clamp255(Y + 1.402f * cr);
                    int g = Clamp255(Y - 0.3437f * cb - 0.7143f * cr);
                    int bl = Clamp255(Y + 1.772f * cb);
                    out[(my * 16 + py) * w + mx * 16 + px] =
                        (uint16_t)(0x8000 | (bl >> 3) << 10 | (g >> 3) << 5 | (r >> 3));
                }
        }
    return 1;
}

int Mdec_DecodeImage(const uint8_t* img, int imgSize, uint16_t* out, int maxPixels, int* outW, int* outH)
{
    int w = NR_Read16(img), h = NR_Read16(img + 2);
    if (w * h > maxPixels)
        return 0;
    if (!Mdec_DecodeFrame(img + 4, imgSize - 4, w, h, out))
        return 0;
    *outW = w;
    *outH = h;
    return 1;
}
