/* Rayos (efectos de tipo 0x58): rayo de la tormenta, chispazos de la caja de poder y del coche electrocutado.
 * Original (0x8006A848-0x8006E594):
 *  - instancia 8006e4b4 / manejador 8006e074: con el mensaje 0 crea dos extremos (8006d95c fijo,
 *    8006d9ec pegado al objeto del ancla: punto local, radio para un destino al azar, ancho) y el rayo 8006dc10
 *    (plantilla: +0x36 segmentos, +0x38 hebras, +0x3B color, +0x3E tiempo del fundido, +0x40 amplitud del
 *    zigzag, +0x42 velocidad de las hebras, +0x44 periodo de regeneracion, +0x48 ancho, +0x4A banderas: 1 al suelo,
 *    2 solo regenera las apagadas, 4, 8 ancho 0 en la punta, 0x20, 0x40 por la superficie del modelo, 0x80 / 0x100
 *    sonido al regenerar). El mensaje 7 / 10 lo apaga.
 *  - rayos en la lista DAT_800af314: actualizar 8006dae0 / 8006c068 (los extremos se sueltan si muere su
 *    entidad, regenera cada +0x28 tics 8006bdbc, funde el color, mueve las hebras), dibujar 8006db94 / 8006c54c
 *    (cada hebra como tiras POLY_GT4 aditivas con la textura blanca 2x2 de 80039098, de color en el centro a negro
 *    en los bordes, ancho escalado por la distancia; LINE_F2 si no tiene ancho).
 *  - hebra 8006afd0: puntos del origen al destino (8006a848: punto del objeto, al azar en una esfera o en el suelo,
 *    o el coche humano mas cercano) con pasos de 1/2, 1 o 3/2 segmento y un desplazamiento lateral acumulado que se
 *    estrecha hacia la punta. */
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
#include "render.h"
#include "snd.h"
#include "fx_int.h"
#include "carphys_int.h"

/* Extremo (0x20 bytes) */
typedef struct
{
    GfxObj*  obj;        /* +0x00 */
    void*    ent;        /* +0x04 */
    int      entHandle;  /* +0x08 */
    int      pos[3];     /* +0x0C (con 0x8000: el byte bajo de pos[0] es el punto local) */
    uint16_t flags;      /* +0x18 1 / 8 al suelo, 2 a un coche humano cercano, 0x10 vertice al azar, 0x8000 punto */
    int16_t  radius;     /* +0x1A (< 0: escala del objeto) */
    int16_t  width;      /* +0x1C */
} BoltEnd;

/* Hebra (0x10 bytes) */
typedef struct
{
    int*     pts;        /* +0x00 n + 1 puntos */
    int16_t* vel;        /* +0x04 */
    int16_t  n;          /* +0x08 */
    uint16_t flags;      /* +0x0A 1 apagada */
    uint8_t  rgb[3];     /* +0x0C */
} Strand;

typedef struct Bolt
{
    struct Bolt* next;   /* +0x00 */
    BoltEnd* src;        /* +0x04 */
    BoltEnd* dst;        /* +0x08 */
    int16_t  nstrands;   /* +0x0C */
    uint16_t flags;      /* +0x0E 1 sin regenerar, 2 regenera, 8 local, 0x20, 0x40 superficie, 0x800 nuevo,
                          * 0x1000 muerto, 0x2000 solo las apagadas, 0x4000, 0x8000 toca regenerar */
    Strand*  strands;    /* +0x10 */
    int16_t  velK;       /* +0x14 */
    int16_t  jitter;     /* +0x16 */
    int16_t  timer;      /* +0x18 */
    uint8_t  rgb[3];     /* +0x1A */
    int16_t  fade[3];    /* +0x1E */
    int16_t  msg;        /* +0x24 */
    int8_t   count;      /* +0x26 regeneraciones (0 sin fin) */
    int16_t  period;     /* +0x28 */
    int16_t  snd;        /* +0x2A */
} Bolt;

static Bolt* s_bolts;   /* DAT_800af314 */

/* ---- extremos ---- */

