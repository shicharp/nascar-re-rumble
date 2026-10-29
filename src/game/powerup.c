/* Poderes: cajas de la pista y efectos sobre los coches. Ver powerup.h.
 * Efectos (fx.c): chispas de la caja (8003558c), destello del poder (0x401), onda (0x40E), tornado (0x404),
 * helada (0x417), rayo (0x40D) y la nube de la tormenta (fx_storm.c) con sus rayos. */
#include <stdlib.h>
#include <string.h>
#include "nr.h"
#include "exe.h"
#include "globals.h"
#include "model.h"
#include "vecmath.h"
#include "racecar.h"
#include "carphys_int.h"
#include "ai.h"
#include "actor.h"
#include "cnet.h"
#include "pause.h"
#include "powerup.h"
#include "snd.h"
#include "fx.h"

/* modelos que trae la aRSL de las cajas: proyectiles de los poderes 0 y 4, y el tornado */
static ModelHdr* s_projModel[2];   /* DAT_800af7d0 / DAT_800af7c8 */
static ModelHdr* s_twisterModel;   /* DAT_800af7c0 */

/* Solo en el port (depuracion): NR_POWERLOG=1 apunta en el log cada caja recogida y cada poder usado */
static int Power_LogOn(void)
{
    static int s_on = -1;
    if (s_on < 0)
        s_on = getenv("NR_POWERLOG") != NULL;
    return s_on;
}

int g_TornadoCount;   /* DAT_800af7bc: tornados en la pista (Race_ResetGlobals 8002cfb0 lo pone a 0) */

static uint8_t s_strikeTimer;   /* DAT_800af224: tics hasta el siguiente rayo de una tormenta suelta */

void Pickup_Reset(void)
{
    g_TornadoCount = 0;
    s_strikeTimer = 0;   /* port: el original lo deja de la carrera anterior */
    s_projModel[0] = s_projModel[1] = NULL;
    s_twisterModel = NULL;
}

int Power_Duration(int type)
{
    return (unsigned)type < POWER_COUNT ? Exe_U16(0x800aa90cu + (uint32_t)type * 4) : 0;
}

/* Campos de la caja (+0xE0 animacion, +0xF4 tics hasta reaparecer (< 0 = disponible), +0xF6 banderas del tACT
 * (+0x2A; bit 1: la IA no la coge), +0xF8 angulo de giro) */
#define PK_ANIM(a)  ((AnimState*)((a)->ext))
#define PK_TIMER(a) A16(a, 0xf4)
#define PK_FLAGS(a) AU16(a, 0xf6)
#define PK_ANGLE(a) A16(a, 0xf8)

/* 80031854 */
static int Pickup_Create(Actor* a, const ActorDef* d)
{
    Actor_DefaultHandler(2, a, (intptr_t)d, 0);
    const uint8_t* t = d->def;
    PK_FLAGS(a) = *(const uint16_t*)(t + 0x2a);
    a->obj.pos[1] = Track_GroundHeight(&a->tpos, a->obj.pos, 0) + *(const int32_t*)(t + 0x24);
    Actor_UpdateCenter(a);
    PK_TIMER(a) = -1;
    PK_ANGLE(a) = 0;
    if (!s_projModel[0])
        s_projModel[0] = Actor_ResModel(a, 2);
    if (!s_projModel[1])
        s_projModel[1] = Actor_ResModel(a, 3);
    if (!s_twisterModel)
        s_twisterModel = Actor_ResModel(a, 4);
    if (a->obj.model && a->obj.model->anim)
        Anim_Init(a->obj.model->anim, PK_ANIM(a), 1, 0);
    else
        PK_ANIM(a)->seqs = NULL;
    a->objClass = 2;
    return 0;
}

/* 80031f04: gira sin parar (6 unidades por tic hacia media vuelta mas alla) y anima el modelo */
static void Pickup_Spin(Actor* a, int dt)
{
    int ang = PK_ANGLE(a);
    int d = (((ang + 0x800) & 0xfff) - ang) & 0xfff;
    if (d != 0)
    {
        if (d > 0x800)
            d -= 0x1000;
        int step = Abs_(d) < dt * 6 ? Abs_(d) : dt * 6;
        if (d < 0)
            step = -step;
        ang = (PK_ANGLE(a) + step) & 0xfff;
        PK_ANGLE(a) = (int16_t)ang;
        Math_RotMatrix(ang, 0, 0, a->obj.rot);
        Actor_UpdateCenter(a);
        a->vel[0] = a->obj.rot[6];
        a->vel[1] = a->obj.rot[7];
        a->vel[2] = a->obj.rot[8];
    }
    if (a->obj.model && a->obj.model->anim)
    {
        Anim_Step(a, PK_ANIM(a), dt, 0);
        Obj_SetFrame(&a->obj, PK_ANIM(a)->frame);
    }
}

/* 800319c4: el coche se lleva un poder al azar segun su puesto (si no lleva ya uno). La caja reaparece a los
 * 0x708 tics (0x1C2 con Mayhem). */
