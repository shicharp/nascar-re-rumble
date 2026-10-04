/* IA de los coches, vueltas y posiciones (parte de Race_UpdateSim 80030cac).
 * El controlador de la IA (0x2A8 bytes, entidad +0xE0) conserva la disposicion del original:
 *   +0x00 red Cnet, +0x04 banderas (0x40 sin camino, 0x400 bloquea, 0x2000 adelanta, 0x4000 esquiva, 0x8000
 *   poder), +0x08 distancia recorrida desde el nodo de salida, +0x0C intentos de recuperacion, +0x0D agresividad,
 *   +0x0E/+0x10/+0x12/+0x14 longitudes de los tramos (mundo >> 8), +0x16 bloqueo, +0x18 direccion, +0x1A pedal
 *   (< 0 freno), +0x1C/+0x1E velocidad maxima (0xB4), +0x20 velocidad deseada, +0x22 rumbo al objetivo,
 *   +0x24..+0x2E nodos (anterior, salida, destino y los 3 siguientes), +0x30 tiempo en el tramo, +0x34 marcha
 *   atras, +0x38 tiempo lento, +0x3C tiempo hasta la proxima burla, +0x40 fisica, +0x44 ruedas, +0x274 objetivo.
 * Pendiente: actores y poderes (8002d4b0 solo devuelve "nada"), mensajes y burlas (8002b920 / 80041c6c). */
#include <stdlib.h>
#include <string.h>
#include "nr.h"
#include "globals.h"
#include "track.h"
#include "cnet.h"
#include "vecmath.h"
#include "frontend.h"
#include "exe.h"
#include "racecar.h"
#include "carphys.h"
#include "carphys_int.h"
#include "ai.h"

static inline int Min_(int a, int b) { return a < b ? a : b; }

#define CTRL(e)     ((e)->ctrl)
#define NODEP(net, i) ((net) + 4 + (i) * 12)
#define NX(net, i)  ((int)(int16_t)NR_Read16(NODEP(net, i) + 6) << 8)
#define NY(net, i)  ((int)(int16_t)NR_Read16(NODEP(net, i) + 8) << 8)
#define NZ(net, i)  ((int)(int16_t)NR_Read16(NODEP(net, i) + 10) << 8)
#define NFLAGS(net, i) NR_Read16(NODEP(net, i) + 4)

static RaceCar* s_aiList[RACE_MAX_CARS];   /* DAT_800af7c4 (la ultima creada primero) */
static int      s_numAi;

static const uint8_t* Ai_Net(RaceCar* e)
{
    return PTR(CTRL(e), 0);
}

static uint8_t* Ai_Phys(RaceCar* e)
{
    return PTR(CTRL(e), 0x40);
}

static int Wrap12(int a)
{
    a &= 0xfff;
    if (a > 0x800)
        a -= 0x1000;
    return a;
}

/* 0x80000000 / (n >> 1): inverso para normalizar con un producto >> 16 */
static uint32_t Recip(int n)
{
    return (n >> 1) == 0 ? 0 : 0x80000000u / (uint32_t)(n >> 1);
}

static int MulU16(uint32_t a, uint32_t b)
{
    return (int)(((uint64_t)a * b) >> 16);
}

/* longitud del tramo entre dos nodos (mundo >> 8) */
static int16_t Ai_SegLen(const uint8_t* net, int from, int to)
{
    int d[2] = { NX(net, to) - NX(net, from), NZ(net, to) - NZ(net, from) };
    return (int16_t)(Vec_ApproxLen2(d) >> 8);
}

/* 8004bdf4: rumbo de un nodo a otro */
static int Cnet_NodeAngle(const uint8_t* net, int a, int b)
{
    int d[3] = { NX(net, b) - NX(net, a), NY(net, b) - NY(net, a), NZ(net, b) - NZ(net, a) };
    Vec_Scale((int)Recip(Vec_ApproxLen3(d)), d);
    return Math_Atan2(d[0], d[2]) & 0xfff;
}

static int Ai_FindNearest3D(const uint8_t* net, const int* pos)
{
    int best = -1, bestD = 0;
    for (int i = 0; i < Cnet_Count(net); i++)
    {
        int d[3] = { pos[0] - NX(net, i), pos[1] - NY(net, i), pos[2] - NZ(net, i) };
        int l = Vec_ApproxLen3(d);
        if (best < 0 || l < bestD)
        {
            best = i;
            bestD = l;
        }
    }
    return best;
}

/* tabla de pilotos (DAT_800aa5f3, 12 bytes por piloto): +0 nivel, +1 bloqueo */
static int Driver_Byte(int driver, int k)
{
    return Exe_U8(0x800aa5f3u + (uint32_t)driver * 0xc + (uint32_t)k);
}

/* 80030504: elige el siguiente nodo desde 'node' (puntua los 3 enlaces; atajos segun el nivel y la posicion).
 * Devuelve el nodo, o -2 si no hay salida. */
