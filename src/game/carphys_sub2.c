#include <string.h>
#include "nr.h"
#include "track.h"
#include "vecmath.h"
#include "carphys.h"
#include "carphys_int.h"
#include "fx.h"
#include "snd.h"

/* Longitud 16.16 de un vector con dos escalas segun su tamano (patron repetido en el original) */
static int Vec_Len16(const int* v)
{
    int s0 = v[0], s1 = v[1], s2 = v[2];
    if ((unsigned)(s0 + 0x67fff) < 0xcffff && s1 < 0x68000 && s1 > -0x68000 && s2 < 0x68000 && s2 > -0x68000)
        return Math_SquareRoot12(Vec_LenSq16(v)) << 2;
    int64_t a = (int64_t)(s0 >> 4) * (s0 >> 4), b = (int64_t)(s1 >> 4) * (s1 >> 4), c = (int64_t)(s2 >> 4) * (s2 >> 4);
    return Math_SquareRoot12((int)(a >> 16) + (int)(b >> 16) + (int)(c >> 16)) << 6;
}

/* 80054a14: velocidad del mundo (+0x18C), su modulo en la entidad (+0xCC), limite de velocidad 'limit' y
 * avance de la posicion */
void Car_Integrate(uint8_t* c, int dt, int limit, int fromStep)
{
    RaceCar* e = (RaceCar*)ENT(c);
    if (U8(c, 0x189))
    {
        if ((int)U8(c, 0x189) < dt)
            U8(c, 0x189) = 0;
        else
            U8(c, 0x189) -= (uint8_t)dt;
    }
    (void)fromStep;   /* (actor bajo el coche con la bandera 0x800: pendiente con los actores) */
    if (Abs_(IA(c, 0x52)) < 0xc001 && Abs_(IA(c, 0x53)) < 0xc001 && Abs_(IA(c, 0x54)) < 0xc001)
        U16(c, 0x198) &= 0xfffe;
    else
        U16(c, 0x198) |= 1;
    if (Abs_(IA(c, 0x52)) < 0x3001 && Abs_(IA(c, 0x53)) < 0x3001 && Abs_(IA(c, 0x54)) < 0x3001)
        U16(c, 0x198) &= 0xffbf;
    else
        U16(c, 0x198) |= 0x40;
    if (!(U8(c, 0x180) & 0x10))
    {
        IA(c, 0x5b) = e->obj.pos[0];
        IA(c, 0x5c) = e->obj.pos[1];
        IA(c, 0x5d) = e->obj.pos[2];
    }
    int sp = Vec_Len16(VEC(c, 0x18c));
    if (e->type == 1 && e->speed - (sp >> 8) > 0x30)
        Speech_Say(0x2a, 0xc, e, 1);   /* frenazo brusco (golpe fuerte) */
    e->speed = sp >> 8;
    if (e->speed < 8 && (U8(c, 0x180) & 1))
        e->speed = 0;
    if (limit > 0 && limit < e->speed)
    {
        Vec_Normalize(VEC(c, 0x18c));
        int vx = (IA(c, 99) >> 8) * limit, vy = (IA(c, 100) >> 8) * limit, vz = (IA(c, 0x65) >> 8) * limit;
        IA(c, 99) = vx;
        IA(c, 100) = vy;
        IA(c, 0x65) = vz;
        IA(c, 0x6a) = vx * 0x4000;
        IA(c, 0x6b) = vy * 0x4000;
        IA(c, 0x6c) = vz * 0x4000;
    }
    if (I16(c, 0x1a6) != 0)
        IA(c, 0x6b) += I16(c, 0x1a6) * -0x4000;
    if (IA(c, 100) > 0x4000)
    {
        IA(c, 100) = 0x4000;
        IA(c, 0x6b) = 0x10000000;
    }
    e->dir[0] = IA(c, 99);
    e->dir[1] = IA(c, 100);
    e->dir[2] = IA(c, 0x65);
    Vec_Normalize(e->dir);
    int mv[3];
    if ((!(U16(c, 0x198) & 0x20) || U8(c, 0x188) != 0) && !(U16(c, 0x198) & 0x80))
    {
        mv[0] = (IA(c, 99) * dt) >> 8;
        mv[1] = (IA(c, 100) * dt) >> 8;
        mv[2] = (IA(c, 0x65) * dt) >> 8;
    }
    else
    {
        mv[0] = mv[1] = mv[2] = 0;
        IA(c, 0x55) = IA(c, 0x56) = IA(c, 0x57) = 0;
        IA(c, 99) = IA(c, 100) = IA(c, 0x65) = 0;
        IA(c, 0x6a) = IA(c, 0x6b) = IA(c, 0x6c) = 0;
    }
    int t[2] = { mv[0], mv[2] };
    IA(c, 0x4b) += Vec_ApproxLen2(t);
    e->obj.pos[0] += mv[0];
    e->obj.pos[1] += mv[1];
    e->obj.pos[2] += mv[2];
}

