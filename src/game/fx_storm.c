/* Nube de la tormenta (poder 5): 19 bocanadas de humo oscuro que giran sobre el coche (o sobre la tormenta mientras
 * vaga), con dos canales de relampagos que las iluminan, y una lluvia de hasta 30 gotas.
 * Original (0x80070E14-0x80071E4C, lista DAT_800af31c, 0x218 bytes por nube):
 *  - crear 80070e58 (desde Weather_Create 8003219c): +4 estado (0 nueva, 1 apareciendo, 2, 3 desvaneciendose,
 *    4 acabada), +8 su tiempo (0x200 tics), +0xC posicion, +0x18 desplazamientos de las bocanadas (800118b4, 19 x 3
 *    ints), +0x1C tamanos (80011a0c), +0x20 angulos (en +0x54), +0x24 sus velocidades (800119e4, shorts), +0x28 canal
 *    de relampago de cada bocanada (80011998, -1 ninguno), +0x2C tabla de 2 bits por paso de 16 tics (80011a58, ciclo
 *    DAT_800af320 = 0x2000), +0x30 tiempo de cada canal (el segundo empieza en 0x1000), +0x34 su valor, +0x38 gotas
 *    {x, y, z} (en +0xB0), +0x3C cuantas, +0x40 / +0x48 lo que se ha movido en x / z (/ 8), +0x4C coche, +0x50
 *    entidad de la tormenta.
 *  - actualizar 80070f80 (desde 80038998): 0x200 por delante del coche (o de la tormenta) y 0x300 por encima; las
 *    gotas caen 15 por tic hasta -0x4B0 y salen 100 por cada 256 tics (no al desvanecerse).
 *  - dibujar Storm_DrawRainClouds 800714a0 (desde 80038ccc): a menos de 0x78000 y dentro de la vista (8001931c con
 *    radio 0x5572), cada bocanada es un POLY_FT4 girado (u 1-0x3F, v 0xC1-0xFE de la pagina 8) restando luz (con el
 *    relampago: 0 -> 0x7F, 1 -> 0x20 restando; 2 -> 0x3F, 3 -> 0x7F sumando) y otro sumando 0x28 por encima; la
 *    lluvia, a menos de 3000 * 16 por delante, en triangulos POLY_G3 aditivos del gris 0x3C a negro (el original deja
 *    de dibujar gotas en la primera que cae fuera de la pantalla).
 *  - desvanecer 80071dd4, liberar 80071de4, liberar todas 80070e1c (8002456c). */
#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "gte.h"
#include "exe.h"
#include "gfx.h"
#include "texslot.h"
#include "render.h"
#include "racecar.h"
#include "actor.h"
#include "fx_int.h"
#include "carphys_int.h"

#define PUFFS     19
#define MAX_DROPS 30

struct Storm
{
    struct Storm* next;
    int      state;              /* +0x04 */
    int16_t  fade;               /* +0x08 */
    int      pos[3];             /* +0x0C */
    int      angle[PUFFS];       /* +0x54 */
    int      animT[2];           /* +0xA0 */
    int      anim[2];            /* +0xA8 */
    int      drops[MAX_DROPS][3];/* +0xB0 */
    int      numDrops;           /* +0x3C */
    int      move[3];            /* +0x40 */
    RaceCar* car;                /* +0x4C */
    Actor*   ent;                /* +0x50 */
};

static Storm* s_storms;          /* DAT_800af31c */

static int Storm_Off(int i, int k)   { return Exe_S32(0x800118b4u + (uint32_t)(i * 12 + k * 4)); }
static int Storm_Size(int i)         { return Exe_S32(0x80011a0cu + (uint32_t)i * 4); }
static int Storm_Chan(int i)         { return Exe_S32(0x80011998u + (uint32_t)i * 4); }
static int Storm_Spin(int i)         { return (int16_t)Exe_U16(0x800119e4u + (uint32_t)i * 2); }

/* 80070e58 */
Storm* Storm_Create(void)
{
    Storm* s = (Storm*)calloc(1, sizeof(Storm));
    if (!s)
        return NULL;
    s->animT[1] = 0x1000;
    s->next = s_storms;
    s_storms = s;
    return s;
}

/* 80071dd4 */
void Storm_Fade(Storm* s)
{
    if (!s)
        return;
    s->state = 3;
    s->fade = 0;
}

/* 80071de4 */
void Storm_Free(Storm* s)
{
    for (Storm** p = &s_storms; *p; p = &(*p)->next)
        if (*p == s)
        {
            *p = s->next;
            free(s);
            return;
        }
}

int Storm_Count(void)
{
    int n = 0;
    for (Storm* s = s_storms; s; s = s->next)
        n++;
    return n;
}

