/* Restos (efectos de tipo 0x5A): las caras del modelo de un objeto que se rompe salen volando, giran, caen y
 * desaparecen al tocar el suelo o al acabarse su vida. En GlblData solo la clase 0x18 (la de casi todas las
 * clases de objeto al morir: conos, vallas...).
 * Original (0x80046978-0x80048B64, 0x8C bytes por instancia y 0x58 por trozo, 200 trozos en total
 * DAT_800af860 / DAT_800af868):
 *  - crear 80048b64 (plantilla de 0x4C bytes: +0x34 clase al morir cada trozo, +0x35 clase que siguen,
 *    +0x36 cuantos la siguen, +0x38 / +0x3A velocidad hacia fuera, +0x3C / +0x3E y +0x40 / +0x42 empuje extra
 *    (cantidad y direccion), +0x44 / +0x46 giro, +0x48 gravedad (%), +0x49 maximo de trozos, +0x4A banderas:
 *    4 sin subir, 8 / 0x10 otras variantes sin uso en GlblData).
 *  - manejador 800481bc, trozos 800477b4 (caras al azar del frame del objeto, sus vertices girados con el
 *    objeto y centrados; velocidad 800473f8 desde el centro hacia fuera, hacia arriba), mover 80047d80 (gravedad
 *    y giro, mira el suelo cada 8 frames), recoger 80047f5c, dibujar 800487e8 (matriz de dos angulos 800482dc,
 *    proyeccion con desplazamiento segun la distancia 80048604 / 80048654, la cara original como POLY_FT4
 *    80048724). */
#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "gte.h"
#include "gfx.h"
#include "globals.h"
#include "track.h"
#include "vecmath.h"
#include "model.h"
#include "fx_int.h"
#include "carphys_int.h"

typedef struct Piece
{
    int16_t  v[4][4];     /* +0x00 vertices respecto al centro */
    int16_t  n[4];        /* +0x20 normal (en el original +0x10) */
    uint8_t  attr;        /* +0x28 */
    uint8_t  face;        /* +0x29 */
    uint8_t  anchored;    /* +0x2A */
    int16_t  angA, angB;  /* +0x2C / +0x2E */
    int16_t  spinA, spinB;/* +0x30 / +0x32 */
    int      vel[3];      /* +0x34 */
    int      grav;        /* +0x40 */
    int16_t  life;        /* +0x44 (< 0 muerto) */
    int16_t  radius;      /* +0x46 */
    struct Piece* next;   /* +0x48 */
    int      pos[3];      /* +0x4C (o el ancla de la clase que lo sigue) */
    FxAnchor* anchor;
} Piece;

typedef struct
{
    FxInst   h;           /* 0x00-0x63 */
    TrackPos tp;          /* +0x64 */
    uint8_t  dieCls;      /* +0x68 */
    uint8_t  trailCls;    /* +0x69 */
    int16_t  speedMin;    /* +0x6A */
    int16_t  speedMax;    /* +0x6C */
    int16_t  pushMin;     /* +0x6E */
    int16_t  pushMax;     /* +0x70 */
    int16_t  pushDir[3];  /* +0x74 */
    int16_t  spinMin;     /* +0x7A */
    int16_t  spinMax;     /* +0x7C */
    int8_t   trails;      /* +0x7E */
    uint8_t  mode;        /* +0x7F */
    int8_t   grav;        /* +0x80 */
    int8_t   maxPieces;   /* +0x81 */
    const uint8_t* faces; /* +0x84 caras del modelo */
    Piece*   pieces;      /* +0x88 */
} Debris;

static int s_numPieces;   /* DAT_800af868 */

static void Piece_Free(Debris* d, Piece* prev, Piece* p)
{
    if (prev)
        prev->next = p->next;
    else
        d->pieces = p->next;
    if (p->anchored && p->anchor)
    {
        p->anchor->count--;
        p->anchor->flags |= 1;
    }
    free(p);
    s_numPieces--;
}

