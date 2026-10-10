/* Anillos y cupulas de luz (efectos de tipo 0x5C): ondas en el suelo de los golpes, destello de la caja de poder,
 * onda expansiva... y la cupula que tambien usan los escudos de los coches.
 * Original (0x80068078-0x8006A848, 0xA0 bytes por instancia):
 *  - crear 8006a578 (plantilla de 0x54 bytes: +0x34 crecimiento, +0x36 su frenado, +0x38 radio maximo, +0x3A ancho,
 *    +0x3C / +0x3E orientacion, +0x40 segmentos, +0x42 color interior, +0x45 exterior, +0x48 tiempo del fundido,
 *    +0x4B subida, +0x4C radio, +0x4E / +0x50 giro, +0x52 banderas: 0x10 tres coronas, 0x20 sin la camara,
 *    0x40 cupula, 0x80 sin la parte de arriba, 0x100 conica, 0x200 medio, 0x400 dos octantes, 0x800 en relieve).
 *  - manejador 8006a214 (0: velocidad del fundido 8006a120 y la malla de la cupula 80068400), actualizar 8006939c
 *    (crece, frena, sube, gira y se funde; muere al apagarse o al llegar al radio maximo), dibujar 8006a394:
 *    anillos 800697e4 (cuartos de octante con la tabla de simetrias 800ab508, POLY_G4 aditivos del color interior
 *    al exterior) o cupula 80068a18 (malla de un octante 80068078 / 8006831c copiada en 16 con signos 800686a4,
 *    POLY_G3 / G4 aditivos con el color por anillos). */
#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "gte.h"
#include "exe.h"
#include "gfx.h"
#include "globals.h"
#include "vecmath.h"
#include "fx_int.h"
#include "carphys_int.h"

/* ---- cupula (80068400) ---- */

struct FxDome
{
    int16_t* verts;      /* +0x00 octante: 4 shorts por vertice (esfera unidad 4.12) */
    uint8_t* faces;      /* +0x04 {n, a, b, c, d} */
    int16_t  nfaces;     /* +0x08 */
    int16_t  nverts;     /* +0x0A */
    int8_t   rings;      /* +0x0C */
    uint8_t  n;          /* +0x0D caras por anillo */
    int16_t* out;        /* +0x10 vertices de todas las copias */
    uint8_t* flags;      /* +0x14 (en el original, memoria de la lista de dibujo) */
    uint8_t  mode;       /* +0x18 1 sin la parte de arriba, 2 sin la de abajo, 4 media, 8 girada, 0x10 conica */
    uint8_t  scale[3];   /* +0x19 escala por eje (0x80 = 1) */
};

/* 80068078: un octante de la esfera (rumbo 0..0x200, altura 0..0x400); conica: la y crece por anillos */
static void Dome_Verts(int16_t* v, int step, int cone)
{
    int y = 0, dy = 0;
    v[0] = 0;
    v[1] = cone ? 0x800 : 0x1000;
    v[2] = 0;
    if (cone)
    {
        int k = 0x400 / step;
        y = 0x800;
        dy = k ? 0x800 / k : 0;
    }
    int idx = 1;
    for (int e = step; (int16_t)e < 0x401; e += step)
    {
        int se, ce;
        Math_SinCos((int16_t)e, &se, &ce);
        se >>= 4;
        ce >>= 4;
        for (int a = 0; (int16_t)a < 0x201; a += step)
        {
            int sa, ca;
            Math_SinCos((int16_t)a, &sa, &ca);
            sa >>= 4;
            ca >>= 4;
            int16_t* p = v + idx++ * 4;
            p[0] = (int16_t)(se * sa >> 12);
            p[1] = (int16_t)(cone ? y * ce >> 12 : ce);
            p[2] = (int16_t)(se * ca >> 12);
        }
        if (cone)
            y += dy;
    }
}

