#include <stdlib.h>
#include <string.h>
#include "nr.h"
#include "model.h"
#include "track.h"
#include "cnet.h"
#include "vecmath.h"
#include "exe.h"
#include "frontend.h"
#include "carphys.h"
#include "carphys_int.h"
#include "snd.h"

/* 8004d668: grupo de parametros segun el piloto (personajes especiales 0x21-0x37) y tipo de coche */
static void Car_ParamIndex(uint8_t* c, int* group, int* car)
{
    const uint8_t* desc = PTR(c, 0x1b4);
    unsigned drv = desc[1];
    if (drv > 0x20 && drv < 0x27)
        *group = 1;
    else if (drv > 0x20 && drv < 0x2e)
        *group = 2;
    else if (drv > 0x20 && drv < 0x38)
        *group = (int)drv - 0x2b;
    else
        *group = 0;
    *car = desc[2];
}

/* 800779a0: tubos de escape (+0x444 numero, +0x445 banderas, +0x44C registros de 0xE bytes: punto del modelo,
 * su posicion local y la direccion del humo >> 4) segun la tabla 800ab618 (u16 escala de la llama, n, puntos
 * en 800ab5bc {punto, rumbo, inclinacion}); largo de las llamas (+0x440 / +0x442) de 800ab6d8 / 800ab708 */
static void Car_InitExhaust(uint8_t* c)
{
    int g, k;
    Car_ParamIndex(c, &g, &k);
    int cfg = (int8_t)Exe_U8(0x800ab688u + g * 3 + k);
    if (cfg < 0)
    {
        U8(c, 0x444) = 0;
        return;
    }
    U8(c, 0x445) = Exe_U8(0x800ab6b0u + g * 3 + k);
    uint32_t t = 0x800ab618u + (uint32_t)cfg * 8;
    int n = (int8_t)Exe_U8(t + 2);
    U8(c, 0x444) = (uint8_t)n;
    unsigned scale = Exe_U16(t);
    GfxObj* o = &((RaceCar*)ENT(c))->obj;
    for (int i = 0; i < n; i++)
    {
        uint32_t pd = 0x800ab5bcu + (uint32_t)(int8_t)Exe_U8(t + 3 + i) * 6;
        uint8_t* x = B_(c) + 0x44c + i * 0xe;
        x[0] = Exe_U8(pd);
        int m[9], lp[3];
        Math_RotMatrix((int16_t)Exe_U16(pd + 2), (int16_t)Exe_U16(pd + 4), 0, m);
        Obj_GetLocalPoint(o, (int8_t)x[0], lp);
        for (int a = 0; a < 3; a++)
        {
            *(int16_t*)(x + 2 + a * 2) = (int16_t)lp[a];
            *(int16_t*)(x + 8 + a * 2) = (int16_t)(m[6 + a] >> 4);
        }
        int div = I16(PTR(c, 0x3e8), 4);
        if (div)
        {
            int a = ((int)((uint32_t)Exe_S32(0x800ab6dcu) * scale) >> 8) - ((int)((uint32_t)Exe_S32(0x800ab6d8u) * scale) >> 8);
            int b = ((int)((uint32_t)Exe_S32(0x800ab70cu) * scale) >> 8) - ((int)((uint32_t)Exe_S32(0x800ab708u) * scale) >> 8);
            I16(c, 0x440) = (int16_t)(a * 0x100 / div);
            I16(c, 0x442) = (int16_t)(b * 0x100 / div);
        }
    }
}

/* 8004d8dc: 12 bytes en 800ab1e4 por la tabla 800ab3e0 */
static uint32_t Car_ParamsAddr(uint8_t* c)
{
    int g, k;
    Car_ParamIndex(c, &g, &k);
    return 0x800ab1e4u + Exe_U8(0x800ab3e0u + g * 3 + k) * 0xc;
}

