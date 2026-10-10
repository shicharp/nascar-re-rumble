/* Sistemas de particulas (efectos de tipo 0x5E): chispas, humo, tierra, trozos de hielo, anillos de humo...
 * Original (0x8003A000-0x8003C1D0, 0xB8 bytes por instancia y 0x24 por particula):
 *  - crear 8003c08c / 8003aab4 (campos de la plantilla, grupo de max particulas, gravedad, viento, pasos de
 *    color 8003a8d4), manejador 8003be84 (0 crear: origen y rafaga inicial; 1 emitir; 10 liberar 80039c34).
 *  - emitir 80039e80: n particulas en un cono alrededor del eje Y del marco (inclinacion entre +0x84 y +0x86,
 *    rumbo al azar) con radio, velocidad y vida de la plantilla; la rafaga se guarda (+0xA8) para repetirla
 *    igual en las siguientes emisiones. Limite global de 300 particulas (500 para los anillos 0x1C / 0x1D).
 *  - mover 8003b028: aceleracion = gravedad + (viento - v) * rozamiento (8003af54, GPL de la GTE), vaiven de
 *    las hojas (+0x8D & 4), color cada 4 frames (80039d28 entre los dos colores, 80039c90 fundido a negro),
 *    muere al caer bajo el suelo, al acabarse su vida o al encogerse.
 *  - dibujar 8003b8c4: TILE de 1 a 7 pixeles (8003b7dc) o sprites de la pagina 8 de la pista (8003c1d0:
 *    cuadrados de cara a la camara o planos en XZ, POLY_FT4 / POLY_GT4 aditivos o sustractivos). Tabla de
 *    sprites 800aa938 (9 x {pagina, u0, v0, u1, v1, region, tpages}, 8003bd60). */
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
#include "fx_int.h"
#include "carphys_int.h"
#include "vecmath.h"

typedef struct Particle
{
    struct Particle* next;   /* +0x00 */
    int16_t  pos[3];         /* +0x04 (1/16 del mundo; respecto al origen o al marco local) */
    int16_t  lifeK;          /* +0x0A */
    int16_t  vel[3];         /* +0x0C */
    int16_t  drag;           /* +0x12 */
    uint8_t  flags;          /* +0x14 1/2/4 canal en su tope, 8/0x10/0x20 sentido del vaiven, 0x40 vaiven */
    uint8_t  rgb[3];         /* +0x15 */
    uint16_t ticks;          /* +0x18 vida (13 bits) | lado del punto << 13 */
    int8_t   flutter;        /* +0x1A */
    int8_t   spin;           /* +0x1B */
    uint16_t size;           /* +0x1C (8.8) */
    uint8_t  pad1e[3];
    int8_t   grow;           /* +0x21 */
    int16_t  angle;          /* +0x22 */
} Particle;

static_assert(sizeof(Particle) == 0x24, "Particle debe medir 0x24");

typedef struct
{
    FxInst    h;             /* 0x00-0x63 */
    Particle* freeList;      /* +0x64 */
    Particle* active;        /* +0x68 */
    Particle* pool;          /* +0x6C */
    int16_t   wind[3];       /* +0x70 */
    int16_t   drag;          /* +0x76 */
    int16_t   unk78;
    int16_t   gravity;       /* +0x7A */
    int16_t   unk7c;
    int16_t   lifeK;         /* +0x7E (< 0: al azar) */
    int16_t   speedMin;      /* +0x80 */
    int16_t   speedMax;      /* +0x82 */
    int16_t   pitchMin;      /* +0x84 */
    int16_t   pitchMax;      /* +0x86 */
    int16_t   count;         /* +0x88 por emision */
    int16_t   max;           /* +0x8A */
    uint8_t   mode;          /* +0x8C 1 fundido a negro, 2 color de ida y vuelta */
    uint8_t   draw;          /* +0x8D 1 doble, 2 giro al azar, 4 vaiven, 8 semitransparente, 0x10 aditivo,
                              * 0x20 sustractivo, 0x40 de cara a la camara, 0x80 con velocidad inicial */
    int8_t    dotSize;       /* +0x8E (< 0: al azar) */
    uint8_t   c0[3];         /* +0x8F */
    uint8_t   c1[3];         /* +0x92 */
    uint8_t   sprite;        /* +0x95 0 = puntos */
    int16_t   step[3];       /* +0x96 */
    int16_t   yaw0, pitch0;  /* +0x9C / +0x9E */
    int16_t   radius;        /* +0xA0 */
    uint8_t   jitV, jitP;    /* +0xA2 / +0xA3 */
    int16_t   numActive;     /* +0xA4 */
    int16_t   fade;          /* +0xA6 */
    Particle* burst;         /* +0xA8 */
    int16_t   yaw;           /* +0xAC */
    int16_t   yawSpeed;      /* +0xAE */
    int16_t   vel0[3];       /* +0xB0 */
} Psys;

/* 800aa938: sprites (pagina = slot, rectangulo de la textura) */
typedef struct
{
    int      slot;
    uint8_t  u0, v0, u1, v1;
    uint16_t clut;
    int      tpage;           /* sin modo de semitransparencia */
} PsysSprite;