/* 8006831c: triangulos desde el polo y despues cuadrados entre anillos */
static void Dome_Faces(uint8_t* f, int ringPts, int nverts)
{
    int k = 0, top = 1, b = 1;
    for (int i = 0; i < ringPts - 1; i++, f += 5, k++)
    {
        f[0] = 3;
        f[1] = 0;
        f[2] = (uint8_t)top;
        f[3] = (uint8_t)++top;
    }
    for (int v = ringPts + 1; v < nverts; v++)
    {
        for (int i = 0; i < ringPts - 1; i++, f += 5, k++)
        {
            f[0] = 4;
            f[1] = (uint8_t)b;
            f[2] = (uint8_t)v;
            f[3] = (uint8_t)++v;
            f[4] = (uint8_t)++b;
        }
        b++;
    }
}

/* 80068400 */
FxDome* Dome_Create(int step, int mode)
{
    int n;
    if ((unsigned)(step - 1) < 0x200)
        n = 0x200 / (0x200 / step);
    else
        n = 0x200;
    n = 0x200 / n;   /* divisiones del octante */
    FxDome* d = (FxDome*)calloc(1, sizeof(FxDome));
    if (!d)
        return NULL;
    int ringPts = n + 1;
    d->rings = (int8_t)(ringPts * 2 - 2);
    d->n = (uint8_t)n;
    d->nverts = (int16_t)(ringPts * (ringPts * 2 - 2) + 1);
    d->nfaces = (int16_t)(ringPts + n * (ringPts * 2 - 3) - 1);
    int copies = (mode & 2) ? 8 : 16;
    d->verts = (int16_t*)calloc((size_t)d->nverts, 8);
    d->faces = (uint8_t*)calloc((size_t)d->nfaces, 5);
    d->out = (int16_t*)calloc((size_t)d->nverts * copies, 8);
    d->flags = (uint8_t*)calloc((size_t)d->nverts * copies, 1);
    if (!d->verts || !d->faces || !d->out || !d->flags)
    {
        Dome_Free(d);
        return NULL;
    }
    d->mode = (uint8_t)mode;
    if (mode & 1)
        d->rings = (int8_t)(d->rings >> 1);
    Dome_Verts(d->verts, 0x200 / n, mode & 0x10);
    Dome_Faces(d->faces, ringPts, d->nverts);
    d->scale[0] = d->scale[1] = d->scale[2] = 0x80;
    return d;
}

/* 80068654 */
void Dome_Free(FxDome* d)
{
    if (!d)
        return;
    free(d->verts);
    free(d->faces);
    free(d->out);
    free(d->flags);
    free(d);
}

void Dome_SetScale(FxDome* d, int x, int y, int z)
{
    d->scale[0] = (uint8_t)x;
    d->scale[1] = (uint8_t)y;
    d->scale[2] = (uint8_t)z;
}

typedef struct
{
    int16_t x, y, z;
    uint8_t ix, iz;      /* componente del octante que va a x y a z (0 o 2) */
} DomeCopy;

static void Copy_Set(DomeCopy* c, int16_t x, int16_t y, int16_t z, int ix, int iz)
{
    c->x = x;
    c->y = y;
    c->z = z;
    c->ix = (uint8_t)ix;
    c->iz = (uint8_t)iz;
}

/* Vertices proyectados en el orden de la primitiva */
static void XY4(POLY_G4* p, const uint32_t* s, int a, int b, int c, int d)
{
    setXY4(p, (int16_t)s[a], (int16_t)(s[a] >> 16), (int16_t)s[b], (int16_t)(s[b] >> 16), (int16_t)s[c],
           (int16_t)(s[c] >> 16), (int16_t)s[d], (int16_t)(s[d] >> 16));
}

