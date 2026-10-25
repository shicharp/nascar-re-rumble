#include <stdint.h>
#include <stdlib.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "gfx.h"
#include "render.h"
#include "sprite.h"
#include "globals.h"
#include "vecmath.h"

/* 800aab44: puntos de las formas (x, y en 1/32768 del tamano) */
static const int16_t s_points[124][2] = {
    { 4352, 0 }, { 2176, 3769 }, { -2175, 3769 }, { -4351, 0 }, { -2175, -3768 }, { 2176, -3768 },
    { 3840, 0 }, { 1920, 3326 }, { -1919, 3326 }, { -3839, 0 }, { -1919, -3325 }, { 1920, -3325 },
    { 0, 0 }, { 4096, 0 }, { 2048, 3547 }, { -2047, 3547 }, { -4095, 0 }, { -2047, -3546 },
    { 2048, -3546 }, { -221, -127 }, { 0, -255 }, { 222, -127 }, { 222, 128 }, { 0, 256 },
    { -221, 128 }, { 4352, 0 }, { 3077, 3077 }, { 0, 4352 }, { -3076, 3077 }, { -4351, 0 },
    { -3076, -3076 }, { 0, -4351 }, { 3077, -3076 }, { 3840, 0 }, { 2715, 2715 }, { 0, 3840 },
    { -2714, 2715 }, { -3839, 0 }, { -2714, -2714 }, { 0, -3839 }, { 2715, -2714 }, { 0, 0 },
    { 4096, 0 }, { 2896, 2896 }, { 0, 4096 }, { -2895, 2896 }, { -4095, 0 }, { -2895, -2895 },
    { 0, -4095 }, { 2896, -2895 }, { -236, -97 }, { -97, -236 }, { 98, -236 }, { 237, -97 },
    { 237, 98 }, { 98, 237 }, { -97, 237 }, { -236, 98 }, { 4352, 0 }, { 3769, 2176 },
    { 2176, 3769 }, { 0, 4352 }, { -2175, 3769 }, { -3768, 2176 }, { -4351, 0 }, { -3768, -2175 },
    { -2175, -3768 }, { 0, -4351 }, { 2176, -3768 }, { 3769, -2175 }, { 3840, 0 }, { 3326, 1920 },
    { 1920, 3326 }, { 0, 3840 }, { -1919, 3326 }, { -3325, 1920 }, { -3839, 0 }, { -3325, -1919 },
    { -1919, -3325 }, { 0, -3839 }, { 1920, -3325 }, { 3326, -1919 }, { 0, 0 }, { 4096, 0 },
    { 3547, 2048 }, { 2048, 3547 }, { 0, 4096 }, { -2047, 3547 }, { -3546, 2048 }, { -4095, 0 },
    { -3546, -2047 }, { -2047, -3546 }, { 0, -4095 }, { 2048, -3546 }, { 3547, -2047 }, { -246, -65 },
    { -180, -180 }, { -65, -246 }, { 66, -246 }, { 181, -180 }, { 247, -65 }, { 247, 66 },
    { 181, 181 }, { 66, 247 }, { -65, 247 }, { -180, 181 }, { -246, 66 }, { 0, 0 },
    { 4096, 0 }, { 3784, 1567 }, { 2896, 2896 }, { 1567, 3784 }, { 0, 4096 }, { -1566, 3784 },
    { -2895, 2896 }, { -3783, 1567 }, { -4095, 0 }, { -3783, -1566 }, { -2895, -2895 }, { -1566, -3783 },
    { 0, -4095 }, { 1567, -3783 }, { 2896, -2895 }, { 3784, -1566 },
};

