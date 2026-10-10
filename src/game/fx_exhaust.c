/* Llamas del escape: una tira con textura que sale de cada tubo hacia atras, mas larga cuanto mas revolucionado va
 * el motor, con un halo en la boca del tubo; con el turbo (+0x33A & 8) la llama azul y mas larga. Los petardeos
 * (+0x445 & 8, 80077c60) cambian la llama por un destello blanco en el tubo.
 * Original:
 *  - 800780d0 (desde 8007567c, al dibujar la vista): por coche y tubo; al final baja las banderas de humo y
 *    petardeo del frame (+0x445 &= 0xE3). Solo si el coche se ha dibujado en la vista (+0x49A + vista, que pone
 *    HumanCar_Handler / AiCar_Handler).
 *  - 80077fbc: extremos de la llama en el mundo (tubo y tubo + direccion * largo).
 *  - 80013f54: la tira (los dos extremos con la camara, ancho escalado por la distancia) y su halo.
 *  - 80077900 (desde 80075808, Level_WaitLoaded) con 80013ea4: textura de cada tipo de llama.
 * Tabla 800ab6d8 (2 tipos de 0x30 bytes: 0 normal, 1 turbo): +0 / +4 largo minimo y maximo (el maximo lo usa
 * 800779a0 para la escala de +0x440 / +0x442), y desde +8 los parametros de 80013f54 {+0 color en el tubo, +4
 * color en la punta, +8 / +0xC medio ancho en cada extremo, +0x10..+0x13 u0 v0 u1 v1, +0x14 slot de la pagina,
 * +0x18 region (CLUT en +4), +0x1C / +0x20 tpage opaca / aditiva, +0x24 banderas: 1 con textura, 2 degradado}.
 * Los dos tipos usan POLY_GT4 aditivo de la pagina 8 (slot 7); las variantes sin textura o planas de 80013f54 no
 * las usa ninguna tabla. */
#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "gte.h"
#include "exe.h"
#include "gfx.h"
#include "globals.h"
#include "texslot.h"
#include "render.h"
#include "racecar.h"
#include "sprite.h"
#include "fx_int.h"
#include "carphys_int.h"

#define FLAME_TABLE 0x800ab6d8u
#define FLAME_TYPES 2              /* DAT_800af340 */

typedef struct
{
    int      minLen;
    uint32_t colA, colB;           /* RGB de GPU (en el original << 8) */
    int      w0, w1;
    uint8_t  u0, v0, u1, v1;
    uint16_t clut;
    int      tpage;                /* aditiva */
    unsigned flags;
} Flame;

static Flame s_flames[FLAME_TYPES];

/* 80077900 / 80013ea4 */
void Exhaust_InitTextures(void)
{
    for (int k = 0; k < FLAME_TYPES; k++)
    {
        uint32_t t = FLAME_TABLE + (uint32_t)k * 0x30;
        Flame* f = &s_flames[k];
        f->minLen = Exe_S32(t);
        f->colA = (uint32_t)Exe_S32(t + 8) >> 8;
        f->colB = (uint32_t)Exe_S32(t + 0xc) >> 8;
        f->w0 = Exe_S32(t + 0x10);
        f->w1 = Exe_S32(t + 0x14);
        f->u0 = Exe_U8(t + 0x18);
        f->v0 = Exe_U8(t + 0x19);
        f->u1 = Exe_U8(t + 0x1a);
        f->v1 = Exe_U8(t + 0x1b);
        f->flags = (unsigned)Exe_S32(t + 0x2c);
        int slot = Exe_S32(t + 0x1c);
        const uint8_t* r = Cpag_FindRegion(slot, f->u0, f->v0);
        f->clut = r ? NR_Read16(r + 4) : 0;
        int du, dv;
        int tp = TexSlot_GetTPage(slot, 0, &du, &dv);
        f->tpage = (tp < 0 ? 0 : (tp & ~0x60)) | 0x20;
    }
}