/* 800686a4: copia el octante con signos y ejes cambiados y la escala por eje */
static void Dome_Copy(FxDome* d, const DomeCopy* c, int copy)
{
    int16_t* o = d->out + copy * d->nverts * 4;
    const int16_t* v = d->verts;
    for (int i = 0; i < d->nverts; i++, v += 4, o += 4)
    {
        o[0] = (int16_t)(c->x * v[c->ix] >> 12);
        o[1] = (int16_t)(c->y * v[1] >> 12);
        o[2] = (int16_t)(c->z * v[c->iz] >> 12);
        for (int a = 0; a < 3; a++)
            if (d->scale[a] == 0x40)
                o[a] = (int16_t)(o[a] >> 1);
            else if (d->scale[a] != 0x80)
                o[a] = (int16_t)(o[a] * d->scale[a] >> 7);
    }
}

static void Fx_DrawMode(int depth)
{
    DR_TPAGE* t = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
    if (!t)
        return;
    setDrawTPage(t, 1, 1, 0xa0);   /* 0xE10006A0: aditivo */
    Gfx_AddPrimDepth(depth, t);
}

#define RGBC(p, n, c) ((p)->r##n = (uint8_t)(c), (p)->g##n = (uint8_t)((c) >> 8), (p)->b##n = (uint8_t)((c) >> 16))

/* 80068a18: cupula de radio r en el marco w, del color interior (arriba) al exterior por anillos */
void Dome_Draw(FxDome* d, const int* w, int r, const uint8_t* c0, const uint8_t* c1)
{
    DomeCopy cp[16];
    int nc = 0;
    int16_t R = (int16_t)r, N = (int16_t)-r;
    Copy_Set(&cp[nc++], R, R, R, 0, 2);
    Copy_Set(&cp[nc++], R, R, R, 2, 0);
    Copy_Set(&cp[nc++], N, R, R, 0, 2);
    Copy_Set(&cp[nc++], N, R, R, 2, 0);
    if (!(d->mode & 4))
    {
        Copy_Set(&cp[nc++], R, R, N, 0, 2);
        Copy_Set(&cp[nc++], N, R, N, 0, 2);
        Copy_Set(&cp[nc++], R, R, N, 2, 0);
        Copy_Set(&cp[nc++], N, R, N, 2, 0);
    }
    if (!(d->mode & 2))
    {
        Copy_Set(&cp[nc++], R, N, R, 0, 2);
        Copy_Set(&cp[nc++], R, N, R, 2, 0);
        Copy_Set(&cp[nc++], N, N, R, 0, 2);
        Copy_Set(&cp[nc++], N, N, R, 2, 0);
        if (!(d->mode & 4))
        {
            Copy_Set(&cp[nc++], R, N, N, 0, 2);
            Copy_Set(&cp[nc++], N, N, N, 0, 2);
            Copy_Set(&cp[nc++], R, N, N, 2, 0);
            Copy_Set(&cp[nc++], N, N, N, 2, 0);
        }
    }
    /* pasos de color por anillo en 4.4 */
    int acc[3], step[3];
    for (int a = 0; a < 3; a++)
    {
        acc[a] = c0[a] << 4;
        int diff = (int)c1[a] - (int)c0[a];
        switch (d->rings)
        {
        case 2:
            step[a] = diff * 8;
            break;
        case 4:
            step[a] = diff * 4;
            break;
        case 8:
            step[a] = diff * 2;
            break;
        default:
            step[a] = d->rings ? diff * 0x10 / d->rings : 0;
            break;
        }
    }
    int m[12];
    int rel[3] = { w[9] - g_Camera.pos[0], w[10] - g_Camera.pos[1], w[11] - g_Camera.pos[2] };
    Fx_RotI(g_Camera.view, rel, m + 9, 1);
    if (!(d->mode & 8))
        memcpy(m, g_Camera.view, 9 * sizeof(int));
    else
        Fx_RotI(g_Camera.view, w, m, 3);
    for (int k = 0; k < nc; k++)
        Dome_Copy(d, &cp[k], k);
    Fx_LoadMatrix(m);
    int total = d->nverts * nc;
    uint32_t* sxy = (uint32_t*)malloc((size_t)total * 8);
    if (!sxy)
        return;
    int* sz = (int*)(sxy + total);
    Fx_ProjectVerts(d->out, total, sxy, sz, d->flags);
    int skip = (d->mode & 1) ? d->n * d->rings : 0;
    uint32_t top = 0, bot = 0;
    int started = 0;
    uint32_t cur = c0[0] | c0[1] << 8 | c0[2] << 16;
    for (int f = 0; f < d->nfaces; f++)
    {
        skip--;
        if ((int8_t)skip < 0)
        {
            started = 1;
            skip = d->n - 1;
            top = cur;
            for (int a = 0; a < 3; a++)
                acc[a] += step[a];
            cur = (uint32_t)((acc[0] >> 4) & 0xff) | (uint32_t)((acc[1] >> 4) & 0xff) << 8 |
                  (uint32_t)((acc[2] >> 4) & 0xff) << 16;
            bot = cur;
        }
        if (!started)
            continue;
        const uint8_t* fc = d->faces + f * 5;
        for (int k = 0; k < nc; k++)
        {
            int base = k * d->nverts;
            int nv = fc[0] == 4 ? 4 : 3;
            uint8_t fl = 0;
            int z = 0x7fffffff;
            for (int i = 1; i <= nv; i++)
            {
                fl |= d->flags[base + fc[i]];
                if ((int16_t)sz[base + fc[i]] < z)
                    z = (int16_t)sz[base + fc[i]];
            }
            if ((fl & 1) || fl != 0xc0)
                continue;
            Fx_DrawMode(z);
            if (nv == 4)
            {
                POLY_G4* p = (POLY_G4*)Gfx_AllocPrim(sizeof(POLY_G4));
                if (!p)
                    break;
                setPolyG4(p);
                setSemiTrans(p, 1);
                RGBC(p, 0, top);
                RGBC(p, 1, bot);
                RGBC(p, 2, top);
                RGBC(p, 3, bot);
                XY4(p, sxy, base + fc[1], base + fc[2], base + fc[4], base + fc[3]);
                Gfx_AddPrimDepth(z, p);
            }
            else
            {
                POLY_G3* p = (POLY_G3*)Gfx_AllocPrim(sizeof(POLY_G3));
                if (!p)
                    break;
                setPolyG3(p);
                setSemiTrans(p, 1);
                RGBC(p, 0, top);
                RGBC(p, 1, bot);
                RGBC(p, 2, bot);
                setXY3(p, (int16_t)sxy[base + fc[1]], (int16_t)(sxy[base + fc[1]] >> 16), (int16_t)sxy[base + fc[2]],
                       (int16_t)(sxy[base + fc[2]] >> 16), (int16_t)sxy[base + fc[3]], (int16_t)(sxy[base + fc[3]] >> 16));
                Gfx_AddPrimDepth(z, p);
            }
        }
    }
    free(sxy);
}