static int Pickup_Collect(Actor* a, RaceCar* e)
{
    if (a->objClass)
        Fx_ObjHit(a->objClass, a);   /* 8003558c: chispas y sonido de la caja (clase 0x1A) */
    if (!e || (e->type != 1 && e->type != 0x11) || !e->phys)
        return 0;
    uint8_t* c = e->phys;
    int mayhem = g_GameInfo->powerUps == 3;
    if (I8(c, 0x338) < 0)
    {
        Fx_QueueImpact(e, 2, NULL, 0, 0x401);   /* 8007734c: destello del poder (clase 0x19) */
        if (e->type == 1 && !g_RacePaused)
            Speech_Say(0x10, 4, e, 1);
        int n = g_NumCars, pos = U8(c, 0x31a);
        int ahead = Car_TrackDeltaPublic(c, g_CarPhys[(pos == 0 ? n : pos) - 1]);
        int behind = Car_TrackDeltaPublic(c, g_CarPhys[pos == n - 1 ? 0 : pos + 1]);
        uint32_t r = (uint32_t)Rand_A();
        int s = 0, took3 = 0;
        if (n == 2)
        {
            if (pos == 0)
                ahead = behind;
            if (Abs_(ahead) < 0x28)
            {
                static const int8_t close[8] = { 10, 9, 3, 1, 7, 5, -1, -2 };
                s = close[r & 7];
                if (s == -1)
                    s = pos == 0 ? 3 : 10;
                else if (s == -2)
                    s = pos != 0 ? 9 : 0;
            }
            else if (pos == 0)
                s = (r & 0xf) == 0xf ? 3 : (int)((r & 1) << 2);
            else
            {
                static const int8_t far[4] = { 3, 1, 7, 5 };
                s = far[r & 3];
            }
        }
        else
        {
            /* umbral: mas alto cuanto mas adelante (n - puesto) / n; Mayhem lo baja */
            int thr = Div_((n - pos) * 0x100, n);
            int random4 = 0;
            if (mayhem)
                thr -= 0x40;
            if (thr <= (int)(r & 0xff))
            {
                if ((((int)((r >> 8) & 0xff) < thr) && pos != 0) || (mayhem && pos != 0 && (r & 0x60) == 0x60))
                    random4 = 1;
                else if (r & 1)
                    s = 3;
                else if (!(r & 2) && pos != n - 1)
                {
                    if (!(r & 4) && pos != 0)
                    {
                        if (!(r & 8) || n - 1 <= pos)
                            random4 = 1;
                        else
                            s = 2;
                    }
                    else
                        s = 6;
                }
                else
                    s = 7;
            }
            else if (pos != 1)
            {
                if (pos == 0 && (r & 4))
                    s = 8;
                else if (pos < 3 && (r & 3) == 3)
                    s = 8;
                else if (pos > 1 && pos == n - 1)
                    s = 3;
                else if (r & 1)
                    s = 0;
                else if (r & 2)
                    s = 4;
                else if (r & 4)
                    s = 6;
                else
                    s = (r & 8) ? 10 : 2;
            }
            else if (Abs_(ahead) < Abs_(behind) && (r & 1))
                took3 = 1;
            else
                random4 = 1;
            if (random4)
            {
                static const int8_t pick[4] = { 5, 1, 9, 10 };
                s = pick[(r >> 4) & 3];
            }
        }
        if (took3)
            I8(c, 0x338) = 3;
        else
        {
            I8(c, 0x338) = (int8_t)s;
            if (s == POWER_MYSTERY)
                I16(c, 0x33e) = (int16_t)Exe_U16(0x800aa92c);
            if (e->type == 1 && s != POWER_MYSTERY && I32(e->ctrl, 0x68) == -1)
                I32(e->ctrl, 0x68) = 0;
            if (e->type == 0x11)
            {
                int16_t w;
                if (s == POWER_MYSTERY)
                    w = (!(r & 0x10) && pos != 0) ? 0x5dc : 9000;
                else if (s == 10)
                    w = (int16_t)((r & 0x3ff) + 0xe10);
                else
                    w = (int16_t)((r & 0xfff) + 0x1c2);
                I16(c, 0x33c) = w;
            }
        }
    }
    if (Power_LogOn())
        NR_Log("[poder] t=%d coche %d (puesto %d) recoge la caja %d: lleva %d\n", g_RaceTime, e->id - 1000,
               U8(c, 0x31a) + 1, a->id, I8(c, 0x338));
    PK_TIMER(a) = mayhem ? 0x1c2 : 0x708;
    return 0;
}

/* 8003203c */
int Pickup_Handler(int msg, Actor* a, intptr_t p1, intptr_t p2)
{
    switch (msg)
    {
    case 1:
        return 0xfc;
    case 2:
        return Pickup_Create(a, (const ActorDef*)p1);
    case 3:
        if (PK_TIMER(a) < 0)
            return Actor_DefaultHandler(msg, a, p1, p2);
        return 0;
    case 9:
    case 0xe:
        return 0;
    case 6:
    {
        int t = PK_TIMER(a) - (int)p1;
        if (PK_TIMER(a) >= 0)
        {
            PK_TIMER(a) = (int16_t)t;
            if ((int16_t)t >= 0)
                return 0;
        }
        Pickup_Spin(a, (int)p1);
        return Actor_DefaultHandler(msg, a, p1, p2);
    }
    case 10:
    {
        const RaceCar* e = (const RaceCar*)p1;
        return PK_TIMER(a) < 0 && e && (e->type == 1 || (e->type == 0x11 && !(PK_FLAGS(a) & 2)));
    }
    case 0xb:
        return PK_TIMER(a) < 0 ? Pickup_Collect(a, (RaceCar*)p1) : 0;
    default:
        return Actor_DefaultHandler(msg, a, p1, p2);
    }
}

/* ---------------------------------------------------------------- efectos sobre el coche */

