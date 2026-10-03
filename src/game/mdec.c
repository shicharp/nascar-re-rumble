#include <math.h>
#include <string.h>
#include "nr.h"
#include "mdec.h"

/* Tabla AC de MPEG-1 (B.14) que usa el bitstream de PS1: codigo sin signo -> run, level */
static const struct { const char* code; uint8_t run, level; } s_acCodes[] = {
    {"11",0,1},{"011",1,1},{"0100",0,2},{"0101",2,1},{"00101",0,3},{"00111",3,1},{"00110",4,1},
    {"000110",1,2},{"000111",5,1},{"000101",6,1},{"000100",7,1},{"0000110",0,4},{"0000100",2,2},
    {"0000111",8,1},{"0000101",9,1},{"00100110",0,5},{"00100001",0,6},{"00100101",1,3},{"00100100",3,2},
    {"00100111",10,1},{"00100011",11,1},{"00100010",12,1},{"00100000",13,1},{"0000001010",0,7},
    {"0000001100",1,4},{"0000001011",2,3},{"0000001111",4,2},{"0000001001",5,2},{"0000001110",14,1},
    {"0000001101",15,1},{"0000001000",16,1},{"000000011101",0,8},{"000000011000",0,9},{"000000010011",0,10},
    {"000000010000",0,11},{"000000011011",1,5},{"000000010100",2,4},{"000000011100",3,3},{"000000010010",4,3},
    {"000000011110",6,2},{"000000010101",7,2},{"000000010001",8,2},{"000000011111",17,1},{"000000011010",18,1},
    {"000000011001",19,1},{"000000010111",20,1},{"000000010110",21,1},{"0000000011010",0,12},
    {"0000000011001",0,13},{"0000000011000",0,14},{"0000000010111",0,15},{"0000000010110",1,6},
    {"0000000010101",1,7},{"0000000010100",2,5},{"0000000010011",3,4},{"0000000010010",5,3},
    {"0000000010001",9,2},{"0000000010000",10,2},{"0000000011111",22,1},{"0000000011110",23,1},
    {"0000000011101",24,1},{"0000000011100",25,1},{"0000000011011",26,1},{"00000000011111",0,16},
    {"00000000011110",0,17},{"00000000011101",0,18},{"00000000011100",0,19},{"00000000011011",0,20},
    {"00000000011010",0,21},{"00000000011001",0,22},{"00000000011000",0,23},{"00000000010111",0,24},
    {"00000000010110",0,25},{"00000000010101",0,26},{"00000000010100",0,27},{"00000000010011",0,28},
    {"00000000010010",0,29},{"00000000010001",0,30},{"00000000010000",0,31},{"000000000011000",0,32},
    {"000000000010111",0,33},{"000000000010110",0,34},{"000000000010101",0,35},{"000000000010100",0,36},
    {"000000000010011",0,37},{"000000000010010",0,38},{"000000000010001",0,39},{"000000000010000",0,40},
    {"000000000011111",1,8},{"000000000011110",1,9},{"000000000011101",1,10},{"000000000011100",1,11},
    {"000000000011011",1,12},{"000000000011010",1,13},{"000000000011001",1,14},{"0000000000010011",1,15},
    {"0000000000010010",1,16},{"0000000000010001",1,17},{"0000000000010000",1,18},{"0000000000010100",6,3},
    {"0000000000011010",11,2},{"0000000000011001",12,2},{"0000000000011000",13,2},{"0000000000010111",14,2},
    {"0000000000010110",15,2},{"0000000000010101",16,2},{"0000000000011111",27,1},{"0000000000011110",28,1},
    {"0000000000011101",29,1},{"0000000000011100",30,1},{"0000000000011011",31,1},
};

static const uint8_t s_zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

/* Matriz de cuantizacion por defecto de la PS1 (la de DecDCTReset) */
static const uint8_t s_quant[64] = {
    2, 16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34, 34, 38, 22, 22, 26, 27, 29, 34, 37, 40,
    22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58, 26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83 };

/* Arbol binario de la tabla VLC: nodo -> hijos; las hojas guardan run/level */
#define VLC_NODES 512
static int16_t s_vlcChild[VLC_NODES][2];
static int16_t s_vlcLeaf[VLC_NODES];   /* -1 = interno; si no, run << 8 | level, o VLC_EOB / VLC_ESC */
#define VLC_EOB 0x7ffe                 /* "10" */
#define VLC_ESC 0x7fff                 /* "000001": 6 bits de run + 10 de level */
static int     s_vlcCount;
static float   s_cos[8][8];
static int     s_init;

static void InsertCode(const char* code, int16_t leaf)
{
    int node = 0;
    for (const char* c = code; *c; c++)
    {
        int b = *c - '0';
        if (!s_vlcChild[node][b])
            s_vlcChild[node][b] = (int16_t)s_vlcCount++;
        node = s_vlcChild[node][b];
    }
    s_vlcLeaf[node] = leaf;
}

static void Mdec_Init(void)
{
    memset(s_vlcChild, 0, sizeof(s_vlcChild));
    for (int i = 0; i < VLC_NODES; i++)
        s_vlcLeaf[i] = -1;
    s_vlcCount = 1;
    for (size_t i = 0; i < sizeof(s_acCodes) / sizeof(s_acCodes[0]); i++)
        InsertCode(s_acCodes[i].code, (int16_t)(s_acCodes[i].run << 8 | s_acCodes[i].level));
    InsertCode("10", VLC_EOB);
    InsertCode("000001", VLC_ESC);
    for (int x = 0; x < 8; x++)
        for (int u = 0; u < 8; u++)
            s_cos[x][u] = (float)((u == 0 ? sqrt(0.5) : 1.0) * cos((2 * x + 1) * u * 3.14159265358979 / 16) / 2);
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
        int node = 0, run, level;
        do
        {
            node = s_vlcChild[node][Bit(br)];
            if (node == 0)
                return 0;                               /* codigo invalido */
        } while (s_vlcLeaf[node] < 0);

        if (s_vlcLeaf[node] == VLC_EOB)
            break;
        if (s_vlcLeaf[node] == VLC_ESC)
        {
            run = Bits(br, 6);
            level = Signed10(Bits(br, 10));
        }
        else
        {
            run = s_vlcLeaf[node] >> 8;
            level = s_vlcLeaf[node] & 0xff;
            if (Bit(br))
                level = -level;
        }
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

int Mdec_DecodeImage(const uint8_t* img, int imgSize, uint16_t* out, int maxPixels, int* outW, int* outH)
{
    if (!s_init)
        Mdec_Init();

    int w = NR_Read16(img), h = NR_Read16(img + 2);
    int magic = NR_Read16(img + 6), qscale = NR_Read16(img + 8), version = NR_Read16(img + 10);
    if (magic != 0x3800 || version != 2 || w * h > maxPixels || (w & 15) || (h & 15))
    {
        NR_Log("Mdec: imagen no soportada (%dx%d magic %04x v%d)\n", w, h, magic, version);
        return 0;
    }

    BitReader br = { img + 12, imgSize - 12, 0 };
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
    *outW = w;
    *outH = h;
    return 1;
}