static PsysSprite s_sprites[10];
static int        s_drawView;

/* 8003bd60 */
void Psys_InitSprites(void)
{
    for (int k = 1; k < 10; k++)
    {
        uint32_t e = 0x800aa914u + (uint32_t)k * 0x24;
        PsysSprite* s = &s_sprites[k];
        s->slot = Exe_S32(e);
        s->u0 = (uint8_t)Exe_S32(e + 4);
        s->v0 = (uint8_t)Exe_S32(e + 8);
        s->u1 = (uint8_t)Exe_S32(e + 12);
        s->v1 = (uint8_t)Exe_S32(e + 16);
        const uint8_t* r = Cpag_FindRegion(s->slot, s->u0, s->v0);
        s->clut = r ? NR_Read16(r + 4) : 0;
        int du, dv;
        int tp = TexSlot_GetTPage(s->slot, 0, &du, &dv);
        s->tpage = tp < 0 ? 0 : (tp & ~0x60);
    }
}

static int16_t Sat16(int v)
{
    return (int16_t)(v < -0x8000 ? -0x8000 : v > 0x7fff ? 0x7fff : v);
}

/* ---- creacion ---- */

/* 8003a8d4: pasos de color por tic (4.12); si bajan se cambian los topes */
static void Psys_ColorSteps(Psys* ps)
{
    if (ps->fade < 0x10)
        ps->step[0] = ps->step[1] = ps->step[2] = 0;
    else if (!(ps->mode & 1))
        for (int k = 0; k < 3; k++)
            ps->step[k] = (int16_t)(((int)ps->c1[k] - (int)ps->c0[k]) * 0x1000 / ps->fade);
    else
    {
        int s = ((int)ps->c0[0] + ps->c0[1] + ps->c0[2]) * 0x1000 / ps->fade;
        ps->step[0] = ps->step[1] = ps->step[2] = (int16_t)s;
    }
    for (int k = 0; k < 3; k++)
        if (ps->step[k] < 0)
        {
            uint8_t t = ps->c1[k];
            ps->c1[k] = ps->c0[k];
            ps->c0[k] = t;
        }
}

static int Psys_Handler(int msg, FxInst* in, intptr_t a, const FxSpawn* sp);

/* 8003aab4 */
static int Psys_Init(Psys* ps, uint8_t* t, GfxObj* obj)
{
    Fx_InitInst(&ps->h, t);
    ps->sprite = T8(t, 0x57);
    ps->yaw = T16(t, 0x64);
    ps->yawSpeed = T16(t, 0x66);
    int size = TS8(t, 0x50);
    int max = T16(t, 0x36), n = T16(t, 0x34);
    /* bandera 0x8000: con el objeto lejos (80013db0 > 0) la mitad de particulas y de 1 pixel */
    if (obj && (T16(t, 0x32) & 0x8000) && Obj_LodLevel(obj) > 0)
    {
        size = 1;
        max = (int16_t)T16(t, 0x36) >> 1;
        n = (int16_t)T16(t, 0x34) >> 1;
    }
    if (max == -1)
        max = n;
    int cls = T8(t, TP_CLASS);
    if (cls == 0x1c || cls == 0x3f)
    {
        if (g_FxParticles + max > 300)
            n = max = max >> 1;
    }
    if (cls == 0x1d || cls == 0x40)
    {
        if (g_FxParticles + max > 300)
        {
            n = max = max - 8;
            if (max < 1)
                n = max = 1;
        }
    }
    if (max > 300)
        max = 300;
    if (max < 1)
        return 0;
    if (max < n)
        n = max;
    /* (80016d4c: con frames lentos la mitad; el port no lo hace) */
    ps->max = (int16_t)max;
    ps->count = (int16_t)n;
    ps->pool = NULL;
    ps->active = NULL;
    ps->drag = (int16_t)(T16(t, 0x44) >> 4);
    ps->gravity = T16(t, 0x3c);
    for (int k = 0; k < 3; k++)
    {
        ps->c0[k] = T8(t, 0x51 + k);
        ps->c1[k] = T8(t, 0x54 + k);
    }
    ps->speedMin = T16(t, 0x38);
    ps->speedMax = T16(t, 0x3a);
    ps->lifeK = T16(t, 0x46);
    ps->yaw0 = T16(t, 0x48);
    ps->pitch0 = T16(t, 0x4a);
    ps->pitchMin = T16(t, 0x4c);
    ps->pitchMax = T16(t, 0x4e);
    ps->jitV = T8(t, 0x5e) & 0xf;
    ps->jitP = T8(t, 0x5f) & 0xf;
    int16_t fade = T16(t, 0x58);
    ps->fade = fade < 0 ? (int16_t)-fade : fade;
    ps->dotSize = (int8_t)size;
    if (ps->sprite == 0)
    {
        int s = (int8_t)size >> 1;
        if (s >= 8)
            s = 7;
        else if (s <= -8)
            s = -7;
        else if (s == 0)
            s = 1;
        ps->dotSize = (int8_t)s;
    }
    ps->mode = T8(t, 0x5a);
    ps->draw = T8(t, 0x5b);
    ps->radius = T16(t, 0x5c);
    int m[9];
    Math_RotMatrix(T16(t, 0x3e), -T16(t, 0x40), 0, m);
    int w = T16(t, 0x42);
    ps->wind[0] = (int16_t)((m[6] >> 4) * w >> 12);
    ps->wind[1] = (int16_t)((m[7] >> 4) * w >> 12);
    ps->wind[2] = (int16_t)((m[8] >> 4) * w >> 12);
    ps->pool = (Particle*)calloc((size_t)ps->max, sizeof(Particle));
    if (!ps->pool)
        return 0;
    for (int i = 0; i < ps->max - 1; i++)
        ps->pool[i].next = &ps->pool[i + 1];
    ps->pool[ps->max - 1].next = NULL;
    ps->numActive = 0;
    ps->unk7c = 0;
    ps->unk78 = 0;
    ps->freeList = ps->pool;
    ps->gravity = (int16_t)(ps->gravity * -0x100 / 100);
    Psys_ColorSteps(ps);
    ps->burst = ps->count < ps->max ? (Particle*)calloc((size_t)ps->count, sizeof(Particle)) : NULL;
    return 1;
}

