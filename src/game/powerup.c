/* Poderes: cajas de la pista y efectos sobre los coches. Ver powerup.h.
 * Pendiente: sonidos y comentarios (8003558c, 8007734c, 8002b920), la llama del turbo (8003da74), los
 * proyectiles (80033468) y las entidades de clima (80033274). */
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
#include "powerup.h"

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

void Pickup_Reset(void)
{
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
    /* 8003558c: sonido de la clase de objeto (pendiente) */
    if (!e || (e->type != 1 && e->type != 0x11) || !e->phys)
        return 0;
    uint8_t* c = e->phys;
    int mayhem = g_GameInfo->powerUps == 3;
    if (I8(c, 0x338) < 0)
    {
        /* 8007734c (sonido) y comentario 8002b920(0x10) del humano: pendientes */
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

/* 80050130: turbo (tabla de marchas +0x39C y empujon; la llama del escape 8003da74(0x13) queda pendiente) */
static void Car_TurboOn(uint8_t* c)
{
    SETPTR(c, 0x3e8, B_(c) + 0x39c);
    Car_AddForwardSpeed(c, I32(c, 0x39c));
}

/* 800501b8 */
static void Car_TurboOff(uint8_t* c)
{
    SETPTR(c, 0x3e8, B_(c) + 0x350);
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
 * punta (el sonido 8007734c(0x40E) queda pendiente) */
static int Car_Shockwave(uint8_t* c)
{
    RaceCar* e = (RaceCar*)ENT(c);
    int hits = 0;
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
 * (2, 3, 6, 9); el escudo (2) corta los malos. El comentario del humano (8002b920(tipo + 0x1D)) queda
 * pendiente. Devuelve los tics que le quedan o -1. */
int Car_AddEffect(uint8_t* c, int type, int ticks)
{
    if (!c || U8(c, 0x339) > 3)
        return -1;
    RaceCar* e = (RaceCar*)ENT(c);
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

/* 8002ec00: caducidad del misterioso, uso del poder (IA: cuando se acaba su espera; humano: R2 mantenido) y
 * cuenta atras de los efectos activos. Los proyectiles (80033468) y el clima (80033274) quedan pendientes: de
 * momento se gastan sin efecto. */
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
            I32(e->ctrl, 0x68) = -2;   /* (comentario 8002b920(poder + 0x12): pendiente) */
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
                    effect = 0;   /* 8002e94c: proyectil (pendiente) */
                break;
            case 1:
            case 5:
            case 7:
                effect = -1;      /* 8002e808: clima (pendiente) */
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