/* 8004734c: empuje de los parametros de creacion (velocidad con la escala +0x40 << 3) */
static int Debris_SpawnPush(Debris* d, const FxSpawn* sp, int* out)
{
    if (!sp || !(sp->flags & 2))
        return 0;
    int k = (d->h.tflags & 0x200) ? 0 : (int16_t)(sp->unk40 << 3);
    if (!k)
        return 0;
    for (int a = 0; a < 3; a++)
        out[a] = (sp->vel[a] >> 4) * k >> 8;
    return 1;
}

/* 800473f8: giro, gravedad y velocidad del trozo */
static void Piece_Launch(Debris* d, int* dir, int* push, int hasPush, Piece* p)
{
    int r = d->spinMax - d->spinMin;
    if (r < 1)
        p->spinA = p->spinB = d->spinMax;
    else
    {
        p->spinA = (int16_t)(d->spinMin + (r * (Rand_B() & 0xfff) >> 12));
        p->spinB = (int16_t)(d->spinMin + (r * (Rand_B() & 0xfff) >> 12));
    }
    int g = ((Rand_B() & 0x1f) + 0xa0) * 8;
    p->grav = g * d->grav / 100 * -0x10;
    Vec_Normalize(dir);
    int sp = d->speedMax, sr = d->speedMax - d->speedMin;
    if (sr > 0)
        sp = sr + d->speedMin + (sr * (Rand_B() & 0xfff) >> 12);
    for (int a = 0; a < 3; a++)
        p->vel[a] = (dir[a] >> 4) * sp >> 8;
    if (p->vel[1] < 0)
        p->vel[1] = -p->vel[1];
    int pu = d->pushMax, pr = d->pushMax - d->pushMin;
    if (pr > 0)
        pu = d->pushMin + (pr * (Rand_B() & 0xfff) >> 12);
    if (pu)
        for (int a = 0; a < 3; a++)
            p->vel[a] += d->pushDir[a] * pu >> 8;
    if (hasPush)
        for (int a = 0; a < 3; a++)
            p->vel[a] += push[a];
    if (d->h.tflags & 0x20)
    {
        FxAnchor* an = d->h.anchor;
        RaceCar* e = (an && !an->isStatic) ? (RaceCar*)an->ent : NULL;
        if (e)
            for (int a = 0; a < 3; a++)
            {
                push[a] = e->dir[a] * e->speed >> 8;
                p->vel[a] += push[a];
            }
    }
    if (d->mode & 4)
        p->vel[1] = 0;
}

/* 80019e98: longitud aproximada de un vector de shorts */
static int Len3s(const int16_t* v)
{
    unsigned a = (unsigned)(v[0] < 0 ? -v[0] : v[0]), b = (unsigned)(v[1] < 0 ? -v[1] : v[1]),
             c = (unsigned)(v[2] < 0 ? -v[2] : v[2]);
    unsigned lo = b;
    if (a < b)
    {
        lo = a;
        a = b;
    }
    unsigned mid = c;
    if (a < c)
    {
        mid = a;
        a = c;
    }
    return (int)((a * 0xf + (lo + mid) * 6) >> 4);
}