/* 8003c08c */
FxInst* Psys_Create(uint8_t* t, GfxObj* obj)
{
    Psys* ps = (Psys*)calloc(1, sizeof(Psys));
    if (!ps)
        return NULL;
    if (!Psys_Init(ps, t, obj))
    {
        free(ps->pool);
        free(ps);
        return NULL;
    }
    ps->h.handler = Psys_Handler;
    ps->h.prims = (int16_t)((ps->draw & 0x30) ? ps->max << 1 : ps->max);
    ps->h.prims2 = 0;
    return &ps->h;
}

/* 800751cc (parte): velocidades de la instancia */
void Psys_SetSpeed(FxInst* in, int min, int max)
{
    Psys* ps = (Psys*)in;
    ps->speedMax = (int16_t)max;
    ps->speedMin = (int16_t)min;
}

/* 80039c34 */
static void Psys_Free(Psys* ps)
{
    g_FxParticles -= ps->numActive;
    free(ps->burst);
    free(ps->pool);
    free(ps);
}

/* ---- emision ---- */

static Particle* Psys_Alloc(Psys* ps)
{
    Particle* p = ps->freeList;
    if (p)
        ps->freeList = p->next;
    return p;
}

/* Particula nueva en el espacio de emision: direccion en el cono, radio, velocidad y rozamiento */
static void Psys_Gen(Psys* ps, Particle* p, int range)
{
    int r;
    if (ps->radius < 0)
        r = (-ps->radius * (Rand_B() & 0xfff)) >> 12;
    else
        r = ps->radius > 0 ? ps->radius : 0;
    unsigned rp = (unsigned)Rand_B();
    int pmin = ps->pitchMin, pmax = ps->pitchMax;
    unsigned ry = (unsigned)Rand_B();
    int m[9];
    Math_RotMatrix((int)((ry & 0xffff) >> 4), pmin + (int)((rp & 0xffff) * (unsigned)(pmax - pmin) >> 16), 0, m);
    const int* dir = m + 3;   /* eje Y */
    if (r == 0)
        p->pos[0] = p->pos[1] = p->pos[2] = 0;
    else
        for (int k = 0; k < 3; k++)
            p->pos[k] = (int16_t)((dir[k] >> 4) * r >> 12);
    int speed;
    if (range == 0)
        speed = ps->speedMax;
    else
        speed = (int16_t)(ps->speedMin + (int16_t)((int)(Rand_B() & 0xfff) * range >> 12));
    for (int k = 0; k < 3; k++)
        p->vel[k] = (int16_t)((dir[k] >> 4) * speed >> 12);
    if (ps->lifeK < 0)
        p->lifeK = (int16_t)(-ps->lifeK * (Rand_B() & 0xfff) >> 12);
    else
        p->lifeK = ps->lifeK;
    int d = p->lifeK ? (int16_t)(0x100000 / p->lifeK) : 0;
    p->drag = (int16_t)((uint32_t)(d * ps->drag) >> 8);
}