/* 8004d6fc: marchas, 10 bytes en 800ab05c por la tabla 800ab3b8 */
static uint32_t Car_GearsAddr(uint8_t* c)
{
    int g, k;
    Car_ParamIndex(c, &g, &k);
    return 0x800ab05cu + Exe_U8(0x800ab3b8u + g * 3 + k) * 10;
}

/* 8004d75c: tabla de marchas (0x4C bytes): +0 velocidad maxima, +4 regimen maximo, +6 minimo, +8, +0xA,
 * +0xC n de marchas, +0x10 / +0x14, +0x18 relaciones (5), +0x22 velocidad maxima por marcha (5),
 * +0x2C curva de par (16) */
static void Car_GearTable(uint8_t* t, unsigned a, unsigned b, unsigned c4, unsigned c5, int curve, int bias)
{
    U16(t, 10) = 0x46;
    I16(t, 8) = (int16_t)(((b & 0xff) * 0x73) >> 7);
    U16(t, 4) = (uint16_t)(b & 0xff);
    U16(t, 6) = 0x10;
    U32(t, 0) = a & 0xff;
    U16(t, 0xc) = 5;
    U32(t, 0x10) = c4 & 0xffff;
    U32(t, 0x14) = c5 & 0xffff;
    for (int i = 0; i < 5; i++)
        U16(t, 0x18 + i * 2) = Exe_U16(0x800aafe4u + i * 2);
    for (int i = 0; i < 16; i++)
        I16(t, 0x2c + i * 2) = (int16_t)(Exe_S16(0x800aaff0u + (uint32_t)((int8_t)curve << 5) + i * 2) + bias);
    int k = (int)(b & 0xff) * 0x1ca60;
    for (int i = 0; i < 5; i++)
    {
        int r = Abs_(Exe_S16(0x800aafe4u + i * 2));
        int v = Div_(k, r) >> 14;
        U16(t, 0x22 + i * 2) = (uint16_t)(v <= (int)(a & 0xff) ? v + 10 : (int)(a & 0xff));
    }
}

/* 8004db30: la normal (+0x350) y otra con los ajustes de 800ab050-800ab058 (+0x39C) */
static void Car_InitGears(uint8_t* c)
{
    uint32_t p = Car_GearsAddr(c);
    int a = Exe_S8(p), b = Exe_S8(p + 1), curve = Exe_S8(p + 6), bias = Exe_S16(p + 8);
    unsigned c4 = Exe_U16(p + 2), c5 = Exe_U16(p + 4);
    Car_GearTable(B_(c) + 0x350, (unsigned)a, (unsigned)b, c4, c5, curve, bias);
    Car_GearTable(B_(c) + 0x39c, (unsigned)(a + Exe_S8(0x800ab050)), (unsigned)(b + Exe_S8(0x800ab051)),
                  ((Exe_U16(0x800ab052) * c4) >> 8) & 0xffff, ((Exe_U16(0x800ab054) * c5) >> 8) & 0xffff, curve,
                  (int16_t)(bias + Exe_U16(0x800ab058)));
}

/* 8004da38: suspension */
static void Car_SetSuspension(uint8_t* c, int travel, int stiff, int damp1, int damp2)
{
    int k = Div_(stiff << 8, travel);
    I32(c, 0x290) = travel;
    I32(c, 0x2d4) = k;
    int r = (Math_SquareRoot12((k >> 4) * (I32(c, 0x134) >> 6)) << 2) >> 3;
    I32(c, 0x2a4) = (r * (damp1 >> 4)) >> 12;
    I32(c, 0x2a8) = (r * (damp2 >> 4)) >> 12;
}

