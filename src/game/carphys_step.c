#include <string.h>
#include "nr.h"
#include "render.h"
#include "model.h"
#include "gte.h"
#include "track.h"
#include "vecmath.h"
#include "carphys.h"
#include "carphys_int.h"
#include "snd.h"
#include "fx.h"

void Car_RotateMatrix(int* m, const int* w);
void Car_BoxCorners(uint8_t* c);
void Car_WallContactsAll(void);
void Car_GroundContactsAll(void);
void Car_GroundAll(int* ground);
void Car_WheelPointsAll(void);
void Car_WheelGroundAll(const int* ground);
void Car_WheelSlopeAll(void);
void Car_Integrate(uint8_t* c, int dt, int limit, int fromStep);
void Car_ClampGround(uint8_t* c, int ground, int dt, int fromStep);
void Car_Airborne(uint8_t* c, int height, int steer, int dt);
int  Car_SteerTorque(uint8_t* c, int dt);
void Car_GroundForces(uint8_t* c, const int16_t* comp, int dt);
void Car_Collide(uint8_t* c, uint8_t* rec, int dt, int ground, int self);
void Car_HandleContacts(uint8_t* c, int dt, uint8_t* self, int fromStep);

void Physics_Init(void)
{
    Rand_Init();
    Human_ResetStatics();
}

/* Promedio de un registro de contacto (+0: n s16, +4 punto, +0x10 normal) */
void Contact_Average(uint8_t* rec)
{
    int n = I16(rec, 0);
    if (n == 0 || n == 1)
        return;
    int* v = VEC(rec, 4);
    for (int k = 0; k < 6; k++)
    {
        if (n == 2)
            v[k] >>= 1;
        else if (n == 4)
            v[k] >>= 2;
        else if (n == 8)
            v[k] >>= 3;
        else
            v[k] /= n;
    }
}