/* 8004ffec: suma 'speed' en la direccion del coche (en el plano) a su velocidad local y la limita a la punta */
static void Car_AddForwardSpeed(uint8_t* c, int speed)
{
    RaceCar* e = (RaceCar*)ENT(c);
    int top = Car_TopSpeed(c);
    int v[3] = { e->obj.rot[6], 0, e->obj.rot[8] }, l[3];
    Vec_Normalize(v);
    Vec_Scale(speed << 8, v);
    Mat_Mul(e->obj.rot, v, l);
    IA(c, 0x55) += l[0];
    IA(c, 0x56) += l[1];
    IA(c, 0x57) += l[2];
    int len = Math_SquareRoot12(Vec_LenSq16(VEC(c, 0x154)));
    if (top << 8 < len << 2)
    {
        Vec_Normalize(VEC(c, 0x154));
        for (int k = 0; k < 3; k++)
            IA(c, 0x55 + k) = (IA(c, 0x55 + k) >> 8) * top;
    }
}

/* 80050130: turbo (tabla de marchas +0x39C, empujon y su sonido 0x13 como fuente en +0x490) */
static void Car_TurboOn(uint8_t* c)
{
    RaceCar* e = (RaceCar*)ENT(c);
    SETPTR(c, 0x3e8, B_(c) + 0x39c);
    Car_AddForwardSpeed(c, I32(c, 0x39c));
    if (IA(c, 0x124) == -1)
        IA(c, 0x124) = Snd_SourceCreate(0x13, e->handle, e->obj.pos[0], e->obj.pos[2], -1, e->speed, e->dir);
}

/* 800501b8 */
static void Car_TurboOff(uint8_t* c)
{
    SETPTR(c, 0x3e8, B_(c) + 0x350);
    if (IA(c, 0x124) != -1)
    {
        Snd_SourceKill(((RaceCar*)ENT(c))->handle, IA(c, 0x124));
        IA(c, 0x124) = -1;
    }
}

/* 8005b068: empujon de la onda expansiva (giro alrededor de arriba x dir y salto hacia fuera) */
static void Car_Blast(uint8_t* c, int* dir)
{
    RaceCar* e = (RaceCar*)ENT(c);
    static const int up[3] = { 0, 0x10000, 0 };
    int t[3], l[3];
    Vec_Normalize(dir);
    Vec_Cross(up, dir, t);
    Vec_Scale(0x8000, t);
    Mat_Mul(e->obj.rot, t, l);
    IA(c, 0x52) += l[0];
    IA(c, 0x53) += l[1];
    IA(c, 0x54) += l[2];
    Vec_Scale(0x4000, dir);
    dir[1] += 0x2000;
    Mat_Mul(e->obj.rot, dir, l);
    IA(c, 0x55) += l[0];
    IA(c, 0x56) += l[1];
    U16(c, 0x198) &= 0xffdf;
    IA(c, 0xfe) = (IA(c, 0xfe) & ~0x30) | 0x40;
    U8(c, 0x189) = 0xff;
    IA(c, 0x57) += l[2];
}

/* 800194a0: longitud aproximada (mayor + (los otros dos) / 4) */
static int Vec_ApproxMax(const int* v)
{
    int a = Abs_(v[0]), b = Abs_(v[1]), d = Abs_(v[2]);
    if (a < b)
    {
        int t = a;
        a = b;
        b = t;
    }
    if (a < d)
    {
        int t = a;
        a = d;
        d = t;
    }
    return a + ((b + d) >> 2);
}

/* 8005b2a0: onda expansiva: empuja a los rivales sin escudo a menos de 0x3000 y acerca la velocidad a la
 * punta */
static int Car_Shockwave(uint8_t* c)
{
    RaceCar* e = (RaceCar*)ENT(c);
    int hits = 0;
    Fx_QueueImpact(c, 1, NULL, 0, 0x40e);   /* 8007734c: onda (clase 0x26) */
    for (int i = 0; i < g_NumCars; i++)
    {
        uint8_t* o = g_CarPhys[i];
        if (!o || o == c || (U16(o, 0x33a) & 4))
            continue;
        RaceCar* oe = (RaceCar*)ENT(o);
        if (e->team == oe->team)
            continue;
        int d[3] = { oe->obj.pos[0] - e->obj.pos[0], oe->obj.pos[1] - e->obj.pos[1], oe->obj.pos[2] - e->obj.pos[2] };
        if (Vec_ApproxMax(d) < 0x3000)
        {
            Car_Blast(o, d);
            hits++;
        }
    }
    int top = Car_TopSpeed(c);
    Car_AddForwardSpeed(c, e->speed + ((top - e->speed) >> 1));
    return hits;
}

#define FX_TYPE(c, i)  I8(c, 0x340 + (i) * 4)
#define FX_TICKS(c, i) I16(c, 0x342 + (i) * 4)

/* 8002f268: activa (o alarga) un efecto. La IA dura 1/4 en los malos (0, 4, 7) y 1,5 veces en los buenos
 * (2, 3, 6, 9); el escudo (2) corta los malos. Al humano le comenta el efecto (8002b920(tipo + 0x1D)).
 * Devuelve los tics que le quedan o -1. */