static int Ai_ChooseNext(RaceCar* e, int node)
{
    uint8_t* ctrl = CTRL(e);
    const uint8_t* net = Ai_Net(e);
    uint32_t l = NR_Read32(NODEP(net, node));
    int link[3] = { (int)(l >> 22), (int)((l >> 12) & 0x3ff), (int)((l >> 2) & 0x3ff) };
    if (link[1] == 0x3ff && link[2] == 0x3ff)
        return link[0];
    const GridCar* g = &g_RaceGrid.car[e->id - 1000];
    int skill = Driver_Byte(g->driver, 0);
    int pos = Ai_Phys(e) ? U8(Ai_Phys(e), 0x31a) : 0;
    int score[3];
    for (int k = 0; k < 3; k++)
    {
        int n = link[k];
        if (n == 0x3ff)
            score[k] = -4;
        else if (n == node || n == I16(ctrl, 0x26))
            score[k] = -2;
        else if (n == I16(ctrl, 0x24))
            score[k] = -1;
        else if (!(NODEP(net, n)[0] & 2) || skill > 0x37)
        {
            int r = Rand_A() & 0xffff;
            uint16_t f = NFLAGS(net, n);
            int adjust = 0;
            if (f & 0x800)
            {
                if (g_GameInfo->powerUps == 0)
                {
                    r = 0;
                    adjust = (f & 0x2000) != 0;
                }
                else
                    adjust = 1;
            }
            else
                adjust = (f & 0x2000) != 0;
            if (adjust)
            {
                switch (g->cls)
                {
                case 1: r = (r - 0x8000) - (g_NumCars - pos) * 0x1000; break;
                case 0: r = 0; break;
                case 2: r = r + 0x4000 + (pos << 12); break;
                default: r = r + 0xf500 + (pos << 9); break;
                }
                if (skill > 0x37)
                    r = 0xffff;
            }
            if (r > 0xffff)
                r = 0xffff;
            if (r < 0)
                r = 0;
            score[k] = r;
        }
        else
            score[k] = -3;
    }
    int best = -4, idx = -1;
    for (int k = 2; k >= 0; k--)
        if (score[k] >= -1 && best <= score[k] && (score[k] != best || !(Rand_A() & 1)))
        {
            best = score[k];
            idx = k;
        }
    if (idx < 0)
        return best == -4 ? -2 : -1;
    return link[idx];
}

/* 80030868 */
static void Ai_QueueNext(RaceCar* e, int from, int16_t* outNode, int16_t* outLen)
{
    int n = Ai_ChooseNext(e, from);
    *outNode = (int16_t)n;
    if (n >= 0)
        *outLen = Ai_SegLen(Ai_Net(e), from, n);
}

/* 8002ea48: vuelve a la red (y a la pista si se ha salido o lleva varios intentos) */
static void Ai_Recover(RaceCar* e)
{
    uint8_t* ctrl = CTRL(e);
    uint8_t* c = Ai_Phys(e);
    if (I32(c, 0x330) != 0)
        return;
    int g = Track_FindSurface(&e->tpos, e->obj.pos, NULL, 0);
    int lim = e->speed > 0x14 ? 2 : 1;
    if ((g == TRACK_NO_SURFACE || lim < U8(ctrl, 0xc)) && !(U16(c, 0x33a) & 0x82))
    {
        Car_Reset(e, 0);
        U8(ctrl, 0xc) = 0xff;
    }
    const uint8_t* net = Ai_Net(e);
    int n = Ai_FindNearest3D(net, e->obj.pos);
    if (n != I16(ctrl, 0x26))
        I16(ctrl, 0x24) = I16(ctrl, 0x26) = (int16_t)n;
    int nx = Ai_ChooseNext(e, n);
    if (nx != I16(ctrl, 0x28))
    {
        I16(ctrl, 0x28) = (int16_t)nx;
        I16(ctrl, 0x2a) = I16(ctrl, 0x2c) = I16(ctrl, 0x2e) = -1;
    }
    I32(ctrl, 0x30) = 0;
    U8(ctrl, 0xc)++;
    if (I16(ctrl, 0x28) >= 0)
        I16(ctrl, 0xe) = Ai_SegLen(net, I16(ctrl, 0x26), I16(ctrl, 0x28));
}