/* Pre-paso por coche: temporizadores, direccion y caja de cambios automatica */
static void Car_PreStep(uint8_t* c, int dt)
{
    RaceCar* e = (RaceCar*)ENT(c);
    uint8_t* gt = PTR(c, 0x3e8);
    e->obj.pos[1] += I16(c, 0x4a2);
    I16(c, 0x4a2) = 0x7000;
    if (U16(c, 0x33a) & 0x20)
    {
        uint16_t t = U16(c, 0x4a4);
        if (t != 0)
        {
            if ((int)t < dt)
            {
                U16(c, 0x4a4) = 0;
                uint8_t b = (uint8_t)((Rand_A() & 0x3f) + 0x40);
                U8(c, 0x4a0) = b;
                if (!(b & 1))
                    IA(c, 0xfe) &= 0xffdfffff;
                else
                    IA(c, 0xfe) |= 0x200000;
                if (e->type == 1)
                    Car_Shake(e, b << 6);
            }
            else
                U16(c, 0x4a4) = (uint16_t)(t - dt);
        }
        uint8_t b = U8(c, 0x4a0);
        if (b != 0)
        {
            if ((int)b < dt)
            {
                uint32_t r = (uint32_t)Rand_A();
                U8(c, 0x4a0) = 0;
                U16(c, 0x4a4) = (uint16_t)(r % 300 + 300);
            }
            else
                U8(c, 0x4a0) = (uint8_t)(b - dt);
        }
    }
    int t186 = I16(c, 0x186) - dt * 2;
    if (t186 < 2)
    {
        t186 = 0;
        IA(c, 0xfe) &= 0xffefffff;
    }
    I16(c, 0x186) = (int16_t)t186;
    U8(c, 0x49b) = 0;
    U8(c, 0x49a) = 0;
    U8(c, 0x19d) = 0xff;
    static const int timers[3] = { 0x315, 0x188, 0x49c };
    for (int k = 0; k < 3; k++)
    {
        uint8_t v = U8(c, timers[k]);
        if (v)
        {
            if ((int)v < dt)
            {
                U8(c, timers[k]) = 0;
                if (timers[k] == 0x49c)
                    I16(c, 0x49e) = 0;
            }
            else
                U8(c, timers[k]) = (uint8_t)(v - dt);
        }
    }
    uint16_t changed = U16(c, 0x43c) ^ U16(c, 0x33a);
    if (changed & 0x20)
    {
        if (!(U16(c, 0x33a) & 0x20))
            U8(c, 0x4a0) = 0;
        else
            U16(c, 0x4a4) = 1;
    }
    if ((changed & 0x80) && (U16(c, 0x33a) & 0x80))
    {
        if (!(Rand_A() & 1))
            IA(c, 0xfe) &= 0xfff7ffff;
        else
            IA(c, 0xfe) |= 0x80000;
    }
    Car_PowerDomes(c, changed);    /* 80078878 / 80078c88 */
    Car_PowerSkinFx(c, changed);
    IA(c, 0xfe) &= 0xffffbfff;
    IA(c, 0xbc) = (U16(c, 0x198) & 0x10) ? 0 : Car_SteerTorque(c, dt);
    if (IA(c, 0xcc) != 0)
        Car_Steer(c, (e->id & 1) ? 4 : 3, 0);
    if (IA(c, 0x10b) != 0)
    {
        int maxA = I16(c, 0x2da);
        int a = Abs_(I16(c, 0x2d8));
        int rate;
        if (Abs_(IA(c, 0x57)) < 0x4000)
            rate = I16(c, 0x2e2);
        else
        {
            rate = 0x80;
            if (a > 0xff)
            {
                rate = maxA < 10 ? 0x80 : Div_(I16(c, 0x2e2) * a, maxA);
                if (rate <= 0x7f)
                    rate = 0x80;
            }
        }
        int mode = IA(c, 0x10b);
        int16_t s = I16(c, 0x2d8);
        if (mode == 3)
        {
            int d = s < 1 ? (int)((uint32_t)(rate * dt) >> 8) : (rate * dt) >> 6;
            s = (int16_t)(s - d);
            if (s < -maxA)
                s = (int16_t)-maxA;
            I16(c, 0x2d8) = s;
        }
        else if (mode == 4)
        {
            int d = s < 0 ? (rate * dt) >> 6 : (int)((uint32_t)(rate * dt) >> 8);
            s = (int16_t)(s + d);
            I16(c, 0x2d8) = s;
            if (maxA < s)
                I16(c, 0x2d8) = I16(c, 0x2da);
        }
        else if (mode == 2)
        {
            if (s < 1)
            {
                if (s < 0)
                {
                    int v = (uint16_t)s + ((rate * dt) >> 6);
                    I16(c, 0x2d8) = (int16_t)v;
                    if ((int32_t)((uint32_t)v << 16) > 0)
                        I16(c, 0x2d8) = 0;
                }
            }
            else
            {
                int v = (uint16_t)s - ((rate * dt) >> 6);
                I16(c, 0x2d8) = (int16_t)v;
                if ((int32_t)((uint32_t)v << 16) < 0)
                    I16(c, 0x2d8) = 0;
            }
        }
    }
    if (IA(c, 0xcc) == 0 && g_RaceTime > 899)
    {
        if (g_PrevRaceTime < 900)
            I8(c, 0x3f4) = 1;
        else if (IA(c, 0x10a) != 0)
        {
            int g = I8(c, 0x3f4);
            if (g != 0)
            {
                if (IA(c, 0xfc) < 0)
                {
                    if (g > 0 && IA(c, 0xfc) > -7000 && g != 1 && IA(c, 0xfb) < I16(gt, 10) << 8)
                        I8(c, 0x3f4) = (int8_t)(g - 1);
                }
                else if (g < 0)
                {
                    if (IA(c, 0xfb) > 0x80)
                        I8(c, 0x3f4) = 1;
                }
                else if (g == I16(gt, 0xc) - 1 && IA(c, 0xfb) < 0x3200)
                    I8(c, 0x3f4) = (int8_t)(g - 1);
                else if (IA(c, 0xfc) < 7000 && g < I16(gt, 0xc) - 1 && (I16(gt, 8) << 8) < IA(c, 0xfb))
                {
                    if (I16(c, 0x2b4) < 0x40)
                        I8(c, 0x3f4) = (int8_t)(g + 1);
                    else if (IA(c, 0x57) > 0x3000)
                        I8(c, 0x3f4) = (int8_t)(g + 1);
                }
            }
        }
    }
    else
    {
        I8(c, 0x3f4) = -1;
        Car_Brake(c, 0x100, 0x1000);
    }
}