/* 8004e1b4: masa, inercias y puntos de la caja del coche desde la caja del modelo */
static void Car_InitBody(RaceCar* e, uint8_t* c, int mass)
{
    for (int i = 0; i < 8; i++)
        U8(c, 4 + i * 4) = 0xff;           /* TrackPos de las 8 esquinas de la caja */
    I32(c, 0x130) = mass;
    I16(c, 0x146) = 10000;
    U8(c, 0x189) = 0;
    I16(c, 0x17c) = 0;
    I16(c, 0x1a6) = 0;
    U8(c, 0x180) = 0;
    U8(c, 0x1a5) = 0;
    U8(c, 0x1a4) = 0;
    I32(c, 0x134) = mass >> 5;
    I16(c, 0x186) = 0;
    SETPTR(c, 0, e);
    I16(c, 0x184) = 0;
    I32(c, 0x12c) = 0;
    U8(c, 0x19d) = 0;
    U8(c, 0x181) = 0;
    I16(c, 0x18a) = 0;
    I32(c, 0x1a0) = 0x4000;
    const uint8_t* bb = e->obj.model ? e->obj.model->bbox : NULL;
    if (!bb)
        return;
    int hy = I16(bb, 0x12);
    int ix = hy << 1;
    if (ix > 0x800)
        ix = 0x800;
    int hx = I16(bb, 0xe);
    int iz = I16(bb, 0x10) * 2;
    int xp = I16(bb, 8) + hx, xm = I16(bb, 8) - hx;
    int zp = I16(bb, 0xc) + hy, zm = I16(bb, 0xc) - hy;
    int iy;
    if (e->type == 1 || (iy = hx * 2, e->type == 0x11))
    {
        U8(c, 0x19a) = 1;
        U8(c, 0x19b) = 2;
        U8(c, 0x19c) = 0;
        int v = I16(bb, 10) + (I16(bb, 0x10) >> 1);
        IA(c, 0xd) = v;
        IA(c, 10) = v;
        v = I16(bb, 10) + I16(bb, 0x10);
        IA(c, 0x13) = v;
        IA(c, 0x10) = v;
        v = I16(bb, 10);
        IA(c, 0x1f) = IA(c, 0x1c) = IA(c, 0x19) = IA(c, 0x16) = v;
        iy = ix;
    }
    else
    {
        if (iz * iy < ix * iy)
        {
            if (ix * iz < ix * iy)
            {
                U8(c, 0x19a) = 1;
                U8(c, 0x19b) = 2;
                U8(c, 0x19c) = 0;
            }
            else
                goto axisB;
        }
        else
        {
            if (iz * iy <= ix * iz)
                goto axisB;
            U8(c, 0x19a) = 2;
            U8(c, 0x19b) = 0;
            U8(c, 0x19c) = 1;
            goto axisDone;
        axisB:
            U8(c, 0x19a) = 0;
            U8(c, 0x19b) = 1;
            U8(c, 0x19c) = 2;
        }
    axisDone:
        {
            int v = I16(bb, 10) + I16(bb, 0x10);
            IA(c, 0x13) = IA(c, 0x10) = IA(c, 0xd) = IA(c, 10) = v;
            v = I16(bb, 10) - I16(bb, 0x10);
            IA(c, 0x16) = IA(c, 0x1f) = IA(c, 0x1c) = IA(c, 0x19) = v;
        }
    }
    int m = IA(c, 0x4d);
    int64_t l1 = (int64_t)((m * iy) >> 8) * (int64_t)((ix * ix + iz * iz) >> 8);
    IA(c, 0x17) = zp;
    IA(c, 0xb) = zp;
    IA(c, 0x15) = xp;
    IA(c, 9) = xp;
    IA(c, 0x1a) = zp;
    IA(c, 0xe) = zp;
    int64_t l2 = (int64_t)((m * ix) >> 8) * (int64_t)((iy * iy + iz * iz) >> 8);
    IA(c, 0x18) = xm;
    IA(c, 0xc) = xm;
    IA(c, 0x1d) = zm;
    IA(c, 0x11) = zm;
    IA(c, 0x1b) = xm;
    IA(c, 0xf) = xm;
    IA(c, 0x20) = zm;
    IA(c, 0x14) = zm;
    IA(c, 0x1e) = xp;
    int64_t l3 = (int64_t)((m * iz) >> 8) * (int64_t)((ix * ix + iy * iy) >> 8);
    IA(c, 0x12) = xp;
    IA(c, 0x50) = (int)(l1 >> 16) / 0x18;
    IA(c, 0x4f) = (int)(l2 >> 16) / 0x18;
    IA(c, 0x4e) = (int)(l3 >> 16) / 0x18;
    uint32_t u = (uint32_t)IA(c, 0x50);
    if ((int)u < 1)
        u = (uint32_t)-(int)(((uint64_t)(uint32_t)-(int)u * 0x3b655u) >> 16);
    else
        u = (uint32_t)(((uint64_t)u * 0x3b655u) >> 16);
    int s = (int16_t)(u >> 4);
    int d = 0x400 - s;
    U16(c, 0x19e) = (uint16_t)(0x400 - ((d >> 31) & d));
    if (IA(c, 0x50) < 0x9c4)
        IA(c, 0x50) = 0x9c4;
    if (IA(c, 0x4f) < 0x9c4)
        IA(c, 0x4f) = 0x9c4;
    if (IA(c, 0x4e) < 0x9c4)
        IA(c, 0x4e) = 0x9c4;
    U16(c, 0x198) = 0x10;
    IA(c, 0x5e) = -1;
    SETPTR(c, 0xe4, B_(c) + 0x24);
    IA(c, 0x54) = IA(c, 0x52) = IA(c, 0x53) = 0;
    IA(c, 0x57) = IA(c, 0x55) = IA(c, 0x56) = 0;
    U8(c, 0x182) = 0;
    U8(c, 0x183) = 0;
    IA(c, 99) = IA(c, 100) = IA(c, 0x65) = 0;
    I16(c, 0x144) = 8;
    e->speed = 0;
    e->dir[0] = e->dir[1] = e->dir[2] = 0;
    IA(c, 0x5b) = e->obj.pos[0];
    IA(c, 0x5c) = e->obj.pos[1];
    IA(c, 0x5d) = e->obj.pos[2];
}