/* 80030928: avanza por la red al pasar cada nodo y mantiene 3 nodos por delante */
static void Ai_UpdatePath(RaceCar* e)
{
    uint8_t* ctrl = CTRL(e);
    const uint8_t* net = Ai_Net(e);
    int from = I16(ctrl, 0x26), to = I16(ctrl, 0x28);
    int d[2] = { NX(net, from) - e->obj.pos[0], NZ(net, from) - e->obj.pos[2] };
    I32(ctrl, 8) = Vec_ApproxLen2(d);
    int skip = 0;
    if (g_RaceTime > 900 && to >= 0 && I16(ctrl, 0xe) <= (I32(ctrl, 8) >> 8))
    {
        int d2[2] = { NX(net, to) - e->obj.pos[0], NZ(net, to) - e->obj.pos[2] };
        if (Vec_ApproxLen2(d2) <= I32(ctrl, 8))
        {
            if ((I16(ctrl, 0xe) << 1) <= (I32(ctrl, 8) >> 8))
            {
                int diff = Wrap12(Cnet_NodeAngle(net, from, to) - I16(Ai_Phys(e), 0x432));
                if (Abs_(diff) > 0x200)
                {
                    Ai_Recover(e);
                    U8(ctrl, 0xc) = 0;
                    skip = 1;
                }
            }
            if (!skip)
            {
                if (I16(ctrl, 0x2a) < 0)
                {
                    Ai_QueueNext(e, to, (int16_t*)(ctrl + 0x2a), (int16_t*)(ctrl + 0x10));
                    if (I16(ctrl, 0x2a) < 0)
                    {
                        U32(ctrl, 4) |= 0x40;
                        if (I16(ctrl, 0x2a) != -2)
                        {
                            e->speed = 0;
                            I32(ctrl, 8) = I16(ctrl, 0xe) << 8;
                        }
                        return;
                    }
                }
                memmove(ctrl + 0x24, ctrl + 0x26, 10);
                I16(ctrl, 0x2e) = -1;
                I32(ctrl, 8) = 0;
                I32(ctrl, 0x30) = 0;
                U8(ctrl, 0xc) = 0;
                I16(ctrl, 0xe) = I16(ctrl, 0x10);
                I16(ctrl, 0x10) = I16(ctrl, 0x12);
                I16(ctrl, 0x12) = I16(ctrl, 0x14);
            }
        }
    }
    int limit = g_GameInfo->unk08 == 0 ? 0x960 : 0x4b0;
    if (I32(ctrl, 0x30) > limit)
        Ai_Recover(e);
    if (I16(ctrl, 0x28) < 0)
        return;
    if (I16(ctrl, 0x2a) < 0)
        Ai_QueueNext(e, I16(ctrl, 0x28), (int16_t*)(ctrl + 0x2a), (int16_t*)(ctrl + 0x10));
    if (I16(ctrl, 0x2a) < 0)
        return;
    if (I16(ctrl, 0x2c) < 0)
        Ai_QueueNext(e, I16(ctrl, 0x2a), (int16_t*)(ctrl + 0x2c), (int16_t*)(ctrl + 0x12));
    if (I16(ctrl, 0x2c) < 0)
        return;
    if (I16(ctrl, 0x2e) < 0)
        Ai_QueueNext(e, I16(ctrl, 0x2c), (int16_t*)(ctrl + 0x2e), (int16_t*)(ctrl + 0x14));
}

/* 8002dbd0: objetivo sobre la trazada, adelantado segun lo recorrido del tramo y la velocidad */
static void Ai_DefaultTarget(RaceCar* e)
{
    uint8_t* ctrl = CTRL(e);
    const uint8_t* net = Ai_Net(e);
    int to = I16(ctrl, 0x28);
    if (to < 0)
        return;
    int top = I16(ctrl, 0x1e);
    if (I16(ctrl, 0x2a) < 0 || e->speed < (top >> 3))
    {
        I32(ctrl, 0x274) = NX(net, to);
        I32(ctrl, 0x278) = NY(net, to);
        I32(ctrl, 0x27c) = NZ(net, to);
        return;
    }
    int len = I16(ctrl, 0xe), t = 0;
    if (len > 1)
    {
        int prog = I32(ctrl, 8);
        uint32_t k = Recip(len * 0x100);
        t = prog < 1 ? -MulU16((uint32_t)-prog, k) : MulU16((uint32_t)prog, k);
        if (t < 0)
            t = 0;
        if (e->speed < (top >> 2))
        {
            if (t > 0x8000)
                t = 0x8000;
        }
        else if ((top >> 1) < e->speed)
            t += 0x3333 + U8(ctrl, 0xd) * 0x40;
    }
    int base = to, end = I16(ctrl, 0x2a);
    if (t > 0x10000)
    {
        int ok = 0;
        if (I16(ctrl, 0x2c) >= 0)
        {
            base = I16(ctrl, 0x2a);
            end = I16(ctrl, 0x2c);
            t -= 0x10000;
            ok = t <= 0x10000;
        }
        if (!ok)
            t = 0x10000;
    }
    I32(ctrl, 0x274) = NX(net, base) + M16(t, NX(net, end) - NX(net, base));
    I32(ctrl, 0x278) = NY(net, base) + M16(t, NY(net, end) - NY(net, base));
    I32(ctrl, 0x27c) = NZ(net, base) + M16(t, NZ(net, end) - NZ(net, base));
}

/* 8002d7c8: coche mas cercano delante (y el mas lejano detras) segun la posicion en la red; los humanos por
 * distancia y angulo. Devuelve el que cuenta (el de delante salvo que el de detras este mas cerca). */