/* 800aad34: poligonos (4 indices de punto; 0x80 = color A del centro, si no color B) */
static const uint8_t s_polys[156][4] = {
    { 0x00, 0x86, 0x01, 0x01 }, { 0x01, 0x87, 0x02, 0x02 }, { 0x02, 0x88, 0x03, 0x03 }, { 0x03, 0x89, 0x04, 0x04 },
    { 0x04, 0x8a, 0x05, 0x05 }, { 0x05, 0x8b, 0x00, 0x00 }, { 0x80, 0x01, 0x02, 0x02 }, { 0x80, 0x02, 0x03, 0x03 },
    { 0x80, 0x03, 0x04, 0x04 }, { 0x80, 0x04, 0x05, 0x05 }, { 0x80, 0x05, 0x06, 0x06 }, { 0x80, 0x06, 0x01, 0x01 },
    { 0x86, 0x87, 0x01, 0x01 }, { 0x87, 0x88, 0x02, 0x02 }, { 0x88, 0x89, 0x03, 0x03 }, { 0x89, 0x8a, 0x04, 0x04 },
    { 0x8a, 0x8b, 0x05, 0x05 }, { 0x8b, 0x86, 0x00, 0x00 }, { 0x06, 0x07, 0x8e, 0x8d }, { 0x8d, 0x8e, 0x01, 0x00 },
    { 0x07, 0x08, 0x8f, 0x8e }, { 0x8e, 0x8f, 0x02, 0x01 }, { 0x08, 0x09, 0x90, 0x8f }, { 0x8f, 0x90, 0x03, 0x02 },
    { 0x09, 0x0a, 0x91, 0x90 }, { 0x90, 0x91, 0x04, 0x03 }, { 0x0a, 0x0b, 0x92, 0x91 }, { 0x91, 0x92, 0x05, 0x04 },
    { 0x0b, 0x06, 0x8d, 0x92 }, { 0x92, 0x8d, 0x00, 0x05 }, { 0x00, 0x01, 0x02, 0x03 }, { 0x03, 0x04, 0x05, 0x00 },
    { 0x00, 0x88, 0x01, 0x01 }, { 0x01, 0x89, 0x02, 0x02 }, { 0x02, 0x8a, 0x03, 0x03 }, { 0x03, 0x8b, 0x04, 0x04 },
    { 0x04, 0x8c, 0x05, 0x05 }, { 0x05, 0x8d, 0x06, 0x06 }, { 0x06, 0x8e, 0x07, 0x07 }, { 0x07, 0x8f, 0x00, 0x00 },
    { 0x80, 0x01, 0x02, 0x02 }, { 0x80, 0x02, 0x03, 0x03 }, { 0x80, 0x03, 0x04, 0x04 }, { 0x80, 0x04, 0x05, 0x05 },
    { 0x80, 0x05, 0x06, 0x06 }, { 0x80, 0x06, 0x07, 0x07 }, { 0x80, 0x07, 0x08, 0x08 }, { 0x80, 0x08, 0x01, 0x01 },
    { 0x88, 0x89, 0x01, 0x01 }, { 0x89, 0x8a, 0x02, 0x02 }, { 0x8a, 0x8b, 0x03, 0x03 }, { 0x8b, 0x8c, 0x04, 0x04 },
    { 0x8c, 0x8d, 0x05, 0x05 }, { 0x8d, 0x8e, 0x06, 0x06 }, { 0x8e, 0x8f, 0x07, 0x07 }, { 0x8f, 0x88, 0x00, 0x00 },
    { 0x08, 0x09, 0x92, 0x91 }, { 0x91, 0x92, 0x01, 0x00 }, { 0x09, 0x0a, 0x93, 0x92 }, { 0x92, 0x93, 0x02, 0x01 },
    { 0x0a, 0x0b, 0x94, 0x93 }, { 0x93, 0x94, 0x03, 0x02 }, { 0x0b, 0x0c, 0x95, 0x94 }, { 0x94, 0x95, 0x04, 0x03 },
    { 0x0c, 0x0d, 0x96, 0x95 }, { 0x95, 0x96, 0x05, 0x04 }, { 0x0d, 0x0e, 0x97, 0x96 }, { 0x96, 0x97, 0x06, 0x05 },
    { 0x0e, 0x0f, 0x98, 0x97 }, { 0x97, 0x98, 0x07, 0x06 }, { 0x0f, 0x08, 0x91, 0x98 }, { 0x98, 0x91, 0x00, 0x07 },
    { 0x00, 0x01, 0x02, 0x03 }, { 0x04, 0x05, 0x06, 0x07 }, { 0x03, 0x04, 0x07, 0x00 }, { 0x00, 0x8c, 0x01, 0x01 },
    { 0x01, 0x8d, 0x02, 0x02 }, { 0x02, 0x8e, 0x03, 0x03 }, { 0x03, 0x8f, 0x04, 0x04 }, { 0x04, 0x90, 0x05, 0x05 },
    { 0x05, 0x91, 0x06, 0x06 }, { 0x06, 0x92, 0x07, 0x07 }, { 0x07, 0x93, 0x08, 0x08 }, { 0x08, 0x94, 0x09, 0x09 },
    { 0x09, 0x95, 0x0a, 0x0a }, { 0x0a, 0x96, 0x0b, 0x0b }, { 0x0b, 0x97, 0x00, 0x00 }, { 0x80, 0x01, 0x02, 0x02 },
    { 0x80, 0x02, 0x03, 0x03 }, { 0x80, 0x03, 0x04, 0x04 }, { 0x80, 0x04, 0x05, 0x05 }, { 0x80, 0x05, 0x06, 0x06 },
    { 0x80, 0x06, 0x07, 0x07 }, { 0x80, 0x07, 0x08, 0x08 }, { 0x80, 0x08, 0x09, 0x09 }, { 0x80, 0x09, 0x0a, 0x0a },
    { 0x80, 0x0a, 0x0b, 0x0b }, { 0x80, 0x0b, 0x0c, 0x0c }, { 0x80, 0x0c, 0x01, 0x01 }, { 0x8c, 0x8d, 0x01, 0x01 },
    { 0x8d, 0x8e, 0x02, 0x02 }, { 0x8e, 0x8f, 0x03, 0x03 }, { 0x8f, 0x90, 0x04, 0x04 }, { 0x90, 0x91, 0x05, 0x05 },
    { 0x91, 0x92, 0x06, 0x06 }, { 0x92, 0x93, 0x07, 0x07 }, { 0x93, 0x94, 0x08, 0x08 }, { 0x94, 0x95, 0x09, 0x09 },
    { 0x95, 0x96, 0x0a, 0x0a }, { 0x96, 0x97, 0x0b, 0x0b }, { 0x97, 0x8c, 0x00, 0x00 }, { 0x0c, 0x0d, 0x9a, 0x99 },
    { 0x99, 0x9a, 0x01, 0x00 }, { 0x0d, 0x0e, 0x9b, 0x9a }, { 0x9a, 0x9b, 0x02, 0x01 }, { 0x0e, 0x0f, 0x9c, 0x9b },
    { 0x9b, 0x9c, 0x03, 0x02 }, { 0x0f, 0x10, 0x9d, 0x9c }, { 0x9c, 0x9d, 0x04, 0x03 }, { 0x10, 0x11, 0x9e, 0x9d },
    { 0x9d, 0x9e, 0x05, 0x04 }, { 0x11, 0x12, 0x9f, 0x9e }, { 0x9e, 0x9f, 0x06, 0x05 }, { 0x12, 0x13, 0xa0, 0x9f },
    { 0x9f, 0xa0, 0x07, 0x06 }, { 0x13, 0x14, 0xa1, 0xa0 }, { 0xa0, 0xa1, 0x08, 0x07 }, { 0x14, 0x15, 0xa2, 0xa1 },
    { 0xa1, 0xa2, 0x09, 0x08 }, { 0x15, 0x16, 0xa3, 0xa2 }, { 0xa2, 0xa3, 0x0a, 0x09 }, { 0x16, 0x17, 0xa4, 0xa3 },
    { 0xa3, 0xa4, 0x0b, 0x0a }, { 0x17, 0x0c, 0x99, 0xa4 }, { 0xa4, 0x99, 0x00, 0x0b }, { 0x01, 0x02, 0x03, 0x04 },
    { 0x00, 0x01, 0x04, 0x05 }, { 0x0b, 0x00, 0x05, 0x06 }, { 0x0a, 0x0b, 0x06, 0x07 }, { 0x09, 0x0a, 0x07, 0x08 },
    { 0x80, 0x01, 0x02, 0x02 }, { 0x80, 0x02, 0x03, 0x03 }, { 0x80, 0x03, 0x04, 0x04 }, { 0x80, 0x04, 0x05, 0x05 },
    { 0x80, 0x05, 0x06, 0x06 }, { 0x80, 0x06, 0x07, 0x07 }, { 0x80, 0x07, 0x08, 0x08 }, { 0x80, 0x08, 0x09, 0x09 },
    { 0x80, 0x09, 0x0a, 0x0a }, { 0x80, 0x0a, 0x0b, 0x0b }, { 0x80, 0x0b, 0x0c, 0x0c }, { 0x80, 0x0c, 0x0d, 0x0d },
    { 0x80, 0x0d, 0x0e, 0x0e }, { 0x80, 0x0e, 0x0f, 0x0f }, { 0x80, 0x0f, 0x10, 0x10 }, { 0x80, 0x10, 0x01, 0x01 },
};