/* 8004dc1c */
static void Car_InitParams(RaceCar* e, uint8_t* c)
{
    SETPTR(c, 0, e);
    uint32_t p = Car_ParamsAddr(c);
    U8(c, 0x311) = Exe_U8(p + 2);
    Car_InitBody(e, c, Exe_U16(p) << 8);
    e->phys2 = c;
    IA(c, 0xbe) = (IA(c, 0x4c) << 1) / 3;
    Car_SetSuspension(c, Exe_U8(p + 7) << 4, Exe_U8(p + 8) * Exe_U16(p) * 0x10, Exe_U8(p + 9) << 12,
                      Exe_U8(p + 10) << 12);
    const uint8_t* bb = e->obj.model ? e->obj.model->bbox : NULL;
    int dy = bb ? (I16(bb, 0x10) << 1) / 6 : 0;
    static const int order[4] = { 1, 0, 2, 3 };
    int v[4][3];
    for (int k = 0; k < 4; k++)
    {
        Obj_GetModelPoint(B_(&e->obj), order[k], v[k]);
        v[k][1] -= dy;
        v[k][2] -= 0x20;
    }
    IA(c, 0xc0) = Abs_(v[2][2]);
    IA(c, 0xc1) = Abs_(v[0][2]);
    IA(c, 0xc2) = Abs_(v[0][0]);
    IA(c, 0xc3) = Abs_(v[0][1]) + Exe_S8(p + 3) * 0x10;
    IA(c, 0xab) = Exe_U8(p + 4) << 4;
    U16(c, 0x2dc) = (uint16_t)(Exe_U8(p + 5) << 5);
    U16(c, 0x312) = (uint16_t)(Exe_U8(p + 6) << 1);
}