/* 80055f44: no dejar el coche por debajo del suelo + margen (+0x1A0 segun la superficie) */
void Car_ClampGround(uint8_t* c, int ground, int dt, int fromStep)
{
    RaceCar* e = (RaceCar*)ENT(c);
    int dy = e->obj.pos[1] - ground;
    int margin;
    (void)fromStep;
    uint16_t sf = U16(c, 0x18a);
    if ((sf & 0x5000) == 0x5000)
        margin = 0xc00;
    else
    {
        margin = 0x1000;
        if (!(sf & 0x4000) && (margin = 0x4000, (sf & 0x1000) != 0))
        {
            if (IA(c, 0x68) < 0x1001)
                goto check;
            int v = IA(c, 0x68) - ((dt * 0x3000) >> 8);
            IA(c, 0x68) = v;
            margin = 0x1000;
            if (v > 0xfff)
                goto check;
        }
    }
    IA(c, 0x68) = margin;
check:
    /* comentario: el coche del humano cae desde mas de 0x800 */
    if (e->type == 1 && (U16(c, 0x198) & 1) && dy > 0x800)
        Speech_Say(0x2a, 0xc, e, 1);
    if (U8(c, 0x181) == 7)
        dy -= 0x2000;
    if (IA(c, 0x68) < dy)
    {
        e->obj.pos[1] = ground + IA(c, 0x68);
        if (IA(c, 100) >= 0)
        {
            IA(c, 100) = -0x100;
            Mat_Mul(e->obj.rot, VEC(c, 0x18c), VEC(c, 0x154));
        }
    }
}

/* 800524b4: en el aire (ninguna rueda apoyada) */
void Car_Airborne(uint8_t* c, int height, int steer, int dt)
{
    RaceCar* e = (RaceCar*)ENT(c);
    int k1 = 0, k0 = 0;
    uint16_t f198 = U16(c, 0x198);
    U16(c, 0x198) = f198 & 0xff7f;
    if (height < 0x801 || (f198 & 0x40) || IA(c, 0x59) < 0xf001 || e->speed < 0x41)
    {
        if (IA(c, 0xfe) & 0x8000)
        {
            IA(c, 0xfe) &= 0xffff7fff;
            Fx_Kill(e, 1);               /* 80078c24: fin de las estelas del salto */
        }
    }
    else if (!(IA(c, 0xfe) & 0x8000))
    {
        if (e->type == 1)
            Speech_Say(2, 0xf, e, 1);    /* salto */
        IA(c, 0xfe) |= 0x8000;
        Car_JumpFx(c);                   /* 80078bb8 */
    }
    int a = Div_(steer * 0xff00, I16(c, 0x312));
    if ((IA(c, 0xfe) & 0x40) || (U16(c, 0x198) & 0x20) || I16(c, 0x184) != 0)
        a = 0;
    static const int up[3] = { 0, 0x10000, 0 };
    IA(c, 0xbc) = 0;
    U16(c, 0x198) |= 0x10;
    Car_ClearFlags(c, 3);
    int travel = IA(c, 0xa4);
    IA(c, 0xb1) = IA(c, 0xb2) = IA(c, 0xb3) = IA(c, 0xb4) = travel;
    int16_t pitch;
    if (!(U16(c, 0x33a) & 0x80))
    {
        pitch = (int16_t)(dt << 6);
        if (height > 0x9ff)
            pitch = (int16_t)(dt << 5);
    }
    else if (height < 0x800)
        pitch = (int16_t)((int16_t)dt * -2);
    else
        pitch = (int16_t)(dt << 5);
    I16(c, 0x1a6) = pitch;
    if (U16(c, 0x33a) & 0x80)
        goto gears;
    {
        int inertia = I16(c, 0x19e) << 4;
        int64_t roll = (int64_t)inertia * (int64_t)M16(a, 0x24000);
        uint32_t lift = 0;
        if (IA(c, 0xfe) & 0x30)
        {
            int f = (IA(c, 0xfe) & 0x10) ? 0x2bf7c : 0x2bf7;
            lift = (uint32_t)M16(inertia, f);
        }
        if (!(IA(c, 0xfe) & 0x30))
            goto gears;
        if (!(U16(c, 0x198) & 0x20) && (U16(c, 0x198) & 1))
            IA(c, 0xfe) &= 0xffffffcf;
        if ((int)lift > 0)
        {
            int d = Vec_Dot16(up, e->obj.rot + 3);
            if (d < -0xe000)
            {
                /* (el original calcula y descarta el producto con el eje X: usa el del eje Y) */
                k1 = M16(Vec_Dot16(up, e->obj.rot + 6), (int)lift);
                k0 = -M16(d, (int)lift);
            }
            else if (d > 0xefff)
            {
                U8(c, 0x188) = 0x19;
                U16(c, 0x198) &= 0xffdf;
            }
            else
            {
                int dx = Vec_Dot16(up, e->obj.rot);
                k1 = M16(Vec_Dot16(up, e->obj.rot + 6), (int)lift);
                k0 = -M16(dx, (int)lift);
            }
        }
        int g = (IA(c, 0xfe) & 0x10) ? 0x15fbe : 0x8cb2;
        int tx = k1 - M16(g, IA(c, 0x52));
        int tz = k0 - M16(g, IA(c, 0x54));
        if (tz != 0)
            IA(c, 0x54) += Div_(tz * dt, IA(c, 0x4f)) * 0x100;
        if (tx != 0)
            IA(c, 0x52) += Div_(tx * dt, IA(c, 0x50)) * 0x100;
        int64_t l3 = (int64_t)(I16(c, 0x19e) << 4) * 0x40000;
        int64_t l4 = (int64_t)(int)(l3 >> 16) * (int64_t)IA(c, 0x53);
        int ty = (int)(roll >> 16) - (int)(l4 >> 16);
        if (ty != 0)
            IA(c, 0x53) += Div_(ty * dt, IA(c, 0x4e)) * 0x100;
    }
gears:
    if (I8(c, 0x3f4) < 0)
        IA(c, 0xbb) += (((IA(c, 0xbb) * -0x10) >> 8) * dt) >> 8;
    else
    {
        uint8_t* gt = PTR(c, 0x3e8);
        int d = Div_(IA(c, 0xff) * dt, I32(gt, 0x10));
        int r = IA(c, 0xfb) + d;
        IA(c, 0xfc) = d;
        IA(c, 0xfb) = r;
        IA(c, 0xbb) = Div_(r, I16(gt, I8(c, 0x3f4) * 2 + 0x18));
    }
    IA(c, 0xba) += (((IA(c, 0xba) * -0x10) >> 8) * dt) >> 8;
}