/* 8006d95c */
static BoltEnd* End_Fixed(const int* pos, int radius, int width, int flags)
{
    BoltEnd* e = (BoltEnd*)calloc(1, sizeof(BoltEnd));
    if (!e)
        return NULL;
    e->flags = (uint16_t)flags;
    e->radius = (int16_t)radius;
    e->width = (int16_t)width;
    memcpy(e->pos, pos, sizeof(e->pos));
    return e;
}

/* 8006d9ec */
static BoltEnd* End_OnObject(void* ent, GfxObj* obj, const int* pos, int lp, int radius, int width, int flags)
{
    BoltEnd* e = (BoltEnd*)calloc(1, sizeof(BoltEnd));
    if (!e)
        return NULL;
    e->flags = (uint16_t)flags;
    e->radius = (int16_t)radius;
    e->width = (int16_t)width;
    if (!obj && ent)
        obj = &((RaceCar*)ent)->obj;
    e->obj = obj;
    if (lp < 0)
        memcpy(e->pos, pos, sizeof(e->pos));
    else
    {
        e->pos[0] = lp & 0xff;
        e->flags |= 0x8000;
    }
    e->ent = ent;
    if (ent)
        e->entHandle = ((RaceCar*)ent)->handle;
    return e;
}

static void Fx_LocalPointAny(GfxObj* o, int lp, int random, int* out)
{
    out[0] = out[1] = out[2] = 0;
    if (!o->model)
        return;
    if (random && !((o->model->flags >> 25) & 1))
    {
        int n;
        const uint8_t* v = Model_FrameVerts(o->model, o->frame, &n);
        const int16_t* p = (const int16_t*)(v + ((Rand_B() & 0xfff) * n >> 12) * 8);
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
        return;
    }
    if ((unsigned)(lp & 0xff) < 0x10)
        Obj_GetLocalPoint(o, lp, out);
}

/* 8006a848: posicion del extremo para una hebra; devuelve el humano alcanzado o -1 */
static int End_Resolve(Bolt* b, BoltEnd* e, int* out, const int* ref)
{
    int hit = -1, rel = 0;
    if (!e)
    {
        out[0] = out[1] = out[2] = 0;
        return -1;
    }
    GfxObj* srcObj = b->src ? b->src->obj : NULL;
    if (e->obj)
        rel = (b->flags >> 3) & 1;
    const int* base;
    int r;
    if (!e->obj)
    {
        r = e->radius < 0 ? -e->radius : e->radius;
        base = e->pos;
    }
    else
    {
        int lp = (e->flags & 0x8000) ? (int8_t)e->pos[0] : -1;
        Fx_LocalPointAny(e->obj, lp, e->flags & 0x10, out);
        if ((b->flags & 8) && !(e->flags & 0x8000))
            for (int a = 0; a < 3; a++)
                out[a] += e->pos[a];
        if (!rel)
        {
            Fx_RotI(e->obj->rot, out, out, 1);
            for (int a = 0; a < 3; a++)
                out[a] += e->obj->pos[a];
        }
        r = (uint16_t)e->radius;
        rel = 0;
        if (e->radius < 0)
            r = (int)((unsigned)(e->obj->bb16 * -e->radius) >> 12);
        base = out;
    }
    int rr = (int16_t)r;
    int tmp[3];
    if ((e->flags & 2) && g_NumHumans > 0)
    {
        unsigned k = (unsigned)Rand_B() % (unsigned)g_NumHumans;
        if (Rand_B() & 1)
        {
            int sh = (b->flags & 0x20) ? 4 : 5;
            RaceCar* c = g_Humans[k];
            if (c)
            {
                int d[3] = { base[0] - c->obj.unk34[0], base[1] - c->obj.unk34[1], base[2] - c->obj.unk34[2] };
                if (Vec_ApproxLen3(d) >> sh < (int16_t)r + c->obj.bb16)
                {
                    memcpy(tmp, c->obj.unk34, sizeof(tmp));
                    base = tmp;
                    hit = (int)k;
                    rr = 0;
                }
            }
        }
    }
    if (rr == 0)
    {
        if (base != out)
            memcpy(out, base, 3 * sizeof(int));
    }
    else if (!(e->flags & 9))
    {
        /* punto al azar en la esfera */
        int sa, ca, sb, cb;
        Math_SinCos(Rand_B() & 0xfff, &sa, &ca);
        Math_SinCos(Rand_B() & 0xfff, &sb, &cb);
        int bx = base[0], by = base[1], bz = base[2];
        out[0] = bx + (((ca >> 2) * (sb >> 2) >> 16) * rr >> 8);
        out[1] = by + ((-sa >> 4) * rr >> 8);
        out[2] = bz + (((ca >> 2) * (cb >> 2) >> 16) * rr >> 8);
    }
    else
    {
        /* punto al azar en el disco, a la altura de la sombra del objeto */
        unsigned u = (unsigned)Rand_B();
        int s, c;
        Math_SinCos(Rand_B() & 0xfff, &s, &c);
        int bx = base[0], bz = base[2];
        int gy = e->obj ? e->obj->shadowY << 4 : base[1];
        int local = e->obj && (b->flags & 8) && srcObj;
        if (!(e->flags & 8) || !ref)
        {
            int rd = (int16_t)((u & 0xfff) * rr >> 12);
            out[0] = bx + ((s >> 4) * rd >> 8);
            out[2] = bz + ((c >> 4) * rd >> 8);
            out[1] = gy - (local ? srcObj->pos[1] : 0);   /* en local solo la altura pasa al marco */
        }
        else
        {
            out[0] = ref[0];
            out[2] = ref[2];
            out[1] = gy;
            if (local)
                for (int a = 0; a < 3; a++)
                    out[a] -= srcObj->pos[a];   /* (como el original, aunque ref ya sea local) */
        }
        rel = 0;
    }
    if (rel && srcObj)
        for (int a = 0; a < 3; a++)
            out[a] -= srcObj->pos[a];
    return hit;
}