/* 80065fe8: posicion y marco desde la linea central (si no hay red) */
static void Car_CurveFrame(const int* pos, int* outPos, int* frame)
{
    const Track* t = g_Track;
    int i = Track_NearestCurvePoint(pos);
    Track_GetCurvePoint(i, outPos);
    if (!frame || !t)
        return;
    int nx[3];
    Track_GetCurvePoint(i + 1 < t->numCurve ? i + 1 : 0, nx);
    frame[6] = outPos[0] - nx[0];
    frame[7] = outPos[1] - nx[1];
    frame[8] = outPos[2] - nx[2];
    Vec_Normalize(frame + 6);
    TrackPos tp = { 0xff, 0xff, -1 };
    Track_FindSurface(&tp, outPos, frame + 3, 0);
    Vec_Cross(frame + 3, frame + 6, frame);
}

/* 8004c5dc: nodo de la red mas cercano a pos, sobre el suelo, mirando al siguiente nodo */
void Car_TrackFrame(const int* pos, int* outPos, int* frame, int* links)
{
    const uint8_t* net = g_NumNets ? g_Nets[0] : NULL;
    if (net && Cnet_Count(net) > 0)
    {
        int n = Cnet_FindNearest3D(net, pos);
        Cnet_NodePos(net, n, outPos);
        TrackPos tp = { 0xff, 0xff, -1 };
        int up[3];
        if (Track_FindSurface(&tp, outPos, up, 0) != TRACK_NO_SURFACE)
        {
            if (!frame)
                return;
            int next = Cnet_Next(net, n), count = 0;
            uint32_t l = NR_Read32(net + 4 + n * 12);
            unsigned lk[3] = { l >> 22, (l >> 12) & 0x3ff, (l >> 2) & 0x3ff };
            next = -1;
            for (int k = 0; k < 3; k++)
                if (lk[k] < 0x3ff)
                {
                    if (next < 0)
                        next = (int)lk[k];
                    count++;
                }
            if (links)
                *links = count;
            if (next < 0 || next >= Cnet_Count(net))
                next = 0;
            int np[3];
            Cnet_NodePos(net, next, np);
            frame[6] = np[0] - outPos[0];
            frame[7] = np[1] - outPos[1];
            frame[8] = np[2] - outPos[2];
            Vec_Normalize(frame + 6);
            frame[3] = up[0];
            frame[4] = up[1];
            frame[5] = up[2];
            Vec_Cross(frame + 3, frame + 6, frame);
            return;
        }
    }
    Car_CurveFrame(pos, outPos, frame);
}

/* 8004c834: devuelve el coche a la pista (ultima posicion buena +0x16C) y lo para */
void Car_Reset(RaceCar* e, int stopped)
{
    uint8_t* c = e->phys;
    if (!c)
        return;
    Car_TrackFrame(VEC(c, 0x16c), e->obj.pos, e->obj.rot, NULL);
    e->obj.pos[1] += 0x1000 + I32(c, 0x290) * 2;
    I16(c, 0x186) = 800;
    if (stopped)
        U32(c, 0x3f8) |= 0x100000;
    else
        U32(c, 0x3f8) &= 0xffefffff;
    int travel = I32(c, 0x290);
    U8(c, 0x3f4) = 1;
    for (int o = 0x148; o <= 0x15c; o += 4)
        I32(c, o) = 0;
    I32(c, 0x18c) = I32(c, 0x190) = I32(c, 0x194) = 0;
    U16(c, 0x198) = 0;
    I32(c, 0x3ec) = I32(c, 0x3f0) = 0;
    I16(c, 0x3f6) = 0;
    I32(c, 0x2c4) = I32(c, 0x2c8) = I32(c, 0x2cc) = I32(c, 0x2d0) = travel;
    U32(c, 0x3f8) &= 0xffffffc1;
    I32(c, 0x16c) = e->obj.pos[0];
    I32(c, 0x170) = e->obj.pos[1];
    I32(c, 0x174) = e->obj.pos[2];
}