/* 800aafa4: formas {primer punto, n puntos, n poligonos (| 0x80 = planos), primer poligono} */
static const uint8_t s_shapes[16][4] = {
    { 0x0d, 0x0c, 0x06, 0x00 }, { 0x0c, 0x07, 0x06, 0x06 }, { 0x0d, 0x0c, 0x06, 0x0c }, { 0x00, 0x13, 0x0c, 0x12 },
    { 0x0d, 0x06, 0x82, 0x1e }, { 0x2a, 0x10, 0x08, 0x20 }, { 0x29, 0x09, 0x08, 0x28 }, { 0x2a, 0x10, 0x08, 0x30 },
    { 0x19, 0x19, 0x10, 0x38 }, { 0x2a, 0x08, 0x83, 0x48 }, { 0x53, 0x18, 0x0c, 0x4b }, { 0x52, 0x0d, 0x0c, 0x57 },
    { 0x53, 0x18, 0x0c, 0x63 }, { 0x3a, 0x25, 0x18, 0x6f }, { 0x53, 0x0c, 0x85, 0x87 }, { 0x6b, 0x11, 0x10, 0x8c },
};

/* Entrada de la lista (0x18 bytes en DAT_800b5500) */
typedef struct
{
    uint32_t colorA;    /* +0x00 (<< 8 en el original) */
    uint32_t colorB;    /* +0x04 */
    uint32_t sxy;       /* +0x08 */
    int      sz;        /* +0x0C */
    uint16_t size;      /* +0x10 */
    int      type;      /* +0x14 */
    float    px, py;    /* solo en el port: centro preciso (PGXP) */
} Sprite;