static RaceCar* Ai_FindCars(RaceCar* e, RaceCar** ahead, RaceCar** behind, int* aheadD, int* behindD)
{
    uint8_t* m = CTRL(e);
    *ahead = *behind = NULL;
    for (int i = 0; i < s_numAi; i++)
    {
        RaceCar* o = s_aiList[i];
        if (o == e)
            continue;
        uint8_t* oc = CTRL(o);
        int rel;
        int16_t oto = I16(oc, 0x28), mto = I16(m, 0x28);
        if (oto == mto)
            rel = I32(oc, 8) - I32(m, 8);
        else if (oto == I16(m, 0x26))
            rel = (I32(oc, 8) - I16(oc, 0xe) * 0x100) - I32(m, 8);
        else if (oto != I16(m, 0x24))
        {
            if (I16(oc, 0x26) == mto)
                rel = I32(oc, 8) + (I16(m, 0xe) * 0x100 - I32(m, 8));
            else if (I16(oc, 0x24) == mto && I16(m, 0x2a) >= 0)
                rel = I32(oc, 8) + (I16(m, 0xe) * 0x100 - I32(m, 8)) + I16(m, 0x10);
            else
                continue;
        }
        else if (I16(oc, 0x2a) >= 0)
            rel = (-(I16(oc, 0xe) * 0x100 - I32(oc, 8)) - I32(m, 8)) - I16(oc, 0x10);
        else
            continue;
        if (rel < -0x1999)
        {
            if (!*behind || Abs_(*behindD) < Abs_(rel))
            {
                *behind = o;
                *behindD = rel;
            }
        }
        else if (!*ahead || Abs_(rel) < Abs_(*aheadD))
        {
            *ahead = o;
            *aheadD = rel;
        }
    }
    for (int h = 0; h < 2; h++)
    {
        RaceCar* o = g_Humans[h];
        if (!o)
            continue;
        int d[2] = { o->obj.pos[0] - e->obj.pos[0], o->obj.pos[2] - e->obj.pos[2] };
        int dist = Vec_ApproxLen2(d);
        if (dist >= 0x20001)
            continue;
        int ang = 0;
        if (dist > 1)
        {
            int k = (int)Recip(dist);
            d[0] = M16(d[0], k);
            d[1] = M16(d[1], k);
            ang = Wrap12(I16(Ai_Phys(e), 0x432) - (Math_Atan2(d[0], d[1]) & 0xfff));
        }
        if (Abs_(ang) < 0x401)
        {
            if (!*ahead || dist < Abs_(*aheadD))
            {
                *ahead = o;
                *aheadD = dist;
            }
        }
        else if (!*behind || Abs_(*behindD) < dist)
        {
            *behind = o;
            *behindD = -dist;
        }
    }
    if (!*ahead)
        return *behind;
    if (*behind && Abs_(*behindD) < Abs_(*aheadD))
        return *behind;
    return *ahead;
}

/* 8002de7c: objetivo de la IA: la trazada, o adelantar / bloquear a otro coche */
static void Ai_UpdateTarget(RaceCar* e, int dt)
{
    uint8_t* ctrl = CTRL(e);
    uint8_t* c = Ai_Phys(e);
    const uint8_t* net = Ai_Net(e);
    if (I32(ctrl, 0x3c) >= 0)
        I32(ctrl, 0x3c) -= dt;
    int to = I16(ctrl, 0x28);
    if (to < 0)
        return;
    const uint8_t* node = NODEP(net, to);
    int lane = (NR_Read16(node + 4) & 0x3f) << 9;
    /* 8002d4b0 (poderes y actores cercanos): pendiente, sin actores */
    U32(ctrl, 4) &= 0xffff0bff;
    RaceCar *ahead, *behind;
    int aheadD = 0, behindD = 0;
    RaceCar* other = Ai_FindCars(e, &ahead, &behind, &aheadD, &behindD);
    if (other && !(node[0] & 1))
    {
        if (ahead == other)
        {
            if (aheadD < 0x6666 && (ahead->type == 1 || !(U32(CTRL(ahead), 4) & 0x2000)) &&
                ahead->speed < e->speed && ahead->speed > 10 &&
                (ahead->type == 1 || I32(CTRL(ahead), 0x34) < 0) && (I16(ctrl, 0x1e) >> 2) < e->speed &&
                Abs_(I16(ctrl, 0x22) - I16(c, 0x432)) < 200)
            {
                int side = (lane >> 1) + (lane >> 2), ox = side, oz = side;
                if (ahead->type == 1)
                {
                    if (I32(ctrl, 0x3c) < 0 && I32(c, 0x330) == 0)
                        I32(ctrl, 0x3c) = (Rand_B() & 0x3fff) + 0x1194;   /* burla 80041c6c: pendiente */
                    if (ahead->obj.rot[6] > 0)
                        oz = -side;
                    if (ahead->obj.rot[8] > 0)
                        ox = -side;
                }
                else
                {
                    if (I32(CTRL(ahead), 0x274) - ahead->obj.pos[0] > 0)
                        oz = -side;
                    if (I32(CTRL(ahead), 0x27c) - ahead->obj.pos[2] < 0)
                        ox = -side;
                }
                int k = 0x3333 - aheadD;
                U32(ctrl, 4) |= 0x2000;
                if (k < -0x3333)
                    k = -0x3333;
                if (k > 0x3333)
                    k = 0x3333;
                if (ahead->type == 0x11)
                    k += k >> 1;
                else if ((U16(c, 0x33a) & 0x200) && k > 0)
                    k = 0;
                I32(ctrl, 0x274) = ox + ahead->obj.pos[0] + M16(ahead->obj.rot[6], k);
                I32(ctrl, 0x27c) = oz + ahead->obj.pos[2] + M16(ahead->obj.rot[8], k);
                I32(ctrl, 0x278) = (int)(int16_t)NR_Read16(node + 8) << 8;
                return;
            }
        }
        else if (behind && behind->type == 1 && e->speed <= behind->speed &&
                 -(U8(ctrl, 0x16) * 0x100 + 0x4ccc) < behindD && behind->team != e->team)
        {
            int side = lane >> 3, oz = side, ox = side;
            if (behind->obj.rot[6] > 0)
                oz = -side;
            if (behind->obj.rot[8] > 0)
                ox = -side;
            U32(ctrl, 4) |= 0x400;
            Ai_DefaultTarget(e);
            I32(ctrl, 0x274) += ox;
            I32(ctrl, 0x27c) += oz;
            return;
        }
    }
    Ai_DefaultTarget(e);
}