int Car_AddEffect(uint8_t* c, int type, int ticks)
{
    if (!c || U8(c, 0x339) > 3)
        return -1;
    RaceCar* e = (RaceCar*)ENT(c);
    if (!g_RacePaused && e->type == 1 && type < 9)
        Speech_Say(type + 0x1d, 5, e, 1);
    if (ticks > 0 && e->type == 0x11)
    {
        if (type == 0 || type == 4 || type == 7)
            ticks >>= 2;
        else if (type == 2 || type == 3 || type == 6 || type == 9)
            ticks += ticks >> 1;
    }
    int n = U8(c, 0x339);
    for (int i = 0; i < n; i++)
    {
        if (FX_TYPE(c, i) == type)
        {
            if (ticks < 0)
                FX_TICKS(c, i) = -1;
            else if (FX_TICKS(c, i) >= 0)
                FX_TICKS(c, i) = (int16_t)(FX_TICKS(c, i) + ticks);
            return FX_TICKS(c, i);
        }
        if (type == 2)
            for (int j = 0; j < n; j++)
            {
                int t = FX_TYPE(c, j);
                if (t == 0 || t == 1 || t == 4 || t == 5 || t == 7)
                    FX_TICKS(c, j) = 1;
            }
    }
    if (Power_LogOn())
        NR_Log("[poder] t=%d coche %d recibe el efecto %d (%d tics)\n", g_RaceTime, e->id - 1000, type, ticks);
    U16(c, 0x33a) |= (uint16_t)(1u << type);
    if (U16(c, 0x33a) & 8)
        Car_TurboOn(c);
    FX_TYPE(c, n) = (int8_t)type;
    FX_TICKS(c, n) = (int16_t)ticks;
    U8(c, 0x339) = (uint8_t)(n + 1);
    if (type == 10)
        Car_Shockwave(c);
    return FX_TICKS(c, n);
}

/* ---------------------------------------------------------------- objetos que se sueltan (entidad 0xD) */

/* Campos (0xEC bytes): +0xE0 poder (0 o 4), +0xE2 tics en que no afecta al que lo solto, +0xE4 su handle,
 * +0xE8 altura / 16 sobre el suelo, +0xEA velocidad vertical */
#define DR_KIND(a)  A8(a, 0xe0)
#define DR_SAFE(a)  A16(a, 0xe2)
#define DR_OWNER(a) A32(a, 0xe4)
#define DR_H(a)     A16(a, 0xe8)
#define DR_VY(a)    A16(a, 0xea)

/* 80033374: cae desde 0x40 sobre el suelo donde estaba el coche */
static int Drop_Create(Actor* a, const ActorDef* d)
{
    Actor_DefaultHandler(2, a, (intptr_t)d, 0);
    const uint8_t* t = d->def;
    int pos[3];
    memcpy(pos, a->obj.pos, sizeof(pos));
    Obj_Init(&a->obj, *(ModelHdr* const*)(t + 0x24));   /* (el Obj_Init del port borra tambien la posicion) */
    memcpy(a->obj.pos, pos, sizeof(pos));
    a->flags2c |= 0x800100;
    DR_KIND(a) = t[0x2c];
    int g = Track_GroundHeight(&a->tpos, a->obj.pos, 0);
    DR_VY(a) = 0x20;
    DR_SAFE(a) = 0x5dc;
    DR_H(a) = 4;
    a->obj.pos[1] = g + 0x40;
    DR_OWNER(a) = *(const int32_t*)(t + 0x28);
    Actor_UpdateCenter(a);
    return 0;
}

/* 80033468 */
static int Drop_Handler(int msg, Actor* a, intptr_t p1, intptr_t p2)
{
    switch (msg)
    {
    case 1:
        return 0xec;
    case 2:
        return Drop_Create(a, (const ActorDef*)p1);
    case 6:
        if (DR_SAFE(a) > 0)
            DR_SAFE(a) = (int16_t)(DR_SAFE(a) - (int)p1);
        if (DR_H(a) >= 1)
        {
            int vy = DR_VY(a) - ((int)p1 >> 1), h = DR_H(a) + vy;
            DR_H(a) = (int16_t)(h < 0 ? 0 : h);
            DR_VY(a) = (int16_t)vy;
            a->obj.pos[1] = Track_GroundHeight(&a->tpos, a->obj.pos, 0) + DR_H(a) * 0x10;
            Actor_UpdateCenter(a);
        }
        return Actor_DefaultHandler(msg, a, p1, p2);
    case 9:
    case 0xe:
        return 0;
    case 10:
    {
        const RaceCar* e = (const RaceCar*)p1;
        return e && (DR_SAFE(a) < 1 || e->handle != DR_OWNER(a)) && (e->type == 1 || e->type == 0x11);
    }
    case 0xb:
    {
        RaceCar* e = (RaceCar*)p1;
        if (!e || (e->type != 1 && e->type != 0x11) || !e->phys)
            return 0;
        uint8_t* c = e->phys;
        if (!(U16(c, 0x33a) & 4))
            Car_AddEffect(c, DR_KIND(a), Power_Duration(DR_KIND(a)));
        a->hp = -1;
        Fx_Push(DR_KIND(a) == 0 ? 0x1f : 0x1e, e->obj.pos, -1);
        return Actor_DefaultHandler(msg, a, p1, p2);
    }
    default:
        return Actor_DefaultHandler(msg, a, p1, p2);
    }
}

/* Parametros de creacion como los tACT falsos del original (en la pila): id del coche | 0x8000, tipo,
 * posicion, vida 1, equipo, modelo +0x24 y datos propios desde +0x28 */
static void Power_FakeDef(uint8_t* t, const RaceCar* e, int type)
{
    memset(t, 0, 0x34);
    *(uint32_t*)t = (uint32_t)e->id | 0x8000;
    t[4] = (uint8_t)type;
    memcpy(t + 8, e->obj.pos, 12);
    *(int16_t*)(t + 0x18) = 1;
    *(int16_t*)(t + 0x1a) = 1;
    t[0x1c] = e->team;
    t[0x1d] = e->unk23;
}