/* ---- instancias ---- */

typedef struct
{
    FxInst  h;            /* 0x00-0x63 */
    int16_t segs;         /* +0x64 */
    int16_t rise;         /* +0x66 */
    int16_t grow;         /* +0x68 */
    int16_t growDec;      /* +0x6A */
    int16_t unk6c;
    int16_t maxScale;     /* +0x6E */
    int16_t width;        /* +0x70 */
    int16_t pad72[10];    /* +0x72 animacion de uv (sin uso en el dibujo) */
    int16_t fade;         /* +0x86 */
    int8_t  riseSpeed;    /* +0x88 */
    uint8_t c0[3];        /* +0x89 interior */
    uint8_t unk8c;
    uint8_t c1[3];        /* +0x8D exterior */
    int16_t yaw, pitch;   /* +0x90 / +0x92 */
    int16_t yawV, pitchV; /* +0x94 / +0x96 */
    uint16_t flags;       /* +0x98 */
    int16_t scale0;       /* +0x9A */
    FxDome* dome;         /* +0x9C */
} Ring;

/* 8006a120: velocidad del fundido para apagar el color mas alto en +0x48 de la plantilla */
static void Ring_FadeSpeed(Ring* r)
{
    if (r->fade < 0x10)
    {
        r->fade = 0;
        return;
    }
    unsigned m = r->c0[0];
    if (m <= r->c0[1])
        m = r->c0[1];
    if (m < r->c0[2])
        m = r->c0[2];
    unsigned m2 = r->c1[0];
    if (m2 <= r->c1[1])
        m2 = r->c1[1];
    if (m2 < r->c1[2])
        m2 = r->c1[2];
    if (m < m2)
        m = m2;
    unsigned d = (unsigned)(T16(r->h.tmpl, 0x48) * 0x960);
    r->fade = d ? (int16_t)((0x80000000u / d >> 4) * m >> 4) : 0;
}