/* ---- hebras ---- */

/* 8006afd0: nuevos puntos de la hebra */
static void Strand_Gen(Bolt* b, Strand* s, int regen)
{
    s->flags &= 0xfffe;
    if (b->snd)
    {
        GfxObj* o = (b->src && b->src->obj) ? b->src->obj : (b->dst ? b->dst->obj : NULL);
        if (o)
            Snd_Queue(b->snd, o->pos[0], o->pos[2]);
    }
    memcpy(s->rgb, b->rgb, 3);
    /* (+0x24 > 0: mensaje 0xE a las entidades de los extremos; ninguna plantilla de GlblData lo usa) */
    int* p0 = s->pts;
    int* pn = s->pts + s->n * 3;
    if (regen)
    {
        int done = 0;
        GfxObj* o = (b->src && b->src->obj) ? b->src->obj : (b->dst ? b->dst->obj : NULL);
        if ((b->flags & 0x40) && o && o->model && o->model->faces)
        {
            /* por la superficie: de un vertice a otro de una cara al azar */
            /* por la superficie: entre los dos primeros vertices (frame 0) de una cara de 3 o 4 lados */
            const uint32_t* f = o->model->faces;
            unsigned fi = f[0] ? (unsigned)Rand_B() % f[0] : 0;
            const uint8_t* fc = (const uint8_t*)(f + 1) + fi * 0x18;
            if (fc[1] < 5 && fc[1] > 2)
            {
                int nv;
                const uint8_t* verts = Model_FrameVerts(o->model, 0, &nv);
                const int16_t* va = (const int16_t*)(verts + fc[0x10] * 8);
                const int16_t* vb = (const int16_t*)(verts + fc[0x11] * 8);
                for (int a = 0; a < 3; a++)
                {
                    p0[a] = va[a];
                    pn[a] = vb[a];
                }
                if (!(b->flags & 8))
                {
                    /* (como el original: el final rotado se pisa y el punto 1 queda en final + posicion) */
                    Fx_RotI(o->rot, p0, p0, 1);
                    for (int a = 0; a < 3; a++)
                    {
                        p0[a] += o->pos[a];
                        s->pts[3 + a] = pn[a] + o->pos[a];
                    }
                }
                done = 1;
            }
        }
        if (!done)
        {
            End_Resolve(b, b->src, p0, NULL);
            End_Resolve(b, b->dst, pn, p0);
        }
    }
    int jit = (int)((uint32_t)(uint16_t)b->jitter << 16) >> 23;
    int d[3] = { pn[0] - p0[0], pn[1] - p0[1], pn[2] - p0[2] };
    int len = Vec_NormalizeLen(d);
    int n = s->n;
    int seg = n ? (len >> 4) / n : 0;
    int st[3][3];
    for (int a = 0; a < 3; a++)
    {
        st[0][a] = (d[a] >> 4) * seg >> 9;   /* medio segmento */
        st[1][a] = st[0][a] + st[0][a];
        st[2][a] = st[0][a] + st[1][a];
    }
    /* perpendiculares: p (un eje pequeno cruzado con d) y q = d x p, cada una en 5 pasos de +p a -p */
    int ad[3] = { Abs_(d[0]), Abs_(d[1]), Abs_(d[2]) };
    int p[3] = { 0, 0, 0 };
    if (ad[0] < 0xcccc)
    {
        p[1] = -d[2];
        p[2] = d[1];
    }
    else if (ad[1] < 0xcccc)
    {
        p[2] = -d[0];
        p[0] = d[2];
    }
    else if (ad[2] < 0xcccc)
    {
        p[1] = d[0];
        p[0] = -d[1];
    }
    int q[3] = { (d[2] * (p[1] >> 4) >> 12) - (d[1] * (p[2] >> 4) >> 12),
                 (d[0] * (p[2] >> 4) >> 12) - (d[2] * (p[0] >> 4) >> 12),
                 (d[1] * (p[0] >> 4) >> 12) - (d[0] * (p[1] >> 4) >> 12) };
    int P[5][3], Q[5][3];
    for (int a = 0; a < 3; a++)
    {
        P[0][a] = -(p[a] * jit >> 12);
        Q[0][a] = -(q[a] * jit >> 12);
        int hp = -(P[0][a] >> 1), hq = -(Q[0][a] >> 1);
        for (int i = 1; i < 5; i++)
        {
            P[i][a] = P[i - 1][a] + hp;
            Q[i][a] = Q[i - 1][a] + hq;
        }
    }
    int pos[3] = { p0[0], p0[1], p0[2] };
    int accA[3] = { 0, 0, 0 }, accB[3] = { 0, 0, 0 };
    int k = 0x1000, dk = n ? 0x1000 / n : 0;
    for (int i = 1; i < n; i++)
    {
        unsigned ra = (unsigned)Rand_B() % 5, rb = (unsigned)Rand_B() % 5, rc = (unsigned)Rand_B();
        int o[3];
        for (int a = 0; a < 3; a++)
        {
            accA[a] += P[ra][a];
            accB[a] += Q[rb][a];
            o[a] = ((accA[a] + accB[a]) >> 4) * k;
        }
        rc %= 3;
        for (int a = 0; a < 3; a++)
        {
            s->pts[i * 3 + a] = pos[a] + st[rc][a] + (o[a] >> 8);
            pos[a] += st[1][a];
        }
        k -= dk;
        if (b->velK && s->vel)
            for (int a = 0; a < 3; a++)
                s->vel[i * 3 + a] = (int16_t)((o[a] >> 12) * b->velK >> 12);
    }
}