/* 8002e94c: suelta el objeto del poder 0 o 4 donde esta el coche */
static void Power_Drop(uint8_t* c, int kind)
{
    RaceCar* e = (RaceCar*)ENT(c);
    uint8_t t[0x34];
    Power_FakeDef(t, e, 0xd);
    *(ModelHdr**)(t + 0x24) = s_projModel[kind == 0 ? 0 : 1];
    *(int32_t*)(t + 0x28) = e->handle;
    t[0x2c] = (uint8_t)kind;
    ActorDef d = { Drop_Handler, NULL, t, NULL };
    Actor* a = Actor_Create(&d);
    if (a)
        a->def = NULL;
    if (a && Power_LogOn())
        NR_Log("[poder] t=%d objeto %d en (%d, %d, %d) curva %d, modelo %p, radio %d\n", g_RaceTime, kind, a->obj.pos[0],
               a->obj.pos[1], a->obj.pos[2], a->tpos.curve, (void*)a->obj.model, a->obj.radius);
}

/* ---------------------------------------------------------------- clima (entidad 8) */

/* Campos (0x10C bytes): +0xE0 coche que lo lanzo, +0xE4 coche al que persigue o en el que esta, +0xE8 efecto de
 * particulas (la nube de la tormenta, 80070e58), +0xEC red Cnet, +0xF0 tipo (0 tormenta,
 * 1 tornado, 2 helada), +0xF1 1 pegado al coche, 2 persiguiendolo, +0xF2 tics (espera al salir, vida del
 * tornado o lo que le queda al efecto), +0xF6 nodo hacia el que va, +0xF8 recorrido del tramo (<< 8), +0xFC su
 * longitud, +0x100 inicio del tramo (0x600 por encima de la red). */
#define WX_OWNER(a)  (*(RaceCar**)((uint8_t*)(a) + 0xe0))
#define WX_TARGET(a) (*(RaceCar**)((uint8_t*)(a) + 0xe4))
#define WX_STORM(a)  (*(Storm**)((uint8_t*)(a) + 0xe8))
#define WX_NET(a)    (*(const uint8_t**)((uint8_t*)(a) + 0xec))
#define WX_KIND(a)   A8(a, 0xf0)
#define WX_STATE(a)  A8(a, 0xf1)
#define WX_TICKS(a)  A16(a, 0xf2)
#define WX_NODE(a)   AU16(a, 0xf6)
#define WX_PROG(a)   A32(a, 0xf8)
#define WX_LEN(a)    A32(a, 0xfc)
#define WX_START(a)  ((int*)((uint8_t*)(a) + 0x100))

static_assert(sizeof(RaceCar*) == 4, "los punteros se guardan en huecos de 32 bits (port Win32)");

/* 8003219c */
static int Weather_Create(Actor* a, const ActorDef* d)
{
    Actor_DefaultHandler(2, a, (intptr_t)d, 0);
    const uint8_t* t = d->def;
    Obj_Init(&a->obj, *(ModelHdr* const*)(t + 0x24));
    a->flags2c |= 4;
    WX_KIND(a) = t[0x28];
    WX_STATE(a) = 0;
    WX_TICKS(a) = WX_KIND(a) == 1 ? 0x2904 : 0x96;
    WX_TARGET(a) = NULL;
    WX_OWNER(a) = *(RaceCar* const*)(t + 0x30);
    if (WX_KIND(a) == 1)
        Actor_UpdateCenter(a);
    const RaceCar* o = WX_OWNER(a);
    WX_NET(a) = g_NumNets > 0 ? g_Nets[0] : NULL;   /* DAT_800af7d4: la red de la salida */
    int* st = WX_START(a);
    st[0] = a->obj.pos[0] = o->obj.pos[0];
    st[1] = a->obj.pos[1] = o->obj.pos[1] + 0x600;
    st[2] = a->obj.pos[2] = o->obj.pos[2];
    memcpy(a->obj.unk34, a->obj.pos, sizeof(a->obj.unk34));
    WX_NODE(a) = 0;
    WX_LEN(a) = 1;
    if (WX_NET(a))
    {
        int n = Cnet_Next(WX_NET(a), Cnet_FindNearest(WX_NET(a), a->obj.pos, 1)), p[3];
        WX_NODE(a) = (uint16_t)(n < 0 ? 0 : n);
        Cnet_NodePos(WX_NET(a), WX_NODE(a), p);
        int v[3] = { p[0] - st[0], p[1] - (st[1] - 0x600), p[2] - st[2] };
        WX_LEN(a) = Vec_ApproxLen3(v);
    }
    WX_PROG(a) = 0;
    if (WX_KIND(a) == 1)
    {
        a->flags2c |= 0x10000;   /* siempre visible */
        Fx_QueueImpact(a, 2, NULL, 0, 0x404);   /* tornado: anillos de humo 0x1C */
        g_TornadoCount++;
    }
    else if (WX_KIND(a) == 2)
        Fx_QueueImpact(a, 2, NULL, 0, 0x417);   /* helada: 0x2F */
    WX_STORM(a) = WX_KIND(a) == 0 ? Storm_Create() : NULL;   /* 80070e58: la nube de la tormenta */
    return 0;
}

/* 8005b1ac: el coche mas cercano a p a menos de lim (longitud aproximada 800194a0) */
static RaceCar* Weather_NearestCar(const int* p, int lim)
{
    RaceCar* best = NULL;
    for (int i = 0; i < g_NumCars; i++)
    {
        if (!g_CarPhys[i])
            continue;
        RaceCar* e = (RaceCar*)ENT(g_CarPhys[i]);
        int v[3] = { e->obj.pos[0] - p[0], e->obj.pos[1] - p[1], e->obj.pos[2] - p[2] };
        int d = Vec_ApproxMax(v);
        if (d < lim)
        {
            best = e;
            lim = d;
        }
    }
    return best;
}

/* Rayo de la tormenta hacia un lado: hasta 2 pasos de 'side' buscando un muro (Track_RayWall); en el muro, el punto
 * se lleva al plano de la pared */