/* 8006a214 */
static int Ring_Handler(int msg, FxInst* in, intptr_t a, const FxSpawn* sp)
{
    (void)a;
    (void)sp;
    Ring* r = (Ring*)in;
    if (msg == 10)
    {
        Fx_BaseHandler(10, in);
        if ((r->flags & 0x40) && r->dome)
            Dome_Free(r->dome);
        free(r);
        return 0;
    }
    if (Fx_BaseHandler(msg, in))
        return 0;
    switch (msg)
    {
    case 0:
        Ring_FadeSpeed(r);
        r->rise = 0;
        r->scale0 = in->scale;
        if (r->flags & 0x40)
        {
            int mode = ((r->flags & 0x100) ? 0x10 : 0) | ((r->flags & 0x80) ? 1 : 0) | ((in->tflags & 0x104) ? 8 : 0);
            r->dome = Dome_Create(0x200 >> (r->segs & 0x1f), mode);
            if (!r->dome)
                return 0;
        }
        return 1;
    case 1:
    case 9:
        return 0;
    case 6:
        return r->fade == 0;
    default:
        return Fx_MiscHandler(msg, in);
    }
}

/* 8006a578 */
FxInst* Ring_Create(uint8_t* t)
{
    Ring* r = (Ring*)calloc(1, sizeof(Ring));
    if (!r)
        return NULL;
    if (!(TU16(t, 0x52) & 0x40))
    {
        int16_t s = T16(t, 0x40);
        if (s & 7)
            s = (int16_t)((s < 0 ? s + 7 : s) & ~7);
        if (s < 8)
            s = 8;
        T16(t, 0x40) = s;
    }
    r->riseSpeed = TS8(t, 0x4b);
    r->growDec = T16(t, 0x36);
    r->grow = T16(t, 0x34);
    r->segs = T16(t, 0x40);
    r->width = T16(t, 0x3a);
    r->maxScale = T16(t, 0x38);
    r->flags = TU16(t, 0x52);
    for (int k = 0; k < 3; k++)
    {
        r->c0[k] = T8(t, 0x42 + k);
        r->c1[k] = T8(t, 0x45 + k);
    }
    r->fade = T16(t, 0x48);
    r->unk8c = T8(t, 0x4a);
    Fx_InitInst(&r->h, t);
    r->h.handler = Ring_Handler;
    r->yaw = T16(t, 0x3c);
    r->pitch = T16(t, 0x3e);
    r->yawV = T16(t, 0x4e);
    r->pitchV = T16(t, 0x50);
    int16_t s = T16(t, 0x4c);
    r->h.scale = s < 0 ? (int16_t)-s : s;   /* (lo pisa Fx_SetupFrame) */
    int prims = (r->flags & 4) ? r->segs << 1 : r->segs;
    if (r->flags & 0x10)
        prims <<= 1;
    if (r->flags & 1)
    {
        r->h.prims = 0;
        r->h.prims2 = (int16_t)prims;
    }
    else
    {
        r->h.prims = (int16_t)prims;
        r->h.prims2 = 0;
    }
    return &r->h;
}