/* 8006dc10 */
static Bolt* Bolt_Create(BoltEnd* src, BoltEnd* dst, int segs, int nstrands, const uint8_t* rgb, int fadeT,
                         int jitter, int velK, int period, int count, int msg, int flags)
{
    Bolt* b = src && dst && segs > 0 && nstrands > 0 ? (Bolt*)calloc(1, sizeof(Bolt)) : NULL;
    if (!b)
    {
        free(src);
        free(dst);
        return NULL;
    }
    b->src = src;
    b->dst = dst;
    b->nstrands = (int16_t)nstrands;
    if (flags & 1)
        flags |= 0x2000;
    b->flags = (uint16_t)flags;
    b->period = (int16_t)period;
    b->velK = (int16_t)velK;
    b->jitter = (int16_t)jitter;
    b->count = (int8_t)count;
    memcpy(b->rgb, rgb, 3);
    b->msg = (int16_t)msg;
    for (int a = 0; a < 3; a++)
        b->fade[a] = (int16_t)(fadeT < 0x10 ? 0 : (rgb[a] << 12) / fadeT);
    b->strands = (Strand*)calloc((size_t)nstrands, sizeof(Strand));
    if (!b->strands)
    {
        free(b);
        free(src);
        free(dst);
        return NULL;
    }
    if (!src->obj)
        b->flags &= 0xfff7;
    for (int i = 0; i < nstrands; i++)
    {
        Strand* s = &b->strands[i];
        s->n = (int16_t)segs;
        s->pts = (int*)calloc((size_t)segs + 1, 12);
        s->vel = velK ? (int16_t*)calloc((size_t)segs + 1, 6) : NULL;
        if (!s->pts || (velK && !s->vel))
        {
            for (int j = 0; j <= i; j++)
            {
                free(b->strands[j].pts);
                free(b->strands[j].vel);
            }
            free(b->strands);
            free(b);
            free(src);
            free(dst);
            return NULL;
        }
    }
    b->timer = b->period;
    for (int i = 0; i < nstrands; i++)
        Strand_Gen(b, &b->strands[i], 1);
    b->next = s_bolts;
    s_bolts = b;
    return b;
}