/* 80070e1c */
void Storm_FreeAll(void)
{
    while (s_storms)
        Storm_Free(s_storms);
}

/* Weather_Update 800323f0: la nube sigue al coche alcanzado (o a la tormenta si car es NULL) */
void Storm_SetTarget(Storm* s, RaceCar* car, Actor* ent)
{
    if (!s)
        return;
    s->car = car;
    s->ent = ent;
}

/* 80070f80 */
void Storm_UpdateAll(int dt)
{
    unsigned period = (unsigned)Exe_S32(0x800af320u);
    for (Storm* s = s_storms; s; s = s->next)
    {
        switch (s->state)
        {
        case 0:
            s->state = 1;
            break;
        case 1:
            s->fade = (int16_t)(s->fade + dt);
            if (s->fade > 0x1ff)
                s->state = 2;
            break;
        case 3:
            s->fade = (int16_t)(s->fade + dt);
            if (s->fade > 0x1ff)
                s->state = 4;
            break;
        case 4:
            continue;
        }
        const GfxObj* o;
        if (s->car)
        {
            memcpy(s->ent->obj.pos, s->car->obj.pos, sizeof(s->ent->obj.pos));
            o = &s->car->obj;
        }
        else if (s->ent)
            o = &s->ent->obj;
        else
            continue;
        /* 80019cac: 0x200 por delante (eje Z del objeto) y 0x300 por encima */
        int p[3];
        for (int k = 0; k < 3; k++)
            p[k] = o->pos[k] + M16(o->rot[6 + k], 0x200);
        p[1] += 0x300;
        for (int k = 0; k < 3; k++)
            s->move[k] = p[k] - s->pos[k];
        s->move[0] >>= 3;
        s->move[2] >>= 3;
        memcpy(s->pos, p, sizeof(s->pos));
        for (int i = 0; i < PUFFS; i++)
            s->angle[i] += Storm_Spin(i) * dt >> 4;
        /* relampagos: 2 bits por cada paso de 16 tics de la tabla */
        for (int k = 0; k < 2; k++)
        {
            s->animT[k] += dt;
            while ((unsigned)s->animT[k] >= period)
                s->animT[k] -= (int)period;
            int t = s->animT[k];
            s->anim[k] = (int)((uint32_t)Exe_S32(0x80011a58u + (uint32_t)(t >> 8) * 4) >> (((t >> 4) & 0xf) << 1)) & 3;
        }
        for (int i = 0; i < s->numDrops;)
        {
            s->drops[i][1] += dt * -0xf;
            if (s->drops[i][1] < -0x4b0)
            {
                s->numDrops--;
                if (s->numDrops > 0)
                    memcpy(s->drops[i], s->drops[s->numDrops], sizeof(s->drops[i]));
            }
            else
                i++;
        }
        if (s->state != 3)
        {
            int n = s->numDrops + (dt * 100 >> 8);
            if (n > MAX_DROPS)
                n = MAX_DROPS;
            while (s->numDrops < n)
            {
                int* d = s->drops[s->numDrops];
                int x = Rand_Fx() & 0x3ff;
                if (!(Rand_Fx() & 1))
                    x = -x;
                d[0] = x;
                d[1] = 0;
                int z = Rand_Fx() & 0x3ff;
                if (!(Rand_Fx() & 1))
                    z = -z;
                d[2] = z;
                s->numDrops++;
            }
        }
    }
}

/* 80010234 con la rotacion de la camara: punto del mundo a pantalla (flags de la GTE) */
static uint32_t Storm_Project(const int* w, int* x, int* y, int* z)
{
    Gte_SetV0((w[0] - g_Camera.pos[0]) >> 4, (w[1] - g_Camera.pos[1]) >> 4, (w[2] - g_Camera.pos[2]) >> 4);
    doCOP2(GTE_OP_RTPS);
    uint32_t sxy = MFC2(14);
    *x = (int16_t)sxy;
    *y = (int16_t)(sxy >> 16);
    *z = (int16_t)MFC2(19);
    return CFC2(31);
}

static void Storm_Puff(int x, int y, int z, int size, int angle, int abr, int col, uint16_t clut, int tpage)
{
    int s, c;
    Math_SinCos(angle, &s, &c);
    int a = (int16_t)((unsigned)(size * c) >> 16), b = (int16_t)((unsigned)(size * s) >> 16);
    POLY_FT4* q = (POLY_FT4*)Gfx_AllocPrim(sizeof(POLY_FT4));
    if (!q)
        return;
    setPolyFT4(q);
    setSemiTrans(q, 1);
    setRGB0(q, col, col, col);
    /* cuadrado girado: v0 arriba-izquierda, v1 arriba-derecha, v2 abajo-izquierda, v3 abajo-derecha */
    setXY4(q, x - b, y - a, x + a, y - b, x - a, y + b, x + b, y + a);
    setUV4(q, 1, 0xc1, 0x3f, 0xc1, 1, 0xfe, 0x3f, 0xfe);
    q->clut = clut;
    q->tpage = (uint16_t)(tpage | abr << 5);
    Gfx_AddPrimDepth(z < 0 ? 0 : z, q);
}