/* Suspension de las 4 ruedas (parte central del sub-paso de 80056c6c). Devuelve 1 si alguna rueda apoya. */
static int Car_Suspension(uint8_t* c, int ground, int sdt, int* minComp, int8_t* deepest, int16_t* compOut,
                          int* heightOut)
{
    RaceCar* e = (RaceCar*)ENT(c);
    const uint8_t* desc = PTR(c, 0x1b4);
    uint8_t* gt = PTR(c, 0x3e8);
    int comp[4], cont[4], ext[4];
    int touching = 0;
    e->obj.shadowY = (short)(ground >> 4);
    e->obj.shadowN[0] = (short)(IA(c, 0x58) >> 4);
    e->obj.shadowN[1] = (short)(IA(c, 0x59) >> 4);
    e->obj.shadowN[2] = (short)(IA(c, 0x5a) >> 4);
    int height = e->obj.pos[1] - ground;
    *heightOut = height;
    if (I16(c, 0x184) == 0)
        IA(c, 0xff) = IA(c, 0x108) - ((I32(gt, 0x14) * IA(c, 0xfb)) >> (I8(c, 0x3f4) == -1 ? 6 : 8));
    else
    {
        IA(c, 0xff) = 0;
        IA(c, 0xfb) = 0;
        IA(c, 0xfc) = 0;
    }
    if (IA(c, 0x3a) <= 0xafff)
        return 0;
    for (int k = 0; k < 4; k++)
    {
        uint16_t f = U16(c, 0x33a);
        int gh = I32(c, 0x648 + k * 0xc);
        int sc = 0;
        if (!(f & 0x10))
        {
            if (!(f & 0x20))
                sc = (f & 0x40) ? I16(c, 0x2dc) << 1 : I16(c, 0x2dc);
            else if (desc[1] == '4')
                sc = (int)((uint32_t)U16(c, 0x2dc) << 16) >> 0x13;
            else
                sc = (int)((uint32_t)U16(c, 0x2dc) << 16) >> 0x12;
        }
        uint8_t st = U8(c, 0x63a + k);
        switch (st)
        {
        case 2:
        case 3:
        case 9:
            compOut[k] = (int16_t)(sc >> 1);
            break;
        case 4:
        case 0xb:
            compOut[k] = (int16_t)(sc >> 2);
            break;
        case 5:
        case 6:
        case 7:
        case 10:
            compOut[k] = 0;
            break;
        default:
            compOut[k] = (int16_t)sc;
        }
        int drag = 0x100, setDrag = 1;
        switch (st)
        {
        case 4:
        case 6:
        case 8:
            IA(c, 0xac) = 0x800;
            drag = 0x400;
            if ((unsigned)(desc[1] - 0x21) < 6)
                break;
            if (desc[1] == 0x32)
                IA(c, 0xac) = 0x400;
            setDrag = 0;
            break;
        case 5:
        case 7:
        case 10:
            IA(c, 0xac) = 0x1000;
            drag = 0x800;
            if ((unsigned)(desc[1] - 0x21) > 5)
            {
                if (desc[1] == 0x32)
                    IA(c, 0xac) = 0x800;
                setDrag = 0;
            }
            break;
        case 9:
        case 0xb:
        case 0xc:
            if ((unsigned)(desc[1] - 0x21) < 6 || desc[1] == 0x32)
                IA(c, 0xac) >>= 1;
            drag = 0x280;
            break;
        default:
            break;
        }
        if (setDrag)
            IA(c, 0xac) = drag;
        if (IA(c, 0x108) == 0)
            IA(c, 0xac) += 0x1000;
        int wy = I32(c, 0x24c + k * 0xc);
        if (wy - gh < -0x1000)
        {
            gh = IA(c, 0x9e + k);
            cont[k] = gh;
        }
        else
            cont[k] = (IA(c, 0x9e + k) + gh) >> 1;
        IA(c, 0x9e + k) = gh;
        if (cont[k] - gh < I16(c, 0x4a2))
            I16(c, 0x4a2) = (int16_t)(cont[k] - gh);
        int h = cont[k];
        int sh = -1, sh2 = 0;
        if (st == 4)
        {
            sh = 4;
            sh2 = 0xb;
        }
        else if (st == 8)
        {
            sh = 3;
            sh2 = 10;
        }
        if (sh > 0)
        {
            int sn, cs;
            Math_SinCos((I32(c, 0x248 + k * 0xc) + I32(c, 0x250 + k * 0xc)) << sh, &sn, &cs);
            h += sn >> sh2;
        }
        cont[k] = h;
        int v = M16(wy - h, IA(c, 0xa5 + k)) - IA(c, 0xa2 + (k >> 1));
        ext[k] = v;
        if (v < *minComp)
        {
            *minComp = v;
            *deepest = (int8_t)k;
        }
    }
    int mc = *minComp;
    if (mc < 0)
    {
        int d = *deepest;
        int push[3];
        if (mc < -(IA(c, 0xa2 + (d >> 1)) >> 1))
        {
            int v = M16(I32(c, 0x24c + d * 0xc) - cont[d], IA(c, 0x19e + d * 3)) - IA(c, 0xa2 + (d >> 1));
            *minComp = v;
            push[0] = -M16(IA(c, 0x19d + d * 3), v);
            push[1] = -M16(IA(c, 0x19e + d * 3), v);
            push[2] = -M16(IA(c, 0x19f + d * 3), v);
        }
        else
        {
            push[0] = -M16(e->obj.rot[3], mc);
            push[1] = -M16(e->obj.rot[4], mc);
            push[2] = -M16(e->obj.rot[5], mc);
        }
        e->obj.pos[0] += push[0];
        e->obj.pos[1] += push[1];
        e->obj.pos[2] += push[2];
        for (int k = 0; k < 4; k++)
        {
            I32(c, 0x248 + k * 0xc) += push[0];
            I32(c, 0x24c + k * 0xc) += push[1];
            I32(c, 0x250 + k * 0xc) += push[2];
            ext[k] -= mc;
        }
    }
    for (int k = 0; k < 4; k++)
    {
        U8(c, 0x638) = 0;
        if (ext[k] < 0)
            ext[k] = 0;
        int travel = IA(c, 0xa4);
        int16_t s = (int16_t)((uint16_t)ext[k] + (((travel - ext[k]) >> 31) & ((uint16_t)travel - (uint16_t)ext[k])));
        int x = travel - s;
        comp[k] = x;
        if (x == 0)
        {
            IA(c, 0x100 + k) = 0;
            IA(c, 0x104 + k) = 0;
        }
        else
        {
            IA(c, 0x100 + k) = (IA(c, 0xb5) * x) >> 8;
            int vel = Div_((IA(c, 0xb1 + k) - s) * 0x100, sdt);
            int damp = vel < 1 ? IA(c, 0xa9) : IA(c, 0xaa);
            IA(c, 0x104 + k) = (damp * vel) >> 8;
            touching = 1;
        }
        int dmp = IA(c, 0x104 + k), spr = IA(c, 0x100 + k), lim;
        int over;
        if (dmp < 0)
        {
            lim = spr < 0 ? spr : -spr;
            over = dmp < lim;
        }
        else
        {
            lim = Abs_(spr);
            over = lim < dmp;
        }
        if (over)
            IA(c, 0x104 + k) = lim;
        IA(c, 0xb1 + k) = s;
    }
    if (!touching)
        return 0;
    if (U16(c, 0x198) & 0x10)
        IA(c, 0xfe) |= 0x4001;
    U16(c, 0x198) &= 0xffc1;
    IA(c, 0xfe) = (IA(c, 0xfe) & ~0x10) | 0x20;
    if (U8(c, 0x181) != 7 && I16(c, 0x184) != 0)
        I16(c, 0x184) = 0;
    if (IA(c, 0xfe) & 0x8000)
    {
        IA(c, 0xfe) &= 0xffff7fff;
        Fx_Kill(ENT(c), 1);   /* 80078c24 */
    }
    uint8_t bit = 1;
    for (int k = 0; k < 4; k++, bit <<= 1)
        if (I32(c, 0x648 + k * 0xc) != TRACK_NO_SURFACE && comp[k] != 0)
            U8(c, 0x638) |= bit;
    return 1;
}