/* 8006bf90 */
static void Bolt_Free(Bolt* b)
{
    free(b->src);
    free(b->dst);
    for (int i = 0; i < b->nstrands; i++)
    {
        free(b->strands[i].pts);
        free(b->strands[i].vel);
    }
    free(b->strands);
    free(b);
}

/* 8006bdbc: funde la hebra y la regenera si toca; devuelve si sigue viva */
static int Strand_Update(Bolt* b, Strand* s, int dr, int dg, int db)
{
    int d[3] = { dr, dg, db };
    for (int a = 2; a >= 0; a--)
        if (s->rgb[a])
            s->rgb[a] = (int)s->rgb[a] < d[a] ? 0 : (uint8_t)(s->rgb[a] - d[a]);
    int alive = (dr && s->rgb[0]) || (dg && s->rgb[1]) || (db && s->rgb[2]);
    if ((b->flags & 0x8000) && ((Rand_B() & 1) || !(b->flags & 2)) && (!(b->flags & 0x2000) || !alive))
    {
        if (b->count < 1 || --b->count > 0)
        {
            Strand_Gen(b, s, 1);
            alive = 1;
        }
        else
            b->flags = (uint16_t)((b->flags & 0xfffd) | 1);
    }
    if (!alive)
        s->flags |= 1;
    return alive;
}

/* Un extremo cuya entidad ha muerto se queda fijo donde estaba */
static void End_Detach(Bolt* b, BoltEnd* e, int isSrc)
{
    if (!e || !e->ent || !e->obj || ((RaceCar*)e->ent)->handle == e->entHandle)
        return;
    int lp = (e->flags & 0x8000) ? (int8_t)e->pos[0] : -1;
    Fx_LocalPointAny(e->obj, lp, e->flags & 0x10, e->pos);
    if ((b->flags & 8) && b->src && b->src->obj)
    {
        GfxObj* so = b->src->obj;
        Fx_RotI(so->rot, e->pos, e->pos, 1);
        for (int a = 0; a < 3; a++)
            e->pos[a] += so->pos[a];
    }
    e->obj = NULL;
    e->ent = NULL;
    if (isSrc)
        b->flags &= 0xfff7;
}

/* 8006c068 */
static void Bolt_Update(Bolt* b, int dt)
{
    End_Detach(b, b->dst, 0);
    End_Detach(b, b->src, 1);
    if (b->flags & 0x4000)
        return;
    if (b->flags & 2)
    {
        b->timer = (int16_t)(b->timer - dt);
        if (b->timer < 0)
        {
            b->flags |= 0x8000;
            b->timer = b->period;
        }
    }
    int any = 0;
    for (int i = 0; i < b->nstrands; i++)
    {
        Strand* s = &b->strands[i];
        any |= Strand_Update(b, s, (int)((unsigned)(uint16_t)b->fade[0] * dt) >> 8,
                             (int)((unsigned)(uint16_t)b->fade[1] * dt) >> 8,
                             (int)((unsigned)(uint16_t)b->fade[2] * dt) >> 8);
        if (b->velK && s->vel)
            for (int j = 1; j < s->n; j++)
                for (int a = 0; a < 3; a++)
                    s->pts[j * 3 + a] += s->vel[j * 3 + a] * dt >> 3;
    }
    if (!any && (b->flags & 1))
        b->flags |= 0x1000;
    b->flags &= 0x7fff;
}