/* 80052b68: par de giro de las ruedas delanteras segun el angulo de la direccion */
int Car_SteerTorque(uint8_t* c, int dt)
{
    int r = Div_(I16(c, 0x2d8) << 0xb, I16(c, 0x312));
    int k = ((int)((uint32_t)U16(c, 0x2b4) << 16) >> 0x11) * (r < 1 ? -r : r);
    k >>= 10;
    int sh, sh2;
    if (!(U32(c, 0x3f8) & 0x80))
    {
        sh = 5;
        if (I16(c, 0x2b4) < 0x81)
            sh2 = 5;
        else
        {
            sh = 6;
            sh2 = 4;
        }
    }
    else
    {
        sh = 6;
        sh2 = 4;
    }
    if (k == 0)
        k = 0;
    else if (I32(c, 0x15c) < 0x100)
    {
        if (I32(c, 0x15c) > -0x101)
            k = 0;
        else
        {
            int d = (k * dt) >> sh2;
            I32(c, 0x154) = I16(c, 0x2d8) < 1 ? I32(c, 0x154) + d : I32(c, 0x154) - d;
            if (I16(c, 0x2d8) > 0)
                k = -(k << sh);
            else
                k <<= sh;
        }
    }
    else
    {
        int d = (k * dt) >> sh2;
        I32(c, 0x154) = I16(c, 0x2d8) < 1 ? I32(c, 0x154) + d : I32(c, 0x154) - d;
        if (I16(c, 0x2d8) < 1)
            k = -(k << sh);
        else
            k <<= sh;
    }
    if (U32(c, 0x3f8) & 0x80)
    {
        int ar = r < 1 ? -r : r;
        int h = (((int)I16(c, 0x2b6) + (int)I16(c, 0x2b4)) >> 2) * ar >> 10;
        if (h != 0)
        {
            if (I32(c, 0x154) < 0x101)
            {
                if (I32(c, 0x154) < -0x100)
                    k += h * 0x40;
            }
            else
                k += h * -0x40;
        }
    }
    return Div_(I32(c, 0x15c) * I16(c, 0x2d8), I32(c, 0x2fc)) * 0x10 + k;
}