/* 8001016c para un punto del mundo con la rotacion de la camara (relativo a ella, a 1/16) */
static uint8_t Exhaust_Project(const int* w, uint32_t* sxy, int* sz)
{
    Gte_SetV0((w[0] - g_Camera.pos[0]) >> 4, (w[1] - g_Camera.pos[1]) >> 4, (w[2] - g_Camera.pos[2]) >> 4);
    doCOP2(GTE_OP_RTPS);
    if (CFC2(31) & 0x1c66000)
        return 1;
    *sxy = MFC2(14);
    *sz = (int)MFC2(19);
    return (uint8_t)((*sz < 0x20) | Wide_OnScreenX((int16_t)*sxy) << 6 | ((unsigned)(*sxy >> 16) < 2u * 0x78) << 7);
}

/* 80013f54: la llama de p1 (tubo) a p2 (punta) */
static void Exhaust_DrawFlame(const int* p1, const int* p2, const Flame* f)
{
    uint32_t s1 = 0, s2 = 0;
    int z1 = 0, z2 = 0;
    Render_LoadCameraRotation();
    uint8_t f1 = Exhaust_Project(p1, &s1, &z1);
    uint8_t f2 = Exhaust_Project(p2, &s2, &z2);
    if ((f1 | f2) & 1)
        return;
    if (((f1 | f2) & 0xc0) != 0xc0)
        return;
    int x1 = (int16_t)s1, y1 = (int16_t)(s1 >> 16), x2 = (int16_t)s2, y2 = (int16_t)(s2 >> 16);
    /* halo en la boca del tubo */
    Sprite_Add(1, f->colA, 0, s1, z1 - 0x14, f->w0 >> 1, (float)x1, (float)y1);

    /* medio ancho en pantalla de cada extremo (w * H / z), en la perpendicular de la tira; el largo de la tira en
     * pantalla es la aproximacion de la norma de 8006afd0 (mayor + menor / 4 - menor / 64, con el ajuste de 45) */
    const RenderProj* pr = Render_GetProjection();
    int dy = y2 - y1, dx = x1 - x2;
    int a = dy < 0 ? -dy : dy, b = dx < 0 ? -dx : dx, lo = b;
    if (a < b)
    {
        lo = a;
        a = b;
    }
    if (a >> 1 < lo)
    {
        int t = lo * 0x6a;
        lo = a - lo;
        a = (a * 0x4b + t) >> 7;
    }
    int len = a + (lo >> 2) - (lo >> 6);
    int px1 = 0, py1 = 0, px2 = 0, py2 = 0;
    if (len >= 1)
    {
        int k1 = (f->w0 * (pr->h >> 4) / z1 << 10) / len;
        int k2 = (f->w1 * (pr->h >> 4) / z2 << 10) / len;
        px1 = dy * k1 >> 10;
        py1 = dx * k1 >> 10;
        px2 = dy * k2 >> 10;
        py2 = dx * k2 >> 10;
    }
    /* (el original pone la esquina de la punta del primer lado con el ancho del tubo) */
    int qx[4] = { x2 + px1, x1 + px1, x2 - px2, x1 - px1 };
    int qy[4] = { y2 + py1, y1 + py1, y2 - py2, y1 - py1 };
    int vis = 0;
    for (int k = 0; k < 4; k++)
        if ((int16_t)qx[k] < 0x140 + g_WideMargin && (int16_t)qy[k] < 0xf0)
            vis = 1;
    if (!vis)
        return;
    POLY_GT4* q = (POLY_GT4*)Gfx_AllocPrim(sizeof(POLY_GT4));
    if (!q)
        return;
    setPolyGT4(q);
    setSemiTrans(q, 1);
    setRGB0(q, f->colB & 0xff, (f->colB >> 8) & 0xff, (f->colB >> 16) & 0xff);
    setRGB1(q, f->colA & 0xff, (f->colA >> 8) & 0xff, (f->colA >> 16) & 0xff);
    setRGB2(q, f->colB & 0xff, (f->colB >> 8) & 0xff, (f->colB >> 16) & 0xff);
    setRGB3(q, f->colA & 0xff, (f->colA >> 8) & 0xff, (f->colA >> 16) & 0xff);
    setXY4(q, qx[0], qy[0], qx[1], qy[1], qx[2], qy[2], qx[3], qy[3]);
    /* v0 en la punta y v1 en el tubo; u de un lado al otro */
    setUV4(q, f->u0, f->v0, f->u0, f->v1, f->u1, f->v0, f->u1, f->v1);
    q->clut = f->clut;
    q->tpage = (uint16_t)f->tpage;
    Gfx_AddPrimDepth((z1 + z2) >> 1, q);
}