/* 8006dae0 */
void Bolt_UpdateAll(int dt)
{
    Bolt* prev = NULL;
    for (Bolt* b = s_bolts; b;)
    {
        Bolt* next = b->next;
        if (b->flags & 0x1000)
        {
            if (prev)
                prev->next = next;
            else
                s_bolts = next;
            Bolt_Free(b);
        }
        else
        {
            if (!(b->flags & 0x800))
                b->flags |= 0x800;
            else
                Bolt_Update(b, dt);
            prev = b;
        }
        b = next;
    }
}

void Bolt_FreeAll(void)
{
    while (s_bolts)
    {
        Bolt* n = s_bolts->next;
        Bolt_Free(s_bolts);
        s_bolts = n;
    }
}

/* ---- dibujo ---- */

#define MAX_PTS 0x80

/* 8006c54c */
static void Bolt_Draw(Bolt* b, const int* focus)
{
    if (b->flags & 0x4000)
        return;
    GfxObj* so = (b->flags & 8) && b->src ? b->src->obj : NULL;
    for (int si = 0; si < b->nstrands; si++)
    {
        Strand* s = &b->strands[si];
        if ((s->flags & 1) || s->n + 1 > MAX_PTS)
            continue;
        int n = s->n;
        int near = 0;
        for (int e = 0; e < 2 && !near; e++)
        {
            const int* p = s->pts + (e ? n * 3 : 0);
            int d[3] = { p[0] - focus[0], p[1] - focus[1], p[2] - focus[2] };
            if (so)
                for (int a = 0; a < 3; a++)
                    d[a] += so->unk34[a];   /* objeto +0x34: el centro de su caja */
            near = Vec_ApproxLen3(d) <= 0x1ffff;
        }
        if (!near)
            continue;
        int16_t v[MAX_PTS][4];
        for (int i = 0; i <= n; i++)
        {
            for (int a = 0; a < 3; a++)
                v[i][a] = (int16_t)(so ? s->pts[i * 3 + a] >> 4 : (s->pts[i * 3 + a] - g_Camera.pos[a]) >> 4);
            v[i][3] = 0;
        }
        if (!so)
            Render_LoadCameraRotation();
        else
        {
            int m[12], rel[3] = { so->pos[0] - g_Camera.pos[0], so->pos[1] - g_Camera.pos[1], so->pos[2] - g_Camera.pos[2] };
            Fx_RotI(g_Camera.view, so->rot, m, 3);
            Fx_RotI(g_Camera.view, rel, m + 9, 1);
            Fx_LoadMatrix(m);
        }
        uint32_t sxy[MAX_PTS];
        int sz[MAX_PTS];
        uint8_t fl[MAX_PTS];
        for (int i = 0; i <= n; i++)
        {
            /* 8001016c */
            Gte_SetV0(v[i][0], v[i][1], v[i][2]);
            doCOP2(GTE_OP_RTPS);
            if (CFC2(31) & 0x1c66000)
            {
                fl[i] = 1;
                continue;
            }
            sxy[i] = MFC2(14);
            sz[i] = (int)MFC2(19);
            fl[i] = (uint8_t)((sz[i] < 0x20) | Wide_OnScreenX((int16_t)sxy[i]) << 6 |
                              ((unsigned)(sxy[i] >> 16) < 2u * 0x78) << 7);
        }
        /* medio ancho en pantalla de cada punto (perpendicular a la tira) */
        int16_t ox[MAX_PTS], oy[MAX_PTS];
        int w = b->src->width << 4;
        int dw = (b->dst->width - b->src->width) * 0x10 / (n + 1);
        int prev = 0, cur = 0, next = 1;
        const RenderProj* pr = Render_GetProjection();
        do
        {
            ox[cur] = oy[cur] = 0;
            if (((fl[next] | fl[cur] | fl[prev]) & 0xc1) == 0xc0)
            {
                int zp = (int16_t)sz[prev];
                int sw = zp ? (w >> 4) * pr->h / zp : 3;
                if (sw < 3)
                    sw = 3;
                int ay = (int16_t)(sxy[next] >> 16) - (int16_t)(sxy[prev] >> 16);
                int ax = (int16_t)sxy[prev] - (int16_t)sxy[next];
                int a = Abs_(ay), c = Abs_(ax), lo = c;
                if (a < c)
                {
                    lo = a;
                    a = c;
                }
                if (a >> 1 < lo)
                {
                    int t = lo * 0x6a;
                    lo = a - lo;
                    a = (a * 0x4b + t) >> 7;
                }
                int len = a + (lo >> 2) - (lo >> 6);
                if (len >= 1)
                {
                    int k = (sw << 10) / len;
                    ox[cur] = (int16_t)(ay * k >> 10);
                    oy[cur] = (int16_t)(ax * k >> 10);
                }
            }
            next = cur + 2 > n ? n : cur + 2;
            w += dw;
            if (w < 0)
                w = 0;
            prev = cur;
            cur++;
        } while (cur < n + 1);
        uint32_t col = s->rgb[0] | s->rgb[1] << 8 | s->rgb[2] << 16;
        for (int i = 0; i < n; i++)
        {
            if (((fl[i] | fl[i + 1]) & 0xc1) != 0xc0)
                continue;
            int z = ((int16_t)sz[i] + (int16_t)sz[i + 1]) >> 1;
            int x0 = (int16_t)sxy[i], y0 = (int16_t)(sxy[i] >> 16);
            int x1 = (int16_t)sxy[i + 1], y1 = (int16_t)(sxy[i + 1] >> 16);
            if (ox[i] == 0 && oy[i] == 0)
            {
                /* sin ancho: linea de un pixel (el primer punto se acerca uno al segundo) */
                int dx = x1 - x0, dy = y1 - y0;
                if (dx < 0)
                {
                    if (dy < 0)
                    {
                        if (dx < dy)
                            x0--;
                        else
                            y0--;
                    }
                    else if (dy < -dx)
                        x0--;
                    else
                        y0++;
                }
                else if (dy < 0)
                {
                    if (dx <= -dy)
                        y0--;
                    else
                        x0++;
                }
                else if (dx <= dy)
                    y0++;
                else
                    x0++;
                Fx_DrawModeAdd(z);
                LINE_F2* l = (LINE_F2*)Gfx_AllocPrim(sizeof(LINE_F2));
                if (!l)
                    return;
                setLineF2(l);
                setSemiTrans(l, 1);
                setRGB0(l, s->rgb[0], s->rgb[1], s->rgb[2]);
                setXY2(l, x0, y0, x1, y1);
                Gfx_AddPrimDepth(z, l);
                continue;
            }
            for (int side = 0; side < 2; side++)
            {
                int qx[4], qy[4];
                uint32_t c[4];
                if (side == 0)
                {
                    qx[0] = x0; qy[0] = y0; c[0] = col;
                    qx[1] = x0 - ox[i]; qy[1] = y0 - oy[i]; c[1] = 0;
                    qx[2] = x1; qy[2] = y1; c[2] = col;
                    qx[3] = x1 - ox[i + 1]; qy[3] = y1 - oy[i + 1]; c[3] = 0;
                }
                else
                {
                    qx[0] = x0; qy[0] = y0; c[0] = col;
                    qx[1] = x1; qy[1] = y1; c[1] = col;
                    qx[2] = x0 + ox[i]; qy[2] = y0 + oy[i]; c[2] = 0;
                    qx[3] = x1 + ox[i + 1]; qy[3] = y1 + oy[i + 1]; c[3] = 0;
                }
                int vis = 0;
                for (int k = 0; k < 4; k++)
                    if ((int16_t)qx[k] < 0x140 + g_WideMargin && (int16_t)qy[k] < 0xf0)
                        vis = 1;
                if (!vis)
                    break;   /* (si la primera no se ve, el original tampoco dibuja la segunda) */
                POLY_GT4* p = (POLY_GT4*)Gfx_AllocPrim(sizeof(POLY_GT4));
                if (!p)
                    return;
                setPolyGT4(p);
                setSemiTrans(p, 1);
                setRGB0(p, c[0] & 0xff, (c[0] >> 8) & 0xff, (c[0] >> 16) & 0xff);
                setRGB1(p, c[1] & 0xff, (c[1] >> 8) & 0xff, (c[1] >> 16) & 0xff);
                setRGB2(p, c[2] & 0xff, (c[2] >> 8) & 0xff, (c[2] >> 16) & 0xff);
                setRGB3(p, c[3] & 0xff, (c[3] >> 8) & 0xff, (c[3] >> 16) & 0xff);
                setXY4(p, qx[0], qy[0], qx[1], qy[1], qx[2], qy[2], qx[3], qy[3]);
                setUV4(p, 0, 0, 0, 0, 0, 0, 0, 0);
                p->clut = 0;
                p->tpage = Fx_WhiteTPage();
                Gfx_AddPrimDepth(z, p);
            }
        }
    }
}