/* 8002f094: distancia por la pista (en puntos de TCRV) entre dos coches; > 0 si el primero va delante */
int Car_TrackDeltaPublic(uint8_t* p1, uint8_t* p2)
{
    if (!p1 || !p2 || !g_Track)
        return 0;
    int len = g_Track->numCurve;
    int v = I32(p1, 0x328) != I32(p2, 0x328) ? len * (I32(p1, 0x328) - I32(p2, 0x328)) : 0;
    int d1 = ((RaceCar*)ENT(p1))->tpos.curve - I16(p1, 0x318);
    if ((len >> 1) < Abs_(d1))
        d1 += d1 < 1 ? len : -len;
    int v3 = v + (I32(p1, 0x324) - I32(p2, 0x324)) + d1;
    int d2 = ((RaceCar*)ENT(p2))->tpos.curve - I16(p2, 0x318);
    if ((len >> 1) < Abs_(d2))
        return v3 - (d2 + (d2 > 0 ? -len : len));
    return v3 - d2;
}

/* 8002f1a0: distancia al humano mas cercano por la pista */
static int Ai_RubberBand(RaceCar* e)
{
    int best = 0, found = 0;
    for (int h = 0; h < 2; h++)
        if (g_Humans[h])
        {
            int v = Car_TrackDeltaPublic(Ai_Phys(e), g_Humans[h]->phys);
            if (!found || Abs_(v) < Abs_(best))
            {
                best = v;
                found = 1;
            }
        }
    return best;
}

/* 8002f4ac: conduccion de la IA: direccion hacia el objetivo, velocidad segun la curva y la red, marcha atras
 * si se atasca contra un muro y potencia segun la distancia al jugador */