/* 800477b4: rompe el modelo del objeto del ancla en trozos */
static int Debris_Break(Debris* d, const FxSpawn* sp)
{
    FxInst* in = &d->h;
    GfxObj* o = in->anchor ? in->anchor->obj : NULL;
    d->pieces = NULL;
    if (!o || !o->model || !o->model->faces)
        return 0;
    const ModelHdr* m = o->model;
    int nfaces = (int)m->faces[0];
    d->faces = (const uint8_t*)(m->faces + 1);
    int nv;
    const uint8_t* verts = Model_FrameVerts(m, o->frame, &nv);
    const uint32_t* nl = m->normals ? (const uint32_t*)(uintptr_t)m->normals[1 + o->frame] : NULL;
    int want = nfaces;
    if (d->maxPieces > 0 && d->maxPieces < nfaces)
        want = d->maxPieces;
    if (200 - s_numPieces < want)
        want = 200 - s_numPieces;
    if (want <= 0)
        return 0;
    int trails = want;
    if (d->trails != -1)
    {
        int u = d->trails - want;
        trails = ((u >> 31) & u) + want;   /* min(trails, want) */
    }
    int push[3];
    int hasPush = Debris_SpawnPush(d, sp, push);
    memcpy(in->frame, o->rot, 9 * sizeof(int));
    memcpy(in->frame + 9, o->pos, 3 * sizeof(int));
    d->tp.seg = 0xff;
    d->tp.curve = 0xff;
    d->tp.face = -1;
    s_numPieces += want;
    int left = want;
    for (int k = nfaces; k > 0 && left > 0; k--)
    {
        unsigned r = (unsigned)Rand_B() & 0xffff;
        if ((int)(r * (unsigned)k) >> 16 >= left)
            continue;
        int fi = nfaces - k;
        const uint8_t* f = d->faces + fi * 0x18;
        Piece* p = (Piece*)calloc(1, sizeof(Piece));
        if (!p)
            break;
        p->face = (uint8_t)fi;
        p->attr = f[1];
        for (int j = 0; j < 4; j++)
        {
            int vi = f[0x10 + j] < nv ? f[0x10 + j] : 0;
            memcpy(p->v[j], verts + vi * 8, 8);
            Fx_RotS(o->rot, p->v[j], p->v[j]);
        }
        if (nl && f[0x14] < nl[0])
        {
            memcpy(p->n, (const uint8_t*)(nl + 1) + f[0x14] * 8, 8);
            Fx_RotS(o->rot, p->n, p->n);
        }
        int c[3];
        for (int a = 0; a < 3; a++)
        {
            c[a] = (p->v[0][a] + p->v[1][a] + p->v[2][a] + p->v[3][a]) >> 2;
            for (int j = 0; j < 4; j++)
                p->v[j][a] = (int16_t)(p->v[j][a] - c[a]);
        }
        p->angA = p->angB = 0;
        p->life = in->delay ? (int16_t)(in->delay + (r & 0xff)) : 0;
        p->radius = (int16_t)(Len3s(p->v[0]) >> 4);
        int wp[3] = { in->frame[9] + c[0], in->frame[10] + c[1], in->frame[11] + c[2] };
        if (k < trails && d->trailCls)
        {
            /* el trozo lleva su propio efecto en un ancla fija que se mueve con el */
            p->anchored = 1;
            p->anchor = Fx_NewAnchor(NULL, 0, 0, NULL, NULL, 0);
            if (p->anchor)
            {
                p->anchor->scale = p->radius;
                p->anchor->count++;
                memcpy(p->anchor->pos, wp, sizeof(wp));
                Fx_SpawnInst(p->anchor, NULL, d->trailCls, NULL, (in->cflags >> 1) & 1);
            }
            else
                p->anchored = 0;
        }
        if (!p->anchored)
            memcpy(p->pos, wp, sizeof(wp));
        Piece_Launch(d, c, push, hasPush, p);
        p->next = d->pieces;
        d->pieces = p;
        left--;
    }
    s_numPieces -= left;   /* (los que no salieron) */
    in->killTimer = 0;
    in->delay = 0;
    if (d->pieces)
        return 1;
    in->state |= 1;
    return 0;
}

static int* Piece_Pos(Piece* p)
{
    return (p->anchored && p->anchor) ? p->anchor->pos : p->pos;
}

/* 800481bc */
static int Debris_Handler(int msg, FxInst* in, intptr_t a, const FxSpawn* sp)
{
    (void)a;
    Debris* d = (Debris*)in;
    if (msg == 10)
    {
        Fx_BaseHandler(10, in);
        while (d->pieces)
            Piece_Free(d, NULL, d->pieces);
        free(d);
        return 0;
    }
    if (Fx_BaseHandler(msg, in) || msg == 1)
        return 0;
    if (msg == 0)
        return (d->mode & 8) ? 0 : Debris_Break(d, sp);   /* (+0x7F & 8: 80046cf8, sin uso en GlblData) */
    return Fx_MiscHandler(msg, in);
}