static Sprite s_sprites[SPRITE_MAX];
static int    s_count;      /* DAT_800af854 */

void Sprite_Clear(void)
{
    s_count = 0;
}

void Sprite_Add(int type, uint32_t colorA, uint32_t colorB, uint32_t sxy, int sz, int size, float px, float py)
{
    if (s_count >= SPRITE_MAX)
        return;
    Sprite* s = &s_sprites[s_count++];
    s->colorA = colorA;
    s->colorB = colorB;
    s->sxy = sxy;
    s->sz = sz;
    s->size = (uint16_t)size;
    s->type = type;
    s->px = px;
    s->py = py;
}

#define RGBW(p, n, c) ((p)->r##n = (uint8_t)(c), (p)->g##n = (uint8_t)((c) >> 8), (p)->b##n = (uint8_t)((c) >> 16))

/* 80045268 */
void Sprite_DrawAll(void)
{
    const RenderProj* pr = Render_GetProjection();
    unsigned limX = 0xa0u << 4, limY = 0x78u << 4;   /* bytes bajos de DAT_800afa34/35 << 4 */
    int lastSz = -0x800000;
    for (int i = 0; i < s_count; i++)
    {
        const Sprite* s = &s_sprites[i];
        if ((unsigned)s->type >= sizeof(s_shapes) / sizeof(s_shapes[0]))
            continue;
        const uint8_t* shape = s_shapes[s->type];
        unsigned scale = s->size;
        if (s->sz != 0)
        {
            if (s->sz <= 0x3f)
                continue;
            scale = (unsigned)((int)s->size * pr->h / s->sz);
            if ((int)scale <= 0x10)
                continue;
        }
        if (s->sz != lastSz)
        {
            /* modo de dibujo: semitransparencia aditiva (B + F) para los poligonos sin textura */
            DR_TPAGE* t = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
            if (!t)
                return;
            setDrawTPage(t, 1, 1, 0xa0);
            Gfx_AddPrimDepth(s->sz, t);
            lastSz = s->sz;
        }

        int16_t vx[32], vy[32];
        float fx[32], fy[32];
        uint8_t fl[32];
        int cx = (int16_t)s->sxy, cy = (int16_t)(s->sxy >> 16);
        int np = shape[1] < 32 ? shape[1] : 32;
        for (int k = 0; k < np; k++)
        {
            const int16_t* p = s_points[shape[0] + k];
            int ox = (int)((p[0] * (int)scale + 0x4000) >> 15);
            int oy = (int)((p[1] * (int)scale + 0x4000) >> 15);
            vx[k] = (int16_t)(cx + ox);
            vy[k] = (int16_t)(cy + oy);
            fx[k] = s->px + (float)ox;
            fy[k] = s->py + (float)oy;
            fl[k] = (uint8_t)(((unsigned)(cx + ox) < limX) << 6 | ((unsigned)(cy + oy) < limY) << 7);
        }

        int flat = shape[2] & 0x80;
        int npoly = shape[2] & 0x7f;
        for (int k = 0; k < npoly; k++)
        {
            const uint8_t* q = s_polys[shape[3] + k];
            int a = q[0] & 0x7f, b = q[1] & 0x7f, c = q[2] & 0x7f, d = q[3] & 0x7f;
            if (a >= np || b >= np || c >= np || d >= np)
                continue;
            if ((uint8_t)(fl[a] | fl[b] | fl[c] | fl[d]) != 0xc0)
                continue;
            int tri = q[2] == q[3];
#define COL(idx) ((q[idx] & 0x80) ? s->colorA : s->colorB)
            void* prim;
            if (!flat)
            {
                if (tri)
                {
                    POLY_G3* p = (POLY_G3*)(prim = Gfx_AllocPrim(sizeof(POLY_G3)));
                    if (!p)
                        return;
                    setPolyG3(p);
                    setXY3(p, vx[a], vy[a], vx[b], vy[b], vx[d], vy[d]);
                    RGBW(p, 0, COL(0));
                    RGBW(p, 1, COL(1));
                    RGBW(p, 2, COL(3));
                    setSemiTrans(p, 1);
#if USE_PGXP
                    p->x0 = fx[a]; p->y0 = fy[a]; p->x1 = fx[b]; p->y1 = fy[b]; p->x2 = fx[d]; p->y2 = fy[d];
#endif
                }
                else
                {
                    POLY_G4* p = (POLY_G4*)(prim = Gfx_AllocPrim(sizeof(POLY_G4)));
                    if (!p)
                        return;
                    setPolyG4(p);
                    setXY4(p, vx[a], vy[a], vx[b], vy[b], vx[d], vy[d], vx[c], vy[c]);
                    RGBW(p, 0, COL(0));
                    RGBW(p, 1, COL(1));
                    RGBW(p, 2, COL(3));
                    RGBW(p, 3, COL(2));
                    setSemiTrans(p, 1);
#if USE_PGXP
                    p->x0 = fx[a]; p->y0 = fy[a]; p->x1 = fx[b]; p->y1 = fy[b];
                    p->x2 = fx[d]; p->y2 = fy[d]; p->x3 = fx[c]; p->y3 = fy[c];
#endif
                }
            }
            else
            {
                if (tri)
                {
                    POLY_F3* p = (POLY_F3*)(prim = Gfx_AllocPrim(sizeof(POLY_F3)));
                    if (!p)
                        return;
                    setPolyF3(p);
                    setXY3(p, vx[a], vy[a], vx[b], vy[b], vx[d], vy[d]);
                    RGBW(p, 0, s->colorA);
                    setSemiTrans(p, 1);
#if USE_PGXP
                    p->x0 = fx[a]; p->y0 = fy[a]; p->x1 = fx[b]; p->y1 = fy[b]; p->x2 = fx[d]; p->y2 = fy[d];
#endif
                }
                else
                {
                    POLY_F4* p = (POLY_F4*)(prim = Gfx_AllocPrim(sizeof(POLY_F4)));
                    if (!p)
                        return;
                    setPolyF4(p);
                    setXY4(p, vx[a], vy[a], vx[b], vy[b], vx[d], vy[d], vx[c], vy[c]);
                    RGBW(p, 0, s->colorA);
                    setSemiTrans(p, 1);
#if USE_PGXP
                    p->x0 = fx[a]; p->y0 = fy[a]; p->x1 = fx[b]; p->y1 = fy[b];
                    p->x2 = fx[d]; p->y2 = fy[d]; p->x3 = fx[c]; p->y3 = fy[c];
#endif
                }
            }
#undef COL
            Gfx_AddPrimDepthPgxp(s->sz, prim, 0xffff);
        }
    }
}