static void Ai_Drive(RaceCar* e, int dt)
{
    uint8_t* ctrl = CTRL(e);
    uint8_t* c = Ai_Phys(e);
    const uint8_t* net = Ai_Net(e);
    if (g_RaceTime < 900)
    {
        I16(ctrl, 0x18) = 0;
        I16(ctrl, 0x1a) = 0x100;
        return;
    }
    int heading = I16(c, 0x432);
    int dx = I32(ctrl, 0x274) - e->obj.pos[0], dz = I32(ctrl, 0x27c) - e->obj.pos[2];
    int hi = Abs_(dx), lo = Abs_(dz);
    if (hi < lo)
    {
        int t = hi;
        hi = lo;
        lo = t;
    }
    if ((hi >> 1) < lo)
    {
        int t = lo * 0x6a;
        lo = hi - lo;
        hi = (hi * 0x4b + t) >> 7;
    }
    int len = (hi + (lo >> 2)) - (lo >> 6);
    int ang;
    if (len < 2)
        ang = heading;
    else
    {
        int k = (int)Recip(len);
        ang = Math_Atan2(M16(dx, k), M16(dz, k)) & 0xfff;
    }
    I16(ctrl, 0x22) = (int16_t)ang;
    int diff = Wrap12(ang - heading);
    int ad = Abs_(diff);
    int s = I16(ctrl, 0x1e), top = s;
    int to = I16(ctrl, 0x28) < 0 ? I16(ctrl, 0x26) : I16(ctrl, 0x28);
    uint16_t f = NFLAGS(net, to);
    if (!(f & 0x8000))
    {
        if (f & 0x4000)
            top = Min_(top, s >> 1);
    }
    else
        top = Min_(top, (f & 0x4000) ? (s >> 2) : (s >> 1) + (s >> 2));
    switch (f & 3)
    {
    case 1: ad += ad >> 2; break;
    case 2: ad += ad >> 1; break;
    case 3: ad <<= 1; break;
    }
    int a2 = ad;
    if (U16(c, 0x33a) & 0x50)
    {
        a2 = ad >> 1;
        if (U16(c, 0x33a) & 0x10)
            a2 = ad + a2;
    }
    int want;
    if (a2 >= 0x400)
        want = (top >> 2) + (top >> 3);
    else if (a2 >= 0x300)
        want = top >> 1;
    else if (a2 >= 0x200)
        want = (top >> 1) + (top >> 3);
    else if (a2 >= 0x180)
        want = (top >> 1) + (top >> 2);
    else if (a2 >= 0x100)
        want = (top >> 1) + (top >> 2) + (top >> 3);
    else if (a2 >= 0xc0)
        want = (top >> 1) + (top >> 2) + (top >> 3) + (top >> 4);
    else
        want = top;
    if (a2 < 0x20 && U8(c, 0x338) == 3)
        I16(c, 0x33c) = 0;
    I16(ctrl, 0x20) = (int16_t)want;
    if (diff > 0x180)
        diff = 0x180;
    if (diff < -0x180)
        diff = -0x180;
    int steer = diff;
    if (I32(ctrl, 0x38) > 0x5dc)
    {
        I32(ctrl, 0x38) = -1;
        Ai_Recover(e);
    }
    int wall = U8(c, 0x180) & 1;
    int back = I32(ctrl, 0x34);
    int reverse = 0;
    if (back < 0)
    {
        int adiff = Abs_(ang - heading);
        if ((back < -299 || (back < -0x95 && wall)) && wall && ((e->speed < 0x28 && adiff > 100) || adiff > 400))
        {
            I32(ctrl, 0x34) = 200;
            reverse = 1;
        }
    }
    else
        reverse = 1;
    if (reverse)
    {
        if (I32(ctrl, 0x34) < 100 && wall)
            I32(ctrl, 0x34) = 0;
        else
            steer = -steer;
    }
    I16(ctrl, 0x18) = (U32(ctrl, 4) & 0x40) ? 0 : (int16_t)steer;
    if (U32(ctrl, 4) & 0x40)
        I16(ctrl, 0x1a) = (int16_t)0xf000;
    else if (I32(ctrl, 0x34) >= 0)
    {
        I8(c, 0x3f4) = 0;
        I16(ctrl, 0x1a) = 0x1000;
    }
    else
    {
        if (I8(c, 0x3f4) == 0)
            I8(c, 0x3f4) = 1;
        int tgt = I16(ctrl, 0x20);
        if (g_GameInfo->unk08 < 3 || g_GameInfo->powerUps == 3)
        {
            int rb = Ai_RubberBand(e);
            int L = g_Track ? g_Track->numCurve : 0, q = L >> 4;
            int base = U8(c, 0x314);
            if (rb < 1)
            {
                int mx = 0xff;
                if (g_GameInfo->powerUps != 3)
                    mx = Min_(base + 10, 0xff);
                if (rb < -q)
                    U8(c, 0x431) = (uint8_t)mx;
                else
                    U8(c, 0x431) = (uint8_t)(base + Div_((q + rb) * (mx - base), q));
            }
            else
            {
                if (q < rb)
                    U8(c, 0x431) = 0;
                else
                    U8(c, 0x431) = (uint8_t)Div_((q - rb) * base, q);
                if (g_GameInfo->unk08 == 0 && (L >> 6) < rb)
                {
                    tgt >>= 3;
                    if (tgt < 0x15)
                        tgt = 0x15;
                }
            }
        }
        else
            U8(c, 0x431) = U8(c, 0x314);
        if (U32(ctrl, 4) & 0x4000)
        {
            int lim = (I16(ctrl, 0x1e) >> 1) + (I16(ctrl, 0x1e) >> 2);
            if (lim < tgt)
                tgt = lim;
        }
        if (e->speed < tgt)
            I16(ctrl, 0x1a) = 0x1000;
        else if (e->speed - tgt > 10 || tgt < 10)
            I16(ctrl, 0x1a) = (int16_t)0xf000;
    }
    if (I32(ctrl, 0x34) > -1000)
        I32(ctrl, 0x34) -= dt;
    int slow = I16(ctrl, 0x1e) >> 2;
    if (I32(ctrl, 0x38) < 0)
    {
        if (e->speed < slow)
            I32(ctrl, 0x38) = dt;
    }
    else
        I32(ctrl, 0x38) = slow <= e->speed ? -1 : I32(ctrl, 0x38) + dt;
    I32(ctrl, 0x30) += dt;
}