/* 80048b64 */
FxInst* Debris_Create(uint8_t* t)
{
    Debris* d = (Debris*)calloc(1, sizeof(Debris));
    if (!d)
        return NULL;
    Fx_InitInst(&d->h, t);
    d->speedMin = T16(t, 0x38);
    d->speedMax = T16(t, 0x3a);
    d->pushMin = T16(t, 0x3c);
    d->pushMax = T16(t, 0x3e);
    int m[9];
    Math_RotMatrix(T16(t, 0x40), -T16(t, 0x42), 0, m);
    d->pushDir[0] = (int16_t)(m[6] >> 4);
    d->pushDir[1] = (int16_t)(m[7] >> 4);
    d->pushDir[2] = (int16_t)(m[8] >> 4);
    d->spinMin = T16(t, 0x44);
    d->spinMax = T16(t, 0x46);
    d->h.handler = Debris_Handler;
    d->grav = TS8(t, 0x48);
    d->dieCls = T8(t, 0x34);
    d->trailCls = T8(t, 0x35);
    d->trails = TS8(t, 0x36);
    d->mode = T8(t, 0x4a);
    d->maxPieces = (int8_t)(T8(t, 0x49) < 0x33 ? T8(t, 0x49) : 0x32);
    d->h.prims2 = (int16_t)((d->maxPieces < 0 || (d->mode & 8)) ? 0x14 : d->maxPieces);
    d->h.prims = 0;
    return &d->h;
}

/* 80047d80 */
void Debris_UpdateAll(FxInst* list, int dt)
{
    for (FxInst* in = list; in; in = in->next)
    {
        Debris* d = (Debris*)in;
        if ((in->state & 8) || (d->mode & 0x18))
            continue;
        if (!d->pieces)
        {
            in->state |= 1;
            continue;
        }
        unsigned frame = (unsigned)g_FrameCount, i = 0;
        for (Piece* p = d->pieces; p; p = p->next, i++)
        {
            int k = dt * 0xda;   /* tics -> 16.16 de 1/300 s a 1/... del original */
            p->vel[1] += M16(p->grav, k);
            int* pos = Piece_Pos(p);
            for (int a = 0; a < 3; a++)
                pos[a] += M16(p->vel[a], k);
            p->angA = (int16_t)(p->angA + (int16_t)((uint32_t)(p->spinA * dt) >> 8));
            p->angB = (int16_t)(p->angB + (int16_t)((uint32_t)(p->spinB * dt) >> 8));
            if (((frame ^ i) & 7) == 0)
            {
                int g = Track_FindSurface(&d->tp, pos, NULL, 0);
                if (!(g < pos[1]))
                    p->life = -1;
            }
            if (p->life > 0)
            {
                p->life = (int16_t)(p->life - dt);
                if (p->life <= 0)
                    p->life = -1;
            }
        }
    }
}

/* 80047f5c: recoge los muertos (con la clase +0x68 donde caen, uno de cada dos) */
void Debris_ReapAll(FxInst* list)
{
    for (FxInst* in = list; in; in = in->next)
    {
        Debris* d = (Debris*)in;
        if (d->mode & 0x18)
            continue;
        Piece* prev = NULL;
        for (Piece* p = d->pieces; p;)
        {
            Piece* next = p->next;
            if (p->life < 0)
            {
                FxSpawn sp;
                memset(&sp, 0, sizeof(sp));
                memcpy(sp.pos, Piece_Pos(p), sizeof(sp.pos));
                sp.scale = (p->anchored ? p->anchor->scale : p->radius) << 4;
                sp.flags = 5;
                Piece_Free(d, prev, p);
                if (!d->pieces)
                    in->state |= 1;
                if ((s_numPieces & 3) > 1 && d->dieCls)
                    Fx_Spawn(NULL, d->dieCls, &sp, (in->cflags >> 1) & 1, NULL);
            }
            else
                prev = p;
            p = next;
        }
    }
}