static void Weather_WallPoint(const int* from, const int* side, int sign, int* out)
{
    int p[3] = { from[0], from[1], from[2] };
    for (int i = 0; i < 2; i++)
    {
        int q[3] = { p[0] + sign * side[0], p[1] + sign * side[1], p[2] + sign * side[2] }, n[3], hit[6];   /* Track_RayWall: los dos extremos del muro */
        memcpy(out, q, sizeof(q));
        if (Track_RayWall(p, q, n, hit))
        {
            int d[3] = { hit[0] - q[0], hit[1] - q[1], hit[2] - q[2] };
            Vec_Scale(Vec_Dot16(d, n), n);
            for (int k = 0; k < 3; k++)
                out[k] = q[k] + n[k];
            return;
        }
        memcpy(p, q, sizeof(q));
    }
}

/* Weather_Update (tormenta suelta): cada 25 a 49 tics, dos rayos desde 0x380 por encima: al coche mas cercano a
 * menos de 0x2000 o, sin ninguno, a los muros de los dos lados (perpendicular a la marcha sobre el suelo) */
static void Weather_Strike(Actor* a, int dt)
{
    uint8_t prev = s_strikeTimer;
    s_strikeTimer = (uint8_t)(prev - dt);
    if ((int8_t)s_strikeTimer >= 0)
        return;
    unsigned r = (unsigned)Rand_B();
    int p0[3] = { a->obj.pos[0], a->obj.pos[1] + 0x380, a->obj.pos[2] };
    s_strikeTimer = (uint8_t)(r % 0x19 + 0x19);
    int e1[3], e2[3];
    RaceCar* c = Weather_NearestCar(p0, 0x2000);
    if (c)
    {
        memcpy(e1, c->obj.pos, sizeof(e1));
        memcpy(e2, c->obj.pos, sizeof(e2));
    }
    else
    {
        int up[3] = { a->obj.shadowN[0] << 4, a->obj.shadowN[1] << 4, a->obj.shadowN[2] << 4 }, side[3];
        Vec_Cross(up, a->vel, side);
        for (int k = 0; k < 3; k++)
            side[k] >>= 3;
        Weather_WallPoint(p0, side, 1, e1);
        Weather_WallPoint(p0, side, -1, e2);
    }
    Bolt_Strike(p0, e1);
    Bolt_Strike(p0, e2);
}