/* 80056c6c: paso de la fisica de todos los coches (dos sub-pasos si dt >= 0x10; los contactos de la caja y los
 * choques solo en el ultimo), con las marcas de las ruedas (80076500), las chispas al aterrizar (8007734c /
 * 800773d4), el motor (80040600), su fuente de sonido (8003dbb0) y el humo del escape (80077c60). */
void Physics_StepAll(int dt)
{
    static int ground[RACE_MAX_CARS];   /* DAT_800b6100 */
    static int rotW[RACE_MAX_CARS][3];  /* DAT_800b6068 */
    static int minComp[RACE_MAX_CARS];  /* DAT_800b60b0 */
    static uint32_t prevFlags[RACE_MAX_CARS];   /* DAT_800b60c8 */
    static int16_t comp[RACE_MAX_CARS][4];      /* DAT_800b6118 */
    static uint8_t touching[RACE_MAX_CARS];     /* DAT_800af894 */
    static int8_t deepest[RACE_MAX_CARS];       /* DAT_800af8a0 */
    int n = g_NumCars;
    for (int i = 0; i < n; i++)
        Car_PreStep(g_CarPhys[i], dt);
    int sdt = dt, rest = dt >> 1;
    if (dt < 0x10)
        rest = 0;
    else
        sdt -= rest;
    while (sdt != 0)
    {
        for (int i = 0; i < n; i++)
        {
            uint8_t* c = g_CarPhys[i];
            int imp = I32(c, 0x6a4);
            if (imp == 0)
                continue;
            int capd;
            if (I32(c, 0x15c) < 0x8001)
            {
                int u = imp - 0x600;
                capd = (u >> 31) & u;
                I32(c, 0x158) += 0x600 + capd;
            }
            else
            {
                int u = imp - 0x700;
                capd = (u >> 31) & u;
                I32(c, 0x158) += 0x700 + capd;
            }
            if (I32(c, 0x2cc) == 0 || I32(c, 0x2d0) == 0)
                I32(c, 0x148) += Div_(imp * 0x380, I32(c, 0x300));
            if (I32(c, 0x2c4) == 0 || I32(c, 0x2c8) == 0)
                I32(c, 0x148) -= Div_(imp * 0x380, I32(c, 0x304));
            if (I32(c, 0x2c4) == 0 || I32(c, 0x2d0) == 0)
                I32(c, 0x150) += Div_(imp * 0x380, I32(c, 0x308));
            if (I32(c, 0x2c8) == 0 || I32(c, 0x2cc) == 0)
                I32(c, 0x150) -= Div_(imp * 0x380, I32(c, 0x308));
        }
        for (int i = 0; i < n; i++)
        {
            uint8_t* c = g_CarPhys[i];
            RaceCar* e = (RaceCar*)ENT(c);
            IA(c, 0x6a) = IA(c, 0x55) << 8;
            IA(c, 0x6b) = IA(c, 0x56) << 8;
            IA(c, 0x6c) = IA(c, 0x57) << 8;
            int m[9];
            for (int k = 0; k < 9; k++)
                m[k] = e->obj.rot[k] << 6;
            Mat_MulT(m, VEC(c, 0x1a8), VEC(c, 0x1a8));
            IA(c, 99) = IA(c, 0x6a) >> 14;
            IA(c, 100) = IA(c, 0x6b) >> 14;
            IA(c, 0x65) = IA(c, 0x6c) >> 14;
        }
        for (int i = 0; i < n; i++)
        {
            uint8_t* c = g_CarPhys[i];
            RaceCar* e = (RaceCar*)ENT(c);
            int top = Car_TopSpeed(c);
            if (U8(c, 0x431) != 0xcc)
                top = (top >> 1) + (int)((uint32_t)(top >> 1) * U8(c, 0x431)) / 0xcc;
            if (IA(c, 0x10a) == 0 && I8(c, 0x3f4) != 4 && !(U16(c, 0x198) & 0x10))
                top = -top;
            Car_Integrate(c, sdt, top, 1);
            int* w = rotW[i];
            if (!(U16(c, 0x33a) & 0x80))
            {
                w[1] = IA(c, 0x53) + IA(c, 0xbc) + IA(c, 0xbd);
                w[0] = IA(c, 0x52);
                w[2] = IA(c, 0x54);
                Mat_MulT(e->obj.rot, w, w);
                w[2] = (w[2] * sdt) >> 8;
                w[0] = (w[0] * sdt) >> 8;
                w[1] = (w[1] * sdt) >> 8;
            }
            else
            {
                w[1] = (IA(c, 0xfe) & 0x80000) ? (sdt * -0x6000) >> 8 : (sdt * 0x6000) >> 8;
                w[2] = (sdt * 0x3000) >> 8;
                w[0] = (sdt << 0xc) >> 8;
            }
        }
        for (int i = 0; i < n; i++)
            Car_RotateMatrix(((RaceCar*)ENT(g_CarPhys[i]))->obj.rot, rotW[i]);
        for (int i = 0; i < n; i++)
        {
            uint8_t* c = g_CarPhys[i];
            Mat_Mul(((RaceCar*)ENT(c))->obj.rot, VEC(c, 0x1a8), VEC(c, 0x154));
            IA(c, 0x55) >>= 0xe;
            IA(c, 0x56) >>= 0xe;
            IA(c, 0x57) >>= 0xe;
            prevFlags[i] = U16(c, 0x198);
            minComp[i] = 0;
        }
        if (rest == 0)
        {
            for (int i = 0; i < n; i++)
            {
                uint8_t* c = g_CarPhys[i];
                I16(c, 0x146) = (int16_t)(I16(c, 0x146) + dt);
                Car_BoxCorners(c);
            }
            Car_WallContactsAll();
            Car_GroundContactsAll();
            for (int i = 0; i < n; i++)
            {
                uint8_t* c = g_CarPhys[i];
                if (I16(c, 0xec) != 0)
                {
                    U8(c, 0x180) |= 1;
                    Contact_Average(B_(c) + 0xec);
                }
                if (I16(c, 0x10c) != 0)
                {
                    U8(c, 0x180) |= 2;
                    Contact_Average(B_(c) + 0x10c);
                }
            }
            for (int i = 0; i < n; i++)
                Car_HandleContacts(g_CarPhys[i], dt, g_CarPhys[i], 1);
        }
        Car_GroundAll(ground);
        for (int i = 0; i < n; i++)
        {
            uint8_t* c = g_CarPhys[i];
            RaceCar* e = (RaceCar*)ENT(c);
            Car_ClampGround(c, ground[i], sdt, 1);
            IA(c, 0x3a) = Vec_Dot16(VEC(c, 0x160), e->obj.rot + 3);
            I8(c, 0x639) = -1;
        }
        Car_WheelPointsAll();
        Car_WheelGroundAll(ground);
        Car_WheelSlopeAll();
        for (int i = 0; i < n; i++)
        {
            uint8_t* c = g_CarPhys[i];
            RaceCar* e = (RaceCar*)ENT(c);
            touching[i] = 0;
            if (U8(c, 0x180) & 0x10)
            {
                Car_Reset(e, 0);
                IA(c, 0x5b) = e->obj.pos[0];
                IA(c, 0x5c) = e->obj.pos[1];
                IA(c, 0x5d) = e->obj.pos[2];
                continue;
            }
            int height;
            if (Car_Suspension(c, ground[i], sdt, &minComp[i], &deepest[i], comp[i], &height))
            {
                touching[i] = 1;
                continue;
            }
            Car_Airborne(c, height, I16(c, 0x2d8), sdt);
            I16(c, 0x4a2) = 0;
            U8(c, 0x638) = 0;
        }
        if (rest == 0)
        {
            for (int i = 0; i < n; i++)
            {
                uint8_t* c = g_CarPhys[i];
                if (U8(c, 0x180) & 1)
                    Car_Collide(c, B_(c) + 0xec, dt, 0, 1);
                else if ((U8(c, 0x180) & 2) && (U16(c, 0x198) & 0x10))
                    Car_Collide(c, B_(c) + 0x10c, dt, 1, 1);
            }
        }
        for (int i = 0; i < n; i++)
            if (touching[i])
                Car_GroundForces(g_CarPhys[i], comp[i], sdt);
        for (int i = 0; i < n; i++)
            Car_WheelMarksFx(g_CarPhys[i], sdt);   /* 80076500 */
        for (int i = 0; i < n; i++)
        {
            uint8_t* c = g_CarPhys[i];
            RaceCar* e = (RaceCar*)ENT(c);
            uint8_t* gt = PTR(c, 0x3e8);
            if (IA(c, 0x53) < 0x2000 && U8(c, 0x638) == 0xf)
                IA(c, 0xfe) &= 0xffffffbf;
            uint32_t ch = prevFlags[i] ^ U16(c, 0x198);
            if (ch && (ch & 0x10))
                Car_SetCorners(c);
            if (e->speed < 0x10 || minComp[i] >= 0)
                IA(c, 0x1a9) = 0;
            else
            {
                int imp = -Div_(minComp[i] << 9, sdt);
                IA(c, 0x1a9) = imp;
                if (imp > 0x2000)
                {
                    /* golpe al aterrizar: chispas en la rueda que mas se hunde (en el marco del coche, hacia
                     * fuera y arriba), sonido 0xF y, si es fuerte, tierra o hierba con la velocidad tangente */
                    int dk = deepest[i];
                    int rel[3], loc[3];
                    for (int k = 0; k < 3; k++)
                        rel[k] = I32(c, 0x644 + dk * 0xc + k * 4) - e->obj.pos[k];
                    loc[0] = Vec_Dot16(e->obj.rot, rel);
                    loc[1] = Vec_Dot16(e->obj.rot + 3, rel);
                    loc[2] = Vec_Dot16(e->obj.rot + 6, rel);
                    loc[0] += loc[0] < 0 ? 0x100 : -0x100;
                    loc[1] += 0x80;
                    Fx_QueueImpact(c, 1, loc, IA(c, 0x1a9), 6);   /* 8007734c */
                    Fx_Push(0xf, e->obj.pos, 100);
                    if (U8(c, 0x181) != 7 && IA(c, 0x1a9) > 20000 && IA(c, 0x3a) <= 0x9782b)
                    {
                        int d = Vec_Dot16(VEC(c, 0x18c), VEC(c, 0x160)), tv[3];
                        for (int k = 0; k < 3; k++)
                            tv[k] = I32(c, 0x18c + k * 4) - M16(d, I32(c, 0x160 + k * 4));
                        Fx_QueueImpactVel(c, 1, VEC(c, 0x644 + dk * 0xc), tv, 10);   /* 800773d4 */
                    }
                }
                if (IA(c, 0x57) < 0x8001)
                {
                    if (IA(c, 0x1a9) > 0xc00)
                        IA(c, 0x1a9) = 0xc00;
                }
                else if (IA(c, 0x1a9) > 0xb00)
                    IA(c, 0x1a9) = 0xb00;
            }
            int rpm = IA(c, 0xfb), mn = I16(gt, 6);
            if ((rpm >> 8) < mn)
            {
                IA(c, 0xfb) = mn << 8;
                if (IA(c, 0x10a) != 0)
                    I8(c, 0x3f4) = -1;
            }
            else
            {
                int over = I16(gt, 4) * 0x100 - rpm;
                if (over < 0)
                {
                    IA(c, 0xfb) = rpm + over;
                    IA(c, 0xfc) += over;
                }
            }
            /* centro de la caja del modelo en el mundo (3DBB +8 + frame*16): 80015880 + 80015928 + 80016784 */
            if (e->obj.model && e->obj.model->bbox)
            {
                Gte_LoadColumns(e->obj.rot);
                Gte_SetTransShift(e->obj.pos, 0);
                Gte_Rt((const short*)(e->obj.model->bbox + e->obj.frame * 0x10 + 8), e->obj.unk34, 0);
            }
            Engine_Update(c);   /* 80040600 */
            /* el turbo (+0x490) y el roce de los golpes (+0x178) siguen al coche */
            if (IA(c, 0x124) != -1)
                Snd_SourceMove(e->handle, e->obj.pos[0], e->obj.pos[2], -1, e->speed, e->dir, IA(c, 0x124));
            if (IA(c, 0x5e) != -1)
                Snd_SourceMove(e->handle, e->obj.pos[0], e->obj.pos[2], -1, e->speed, e->dir, IA(c, 0x5e));
        }
        sdt = rest;
        rest = 0;
    }
    for (int i = 0; i < n; i++)
        Car_ExhaustFx(g_CarPhys[i], dt);   /* 80077c60 */
    for (int i = 0; i < n; i++)
    {
        uint8_t* c = g_CarPhys[i];
        RaceCar* e = (RaceCar*)ENT(c);
        int thr = IA(c, 0x108);
        IA(c, 0x109) = 0;
        IA(c, 0x108) = 0;
        IA(c, 0xfe) &= 0xffffff7f;
        I16(c, 0x3f6) = (int16_t)(thr >> 4);
        for (int k = 0; k < 4; k++)
            U8(c, 0x63e + k) = U8(c, 0x63a + k);
        IA(c, 0xbd) = M16(IA(c, 0xbd), 0x8000);
        U16(c, 0x432) = (uint16_t)(Math_Atan2(e->obj.rot[6], e->obj.rot[8]) & 0xfff);
        U16(c, 0x43c) = U16(c, 0x33a);
        if (Abs_(IA(c, 0x52)) > 0x18000 || Abs_(IA(c, 0x53)) > 0x18000 || Abs_(IA(c, 0x54)) > 0x18000)
        {
            Vec_Normalize(VEC(c, 0x148));
            Vec_Scale(0x18000, VEC(c, 0x148));
        }
        int off = I16(c, 0x4a2);
        if (Abs_(off) < 0x301)
            e->obj.pos[1] -= off;
        else
            I16(c, 0x4a2) = 0;
    }
}