/* 8006939c */
void Ring_UpdateAll(FxInst* list, int dt)
{
    for (FxInst* in = list; in; in = in->next)
    {
        if (in->state & 8)
            continue;
        Ring* r = (Ring*)in;
        if (!(r->flags & 1) && r->fade != 0)
        {
            int dec = r->fade * dt >> 8;
            int gone = 0;
            uint8_t* ch[6] = { &r->c0[0], &r->c0[1], &r->c0[2], &r->c1[0], &r->c1[1], &r->c1[2] };
            for (int k = 0; k < 6; k++)
                if ((int)*ch[k] < dec)
                {
                    *ch[k] = 0;
                    gone |= 1 << k;
                }
                else
                    *ch[k] = (uint8_t)(*ch[k] - dec);
            if (gone == 0x3f)
                in->state |= 1;
        }
        in->scale = (int16_t)(in->scale + (int16_t)((uint32_t)(r->grow * dt) >> 8));
        r->rise = (int16_t)(r->rise + r->riseSpeed * dt);
        if (r->yawV || r->pitchV)
        {
            r->yaw = (int16_t)((r->yaw + (dt * r->yawV * 0x100 >> 16)) % 0x1000);
            r->pitch = (int16_t)((r->pitch + (dt * r->pitchV * 0x100 >> 16)) % 0x1000);
            Math_RotMatrix(r->yaw, 0x400 - r->pitch, 0, in->frame);
        }
        if (r->growDec > 0)
        {
            r->grow = (int16_t)(r->grow - (r->growDec * dt >> 7));
            if (r->grow < 0x1000)
            {
                r->grow = 0x1000;
                r->growDec = 0;
            }
        }
        if (r->maxScale <= in->scale)
            in->state |= 1;
    }
}