/* 8006db94 */
void Bolt_DrawAll(const int* focus)
{
    for (Bolt* b = s_bolts; b; b = b->next)
        Bolt_Draw(b, focus);
}

/* ---- instancias (tipo 0x58) ---- */

typedef struct
{
    FxInst h;           /* 0x00-0x63 */
    Bolt*  bolt;        /* +0x64 */
} BoltInst;

/* 8006e074 */
static int BoltInst_Handler(int msg, FxInst* in, intptr_t a, const FxSpawn* sp)
{
    (void)a;
    (void)sp;
    BoltInst* bi = (BoltInst*)in;
    uint8_t* t = in->tmpl;
    if (msg == 10)
    {
        Fx_BaseHandler(10, in);
        if (bi->bolt)
            bi->bolt->flags |= 0x1000;   /* 8006e060 */
        free(bi);
        return 0;
    }
    if (Fx_BaseHandler(msg, in))
        return 0;
    switch (msg)
    {
    case 0:
    {
        FxAnchor* an = in->anchor;
        GfxObj* obj = an ? an->obj : NULL;
        unsigned f = TU16(t, 0x4a);
        int fa = f & 1;
        if (f & 0x20)
            fa |= 8;
        int fb = fa | ((TU16(t, TP_FLAGS) & 0x80) ? 0x10 : 0);
        if (f & 4)
            fa |= 2;
        int bf = (f & 2) ? 0x2002 : 2;
        if (f & 0x80)
            bf |= 0x40;
        if (f & 0x40)
            bf |= 0x20;
        int wa = (f & 8) ? 0 : T16(t, 0x48);
        BoltEnd *src, *dst;
        if (!obj)
        {
            src = End_Fixed(in->frame + 9, 0, T16(t, 0x48), fb);
            dst = End_Fixed(in->frame + 9, in->scale, wa, fa);
        }
        else
        {
            void* ent = (an && !an->isStatic) ? an->ent : NULL;
            if (in->tflags & 0x100)
                bf |= 8;
            dst = End_OnObject(ent, obj, in->frame + 9, in->lp, in->scale, wa, fa);
            src = End_OnObject(ent, obj, in->frame + 9, in->lp, 0, T16(t, 0x48), fb);
        }
        if (bi->bolt)
            bi->bolt->flags |= 0x1000;
        bi->bolt = Bolt_Create(src, dst, T16(t, 0x36), T16(t, 0x38), t + 0x3b, T16(t, 0x3e), T16(t, 0x40),
                               T16(t, 0x42), T16(t, 0x44), 0, T16(t, TP_MSG9), bf);
        if (!bi->bolt)
            return 0;
        bi->bolt->snd = (f & 0x100) ? in->snd : 0;
        return 1;
    }
    case 1:
    case 3:
    case 4:
    case 9:
        return 0;
    case 6:
        return (TU16(t, 0x4a) & 0x10) != 0;
    case 7:
        if (TU16(t, 0x4a) & 0x10)
            in->state |= 1;
        return 1;
    default:
        return Fx_MiscHandler(msg, in);
    }
}

/* 8006e4b4 */
FxInst* BoltInst_Create(uint8_t* t)
{
    BoltInst* bi = (BoltInst*)calloc(1, sizeof(BoltInst));
    if (!bi)
        return NULL;
    Fx_InitInst(&bi->h, t);
    bi->h.handler = BoltInst_Handler;
    TU16(t, 0x4a) |= 0x10;
    bi->h.prims = 0;
    bi->h.prims2 = (int16_t)(T16(t, 0x38) * T16(t, 0x36) * 2);
    return &bi->h;
}