/* 800323f0 */
static void Weather_Update(Actor* a, int dt)
{
    int r = Pause_ResultsTime();
    if (r > 0x5dc)
        a->hp = -1;
    /* aviso al humano que va hasta 0x32 puntos de TCRV por delante (Race_Frame1P lo borra cada frame) */
    int len = g_Track ? g_Track->numCurve : 0;
    if (!(WX_STATE(a) & 3) && len > 0)
        for (int h = 0; h < g_NumHumans; h++)
        {
            RaceCar* e = g_Humans[h];
            if (!e || e == WX_OWNER(a))
                continue;
            int d = e->tpos.curve - a->tpos.curve;
            if (d < -(len >> 1))
                d += len;
            else if ((len >> 1) < d)
                d -= len;
            if ((unsigned)(d + 2) < 0x34 && (I32(e->ctrl, 0x60) == 0 || I32(e->ctrl, 0x64) < d))
            {
                I32(e->ctrl, 0x60) = (int32_t)(uintptr_t)a;
                I32(e->ctrl, 0x64) = d;
            }
        }
    if (WX_TICKS(a) >= 0)
        WX_TICKS(a) = (int16_t)(WX_TICKS(a) - dt);
    int kind = WX_KIND(a);
    if (!(WX_STATE(a) & 1) && (kind == 1 || WX_TICKS(a) < 0))
        for (int i = 0; i < g_NumCars; i++)
        {
            uint8_t* c = g_CarPhys[i];
            if (!c)
                continue;
            RaceCar* e = (RaceCar*)ENT(c);
            if (!(WX_STATE(a) & 2))
            {
                if (e == WX_OWNER(a) || (e->team == a->team && kind != 1) || (U16(c, 0x33a) & 4) || e->speed <= 0x45)
                    continue;
            }
            else if (WX_TARGET(a) != e)
                continue;
            int v[2] = { a->obj.pos[0] - e->obj.pos[0], a->obj.pos[2] - e->obj.pos[2] };
            int d = Vec_ApproxLen2(v);
            if (d >= 0x4800)
                continue;
            if (kind == 1)
            {
                if (!(U16(c, 0x33a) & 0x80))
                    Car_AddEffect(c, 7, Power_Duration(7));
                continue;
            }
            if (d < 0xa00)
            {
                /* le alcanza: tormenta -> efecto 5, helada -> 1 (y desaparece); se pega al coche */
                if (kind == 2)
                    Fx_Push(0x1a, e->obj.pos, -1);
                WX_STATE(a) = (uint8_t)((WX_STATE(a) & 0xfd) | 1);
                int eff = kind == 0 ? 5 : 1;
                WX_TICKS(a) = (int16_t)Car_AddEffect(c, eff, Power_Duration(eff));
                WX_TARGET(a) = e;
                if (kind == 2)
                    a->hp = -1;
                memcpy(a->obj.pos, e->obj.pos, sizeof(a->obj.pos));
                Track_FindSurface(&a->tpos, a->obj.pos, NULL, 1);
                a->obj.shadowY = e->obj.shadowY;
                Fx_Kill(a, 0x24);
                if (kind == 0)
                    Fx_QueueImpact(a, 2, NULL, 0, 0x40d);   /* rayo (0x25) */
                else
                {
                    Fx_Kill(a, 0x2f);
                    Fx_Kill(a, 0x30);
                }
                break;
            }
            if (!(WX_STATE(a) & 2))
            {
                WX_STATE(a) |= 2;
                WX_TARGET(a) = e;
            }
        }
    if (!(WX_STATE(a) & 2))
    {
        if (!(WX_STATE(a) & 1))
        {
            /* avanza por la red a 0x100 por tic, interpolando en el tramo actual */
            const uint8_t* net = WX_NET(a);
            int* st = WX_START(a);
            int p[3] = { st[0], st[1] - 0x600, st[2] };
            if (net)
            {
                Cnet_NodePos(net, WX_NODE(a), p);
                WX_PROG(a) += dt * 0x100;
                while (WX_LEN(a) <= WX_PROG(a))
                {
                    WX_PROG(a) -= WX_LEN(a);
                    int nx = Cnet_Next(net, WX_NODE(a));
                    WX_NODE(a) = (uint16_t)(nx < 0 ? WX_NODE(a) : nx);
                    st[0] = p[0];
                    st[1] = p[1] + 0x600;
                    st[2] = p[2];
                    Cnet_NodePos(net, WX_NODE(a), p);
                    int v[3] = { p[0] - st[0], p[1] - (st[1] - 0x600), p[2] - st[2] };
                    WX_LEN(a) = Vec_ApproxLen3(v);
                    if (WX_LEN(a) <= 0)
                    {
                        WX_LEN(a) = 1;
                        break;
                    }
                }
            }
            int half = WX_LEN(a) >> 1;
            uint32_t f = half ? (uint32_t)(((uint64_t)(uint32_t)WX_PROG(a) * (0x80000000u / (uint32_t)half)) >> 16) : 0;
            int dv[3] = { p[0] - st[0], p[1] - (st[1] - 0x600), p[2] - st[2] };
            for (int k = 0; k < 3; k++)
            {
                a->obj.pos[k] = st[k] + (int)(((int64_t)dv[k] * (int32_t)f) >> 16);
                a->vel[k] = dv[k];
            }
            Vec_Normalize(a->vel);
        }
        else if (WX_TARGET(a))
            memcpy(a->obj.pos, WX_TARGET(a)->obj.pos, sizeof(a->obj.pos));
    }
    else if (WX_TARGET(a))
    {
        /* lo persigue a 0x100 por tic */
        const RaceCar* t = WX_TARGET(a);
        int v[3] = { t->obj.pos[0] - a->obj.pos[0], t->obj.pos[1] - (a->obj.pos[1] - 0x600), t->obj.pos[2] - a->obj.pos[2] };
        int l = Vec_ApproxLen3(v);
        Vec_Scale(l < 2 ? 0 : (int)(0x80000000u / (uint32_t)(l >> 1)), v);
        for (int k = 0; k < 3; k++)
        {
            a->obj.pos[k] += (v[k] * dt) >> 8;
            a->vel[k] = v[k];
        }
        Vec_Normalize(a->vel);
    }
    if (kind == 0)
    {
        Storm* s = WX_STORM(a);
        if (s)
        {
            RaceCar* t = WX_TARGET(a);
            uint8_t* ph = t && (t->type == 1 || t->type == 0x11) ? t->phys : NULL;
            if (!(WX_STATE(a) & 1) || !t)
            {
                Storm_SetTarget(s, NULL, a);
                Weather_Strike(a, dt);
            }
            else
            {
                /* pegada al coche: marcas en sus 4 ruedas (registro de la rueda +6) */
                if (ph)
                    for (int k = 0; k < 4; k++)
                        U8(ph, 0x4ae + k * 100) = 0xff;
                Storm_SetTarget(s, t, a);
                a->obj.shadowY = t->obj.shadowY;
            }
            if (WX_STATE(a) & 1)
            {
                if (ph && !(U16(ph, 0x33a) & 0x20))
                    WX_TICKS(a) = 0;
                /* se desvanece en los ultimos 300 tics (80071dd4) y se libera al acabar (80071de4) */
                if (WX_TICKS(a) < 0x12d || WX_TICKS(a) - dt > 300)
                {
                    if (WX_TICKS(a) < 1)
                    {
                        Storm_Free(s);
                        WX_STORM(a) = NULL;
                    }
                }
                else
                    Storm_Fade(s);
            }
        }
        if (!WX_STORM(a))
            a->hp = -1;
    }
    if (kind == 1)
    {
        Actor_UpdateCenter(a);
        if (WX_TICKS(a) < 1)
            a->hp = -1;
    }
    else
        memcpy(a->obj.unk34, a->obj.pos, sizeof(a->obj.unk34));
    Track_FindSurface(&a->tpos, a->obj.pos, NULL, 1);
}

/* 80033274 */
static int Weather_Handler(int msg, Actor* a, intptr_t p1, intptr_t p2)
{
    switch (msg)
    {
    case 1:
        return 0x10c;
    case 2:
        return Weather_Create(a, (const ActorDef*)p1);
    case 5:
        Storm_Free(WX_STORM(a));   /* 80071de4 */
        WX_STORM(a) = NULL;
        if (WX_KIND(a) == 1)
            g_TornadoCount--;
        break;
    case 6:
        Weather_Update(a, (int)p1);
        break;
    case 9:
    case 0xe:
        return 0;
    default:
        break;
    }
    return Actor_DefaultHandler(msg, a, p1, p2);
}