/* ---------------------------------------------------------------- destello del sol (800457e0) */

static void Flare_Add(int type, int b, int g, int r, int b2, int g2, int r2, int x, int y, int size)
{
    Sprite_Add(type, (uint32_t)(b << 16 | g << 8 | r), (uint32_t)(b2 << 16 | g2 << 8 | r2),
               (uint32_t)(y << 16 | (x & 0xffff)), 0, size, (float)x, (float)y);
}

/* (x, y): esquina del cuadrado de 5x5 del sol en pantalla; intensity: lo que se ve de el (16.16, suavizado);
 * type: el byte 0x13 del TSUN (1 a 4). Las manchas van del sol hacia el centro de la pantalla y siguen por el otro
 * lado; se apagan con la distancia al centro (1.0 = 200 pixeles) y con el brillo de la luz del sol */
void Sprite_AddLensFlare(int x, int y, int intensity, int type, int bright)
{
    int cy = g_SplitScreen ? 0x3c : 0x78;
    int dx = 0xa0 - x, dy = cy - y;
    int ax = (abs(dx) << 16) / 200, ay = (abs(dy) << 16) / 200;
    int dist = Math_SquareRoot12(Fix_Mul16(ax, ax) + Fix_Mul16(ay, ay)) * 4;
    unsigned k = (unsigned)(0x10000 - (dist > 0x10000 ? 0x10000 : dist));
    k = (unsigned)(((uint64_t)k * (unsigned)intensity) >> 16);
    k = (unsigned)(((uint64_t)k * (unsigned)bright) >> 16);
    if ((int)k > 0x10000)
        k = 0x10000;
    /* l = k^(1/4) / 4 en 16.16 (dos raices); s = 4 l */
    int l = Math_SquareRoot12(Math_SquareRoot12((int)k) << 2);
    unsigned fade = (unsigned)(0x10000 - l * 4);   /* lo que le falta para el maximo */
#define L(c) ((l * (c)) >> 16)
#define S(n, m) (((int)((fade * (n)) >> 16) + (m)) * 8)
    switch (type)
    {
    case 1:
        Flare_Add(1, L(0x44), L(100), L(0x50), L(0x20), L(0x30), L(0x28), x - (dx * 3 >> 3), y - (dy * 3 >> 3), S(0x1e, 10));
        Flare_Add(0xd, 0, L(0x14), L(0xb4), 0, 0, 0, x, y, S(0x3c, 0x20));
        Flare_Add(0xb, L(300), L(0x168), L(0x168), 0, 0, 0, x, y, S(0x32, 0x14));
        Flare_Add(4, L(0x28), L(0x28), L(0x3c), 0, 0, 0, x + (dx >> 1), y + (dy >> 1), 0x48);
        Flare_Add(4, L(0x28), L(0x50), L(0x50), 0, 0, 0, x + (dx * 3 >> 2), y + (dy * 3 >> 2), S(0x1e, 10));
        Flare_Add(4, L(0x28), L(0x3c), L(0x28), 0, 0, 0, 0xa0, cy, 0x28);
        Flare_Add(4, L(0x14), L(0x34), L(0x50), 0, 0, 0, 0xa0 + (dx >> 2), cy + (dy >> 2), S(0x19, 10));
        Flare_Add(1, L(0x3c), L(0x3c), L(0x3c), L(0x3c), L(0x14), 0, 0xa0 + (dx * 5 >> 3), cy + (dy * 5 >> 3),
                  S(0x1e, 0x14));
        Flare_Add(8, L(100), L(0x34), L(0x14), L(0x1c), L(8), 0, 0xa0 + dx, cy + dy, S(0x1e, 0x11));
        Flare_Add(1, L(0xf0), L(0xf0), L(0xf0), 0, 0, 0, 0xa0 + dx, cy + dy, S(0x1e, 0xf));
        /* las dos ultimas, reflejadas en x */
        Flare_Add(4, L(0x28), L(0x28), L(0x3c), 0, 0, 0, 0x140 - x + (dx >> 2), cy + dy + (dy >> 2), 0x40);
        Flare_Add(4, L(0x28), L(0x28), L(0x3c), 0, 0, 0, 0x140 - x + (dx >> 1), cy + dy + (dy >> 1), 0x38);
        break;
    case 2:
        Flare_Add(4, L(0x44), L(100), L(0x50), 0, 0, 0, x - (dx >> 1), y - (dy >> 1), S(0x1e, 10));
        Flare_Add(1, L(0x140), L(0x140), L(0x140), 0, 0, 0, x, y, S(0x32, 0x14));
        Flare_Add(4, L(0x28), L(0x50), L(0x50), 0, 0, 0, x + (dx >> 1), y + (dy >> 1), S(0x1e, 10));
        Flare_Add(4, L(0x20), L(0x20), L(0x28), 0, 0, 0, 0xa0, cy, S(10, 10));
        Flare_Add(4, L(0x14), L(0x14), L(0x28), 0, 0, 0, 0xa0 + (dx >> 2), cy + (dy >> 2), S(0x19, 10));
        Flare_Add(4, L(0x78), L(0x78), L(0x78), 0, 0, 0, 0xa0 + (dx * 3 >> 2), cy + (dy * 3 >> 2), S(0x1e, 0xf));
        Flare_Add(4, L(0x50), L(0x34), L(0x14), 0, 0, 0, 0xa0 + dx + (dx >> 1), cy + dy + (dy >> 1), S(0x19, 0xf));
        Flare_Add(4, L(0x30), L(0x28), 0, 0, 0, 0, 0xa0 + dx * 2, cy + dy * 2, S(0x1e, 0x14));
        break;
    case 3:
        Flare_Add(1, L(0x8c), L(0x104), L(0x104), 0, 0, 0, x, y, S(0x1e, 0xf));
        Flare_Add(0xd, L(0x3c), L(0x50), L(0xa0), 0, 0, 0, x, y, S(0x37, 0x16));
        break;
    case 4:   /* un brillo fijo, sin depender de lo que se vea */
        Flare_Add(0xd, 0x0f, 0x0a, 0x0a, 0, 0, 0, x, y, 0x1a8);
        break;
    }
#undef L
#undef S
}