/* 80039e80: emite una rafaga (fresh: genera una nueva y la guarda en burst) */
static void Psys_Emit(Psys* ps, Particle* burst, int fresh, const FxSpawn* sp)
{
    FxInst* in = &ps->h;
    int cls = T8(in->tmpl, TP_CLASS);
    int big = cls == 0x1c || cls == 0x3f || cls == 0x1d || cls == 0x40;
    if (!burst)
        fresh = 0;
    if (sp && (sp->flags & 2))
    {
        for (int k = 0; k < 3; k++)
            ps->vel0[k] = (int16_t)(sp->vel[k] >> 4);
        ps->draw |= 0x80;
    }
    FxAnchor* an = in->anchor;
    const int* w = Fx_WorldFrame(an ? an->obj : NULL, in->tflags, in->lp, 1, an, in->frame);
    int wf[12];
    memcpy(wf, w, sizeof(wf));
    int ticks = (int)((uint32_t)(T32(in->tmpl, TP_TIME) * 300) >> 16) & 0x1fff;
    if (ps->speedMin == -1)
        ps->speedMin = ps->speedMax;
    int range = ps->speedMax - ps->speedMin;
    int spin = 0, rot = 0, grow = 0;
    if (ps->sprite)
    {
        spin = TS8(in->tmpl, 0x60);
        rot = (int)((uint32_t)TU16(in->tmpl, 0x62) << 16) >> 20;
        grow = TS8(in->tmpl, 0x61);
    }
    for (int i = 0; i < ps->count; i++)
    {
        Particle* p;
        if (!burst)
        {
            p = Psys_Alloc(ps);
            if (!p)
                return;
            Psys_Gen(ps, p, range);
        }
        else if (fresh)
        {
            p = &burst[i];
            Psys_Gen(ps, p, range);
        }
        else
        {
            p = Psys_Alloc(ps);
            if (!p)
                return;
            Particle* nx = p->next;
            *p = burst[i];
            p->next = nx;
        }
        Particle* q = p;
        if (burst && fresh)
        {
            q = Psys_Alloc(ps);
            if (!q)
                return;
            Particle* nx = q->next;
            *q = *p;
            q->next = nx;
        }
        if (g_FxParticles >= (big ? 0x1f5 : 300))
        {
            /* sin sitio: la particula vuelve a la lista libre */
            q->next = ps->freeList;
            ps->freeList = q;
            return;
        }
        g_FxParticles++;
        for (int k = 0; k < 3; k++)
            q->rgb[k] = ps->step[k] < 0 ? ps->c1[k] : ps->c0[k];
        int jv = 0x20 - ps->jitV, jp = 0x20 - ps->jitP;
        if (jv < 0x20)
            for (int k = 0; k < 3; k++)
                q->vel[k] = (int16_t)(q->vel[k] + (int16_t)(Rand_B() >> jv));
        if (jp < 0x20)
            for (int k = 0; k < 3; k++)
                q->pos[k] = (int16_t)(q->pos[k] + (int16_t)(Rand_B() >> jp));
        if (!(in->tflags & 0x100))
        {
            Fx_RotS(wf, q->vel, q->vel);
            Fx_RotS(wf, q->pos, q->pos);
            for (int k = 0; k < 3; k++)
                q->pos[k] = (int16_t)(q->pos[k] + ((wf[9 + k] - in->origin[k]) >> 4));
        }
        else
        {
            Fx_RotS(in->frame, q->vel, q->vel);
            Fx_RotS(in->frame, q->pos, q->pos);
            for (int k = 0; k < 3; k++)
                q->pos[k] = (int16_t)(q->pos[k] + (in->frame[9 + k] >> 4));
        }
        if (ps->draw & 0x80)
        {
            void* ent = (an && !an->isStatic) ? an->ent : NULL;
            if (!(in->tflags & 4) || !ent)
                for (int k = 0; k < 3; k++)
                    q->vel[k] = (int16_t)(q->vel[k] + ps->vel0[k] * 0x10);
            else
            {
                int16_t r[3];
                Fx_RotS(((RaceCar*)ent)->obj.rot, ps->vel0, r);
                q->vel[0] = (int16_t)(q->vel[0] + r[0] * 0x10);
                q->vel[1] = (int16_t)(q->vel[1] + r[1] * 0x10);
                q->vel[2] = (int16_t)(q->vel[2] + r[2] * 0x10);
            }
        }
        q->flags = 0;
        q->ticks = (uint16_t)ticks;
        if (ps->sprite == 0)
        {
            int s = ps->dotSize < 0 ? (int)((unsigned)Rand_B() % (unsigned)-ps->dotSize) + 1 : ps->dotSize;
            q->ticks = (uint16_t)(ticks | s << 13);
        }
        else
        {
            int a = rot << 4;
            if (rot < 0)
            {
                unsigned u = ((unsigned)Rand_B() % (unsigned)-rot) * 2 + 1 & 0xff;
                a = ((int)u < -rot ? 0x100 - (int)u : (int)u + rot) << 4;
            }
            q->angle = (int16_t)a;
            q->flutter = (int8_t)Rand_B();
            if (q->spin == 0 || spin == 0 || !(ps->draw & 2))
                q->spin = (int8_t)spin;
            else
            {
                unsigned as = (unsigned)(spin < 0 ? -spin : spin);
                q->spin = (int8_t)((int)((unsigned)Rand_B() % (as * 2)) - (int)as + 2);
            }
            int s = ps->dotSize < 0 ? (int)((unsigned)Rand_B() % (unsigned)-ps->dotSize) + 1 : ps->dotSize;
            q->size = (uint16_t)(s << 8);
            q->grow = (int8_t)grow;
        }
        /* 80039bd4 */
        q->next = ps->active;
        ps->active = q;
        ps->numActive++;
    }
}