/* 8002e808: 5 tormenta (tipo 0), 7 tornado (1, con su modelo) y 1 helada (2) salen del coche */
static void Power_Weather(uint8_t* c, int power)
{
    RaceCar* e = (RaceCar*)ENT(c);
    int kind = power == 5 ? 0 : power == 7 ? 1 : power == 1 ? 2 : -1;
    if (kind < 0)
        return;
    uint8_t t[0x34];
    Power_FakeDef(t, e, 8);
    t[0x21] = power == 5 ? 0x17 : power == 1 ? 0xc : 10;
    *(ModelHdr**)(t + 0x24) = kind == 1 ? s_twisterModel : NULL;
    t[0x28] = (uint8_t)kind;
    *(RaceCar**)(t + 0x30) = e;
    ActorDef d = { Weather_Handler, NULL, t, NULL };
    Actor* a = Actor_Create(&d);
    if (a)
        a->def = NULL;
}

/* Race_Frame1P 80026f28: el aviso de clima de cada humano se borra al empezar el frame */
void Power_ClearAlerts(void)
{
    for (int h = 0; h < g_NumHumans; h++)
        if (g_Humans[h] && g_Humans[h]->ctrl)
            I32(g_Humans[h]->ctrl, 0x60) = 0;
}

/* Para el marcador (800429d0): tipo de clima del aviso del humano (0 tormenta, 1 tornado, 2 helada) o -1 si no
 * hay o lo lanzo el mismo */
int Power_AlertKind(const RaceCar* human)
{
    if (!human || !human->ctrl)
        return -1;
    const Actor* a = (const Actor*)(uintptr_t)I32(human->ctrl, 0x60);
    if (!a || WX_OWNER(a) == human)
        return -1;
    return WX_KIND(a);
}

/* 8002ec00: caducidad del misterioso, uso del poder (IA: cuando se acaba su espera; humano: R2 mantenido) y
 * cuenta atras de los efectos activos */
void Car_UpdatePowerUps(uint8_t* c, int dt, int use)
{
    RaceCar* e = (RaceCar*)ENT(c);
    if (I8(c, 0x338) == POWER_MYSTERY)
    {
        int t = I16(c, 0x33e) - dt;
        I16(c, 0x33e) = (int16_t)t;
        if ((int16_t)t < 0)
            I8(c, 0x338) = -1;
    }
    if (use && I8(c, 0x338) >= 0)
    {
        int go = 1;
        if (e->type == 0x11)
        {
            int t = I16(c, 0x33c) - dt;
            I16(c, 0x33c) = (int16_t)t;
            go = (int16_t)t < 0;
        }
        else
        {
            int held = I8(c, 0x338);
            if (!g_RacePaused && held < POWER_MYSTERY)
                Speech_Say(held + 0x12, 5, e, 1);
            I32(e->ctrl, 0x68) = -2;
        }
        if (go)
        {
            int s = I8(c, 0x338), fromMystery = -1, fx = -1;
            I8(c, 0x338) = -1;
            if (s == POWER_MYSTERY)
            {
                int pos = U8(c, 0x31a);
                int r = Rand_A() & 7;
                switch (r)
                {
                case 0: if (pos != 0) r = 10; break;
                case 1: r = pos == 0 ? 0 : 9; break;
                case 2: if (pos == 0) r = 6; break;
                case 4: if (pos != 0) r = 9; break;
                case 5: r = pos == 0 ? 4 : 3; break;
                case 7: r = pos == 0 ? 4 : 10; break;
                default: break;
                }
                if (U16(c, 0x33a) & 4)
                {
                    if (r == 4)
                        r = 6;
                    else if (r == 0)
                        r = 3;
                }
                s = fromMystery = r;
            }
            if (Power_LogOn())
                NR_Log("[poder] t=%d coche %d usa %d%s\n", g_RaceTime, e->id - 1000, s, fromMystery >= 0 ? " (misterioso)" : "");
            int effect = 1;
            switch (s)
            {
            case 0:
            case 4:
                fx = s == 0 ? 0x21 : 0x20;
                if (fromMystery < 0)
                {
                    Power_Drop(c, s);   /* 8002e94c */
                    effect = 0;
                }
                break;
            case 1:
            case 5:
            case 7:
                Power_Weather(c, s);    /* 8002e808 */
                effect = -1;
                break;
            case 2:  fx = 0x14; break;
            case 6:  fx = 0x19; break;
            case 9:  fx = 0x1c; break;
            case 10: fx = 0x15; break;
            default: break;
            }
            if (effect >= 0 && (effect == 0 || U8(c, 0x339) < 4))
            {
                if (effect)
                    Car_AddEffect(c, s, Power_Duration(s));
                if (fx >= 0)
                    Fx_Push(fx, e->obj.pos, -1);   /* cola DAT_800b52f0 {0, efecto, x, z, -1} */
            }
        }
    }
    for (int i = 0; i < U8(c, 0x339); i++)
    {
        if (FX_TICKS(c, i) < 0)
            continue;
        int t = FX_TICKS(c, i) - dt;
        FX_TICKS(c, i) = (int16_t)t;
        if ((int16_t)t > 0)
            continue;
        int type = FX_TYPE(c, i), n = U8(c, 0x339);
        if (i < 3)
            memmove(B_(c) + 0x340 + i * 4, B_(c) + 0x344 + i * 4, (size_t)(n - i - 1) * 4);
        n--;
        U8(c, 0x339) = (uint8_t)n;
        FX_TYPE(c, n) = -1;
        uint32_t rest = 0, bit = 1u << (type & 0x1f);
        for (int j = 0; j < n; j++)
            if (FX_TYPE(c, j) >= 0)
                rest |= 1u << FX_TYPE(c, j);
        if (!(bit & rest))
        {
            if (e->type == 0x11 && (type == 7 || type == 1))
            {
                U8(e->ctrl, 0xc) = 0;
                Ai_Recover(e);
            }
            U16(c, 0x33a) &= (uint16_t)~bit;
            if (bit & 8)
                Car_TurboOff(c);
        }
        i--;
    }
}