/* ---------------------------------------------------------------- creacion */

/* AiCar_Create 8002cfbc, antes de crear la fisica */
void Ai_InitCar(RaceCar* e)
{
    uint8_t* ctrl = CTRL(e);
    const GridCar* g = &g_RaceGrid.car[e->id - 1000];
    I32(ctrl, 0x34) = -1000;
    I32(ctrl, 0x30) = -900;
    I32(ctrl, 0x38) = -900;
    U8(ctrl, 0xc) = 0;
    I32(ctrl, 0x3c) = (Rand_B() & 0x3fff) + 0x1194;
    int lvl = Driver_Byte(g->driver, 0);
    switch (g->cls)
    {
    case 0: U8(ctrl, 0xd) = (uint8_t)lvl; break;
    case 1: U8(ctrl, 0xd) = (uint8_t)(lvl + 0x40); break;
    case 2: U8(ctrl, 0xd) = (uint8_t)(lvl - 0x80); break;
    default: U8(ctrl, 0xd) = (uint8_t)(lvl - 0x40); break;
    }
    U8(ctrl, 0x16) = g_GameInfo->powerUps == 3 ? 0x3f : (uint8_t)Driver_Byte(g->driver, 1);
    const uint8_t* net = g_NumNets ? g_Nets[0] : NULL;
    SETPTR(ctrl, 0, net);
    U32(ctrl, 4) = 0;
    I32(ctrl, 8) = 0;
    e->speed = 0;
    I16(ctrl, 0x1c) = I16(ctrl, 0x1e) = I16(ctrl, 0x20) = 0xb4;
    I16(ctrl, 0x2a) = I16(ctrl, 0x2c) = I16(ctrl, 0x2e) = -1;
    SETPTR(ctrl, 0x40, NULL);
    if (net && Cnet_Count(net) > 0)
    {
        int n = Cnet_FindNearest(net, g_StartPos, 1);
        I16(ctrl, 0x24) = I16(ctrl, 0x26) = (int16_t)n;
        int nx = Ai_ChooseNext(e, n);
        I16(ctrl, 0x28) = (int16_t)nx;
        if (nx < 0)
            U32(ctrl, 4) |= 0x40;
        else
        {
            e->dir[0] = NX(net, nx) - NX(net, n);
            e->dir[1] = NY(net, nx) - NY(net, n);
            e->dir[2] = NZ(net, nx) - NZ(net, n);
            int l = Vec_ApproxLen3(e->dir);
            I16(ctrl, 0xe) = (int16_t)(l >> 8);
            Vec_Scale((int)Recip(l), e->dir);
        }
    }
    else
    {
        I16(ctrl, 0x24) = I16(ctrl, 0x26) = I16(ctrl, 0x28) = -1;
        U32(ctrl, 4) |= 0x40;
    }
    I32(ctrl, 0x274) = e->obj.pos[0];
    I32(ctrl, 0x278) = e->obj.pos[1];
    I32(ctrl, 0x27c) = e->obj.pos[2];
}

/* AiCar_Create 8002cfbc, despues de crear la fisica: cambio automatico y potencia segun el nivel */
void Ai_InitPhys(RaceCar* e)
{
    uint8_t* c = e->phys;
    const GridCar* g = &g_RaceGrid.car[e->id - 1000];
    SETPTR(CTRL(e), 0x40, c);
    I32(c, 0x428) = 1;
    static const uint8_t base[4] = { 0xb4, 0xbc, 0xc2, 0xca };
    int p = base[g->cls < 3 ? g->cls : 3] + ((Driver_Byte(g->driver, 0) >> 3) & 7);
    if (g->flag & 1)
    {
        p += (4 - g->cls) * 4;
        if (p > 0xff)
            p = 0xff;
    }
    U8(c, 0x314) = (uint8_t)p;
    for (int i = s_numAi; i > 0; i--)
        s_aiList[i] = s_aiList[i - 1];
    s_aiList[0] = e;
    s_numAi++;
    e->nextAi = s_numAi > 1 ? s_aiList[1] : NULL;
}

void Ai_Reset(void)
{
    s_numAi = 0;
}

/* ---------------------------------------------------------------- vueltas y posiciones */