/* 8003be84 */
static int Psys_Handler(int msg, FxInst* in, intptr_t a, const FxSpawn* sp)
{
    Psys* ps = (Psys*)in;
    if (msg == 10)
    {
        Fx_BaseHandler(10, in);
        Psys_Free(ps);
        return 0;
    }
    if (Fx_BaseHandler(msg, in))
        return 0;
    switch (msg)
    {
    case 1:
        Fx_SetupFrame(in->tmpl, NULL, in->anchor, sp, in->frame, &in->scale, &in->lp);
        Psys_Emit(ps, ps->burst, (int)(a & 0xff), sp);
        return 1;
    case 0:
    {
        FxAnchor* an = in->anchor;
        const int* o = (an && an->obj) ? an->obj->pos : (an ? an->pos : in->frame + 9);
        memcpy(in->origin, o, sizeof(in->origin));
        Psys_Emit(ps, ps->burst, 1, sp);
        in->killTimer = 0;
        in->delay = 0;
        return 1;
    }
    case 7:
        return 0;
    default:
        return Fx_MiscHandler(msg, in);
    }
}

/* ---- movimiento ---- */

/* 8003af54: a = acc + (viento - v) * rozamiento; v += a * dt; p += v * dt (GPL de la GTE con IR saturados) */
static void Psys_Move(const int16_t* wind, Particle* p, int drag, int dtf, const int* acc)
{
    int ir0 = (int16_t)(drag << 4);
    int a[3], v[3];
    for (int k = 0; k < 3; k++)
        a[k] = Sat16(acc[k] + (int)(((int64_t)(int16_t)(wind[k] - p->vel[k]) * ir0) >> 12));
    for (int k = 0; k < 3; k++)
    {
        int nv = p->vel[k] + (int)(((int64_t)a[k] * dtf) >> 12);
        p->vel[k] = (int16_t)nv;
        v[k] = Sat16(nv);
    }
    for (int k = 0; k < 3; k++)
        p->pos[k] = (int16_t)(p->pos[k] + (int)(((int64_t)v[k] * dtf) >> 12));
}

/* 80039c90: fundido a negro (azul, verde y rojo por ese orden) */
static void Psys_FadeBlack(Particle* p, int d)
{
    if (p->rgb[0] == 0)
        return;
    for (int k = 2; k >= 1; k--)
        if (p->rgb[k])
        {
            if (d <= p->rgb[k])
            {
                p->rgb[k] = (uint8_t)(p->rgb[k] - d);
                return;
            }
            d -= p->rgb[k];
            p->rgb[k] = 0;
        }
    p->rgb[0] = (int)p->rgb[0] < d ? 0 : (uint8_t)(p->rgb[0] - d);
}

/* 80039d28: avanza cada canal hacia su tope (o rebota en modo de ida y vuelta) */
static void Psys_ColorStep(const Psys* ps, Particle* p, const int* d)
{
    uint8_t f = p->flags;
    for (int k = 0; k < 3; k++)
    {
        int v = d[k] + p->rgb[k];
        if (d[k] > 0)
        {
            if (v < ps->c1[k])
                p->rgb[k] = (uint8_t)v;
            else
            {
                p->rgb[k] = ps->c1[k];
                f = (ps->mode & 2) ? (uint8_t)(f ^ (8 << k)) : (uint8_t)(f | (1 << k));
            }
        }
        else if (d[k] < 0)
        {
            if (ps->c0[k] < v)
                p->rgb[k] = (uint8_t)v;
            else
            {
                p->rgb[k] = ps->c0[k];
                f = (ps->mode & 2) ? (uint8_t)(f ^ (8 << k)) : (uint8_t)(f | (1 << k));
            }
        }
    }
    p->flags = f;
}