/* 80077fbc: el tubo x (registro de 0xE bytes en +0x44C) con la llama de largo len (direccion en 4.12) */
static void Exhaust_Flame(uint8_t* c, const uint8_t* x, int type, int len)
{
    const int16_t* lp = (const int16_t*)(x + 2);
    const int16_t* dir = (const int16_t*)(x + 8);
    int a[3], b[3], w1[3], w2[3];
    for (int k = 0; k < 3; k++)
    {
        a[k] = lp[k];
        b[k] = lp[k] + (dir[k] * len >> 12);
    }
    const GfxObj* o = &((RaceCar*)ENT(c))->obj;
    Mat_MulTAdd(o->rot, a, w1);   /* rot y pos van seguidas en el GfxObj (m[9..11]) */
    Mat_MulTAdd(o->rot, b, w2);
    Exhaust_DrawFlame(w1, w2, &s_flames[type]);
}

/* 800166b4 (el punto del modelo con el objeto visible en la vista) */
static uint8_t Exhaust_ProjectPipe(uint8_t* c, const uint8_t* x, uint32_t* sxy, int* sz)
{
    const GfxObj* o = &((RaceCar*)ENT(c))->obj;
    if (!(o->flags & 2))
        return 1;
    const int16_t* lp = (const int16_t*)(x + 2);
    int a[3] = { lp[0], lp[1], lp[2] }, w[3];
    Mat_MulTAdd(o->rot, a, w);
    Render_LoadCameraRotation();
    return Exhaust_Project(w, sxy, sz);
}

/* 800780d0 */
void Car_DrawExhaust(int view)
{
    for (int i = 0; i < g_NumCars; i++)
    {
        uint8_t* c = g_CarPhys[i];
        if (!c)
            continue;
        unsigned fl = U8(c, 0x445);
        int type;
        unsigned scale = 0;
        if ((U16(c, 0x33a) & 8) && (fl & 2))
        {
            type = 1;
            scale = U16(c, 0x442);
        }
        else if (fl & 1)
        {
            type = 0;
            scale = U16(c, 0x440);
        }
        else
            type = -1;
        int base = 0, var = 0;
        if (type >= 0)
        {
            /* largo segun las revoluciones (+0x3EC): de 3/4 a 5/4 del valor con el azar */
            int len = s_flames[type].minLen + ((int)(scale * (uint32_t)I32(c, 0x3ec)) >> 16);
            var = len >> 2;
            base = len - var;
        }
        int drawn = U8(c, 0x49a + view) != 0;
        for (int k = 0; k < U8(c, 0x444); k++)
        {
            const uint8_t* x = B_(c) + 0x44c + k * 0xe;
            uint32_t sxy;
            int sz;
            if (((type < 0 && (fl & 0x10)) || (fl & 8)) && (Exhaust_ProjectPipe(c, x, &sxy, &sz) & 0xc1) == 0xc0)
            {
                float px = (float)(int16_t)sxy, py = (float)(int16_t)(sxy >> 16);
                if (fl & 0x10)
                {
                    /* brillo del tubo (ninguna tabla de coches pone esta bandera) */
                    uint32_t r = (uint32_t)I32(c, 0x3ec) >> 9;
                    Sprite_Add(1, (r | r << 8 | (r >> 1) << 16) & 0xffffff, 0, sxy, sz - 0x10, 0x20, px, py);
                }
                if ((fl & 8) && drawn)
                    Sprite_Add(1, 0xcfcfcf, 0, sxy, sz - 0x14, 0x30, px, py);   /* petardeo */
            }
            if (type >= 0 && !(fl & 8))
            {
                unsigned r = (unsigned)Rand_B();
                if (drawn)
                    Exhaust_Flame(c, x, type, base + (var * (int)(r & 0xff) >> 7));
            }
        }
        U8(c, 0x445) = (uint8_t)(fl & 0xe3);
    }
}