/* 8003014c: progreso por la pista (puntos de TCRV) y vueltas */
void Car_UpdateLap(uint8_t* c, int curve)
{
    if (curve < 0 || !g_Track || g_Track->numCurve <= 0)
        return;
    int len = g_Track->numCurve;
    int last = I16(c, 0x318);
    int d;
    if (last < 0)
    {
        d = curve - g_StartCurve;
        if ((len >> 1) < Abs_(d))
            d += d < 0 ? len : -len;
    }
    else
    {
        int seg = len / 6;
        if (curve == last)
            return;
        d = curve - last;
        if (d < 0)
        {
            if (last < len - seg || seg <= curve)
                return;
            d = curve + (len - last);
        }
        if (d < 1 || seg < d)
            return;
        d += I32(c, 0x324);
        if (d >= len)
        {
            int t = g_RaceTime - I32(c, 0x32c);
            if (I32(c, 0x334) == 0 || t < I32(c, 0x334))
                I32(c, 0x334) = t;
            int slot = ((RaceCar*)ENT(c))->id - 1000;
            int rec = (t + 1) / 3;
            if (rec > 0xffff)
                rec = 0xffff;
            if (slot >= 0 && slot < 6 && (g_CarRecords[slot].b == 0 || rec < g_CarRecords[slot].b))
                g_CarRecords[slot].b = rec;   /* mejor vuelta (DAT_800b12b4 + id * 0x18) */
            I32(c, 0x328)++;
            if (g_Settings.laps <= I32(c, 0x328))
                I32(c, 0x330) = g_RaceTime - 900;   /* meta (fin de carrera 8002b920 / 80074574: pendiente) */
            d -= len;
            I32(c, 0x32c) = g_RaceTime;
        }
    }
    I32(c, 0x324) = d;
    I16(c, 0x318) = (int16_t)curve;
}

/* 8002fc9c: ordena los coches por posicion (vueltas, progreso, meta) y reordena g_CarPhys */
void Race_UpdatePositions(int dt)
{
    uint8_t* order[RACE_MAX_CARS];
    int n = 0;
    uint8_t* cand[RACE_MAX_CARS];
    int nc = 0;
    for (int h = 0; h < 2; h++)
        if (g_Humans[h] && g_Humans[h]->phys)
            cand[nc++] = g_Humans[h]->phys;
    for (int i = 0; i < s_numAi; i++)
        if (s_aiList[i]->phys)
            cand[nc++] = s_aiList[i]->phys;
    for (int k = 0; k < nc; k++)
    {
        uint8_t* p = cand[k];
        int at = 0;
        for (; at < n; at++)
        {
            uint8_t* x = order[at];
            int xt = I32(x, 0x330), pt = I32(p, 0x330);
            if (xt == 0)
            {
                if (pt != 0 || I32(x, 0x328) < I32(p, 0x328))
                    break;
                if (I32(x, 0x328) == I32(p, 0x328) &&
                    (I32(x, 0x324) < I32(p, 0x324) || (I32(p, 0x324) == I32(x, 0x324) && U8(p, 0x31a) < U8(x, 0x31a))))
                    break;
            }
            else if (pt != 0 && pt <= xt && (xt != pt || U8(p, 0x31a) <= U8(x, 0x31a)))
                break;
        }
        for (int j = n; j > at; j--)
            order[j] = order[j - 1];
        order[at] = p;
        n++;
    }
    for (int u = 0; u < n; u++)
    {
        uint8_t* p = order[u];
        if (U8(p, 0x31a) == u)
            I32(p, 0x31c) += dt;
        else
        {
            U8(p, 0x31b) = U8(p, 0x31a);
            I32(p, 0x320) = I32(p, 0x31c);
            U8(p, 0x31a) = (uint8_t)u;
            I32(p, 0x31c) = 0;
        }
        g_CarPhys[u] = p;
    }
    g_NumCars = n;
}

/* Parte de Race_UpdateSim 80030cac: ruta, objetivo y conduccion de la IA, y sus mandos a la fisica */
void Ai_UpdateAll(int dt)
{
    for (int i = 0; i < s_numAi; i++)
        if (I32(Ai_Phys(s_aiList[i]), 0x330) == 0)
            Ai_UpdatePath(s_aiList[i]);
    for (int i = 0; i < s_numAi; i++)
        if (I32(Ai_Phys(s_aiList[i]), 0x330) == 0)
            Ai_UpdateTarget(s_aiList[i], dt);
    for (int i = 0; i < s_numAi; i++)
        if (I32(Ai_Phys(s_aiList[i]), 0x330) == 0)
            Ai_Drive(s_aiList[i], dt);
    for (int i = 0; i < s_numAi; i++)
    {
        RaceCar* e = s_aiList[i];
        uint8_t* c = Ai_Phys(e);
        if (I32(c, 0x330) != 0)
            continue;
        int pedal = I16(CTRL(e), 0x1a);
        if (pedal < 0)
            Car_Brake(c, 0x200, -pedal);
        else
            Car_Throttle(c, pedal);
        Car_Steer(c, 1, I16(CTRL(e), 0x18));
    }
}

void Ai_UpdateLaps(void)
{
    for (int i = 0; i < s_numAi; i++)
        Car_UpdateLap(Ai_Phys(s_aiList[i]), s_aiList[i]->tpos.curve);
    for (int h = 0; h < 2; h++)
        if (g_Humans[h] && g_Humans[h]->phys)
            Car_UpdateLap(g_Humans[h]->phys, g_Humans[h]->tpos.curve);
}