/* 8003b028 */
static void Psys_Update(Psys* ps, int dt)
{
    FxInst* in = &ps->h;
    FxAnchor* an = in->anchor;
    const int* w = Fx_WorldFrame(an ? an->obj : NULL, in->tflags, in->lp, 0, an, in->frame);
    int wy = w[10];
    int ground = 0;
    if (!(in->tflags & 2))
    {
        void* ent = (an && !an->isStatic) ? an->ent : NULL;
        ground = ent ? ((RaceCar*)ent)->obj.shadowY << 4 : wy - 0x800;
    }
    if (!ps->active && in->repeats == -1)
    {
        in->state |= 1;
        return;
    }
    int dtf = (dt << 12) / 300;
    int sR = ps->step[0] * dt * 4 / 300, sG = 0, sB = 0;
    if (!(ps->mode & 1))
    {
        sG = ps->step[1] * dt * 4 / 300;
        sB = ps->step[2] * dt * 4 / 300;
    }
    if (ps->yawSpeed)
        ps->yaw = (int16_t)(ps->yaw + (int16_t)((uint32_t)(ps->yawSpeed * dt) >> 8));
    int16_t wind[3] = { ps->wind[0], ps->wind[1], ps->wind[2] };
    unsigned idx = 0, frame = (unsigned)g_FrameCount;
    Particle* prev = NULL;
    for (Particle* p = ps->active; p;)
    {
        Particle* next = p->next;
        if (p->spin)
        {
            int r = p->spin * dt >> 3;
            if (r == 0)
                r = p->spin > 0 ? 1 : -1;
            if (r < 1)
            {
                int a = p->angle + r;
                p->angle = (int16_t)(a >= 0 ? a : a + 0xfff);
            }
            else
            {
                int x = (0xfff - r) - p->angle;
                p->angle = (int16_t)(x < 0 ? -x : p->angle + r);
            }
        }
        int drag = p->drag;
        int acc[3];
        if (!(ps->draw & 4))
        {
            acc[0] = 0;
            acc[1] = ps->gravity;
            acc[2] = 0;
        }
        else
        {
            /* vaiven (hojas): el angulo de giro decide hacia donde se desliza */
            int f = p->flutter, af = f < 0 ? -f : f;
            int s = dt * ((af >> 1) + 0x3f) * 4 >> 8;
            if (s < 1)
                s = 1;
            if (!(p->flags & 0x40))
            {
                if (s <= f + 0x7f)
                    p->flutter = (int8_t)(p->flutter - s);
                else
                {
                    p->flutter = (int8_t)0x81;
                    p->flags |= 0x40;
                }
            }
            else if (0x7f - f < s)
            {
                p->flutter = 0x7f;
                p->flags &= 0xbf;
            }
            else
                p->flutter = (int8_t)(p->flutter + s);
            int a = p->flutter < 0 ? -p->flutter : p->flutter;
            int sn, cs;
            Math_SinCos(0xfff - p->angle, &sn, &cs);
            sn >>= 8;
            cs >>= 8;
            if (p->flutter > 0)
            {
                sn = -sn;
                cs = -cs;
            }
            acc[0] = (a * -0xc * sn) >> 9;
            acc[2] = (a * -0xc * cs) >> 9;
            if (acc[0] == -1 || acc[0] == 1)
                acc[0] = 0;
            if (acc[2] == -1 || acc[2] == 1)
                acc[2] = 0;
            int g = ps->gravity;
            acc[1] = ((wind[1] - p->vel[1]) * (((drag * (0x7f - a)) >> 8) - (drag >> 1)) * 0x100 >> 16) +
                     (g >> 1) + ((g * a) >> 8);
        }
        Psys_Move(wind, p, drag, dtf, acc);
        if (((frame ^ idx) & 3) == 0)
        {
            if (!(ps->mode & 1))
            {
                int d[3];
                if (!(ps->mode & 2))
                {
                    d[0] = (p->flags & 1) ? 0 : sR;
                    d[1] = (p->flags & 2) ? 0 : sG;
                    d[2] = (p->flags & 4) ? 0 : sB;
                }
                else
                {
                    d[0] = (p->flags & 8) ? -sR : sR;
                    d[1] = (p->flags & 0x10) ? -sG : sG;
                    d[2] = (p->flags & 0x20) ? -sB : sB;
                }
                Psys_ColorStep(ps, p, d);
            }
            else
                Psys_FadeBlack(p, (p->flags & 1) ? 0 : sR);
        }
        int dead = 0;
        if (((frame ^ idx) & 7) == 0 && !(in->tflags & 2) && p->vel[1] < 0 && p->pos[1] * 0x10 + wy < ground)
            dead = 1;
        else
        {
            unsigned t = p->ticks & 0x1fff, nt = t;
            if (t != 0)
            {
                nt = (uint16_t)(t - dt);
                if ((int16_t)nt <= 0)
                    dead = 1;
            }
            if (!dead && p->grow)
            {
                int g = p->grow * dt * 2;
                if (g == 0)
                    g = p->grow > 0 ? 1 : -1;
                if (g < 1)
                {
                    if ((int)p->size < -g)
                    {
                        p->size = 1;
                        dead = 1;
                    }
                    else
                        p->size = (uint16_t)(p->size + g);
                }
                else
                    p->size = (int)(g + p->size) < 0xffff ? (uint16_t)(p->size + g) : 0xffff;
            }
            if (!dead)
                p->ticks = (uint16_t)((nt & 0x1fff) | (p->ticks & 0xe000));
        }
        if (dead)
        {
            /* 80039bf4 */
            if (prev)
                prev->next = next;
            else
                ps->active = next;
            p->next = ps->freeList;
            ps->freeList = p;
            ps->numActive--;
            g_FxParticles--;
        }
        else
            prev = p;
        idx++;
        p = next;
    }
}

/* 8003b77c */
void Psys_UpdateAll(FxInst* list, int dt)
{
    for (FxInst* in = list; in; in = in->next)
        if (!(in->state & 8))
            Psys_Update((Psys*)in, dt);
}

/* ---- dibujo ---- */

static void Psys_DrawMode(int depth, int tpage)
{
    DR_TPAGE* t = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
    if (!t)
        return;
    setDrawTPage(t, 1, 1, tpage);
    Gfx_AddPrimDepth(depth, t);
}