/* 8004dafc: puntos de contacto de la caja (8 esquinas) o solo los 4 de las ruedas */
void Car_SetCorners(uint8_t* c)
{
    if (!(U16(c, 0x198) & 0x10))
    {
        SETPTR(c, 0xe4, B_(c) + 0x218);
        I16(c, 0x144) = 4;
    }
    else
    {
        SETPTR(c, 0xe4, B_(c) + 0x24);
        I16(c, 0x144) = 8;
    }
}

/* 8004d4f0: velocidad local (+0x154) al mundo */
static void Car_VelToWorld(uint8_t* c, int* out)
{
    const int* r = ENT(c) ? ((RaceCar*)ENT(c))->obj.rot : NULL;
    int vx = IA(c, 0x55), vy = IA(c, 0x56), vz = IA(c, 0x57);
    out[0] = M16(vz, r[6]) + M16(vx, r[0]) + M16(vy, r[3]);
    out[1] = M16(vz, r[7]) + M16(vx, r[1]) + M16(vy, r[4]);
    out[2] = M16(vz, r[8]) + M16(vx, r[2]) + M16(vy, r[5]);
}

/* 8004e830 (+ 8004d44c): fisica nueva para la entidad e con sus 4 ruedas (GfxObj consecutivos) */
uint8_t* Car_CreatePhys(RaceCar* e, GfxObj* wheels, const void* desc)
{
    uint8_t* c = (uint8_t*)calloc(1, 0x6a8);
    if (!c)
        return NULL;
    e->phys = c;
    SETPTR(c, 0, e);
    U8(c, 0x31b) = 0xff;
    U8(c, 0x31a) = 0xff;
    I16(c, 0x4a2) = 0;
    IA(c, 199) = IA(c, 200) = 0;
    IA(c, 0x125) = 0;
    IA(c, 0x124) = -1;
    U16(c, 0x318) = 0xffff;
    IA(c, 0xcb) = 900;
    U8(c, 0x338) = 0xff;
    U8(c, 0x310) = (uint8_t)e->id;
    for (int k = 0; k < 4; k++)
        U8(c, 0x340 + k * 4) = 0xff;
    SETPTR(c, 0x1b4, desc);
    for (int k = 0; k < 4; k++)
    {
        SETPTR(c, 0x1f8 + k * 4, &wheels[k]);
        U8(c, 0x208 + k * 4) = 0xff;
    }
    Car_InitParams(e, c);
    const uint8_t* bb = e->obj.model ? e->obj.model->bbox : NULL;
    int yc = bb ? I16(bb, 10) : 0;
    IA(c, 0x88) = IA(c, 0xb);
    IA(c, 0x86) = IA(c, 9);
    IA(c, 0x8b) = IA(c, 0xe);
    IA(c, 0x89) = IA(c, 0xc);
    IA(c, 0x8e) = IA(c, 0x11);
    IA(c, 0x8c) = IA(c, 0xf);
    IA(c, 0x91) = IA(c, 0x14);
    IA(c, 0x8f) = IA(c, 0x12);
    IA(c, 0x90) = IA(c, 0x8d) = IA(c, 0x8a) = IA(c, 0x87) = yc;
    IA(c, 0xa2) = wheels[0].model && wheels[0].model->bbox ? I16(wheels[0].model->bbox, 0x10) : 0x100;
    IA(c, 0xa3) = wheels[2].model && wheels[2].model->bbox ? I16(wheels[2].model->bbox, 0x10) : 0x100;
    IA(c, 0xbf) = IA(c, 0xc0) + IA(c, 0xc1);
    int travel = IA(c, 0xa4);
    IA(c, 0xac) = 0x100;
    I16(c, 0x2e2) = 0x200;
    IA(c, 0xb1) = IA(c, 0xb2) = IA(c, 0xb3) = IA(c, 0xb4) = travel;
    U16(c, 0x2da) = U16(c, 0x312);
    IA(c, 0xb9) = IA(c, 0x4c) << 1;
    Car_InitGears(c);
    SETPTR(c, 0x3e8, B_(c) + 0x350);
    const int8_t* d = (const int8_t*)desc;
    IA(c, 0x10a) = (d[0] == -1 || g_Settings.transmission[d[0] & 1] < 3) ? 1 : 0;
    U8(c, 0x3f4) = 0xff;
    for (int k = 0; k < 4; k++)
    {
        uint8_t* s = B_(c) + 0x4a8 + k * 100;  /* 80075a9c: marcas de las ruedas (pendiente) */
        memset(s, 0, 100);
        s[0] = 0x80;
        U8(c, 0x63e + k) = 0xf;
        U8(c, 0x63a + k) = 0xf;
    }
    /* orientacion: de la salida al siguiente nodo, con la normal del suelo */
    const uint8_t* net = g_NumNets ? g_Nets[0] : NULL;
    int* rot = e->obj.rot;
    if (net && Cnet_Count(net) > 0)
    {
        int n0 = Cnet_FindNearest3D(net, g_StartPos), n1 = Cnet_Next(net, n0);
        int p0[3], p1[3], up[3];
        Cnet_NodePos(net, n0, p0);
        Cnet_NodePos(net, n1 < 0 ? n0 : n1, p1);
        TrackPos tp = { 0xff, 0xff, -1 };
        p0[1] = Track_FindSurface(&tp, p0, up, 0);
        p1[1] = Track_FindSurface(&tp, p1, NULL, 0);
        rot[6] = p1[0] - p0[0];
        rot[7] = p1[1] - p0[1];
        rot[8] = p1[2] - p0[2];
        Vec_Normalize(rot + 6);
        rot[3] = up[0];
        rot[4] = up[1];
        rot[5] = up[2];
        Vec_Cross(rot + 3, rot + 6, rot);
    }
    TrackPos tp = { 0xff, 0xff, -1 };
    int y = Track_FindSurface(&tp, e->obj.pos, NULL, 0);
    if (y != TRACK_NO_SURFACE)
        e->obj.pos[1] = y + IA(c, 0xa3) * 2;
    for (int k = 0; k < 4; k++)
    {
        IA(c, 0x100 + k) = 0;
        IA(c, 0x9e + k) = 0;
    }
    static const int order[4] = { 1, 0, 2, 3 };
    Gte_LoadObjMatrix(e->obj.rot);
    for (int k = 0; k < 4; k++)
        Obj_GetPointGte(B_(&e->obj), order[k], VEC(c, 0x248 + k * 0xc), 0);
    int v[3];
    Car_VelToWorld(c, v);
    e->speed = Math_SquareRoot12(((v[0] * v[0]) >> 8) + ((v[1] * v[1]) >> 8) + ((v[2] * v[2]) >> 8)) << 2;
    e->unk27 = 0xff;
    U8(c, 0x431) = 0xcc;
    IA(c, 0x108) = IA(c, 0x109) = 0;
    I16(c, 0x2b4) = I16(c, 0x2b6) = I16(c, 0x2b8) = 0;
    IA(c, 0xfe) &= ~0x30;
    /* luz del coche y sus ruedas (+0x1B8); el original la recalcula al dibujar (80066184) */
    memcpy(B_(c) + 0x1b8, &g_DefaultLight, 0x40);
    U32(c, 0x1e4) = 0x808080;
    e->obj.light = (Light*)(B_(c) + 0x1b8);
    for (int k = 0; k < 4; k++)
        wheels[k].light = (Light*)(B_(c) + 0x1b8);
    I32(c, 0x16c) = e->obj.pos[0];
    I32(c, 0x170) = e->obj.pos[1];
    I32(c, 0x174) = e->obj.pos[2];
    Car_InitExhaust(c); /* 800779a0 */
    Engine_Create(c);   /* 80040258 */
    return c;
}