/* 800714a0 */
void Storm_DrawAll(void)
{
    if (!s_storms)
        return;
    int clut = -1;
    const uint8_t* r = Cpag_FindRegion(7, 1, 0xc1);
    if (r)
        clut = NR_Read16(r + 4);
    int du, dv, tp = TexSlot_GetTPage(7, 0, &du, &dv);
    tp = tp < 0 ? 0xc : (tp & ~0x60);
    const RenderProj* pr = Render_GetProjection();
    int h16 = pr->h >> 4;
    for (Storm* s = s_storms; s; s = s->next)
    {
        if (s->state == 0 || s->state == 4)
            continue;
        int rel[3] = { s->pos[0] - g_Camera.pos[0], s->pos[1] - g_Camera.pos[1], s->pos[2] - g_Camera.pos[2] };
        int a = Abs_(rel[0]), b = Abs_(rel[1]), d = Abs_(rel[2]);
        if (a < b) { int t = a; a = b; b = t; }
        if (a < d) { int t = a; a = d; d = t; }
        if (a + ((b + d) >> 2) > 0x78000)   /* 800194a0 */
            continue;
        int cam[3];
        Fx_RotI(g_Camera.view, rel, cam, 1);
        int16_t c16[3] = { (int16_t)(cam[0] >> 4), (int16_t)(cam[1] >> 4), (int16_t)(cam[2] >> 4) };
        if (Render_SphereInFrustum(c16, 0x5572) != 2)
            continue;
        Render_LoadCameraRotation();
        int skip = 0;
        if (clut >= 0)
            for (int i = 0; i < PUFFS && !skip; i++)
            {
                int w[3] = { s->pos[0] + Storm_Off(i, 0), s->pos[1] + Storm_Off(i, 1), s->pos[2] + Storm_Off(i, 2) };
                int x, y, z;
                if (Storm_Project(w, &x, &y, &z) & 0x1c66000)
                {
                    skip = 1;
                    break;
                }
                if (z <= 0x40)
                    continue;
                int size = Storm_Size(i) * h16 / z;
                if (x + size * 2 < -g_WideMargin || x - size * 2 >= 0x141 + g_WideMargin)
                    continue;
                if (y + size * 2 < 0 || y - size * 2 >= 0xf1)
                    continue;
                int ch = Storm_Chan(i), fr = ch < 0 ? 0 : s->anim[ch & 1];
                int abr = fr < 2 ? 2 : 1;
                int col = fr == 1 ? 0x20 : fr == 2 ? 0x3f : 0x7f;
                Storm_Puff(x, y, z, size, s->angle[i], abr, col, (uint16_t)clut, tp);
                /* brillo aditivo por encima (200 * H / z) */
                Storm_Puff(x, y - h16 * 200 / z, z, size, s->angle[i], 1, 0x28, (uint16_t)clut, tp);
            }
        if (skip || cam[2] >> 4 >= 3000)
            continue;
        /* lluvia */
        for (int i = 0; i < s->numDrops; i++)
        {
            const int* dr = s->drops[i];
            int w[3] = { s->pos[0] + dr[0], s->pos[1] + dr[1], s->pos[2] + dr[2] };
            int x, y, z;
            Storm_Project(w, &x, &y, &z);
            if (z < 0x40 || !Wide_OnScreenX(x) || y < 0 || y > 0xf0)
                break;
            int wd = (int)pr->h * 10 / z;
            if (wd > 2)
                wd = 2;
            int w2[3] = { w[0] + s->move[0], w[1] + 0xff, w[2] + s->move[2] };
            int x2, y2, z2;
            Storm_Project(w2, &x2, &y2, &z2);
            Fx_DrawModeAdd(z);
            POLY_G3* g = (POLY_G3*)Gfx_AllocPrim(sizeof(POLY_G3));
            if (!g)
                return;
            setPolyG3(g);
            setSemiTrans(g, 1);
            setRGB0(g, 0x3c, 0x3c, 0x3c);
            setRGB1(g, 0, 0, 0);
            setRGB2(g, 0, 0, 0);
            setXY3(g, x, y, x2, y2, x + wd, y);
            Gfx_AddPrimDepth(z, g);
        }
    }
}