/* columna j de la rueda = matriz del coche * m[j] (8004edf0 + 8004ee98); 'mirror' niega las dos primeras
 * (la rueda del otro lado, girada 180 grados sobre su eje Z) */
static void Wheel_SetRot(const int* car, const int* m, GfxObj* w, int mirror)
{
    if (!w)
        return;
    for (int j = 0; j < 3; j++)
    {
        const int* v = m + j * 3;
        for (int r = 0; r < 3; r++)
        {
            int x = M16(car[r], v[0]) + M16(car[3 + r], v[1]) + M16(car[6 + r], v[2]);
            w->rot[j * 3 + r] = (mirror && j < 2) ? -x : x;
        }
    }
}

/* 8004ef54: ruedas para dibujar (giro, direccion, recorrido de la suspension) */
static void Car_PlaceWheels(uint8_t* c)
{
    RaceCar* e = (RaceCar*)ENT(c);
    const int* rot = e->obj.rot;
    GfxObj* w[4];
    for (int k = 0; k < 4; k++)
        w[k] = (GfxObj*)PTR(c, 0x1f8 + k * 4);
    I16(c, 0x2de) = (int16_t)(I16(c, 0x2de) + (int16_t)IA(c, 0xba) * 0x10);
    I16(c, 0x2e0) = (int16_t)(I16(c, 0x2e0) + (int16_t)IA(c, 0xbb) * 0x10);
    for (int k = 0; k < 4; k++)
        if (w[k])
            for (int r = 0; r < 3; r++)
                w[k]->pos[r] = I32(c, 0x248 + k * 0xc + r * 4) - ((rot[3 + r] * IA(c, 0xb1 + k)) >> 16);
    int sr, cr, ss, cs;
    Math_SinCos(I16(c, 0x2de), &sr, &cr);
    int maxA = I16(c, 0x2da), lim = I16(c, 0x312);
    int steer = I16(c, 0x2d8);
    if (Abs_(maxA) < (lim >> 1) && (lim >> 4) < Abs_(maxA))
        steer = Div_(steer * (lim >> 1), maxA);
    Math_SinCos(steer, &ss, &cs);
    int m[9];
    m[0] = cs;
    m[1] = 0;
    m[2] = -ss;
    m[3] = ((sr >> 2) * (ss >> 2)) >> 12;
    m[4] = cr;
    m[5] = ((sr >> 2) * (cs >> 2)) >> 12;
    m[6] = ((cr >> 2) * (ss >> 2)) >> 12;
    m[7] = -sr;
    m[8] = ((cr >> 2) * (cs >> 2)) >> 12;
    Wheel_SetRot(rot, m, w[1], 0);
    Wheel_SetRot(rot, m, w[0], 1);
    Math_SinCos(I16(c, 0x2e0), &sr, &cr);
    int mr[9] = { 0x10000, 0, 0, 0, cr, sr, 0, -sr, cr };
    Wheel_SetRot(rot, mr, w[2], 0);
    Wheel_SetRot(rot, mr, w[3], 1);
    const uint8_t* desc = PTR(c, 0x1b4);
    char d1 = desc ? (char)desc[1] : 0;
    if ((U16(c, 0x33a) & 0x40) && d1 != '5' && d1 != '0' && d1 != '/' && d1 != '6')
    {
        int o[3] = { (rot[0] << 7) >> 16, (rot[1] << 7) >> 16, (rot[2] << 7) >> 16 };
        for (int r = 0; r < 3; r++)
        {
            if (w[0]) w[0]->pos[r] += o[r];
            if (w[3]) w[3]->pos[r] += o[r];
            if (w[1]) w[1]->pos[r] -= o[r];
            if (w[2]) w[2]->pos[r] -= o[r];
        }
    }
    int dy = I16(c, 0x4a2);
    if (dy != 0)
        for (int k = 0; k < 4; k++)
            if (w[k])
                w[k]->pos[1] -= dy;
}

/* 8004f54c (desde Race_DrawView): coloca las ruedas. Port: las de todos los coches; el original solo las de los
 * visibles en la vista con nivel de detalle 0 o 1 (entidad +0x25, marca +0x3F5). Las ruedas borrosas las elige
 * racecar.c. */
void Physics_PlaceWheelsAll(int view)
{
    (void)view;
    for (int i = 0; i < g_NumCars; i++)
        if (g_CarPhys[i])
            Car_PlaceWheels(g_CarPhys[i]);
}