/* 800697e4: anillos (cuartos de octante repetidos con la tabla 800ab508) */
static void Ring_Draw(Ring* r, const int* focus)
{
    FxInst* in = &r->h;
    FxAnchor* an = in->anchor;
    int w[12];
    memcpy(w, Fx_WorldFrame(an ? an->obj : NULL, in->tflags, in->lp, 1, an, in->frame), sizeof(w));
    if (r->riseSpeed)
        w[10] += r->rise;
    int segs = r->segs >> 3, rad = in->scale, wid = r->width;
    int three = r->flags & 0x10, nv = three ? 6 : 4;
    int d[3] = { w[9] - focus[0], w[10] - focus[1], w[11] - focus[2] };
    if (!(in->tflags & 0x4000) && Vec_ApproxLen3(d) - rad * 0x10 >= 0x20000)
        return;
    if (segs == 0)
        return;
    int m[12];
    int rel[3] = { w[9] - g_Camera.pos[0], w[10] - g_Camera.pos[1], w[11] - g_Camera.pos[2] };
    Fx_RotI(g_Camera.view, rel, m + 9, 1);
    if (!(r->flags & 0x20))
        Fx_RotI(g_Camera.view, w, m, 3);
    else
        memcpy(m, in->frame, 9 * sizeof(int));
    Fx_LoadMatrix(m);
    int step = 0x200 / segs, oct = (r->flags & 0x400) ? 2 : 8;
    int nseg = (r->flags & 0x200) ? segs >> 1 : segs;
    int r1 = rad + wid, r3 = rad + wid * 2;
    uint32_t col0 = r->c0[0] | r->c0[1] << 8 | r->c0[2] << 16;
    uint32_t col1 = r->c1[0] | r->c1[1] << 8 | r->c1[2] << 16;
    int ang = 0;
    for (int s = 0; s < nseg; s++)
    {
        int sn, cs;
        int16_t base[6][2];
        Math_SinCos(ang, &sn, &cs);
        sn >>= 4;
        cs >>= 4;
        base[0][0] = (int16_t)(rad * sn >> 12);
        base[0][1] = (int16_t)(rad * cs >> 12);
        base[1][0] = (int16_t)(r1 * sn >> 12);
        base[1][1] = (int16_t)(r1 * cs >> 12);
        base[4][0] = (int16_t)(r3 * sn >> 12);
        base[4][1] = (int16_t)(r3 * cs >> 12);
        ang = (s == segs - 1) ? 0x200 : ang + step;
        Math_SinCos(ang, &sn, &cs);
        sn >>= 4;
        cs >>= 4;
        base[5][0] = (int16_t)(r3 * sn >> 12);
        base[5][1] = (int16_t)(r3 * cs >> 12);
        base[2][0] = (int16_t)(r1 * sn >> 12);
        base[2][1] = (int16_t)(r1 * cs >> 12);
        base[3][0] = (int16_t)(rad * sn >> 12);
        base[3][1] = (int16_t)(rad * cs >> 12);
        for (int o = 0; o < oct; o++)
        {
            uint32_t tb = (uint32_t)Exe_S32(0x800ab508u + o * 4);
            int16_t v[6][4];
            for (int i = 0; i < nv; i++)
            {
                int16_t x = (tb & 4) ? base[i][1] : base[i][0];
                int16_t z = (tb & 4) ? base[i][0] : base[i][1];
                if (tb & 1)
                    x = (int16_t)-x;
                if (tb & 2)
                    z = (int16_t)-z;
                v[i][0] = x;
                v[i][2] = z;
                v[i][3] = 0;
                if (three && (r->flags & 0x800))
                    v[i][1] = (int16_t)(i >= 4 ? 0x20 : (i == 0 || i == 3) ? -0x20 : 0);
                else
                    v[i][1] = 0;
            }
            uint32_t sxy[6];
            int sz[6];
            uint8_t fl[6];
            Fx_ProjectVerts(&v[0][0], nv, sxy, sz, fl);
            int bad = (fl[0] | fl[1] | fl[2] | fl[3]) & 1;
            if (three)
                bad |= (fl[4] | fl[5]) & 1;
            if (bad)
                continue;
            int z = sz[0];
            for (int i = 1; i < nv; i++)
                if ((int16_t)sz[i] < (int16_t)z)
                    z = sz[i];
            z = (int16_t)z;
            Fx_DrawMode(z);
            POLY_G4* p = (POLY_G4*)Gfx_AllocPrim(sizeof(POLY_G4));
            if (!p)
                return;
            setPolyG4(p);
            setSemiTrans(p, 1);
            RGBC(p, 0, col0);
            RGBC(p, 1, col1);
            RGBC(p, 2, col0);
            RGBC(p, 3, col1);
            XY4(p, sxy, 0, 1, 3, 2);
            Gfx_AddPrimDepth(z, p);
            if (three)
            {
                /* tercera corona: del exterior (en el anillo del medio) al interior (en el borde) */
                POLY_G4* q = (POLY_G4*)Gfx_AllocPrim(sizeof(POLY_G4));
                if (!q)
                    return;
                setPolyG4(q);
                setSemiTrans(q, 1);
                RGBC(q, 0, col0);
                RGBC(q, 1, col1);
                RGBC(q, 2, col0);
                RGBC(q, 3, col1);
                XY4(q, sxy, 5, 2, 4, 1);
                Gfx_AddPrimDepth(z, q);
            }
        }
    }
}

/* 8006a394 */
void Ring_DrawAll(FxInst* list, const int* focus)
{
    for (FxInst* in = list; in; in = in->next)
    {
        if (in->state & 8)
            continue;
        Ring* r = (Ring*)in;
        if (!(r->flags & 0x40))
        {
            Ring_Draw(r, focus);
            continue;
        }
        if (!r->dome)
            continue;
        FxAnchor* an = in->anchor;
        const int* w = Fx_WorldFrame(an ? an->obj : NULL, in->tflags, in->lp, (in->tflags & 0x104) != 0, an,
                                     in->frame);
        int d[3] = { w[9] - focus[0], w[10] - focus[1], w[11] - focus[2] };
        if (in->tflags & 0x4000)
            continue;   /* (el original no la dibuja con esa bandera) */
        if (Vec_ApproxLen3(d) - in->scale * 0x10 < 0x20000)
            Dome_Draw(r->dome, w, in->scale, r->c0, r->c1);
    }
}