/* 8003b7dc: puntos (TILE de 1 a 7 pixeles; semitransparentes aditivos con +0x8D & 0x30) */
static void Psys_DrawDots(const Psys* ps)
{
    int semi = ps->draw & 0x30;
    for (const Particle* p = ps->active; p; p = p->next)
    {
        Gte_SetV0(p->pos[0], p->pos[1], p->pos[2]);
        doCOP2(GTE_OP_RTPS);
        uint32_t flag = CFC2(31);
        int sz = (int)MFC2(19);
        uint32_t sxy = MFC2(14);
        int size = p->ticks >> 13;
        if (size == 0)
            size = 1;
        if (flag & 0x1c66000)
            continue;
        int x = (int16_t)sxy, y = (int16_t)(sxy >> 16);
        if (sz < 0 || y < 0 || y >= 0xf0 || !Wide_OnScreenX(x))
            continue;
        if (semi)
            Psys_DrawMode(sz, 0x20);
        TILE* t = (TILE*)Gfx_AllocPrim(sizeof(TILE));
        if (!t)
            return;
        setTile(t);
        if (semi)
            setSemiTrans(t, 1);
        setRGB0(t, p->rgb[0], p->rgb[1], p->rgb[2]);
        setXY0(t, x, y);
        setWH(t, size, size);
        Gfx_AddPrimDepth(sz, t);
    }
}

/* 8003c184: centro proyectado y radio en pantalla (RTPS con DQA = tamano: MAC0 >> 15) */
static int Psys_ProjectCenter(const int16_t* v, int size, uint32_t* sxy, int* sz, int* rad)
{
    Gte_SetV0(v[0], v[1], v[2]);
    CTC2((uint32_t)size, 27);
    CTC2(0, 28);
    doCOP2(GTE_OP_RTPS);
    uint32_t flag = CFC2(31);
    *sz = (int16_t)MFC2(11);   /* IR3 (z respecto a la camara) como profundidad */
    if (flag & 0xffffe000)
        return 1;
    *rad = (int)(MFC2(24) >> 15);
    *sxy = MFC2(14);
    return 0;
}

static void Psys_Quad(const Psys* ps, const Particle* p, int depth, const int* xy)
{
    const PsysSprite* s = &s_sprites[ps->sprite < 10 ? ps->sprite : 1];
    int tp = s->tpage | ((ps->draw & 0x10) ? 0x20 : (ps->draw & 0x20) ? 0x40 : 0);
    if (!(ps->draw & 0x30))
    {
        POLY_FT4* q = (POLY_FT4*)Gfx_AllocPrim(sizeof(POLY_FT4));
        if (!q)
            return;
        setPolyFT4(q);
        setRGB0(q, 0x80, 0x80, 0x80);
        if (ps->draw & 8)
            setSemiTrans(q, 1);
        setXY4(q, xy[0], xy[1], xy[2], xy[3], xy[4], xy[5], xy[6], xy[7]);
        setUV4(q, s->u0, s->v0, s->u1, s->v0, s->u0, s->v1, s->u1, s->v1);
        q->clut = s->clut;
        q->tpage = (uint16_t)tp;
        Gfx_AddPrimDepth(depth, q);
    }
    else
    {
        POLY_GT4* q = (POLY_GT4*)Gfx_AllocPrim(sizeof(POLY_GT4));
        if (!q)
            return;
        setPolyGT4(q);
        setSemiTrans(q, 1);
        setRGB0(q, p->rgb[0], p->rgb[1], p->rgb[2]);
        setRGB1(q, p->rgb[0], p->rgb[1], p->rgb[2]);
        setRGB2(q, p->rgb[0], p->rgb[1], p->rgb[2]);
        setRGB3(q, p->rgb[0], p->rgb[1], p->rgb[2]);
        setXY4(q, xy[0], xy[1], xy[2], xy[3], xy[4], xy[5], xy[6], xy[7]);
        setUV4(q, s->u0, s->v0, s->u1, s->v0, s->u0, s->v1, s->u1, s->v1);
        q->clut = s->clut;
        q->tpage = (uint16_t)tp;
        Gfx_AddPrimDepth(depth, q);
    }
}