/* 800482dc: matriz de dos angulos */
static void Piece_Matrix(int a, int b, int* m)
{
    int sb, cb, sa, ca;
    Math_SinCos(b, &sb, &cb);
    Math_SinCos(a, &sa, &ca);
    m[0] = ca;
    m[1] = 0;
    m[2] = -sa;
    m[3] = (sb >> 2) * (sa >> 2) >> 12;
    m[4] = cb;
    m[5] = (sb >> 2) * (ca >> 2) >> 12;
    m[6] = (cb >> 2) * (sa >> 2) >> 12;
    m[7] = -sb;
    m[8] = (cb >> 2) * (ca >> 2) >> 12;
}

/* 800487e8 */
void Debris_DrawAll(FxInst* list, const int* focus)
{
    for (FxInst* in = list; in; in = in->next)
    {
        Debris* d = (Debris*)in;
        if ((in->state & 8) || (d->mode & 0x18) || (in->tflags & 0x4000))
            continue;
        const int* b = Fx_BaseFrame(in);
        int dd[3] = { b[9] - focus[0], b[10] - focus[1], b[11] - focus[2] };
        if (Vec_ApproxLen3(dd) >= 0x20000)
            continue;
        for (Piece* p = d->pieces; p; p = p->next)
        {
            int rm[9], cm[9], rel[3], crel[3];
            Piece_Matrix(p->angA, p->angB, rm);
            const int* pos = Piece_Pos(p);
            for (int a = 0; a < 3; a++)
                rel[a] = pos[a] - g_Camera.pos[a];
            Fx_RotI(g_Camera.view, rel, crel, 1);
            Fx_RotI(g_Camera.view, rm, cm, 3);
            Gte_LoadColumns(cm);
            Gte_SetTransShift(crel, 0);
            /* 80048604: profundidad del primer vertice; cuanto mas lejos, mas se reduce todo (2^shift) */
            Gte_SetV0(p->v[0][0], p->v[0][1], p->v[0][2]);
            doCOP2(GTE_OP_RTV0TR);
            int z = MFC2_S(27) >> 4;
            int lz = Gte_LeadingZeros(z + (p->radius << 2 >> 4));
            if (lz <= 0x10 || z <= 0xf)
                continue;
            int sh = 0x15 - lz;
            sh = sh < 0 ? 0 : sh > 4 ? 4 : sh;
            Gte_SetTransShift(crel, sh);
            uint32_t sxy[4];
            int zz[4];
            uint8_t fl = 0;
            for (int j = 0; j < 4; j++)
            {
                Gte_SetV0(p->v[j][0] >> sh, p->v[j][1] >> sh, p->v[j][2] >> sh);
                doCOP2(GTE_OP_RTPS);
                if (CFC2(31) & 0x1c66000)
                {
                    fl |= 1;
                    break;
                }
                sxy[j] = MFC2(14);
                zz[j] = MFC2_S(27) >> (4 - sh);
                fl |= (uint8_t)(Wide_OnScreenX((int16_t)sxy[j]) << 6 | ((sxy[j] >> 16) < 2u * 0x78) << 7);
            }
            if ((fl & 1) || fl != 0xc0)
                continue;
            /* 80048724: la cara original (textura y semitransparencia) a 0x7F7F7F */
            const uint8_t* f = d->faces + p->face * 0x18;
            POLY_FT4* q = (POLY_FT4*)Gfx_AllocPrim(sizeof(POLY_FT4));
            if (!q)
                return;
            setPolyFT4(q);
            if (f[0] & 2)
                setSemiTrans(q, 1);
            setRGB0(q, 0x7f, 0x7f, 0x7f);
            setXY4(q, (int16_t)sxy[0], (int16_t)(sxy[0] >> 16), (int16_t)sxy[1], (int16_t)(sxy[1] >> 16),
                   (int16_t)sxy[3], (int16_t)(sxy[3] >> 16), (int16_t)sxy[2], (int16_t)(sxy[2] >> 16));
            setUV4(q, f[4], f[5], f[8], f[9], f[0xe], f[0xf], f[0xc], f[0xd]);
            q->clut = NR_Read16(f + 6);
            q->tpage = NR_Read16(f + 0xa);
            Gfx_AddPrimDepth(zz[0], q);
        }
    }
}