/* 8003c1d0: sprite de una particula (de cara a la camara con +0x8D & 0x40, si no plano en XZ) */
static void Psys_DrawSprite(const Psys* ps, const Particle* p)
{
    int size = p->size >> 8;
    if (ps->draw & 1)
        size <<= 1;
    int xy[8], depth;
    if (!(ps->draw & 0x40))
    {
        /* esquinas 0..3 en XZ (giradas con +0x22: A = c - s, B = c + s) */
        int px = p->pos[0], pz = p->pos[2], A = size, B = size;
        if (p->angle != 0)
        {
            int sn, cs;
            Math_SinCos(p->angle, &sn, &cs);
            int s1 = size * (sn >> 4) >> 12, c1 = size * (cs >> 4) >> 12;
            A = c1 - s1;
            B = c1 + s1;
        }
        int16_t v[4][3] = {
            { (int16_t)(px - A), 0, (int16_t)(pz - B) },
            { (int16_t)(px + B), 0, (int16_t)(pz - A) },
            { (int16_t)(px + A), 0, (int16_t)(pz + B) },
            { (int16_t)(px - B), 0, (int16_t)(pz + A) },
        };
        int y0 = p->pos[1], y1 = p->pos[1];
        if (ps->draw & 4)
        {
            int h = size * p->flutter >> 9;   /* inclinacion con el vaiven */
            y0 = p->pos[1] + h;
            y1 = p->pos[1] - h;
        }
        v[0][1] = v[1][1] = (int16_t)y0;
        v[2][1] = v[3][1] = (int16_t)y1;
        /* 8001016c: basta con que algun vertice este dentro en x y alguno en y (OR de las banderas) */
        uint32_t sxy[4];
        int sz[4], fl = 0;
        for (int k = 0; k < 4; k++)
        {
            Gte_SetV0(v[k][0], v[k][1], v[k][2]);
            doCOP2(GTE_OP_RTPS);
            if (CFC2(31) & 0x1c66000)
                return;
            sxy[k] = MFC2(14);
            sz[k] = (int)MFC2(19);
            int x = (int16_t)sxy[k], y = (int16_t)(sxy[k] >> 16);
            fl |= (sz[k] < 0x20) | (Wide_OnScreenX(x) << 6) | (((unsigned)y < 2u * 0x78) << 7);
        }
        if (fl != 0xc0)
            return;
        depth = (sz[0] + sz[1] + sz[2] + sz[3]) >> 2;
        /* orden de la primitiva: 0, 1, 3, 2 */
        xy[0] = (int16_t)sxy[0]; xy[1] = (int16_t)(sxy[0] >> 16);
        xy[2] = (int16_t)sxy[1]; xy[3] = (int16_t)(sxy[1] >> 16);
        xy[4] = (int16_t)sxy[3]; xy[5] = (int16_t)(sxy[3] >> 16);
        xy[6] = (int16_t)sxy[2]; xy[7] = (int16_t)(sxy[2] >> 16);
    }
    else
    {
        uint32_t sxy;
        int rad;
        if (Psys_ProjectCenter(p->pos, size, &sxy, &depth, &rad))
            return;
        int cx = (int16_t)sxy, cy = (int16_t)(sxy >> 16);
        if (rad > 0x78 || cx + rad < -g_WideMargin || cy + rad < 0 || cx - rad > 2 * 0xa0 + g_WideMargin ||
            cy - rad > 2 * 0x78)
            return;
        if (p->angle == 0)
        {
            xy[0] = cx - rad; xy[1] = cy - rad;
            xy[2] = cx + rad; xy[3] = cy - rad;
            xy[4] = cx - rad; xy[5] = cy + rad;
            xy[6] = cx + rad; xy[7] = cy + rad;
        }
        else
        {
            /* (girados salen a la mitad de tamano: >> 13 sobre senos 4.12, como el original) */
            int sn, cs;
            Math_SinCos(p->angle, &sn, &cs);
            int a = rad * (cs >> 4), b = rad * (sn >> 4);
            int s10 = (a - b) >> 13, s14 = (b + a) >> 13, s5 = (a + b) >> 13, s6 = (b - a) >> 13;
            xy[0] = cx - s10; xy[1] = cy - s14;
            xy[2] = cx + s5;  xy[3] = cy + s6;
            xy[4] = cx - s5;  xy[5] = cy - s6;
            xy[6] = cx + s10; xy[7] = cy + s14;
        }
    }
    Psys_Quad(ps, p, depth, xy);
}

/* 8003b8c4 */
static void Psys_Draw(Psys* ps, const int* focus)
{
    FxInst* in = &ps->h;
    static const int ident[9] = { 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x10000 };
    if (!(in->tflags & 0x100))
    {
        int d[3] = { in->origin[0] - focus[0], in->origin[1] - focus[1], in->origin[2] - focus[2] };
        if (!(in->tflags & 0x4000) && Vec_ApproxLen3(d) > 0x1ffff)
            return;
        Render_LoadObjectMatrix16(ident, in->origin);
    }
    else
    {
        const int* b = Fx_BaseFrame(in);
        int m[12];
        memcpy(m, b, sizeof(m));
        if (ps->yaw)
            Math_RotMatrix(ps->yaw, 0, 0, m);
        int d[3] = { m[9] - focus[0], m[10] - focus[1], m[11] - focus[2] };
        if (!(in->tflags & 0x400))
        {
            if (!(in->tflags & 0x4000) && Vec_ApproxLen3(d) > 0x1ffff)
                return;   /* (con la bandera 8, prueba de la esfera del objeto: sin uso en GlblData) */
        }
        else
        {
            FxAnchor* an = in->anchor;
            void* ent = (an && !an->isStatic) ? an->ent : NULL;
            if (ent && !(((RaceCar*)ent)->obj.flags & (s_drawView ? 8 : 2)))
                return;
        }
        Render_LoadObjectMatrix16(m, m + 9);
    }
    if (ps->sprite == 0)
        Psys_DrawDots(ps);
    else
        for (const Particle* p = ps->active; p; p = p->next)
            Psys_DrawSprite(ps, p);
}

/* 8003be24 */
void Psys_DrawAll(FxInst* list, const int* focus, int view)
{
    s_drawView = view;
    for (FxInst* in = list; in; in = in->next)
        if (!(in->state & 8))
            Psys_Draw((Psys*)in, focus);
}
