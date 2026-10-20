/* Vehiculos de la pista (tACT tipo 5): siguen una red Cnet (la tercera entrada de su aRSL) de nodo en nodo, con
 * los giros suavizados en las esquinas, a una altura fija sobre la red (dirigibles y helicopteros sobre el
 * circuito; en MC3 tambien vehiculos a ras de suelo). Ver vehicle.h.
 *
 * Campos (0x15C bytes; como en el original, por desplazamiento):
 *   +0xE0 animacion (AnimState), +0xF4 red, +0xF8 / +0xFA / +0xFC desvio de la curva del giro (>> 8 del mundo),
 *   +0xFE velocidad inicial, +0x100 avance en el tramo (unidades de la red << 8), +0x104 / +0x106 largo del tramo
 *   y del siguiente, +0x108 medio giro, +0x10A altura / 16, +0x10C / +0x110 giro de rumbo / cabeceo,
 *   +0x10E 1 / medio giro (16384 / +0x108), +0x112, +0x114 velocidad de crucero, +0x116 velocidad deseada,
 *   +0x118 frenada en las curvas (0x1000 = ninguna), +0x11A / +0x11C aceleracion y frenada, +0x11E probabilidad
 *   (de 4096) de ignorar el modo de ruta, +0x120 banderas, +0x124 empujon (tics, el signo dice quien empujo),
 *   +0x126 nodo objetivo, +0x128 siguiente nodo, +0x12A el de despues (-1 sin elegir), +0x12C actual, +0x12E
 *   anterior, +0x130..+0x134 rumbo (inicio del giro, actual, destino), +0x136..+0x13A cabeceo (igual), +0x13C /
 *   +0x13E alabeo (maximo del giro y actual), +0x140 seno del rumbo, +0x144 coseno del cabeceo, +0x148 coseno del
 *   rumbo, +0x14C direccion del tramo siguiente (16.16), +0x158 secuencia de muerte (0xFF ninguna), +0x159,
 *   +0x15A muerte (0 viva, 1 animandose, 2 acabada).
 *   Banderas +0x120 (tACT +0x24 | tACT +0x36 << 24): 1 puede volver al nodo anterior, 2 frena a la cuarta en las
 *   curvas, 8 no la empujan otros vehiculos, 0x10 en el suelo (despega hasta la altura), 0x20 se alabea en los
 *   giros, 0x40 parada (aterriza), 0x80 tambien el choque comun, 0x100 sin cabeceo (rumbo plano), 0x200 se
 *   apoya en la normal del suelo, 0x400 al llegar a un extremo da media vuelta, 0x800 gira de golpe al cruzar el
 *   nodo, 0x1000 rumbo fijo, 0x2000 se cae al quedarse sin vida, 0x8000 desaparece al final de la ruta, 0x10000
 *   frenando por la curva; bits 24-27 modo de ruta (orden - 1: 6 / 14 hacia el nodo objetivo), 28-29 fase del
 *   giro (1 entrando, 2 saliendo), 30-31 caida (1 cayendo, 3 estrellado).
 * Ningun vehiculo de las pistas usa 0x10, 0x40, 0x2000 ni secuencia de muerte; los de MC3 (ids 19, 89, 90)
 * usan 1, 8, 0x400, 0x800 y 0x1000. */
#include <stdlib.h>
#include <string.h>
#include "nr.h"
#include "model.h"
#include "cnet.h"
#include "vecmath.h"
#include "carphys_int.h"
#include "fx.h"
#include "vehicle.h"

#define V_NET(a)     (*(const uint8_t**)((uint8_t*)(a) + 0xf4))
#define V_CURVEV(a)  ((int16_t*)((uint8_t*)(a) + 0xf8))
#define V_BASESPD(a) A16(a, 0xfe)
#define V_DIST(a)    A32(a, 0x100)
#define V_SEGLEN(a)  A16(a, 0x104)
#define V_NEXTLEN(a) A16(a, 0x106)
#define V_TURNLEN(a) A16(a, 0x108)
#define V_HEIGHT(a)  A16(a, 0x10a)
#define V_DYAW(a)    A16(a, 0x10c)
#define V_TURNINV(a) A16(a, 0x10e)
#define V_DPITCH(a)  A16(a, 0x110)
#define V_U112(a)    A16(a, 0x112)
#define V_CRUISE(a)  A16(a, 0x114)
#define V_TSPEED(a)  A16(a, 0x116)
#define V_TURNK(a)   A16(a, 0x118)
#define V_ACCEL(a)   A16(a, 0x11a)
#define V_DECEL(a)   A16(a, 0x11c)
#define V_CHANCE(a)  A16(a, 0x11e)
#define V_FL(a)      (*(uint32_t*)((uint8_t*)(a) + 0x120))
#define V_BUMP(a)    A16(a, 0x124)
#define V_TARGET(a)  A16(a, 0x126)
#define V_NEXT(a)    A16(a, 0x128)
#define V_NEXT2(a)   A16(a, 0x12a)
#define V_CUR(a)     A16(a, 0x12c)
#define V_PREV(a)    A16(a, 0x12e)
#define V_YAW0(a)    A16(a, 0x130)
#define V_YAW(a)     A16(a, 0x132)
#define V_YAWT(a)    A16(a, 0x134)
#define V_PIT0(a)    A16(a, 0x136)
#define V_PIT(a)     A16(a, 0x138)
#define V_PITT(a)    A16(a, 0x13a)
#define V_ROLLT(a)   A16(a, 0x13c)
#define V_ROLL(a)    A16(a, 0x13e)
#define V_HEAD(a)    ((int*)((uint8_t*)(a) + 0x140))   /* seno del rumbo, coseno del cabeceo, coseno del rumbo */
#define V_NDIR(a)    ((int*)((uint8_t*)(a) + 0x14c))
#define V_DEATHSEQ(a) A8(a, 0x158)
#define V_ASK10(a)   A8(a, 0x159)
#define V_DYING(a)   A8(a, 0x15a)
#define V_ANIM(a)    ((AnimState*)((uint8_t*)(a) + 0xe0))

#define PHASE(fl)    ((fl) >> 28 & 3)

/* nodo i de la red: +0 enlaces, +6 x, +8 y, +0xA z (mundo >> 8) */
static const uint8_t* Node(const uint8_t* net, int i)
{
    return net + 4 + i * 12;
}

static int NodeC(const uint8_t* net, int i, int k)
{
    return *(const int16_t*)(Node(net, i) + 6 + k * 2);
}

static int MulHi16(int a, int b)
{
    return (int)(((int64_t)a * b) >> 16);
}

/* num / den en 16.16 como el original: num * (2^31 / (den >> 1)) >> 16, con los signos aparte */
static int Frac16(int num, int den)
{
    int neg = (num < 0) != (den < 0);
    uint32_t n = (uint32_t)(num < 0 ? -num : num), d = (uint32_t)(den < 0 ? -den : den) >> 1;
    if (!d)
        return 0;
    uint32_t r = (uint32_t)(((uint64_t)n * (0x80000000u / d)) >> 16);
    return neg ? -(int)r : (int)r;
}

/* longitud horizontal aproximada (octogonal) de (x, z), para el cabeceo */
static int FlatLen(int x, int z)
{
    int ax = abs(x), az = abs(z), hi = ax, lo = az;
    if (hi < lo)
    {
        hi = az;
        lo = ax;
    }
    if ((hi >> 1) < lo)
    {
        int t = lo * 0x6a;
        lo = hi - lo;
        hi = (hi * 0x4b + t) >> 7;
    }
    return (hi + (lo >> 2)) - (lo >> 6);
}

/* GTE LZCS: bits iniciales iguales al de signo */
static int LeadingZeros(int v)
{
    uint32_t u = (uint32_t)(v < 0 ? ~v : v);
    int n = 0;
    while (n < 32 && !(u & 0x80000000u))
    {
        u <<= 1;
        n++;
    }
    return n;
}

/* 8004bd14: el nodo que esta justo en la posicion (x, z) */
static int Veh_StartNode(const uint8_t* net, const int* pos)
{
    int n = Cnet_Count(net);
    for (int i = 0; i < n; i++)
        if (NodeC(net, i, 0) == pos[0] >> 8 && NodeC(net, i, 2) == pos[2] >> 8)
            return i;
    return -1;
}

/* 8004a3a8: elige el enlace por el que seguir desde 'node'. Cada enlace puntua: -4 no hay, -3 nodo apagado
 * (bit 1), -2 el nodo actual y -1 el anterior (salvo con la bandera 1), al azar o, en los modos 6 / 14, por el
 * producto escalar con la direccion al nodo objetivo. Gana el mayor (empates a cara o cruz); -2 si no queda
 * ninguno. */
static int Veh_ChooseNode(Actor* a, int node)
{
    const uint8_t* net = V_NET(a);
    uint32_t mode = A8(a, 0x123) & 0xf;
    if (mode)
    {
        if (V_CHANCE(a) < 1 || V_CHANCE(a) <= (int16_t)(Rand_A() & 0xfff))
        {
            uint32_t m = mode > 8 ? mode - 8 : mode;
            if (m != 6)
                mode = 0;
        }
        else
            mode = 0;
    }
    uint32_t links = NR_Read32(Node(net, node));
    int cand[3] = { (int)(links >> 22), (int)(links >> 12 & 0x3ff), (int)(links >> 2 & 0x3ff) }, score[3];
    uint32_t fl = V_FL(a);
    for (int i = 0; i < 3; i++)
    {
        int c = cand[i];
        if (c == 0x3ff)
            score[i] = -4;
        else if (c == V_CUR(a) && !(fl & 1))
            score[i] = -2;
        else if (c == V_PREV(a) && !(fl & 1))
            score[i] = -1;
        else if (Node(net, c)[0] & 2)
            score[i] = -3;
        else if (mode != 6 && mode != 14)
            score[i] = Rand_A() & 0xffff;
        else
        {
            int dot = (NodeC(net, V_TARGET(a), 0) - NodeC(net, node, 0)) * (NodeC(net, c, 0) - NodeC(net, node, 0)) +
                      (NodeC(net, V_TARGET(a), 2) - NodeC(net, node, 2)) * (NodeC(net, c, 2) - NodeC(net, node, 2));
            if (mode - 9 < 5)
                dot = -dot;
            if (dot >= 0)
                score[i] = dot == 0 ? 0x20 : 0x3f - LeadingZeros(dot);
            else
                score[i] = LeadingZeros(-dot);
        }
    }
    int best = -4, bi = -1;
    for (int i = 2; i >= 0; i--)
        if (score[i] > -2 && best <= score[i] && (score[i] != best || (Rand_A() & 1) == 0))
        {
            best = score[i];
            bi = i;
        }
    if (bi < 0)
        return best == -4 ? -2 : -1;
    return cand[bi];
}

/* 80034ba4: matriz del vehiculo (0x200 apoyado en la normal del suelo, 0x100 rumbo plano; si no, rumbo,
 * cabeceo y alabeo) */
static void Veh_Orient(Actor* a)
{
    uint32_t fl = V_FL(a);
    int* r = a->obj.rot;
    if (!(fl & 0x200))
    {
        if (!(fl & 0x100))
        {
            Math_RotMatrix(V_YAW(a), V_PIT(a), V_ROLL(a), r);
            Actor_UpdateCenter(a);
            return;
        }
    }
    else
    {
        if (!(fl & 0x100))
        {
            /* 80017fc8 con la normal suavizada (3/4 la anterior): Y = normal, X = normal x rumbo, Z = X x normal */
            int n[3];
            Track_FindSurface(&a->tpos, a->obj.pos, n, 0);
            for (int k = 0; k < 3; k++)
                n[k] = (n[k] + r[3 + k] * 3) >> 2;
            Vec_Cross(n, V_HEAD(a), r);
            Vec_Normalize(r);
            Vec_Cross(r, n, r + 6);
            memcpy(r + 3, n, sizeof(n));
            return;
        }
        a->obj.pos[1] = Track_FindSurface(&a->tpos, a->obj.pos, NULL, 0);
    }
    int s = V_HEAD(a)[0], c = V_HEAD(a)[2];
    r[1] = r[3] = r[5] = r[7] = 0;
    r[4] = 0x10000;
    r[0] = c;
    r[2] = -s;
    r[6] = s;
    r[8] = c;
}

/* 8004a69c: elige el nodo de despues del siguiente y la direccion de ese tramo. smooth = 0 orienta de golpe;
 * 1 prepara el giro: medio giro +0x108 (la mitad del tramo mas corto o lo que queda del actual), el rumbo y el
 * cabeceo a recorrer, el alabeo (0x20) y el desvio de la curva +0xF8 (la diferencia entre las dos direcciones
 * escaladas por la parte de cada tramo que ocupa el giro). */
static void Veh_PickNext(Actor* a, int smooth)
{
    int n2 = Veh_ChooseNode(a, V_NEXT(a));
    V_NEXT2(a) = (int16_t)n2;
    if (n2 < 0)
        return;
    const uint8_t* net = V_NET(a);
    int* nd = V_NDIR(a);
    int next = V_NEXT(a);
    for (int k = 0; k < 3; k++)
        nd[k] = (NodeC(net, n2, k) - NodeC(net, next, k)) * 0x100;
    int len = Vec_ApproxLen3(nd);
    V_NEXTLEN(a) = (int16_t)((uint32_t)len >> 8);
    Vec_Scale(len < 2 ? 0 : (int)(0x80000000u / (uint32_t)(len >> 1)), nd);
    if (a->type == 0x11)
        return;
    if (V_NEXT2(a) != V_CUR(a) || !(V_FL(a) & 0x400))
    {
        if (V_YAW(a) != V_YAWT(a) && !(V_FL(a) & 0x1000))
            V_YAW(a) = V_YAWT(a);
        V_YAWT(a) = (int16_t)(Math_Atan2(nd[0], nd[2]) & 0xfff);
    }
    uint32_t fl = V_FL(a);
    V_PITT(a) = (int16_t)((0x1000 - Math_Atan2(nd[1], FlatLen(nd[0], nd[2]))) & 0xfff);
    V_FL(a) = fl & 0xcfffffff;
    if (fl & 0x20)
        V_ROLL(a) = V_ROLLT(a) = 0;
    int s;
    if (!smooth)
    {
        if (!(V_FL(a) & 0x100))
        {
            V_PIT0(a) = V_PIT(a) = V_PITT(a);
            Math_SinCos(V_PITT(a), &s, &V_HEAD(a)[1]);
        }
        if (V_FL(a) & 0x1000)
            return;
        V_YAW0(a) = V_YAW(a) = V_YAWT(a);
        Math_SinCos(V_YAWT(a), &V_HEAD(a)[0], &V_HEAD(a)[2]);
        return;
    }
    if (V_FL(a) & 0x800)
        return;
    if (V_CUR(a) == V_NEXT2(a) && (V_FL(a) & 0x400))
    {
        V_DYAW(a) = 0;
        V_DPITCH(a) = 0;
        return;
    }
    int dy = (V_YAWT(a) - V_YAW0(a)) & 0xfff;
    if (!dy)
        V_DYAW(a) = 0;
    int dp = 0;
    if (!(V_FL(a) & 0x100))
        dp = (V_PITT(a) - V_PIT0(a)) & 0xfff;
    if (!dp)
        V_DPITCH(a) = 0;
    if (!dy && !dp)
        return;
    if (dp)
        V_DPITCH(a) = (int16_t)(dp > 0x800 ? dp - 0x1000 : dp);
    /* medio giro: la mitad de cada tramo o lo que queda del actual */
    int h = V_SEGLEN(a) / 2, rem = V_SEGLEN(a) - (V_DIST(a) >> 8);
    if (rem < h)
        h = rem;
    if (V_NEXTLEN(a) / 2 < h)
        h = V_NEXTLEN(a) / 2;
    if (h < 0)
        h = 0;
    V_TURNLEN(a) = (int16_t)h;
    V_TURNINV(a) = (int16_t)(h < 1 ? 0x7fff : (int)(0x80000000u / ((uint32_t)(h << 8) >> 1) >> 10));
    if (dy)
    {
        V_DYAW(a) = (int16_t)(dy > 0x800 ? dy - 0x1000 : dy);
        if (V_FL(a) & 0x20)
        {
            /* alabeo: -0x300 / 0x800 del giro, por la parte de la velocidad de crucero que lleva */
            int d = V_YAWT(a) - V_YAW0(a);
            if (d > 0x800)
                d -= 0x1000;
            else if (d < -0x800)
                d += 0x1000;
            d = d < -0x800 ? -0x800 : d > 0x800 ? 0x800 : d;
            int bank = d * -0x300 / 0x800;
            int u = Frac16(a->speed, V_CRUISE(a));
            if (u > 0x10000)
                u = 0x10000;
            V_ROLLT(a) = (int16_t)(((int64_t)(bank << 16) * u) >> 32);
        }
    }
    int u15 = Frac16(V_TURNLEN(a) * 0x100, V_SEGLEN(a) * 0x100);
    int u8 = Frac16(V_TURNLEN(a) * 0x100, V_NEXTLEN(a) * 0x100);
    int cur = V_CUR(a);
    for (int k = 0; k < 3; k++)
        V_CURVEV(a)[k] = (int16_t)((MulHi16((NodeC(net, n2, k) - NodeC(net, next, k)) * 0x100, u8) -
                                    MulHi16((NodeC(net, next, k) - NodeC(net, cur, k)) * 0x100, u15)) >> 10);
    V_FL(a) = (V_FL(a) & 0xcfffffff) | 0x10000000;
}

/* 8004af64: durante el giro (d = distancia al nodo, negativa antes de cruzarlo): alabeo, rumbo y cabeceo
 * interpolados y la posicion desplazada por el desvio de la curva * (1 - |d| / medio giro)^2 */
static void Veh_Turn(Actor* a, int d)
{
    int tl8 = V_TURNLEN(a) * 0x100, ad = abs(d);
    if (V_ROLLT(a) != 0 && PHASE(V_FL(a)) != 0)
        V_ROLL(a) = (int16_t)((((int64_t)(V_ROLLT(a) << 16) * Frac16(tl8 - ad, tl8)) >> 32) & 0xfff);
    if (V_DYAW(a) != 0)
    {
        if (!(V_FL(a) & 0x1000))
            V_YAW(a) = (int16_t)((V_YAW0(a) + (int16_t)((uint32_t)(((V_TURNINV(a) * 4 * V_DYAW(a)) >> 9) * (tl8 + d)) >> 16)) & 0xfff);
        int k = MulHi16(V_TURNINV(a) << 10, tl8 - ad) >> 2;
        k = k * k >> 12;
        for (int i = 0; i < 3; i++)
            a->obj.pos[i] += MulHi16(V_CURVEV(a)[i] << 8, k);
    }
    if (V_DPITCH(a) != 0)
    {
        int t = MulHi16((tl8 + d) >> 1, V_TURNINV(a) << 10);
        V_PIT(a) = (int16_t)((V_PIT0(a) + (int16_t)(((int64_t)(V_DPITCH(a) << 16) * t) >> 32)) & 0xfff);
    }
}

/* 8004b214: avanza por la red. Devuelve 1 si se movio y 2 si cambio la orientacion. */
static uint32_t Veh_Follow(Actor* a, int dt)
{
    int bump = V_BUMP(a);
    if (bump != 0)
    {
        int r = abs(bump) - dt;
        if (r < 0)
        {
            V_TSPEED(a) = (V_FL(a) & 0x10000) ? (int16_t)((V_CRUISE(a) * V_TURNK(a) * 0x10) >> 16) : V_CRUISE(a);
            r = 0;
        }
        V_BUMP(a) = (int16_t)(V_BUMP(a) < 0 ? -r : r);
    }
    /* acelera hasta la velocidad deseada (si no lo estan empujando) o frena hasta ella */
    int sp = a->speed, ts = V_TSPEED(a);
    if (sp < ts)
    {
        if (V_BUMP(a) == 0)
        {
            sp += dt * V_ACCEL(a) / 16;
            if (ts < sp)
                sp = ts;
        }
    }
    else if (ts < sp)
    {
        sp -= dt * V_DECEL(a) / 16;
        if (sp < ts)
            sp = ts;
        if (sp < 0)
            sp = 0;
    }
    a->speed = sp;
    int move = sp * dt;
    if (move < 1)
        return 0;
    uint32_t res = 0;
    int len;
    for (;;)
    {
        V_DIST(a) += move;
        len = V_SEGLEN(a);
        if (V_DIST(a) >> 8 < len)
            break;
        /* cruza el nodo siguiente */
        if (V_NEXT2(a) < 0)
        {
            Veh_PickNext(a, 0);
            if (V_NEXT2(a) < 0)
            {
                if (V_NEXT2(a) != -2 || !(V_FL(a) & 0x8000))
                {
                    V_FL(a) |= 0x40;   /* fin de la ruta: se para */
                    a->speed = 0;
                    V_DIST(a) = V_SEGLEN(a) << 8;
                    if (a->type != 8)
                        return 0;
                }
                Actor_Destroy(a);
                return 0;
            }
            res |= 2;
        }
        else
        {
            int s;
            if (V_FL(a) & 0x800)
            {
                if (V_PIT(a) != V_PITT(a) && !(V_FL(a) & 0x100))
                {
                    V_PIT0(a) = V_PIT(a) = V_PITT(a);
                    Math_SinCos(V_PITT(a), &s, &V_HEAD(a)[1]);
                    res |= 2;
                }
                if (V_YAW(a) != V_YAWT(a) && !(V_FL(a) & 0x1000))
                {
                    V_YAW0(a) = V_YAW(a) = V_YAWT(a);
                    Math_SinCos(V_YAWT(a), &V_HEAD(a)[0], &V_HEAD(a)[2]);
                    res |= 2;
                }
            }
            if (PHASE(V_FL(a)) == 1)
                V_FL(a) = (V_FL(a) & 0xcfffffff) | 0x20000000;
        }
        memcpy(a->vel, V_NDIR(a), sizeof(a->vel));
        if (V_CUR(a) == V_NEXT2(a) && (V_FL(a) & 0x400))
        {
            /* vuelve por donde vino: media vuelta */
            Math_RotMatrix((V_YAWT(a) + 0x800) & 0xfff, 0, 0, a->obj.rot);
            Actor_UpdateCenter(a);
            Veh_Orient(a);
            if (!(V_FL(a) & 0x1000))
                V_YAWT(a) = V_YAW0(a) = V_YAW(a) = (int16_t)(Math_Atan2(a->vel[0], a->vel[2]) & 0xfff);
            res = 3;
        }
        int n2 = V_NEXT2(a), rest = V_DIST(a) - len * 0x100;
        V_NEXT2(a) = -1;
        V_DIST(a) = 0;
        V_PREV(a) = V_CUR(a);
        V_CUR(a) = V_NEXT(a);
        V_NEXT(a) = (int16_t)n2;
        V_SEGLEN(a) = V_NEXTLEN(a);
        static int s_log = -1;   /* solo en el port: NR_VEHLOG=1, cada nodo que cruza */
        if (s_log < 0)
            s_log = getenv("NR_VEHLOG") != NULL;
        if (s_log)
            NR_Log("[vehiculo %d] nodo %d -> %d (tramo %d), velocidad %d / %d, pos %d %d %d, rumbo %03x\n", a->id,
                   V_CUR(a), V_NEXT(a), V_SEGLEN(a), a->speed, V_TSPEED(a), a->obj.pos[0], a->obj.pos[1],
                   a->obj.pos[2], V_YAW(a) & 0xfff);
        if (a->tpos.curve < 0)
            Actor_FindTrackPos(a);
        if (rest < 1 || (V_FL(a) & 0x40))
            goto done;
        move = rest;
    }
    {
        /* posicion en el tramo: nodo actual + (siguiente - actual) * avance / largo, mas la altura */
        res |= 1;
        int t = len < 2 ? 0x10000 : (int)(uint32_t)(((uint64_t)(uint32_t)V_DIST(a) * (0x80000000u / (uint32_t)(len >> 1))) >> 16);
        const uint8_t* net = V_NET(a);
        int cur = V_CUR(a), next = V_NEXT(a);
        for (int k = 0; k < 3; k++)
            a->obj.pos[k] = NodeC(net, cur, k) * 0x100 + MulHi16(NodeC(net, next, k) - NodeC(net, cur, k), t);
        if (!(V_FL(a) & 0x10))
            a->obj.pos[1] += V_HEIGHT(a) * 0x10;
        uint32_t fl = V_FL(a);
        int s;
        if (PHASE(fl) == 2)
        {
            /* saliendo del giro */
            if (V_DIST(a) >> 8 < V_TURNLEN(a))
            {
                res = 3;
                if (fl & 0x800)
                    goto done;
                Veh_Turn(a, V_DIST(a));
            }
            else
            {
                if (!(fl & 0x800))
                {
                    if (!(fl & 0x1000))
                        V_YAW0(a) = V_YAW(a) = V_YAWT(a);
                    if (!(V_FL(a) & 0x100))
                        V_PIT0(a) = V_PIT(a) = V_PITT(a);
                }
                fl = V_FL(a);
                V_FL(a) = fl & 0xcfffffff;
                if (fl & 0x20)
                    V_ROLL(a) = V_ROLLT(a) = 0;
            }
            if (!(V_FL(a) & 0x800))
            {
                if (!(V_FL(a) & 0x100))
                    Math_SinCos(V_PIT(a), &s, &V_HEAD(a)[1]);
                if (!(V_FL(a) & 0x1000))
                    Math_SinCos(V_YAW(a), &V_HEAD(a)[0], &V_HEAD(a)[2]);
            }
        }
        else
        {
            int rem = V_SEGLEN(a) * 0x100 - V_DIST(a);
            if (PHASE(fl) == 1 && rem >> 8 < V_TURNLEN(a))
            {
                /* entrando en el giro */
                if (!(fl & 0x800))
                {
                    Veh_Turn(a, -rem);
                    res = 3;
                    if (!(V_FL(a) & 0x1000))
                    {
                        Math_SinCos(V_PIT(a), &s, &V_HEAD(a)[1]);
                        Math_SinCos(V_YAW(a), &V_HEAD(a)[0], &V_HEAD(a)[2]);
                    }
                }
            }
            else if (V_NEXT2(a) < 0)
                Veh_PickNext(a, 1);
        }
    }
done:
    {
        /* frena en las curvas segun lo que gira (0x118: 0x1000 nada; con 2, a la cuarta) */
        uint32_t fl = V_FL(a);
        if (fl & 0x10000)
        {
            if (PHASE(fl) == 0)
            {
                V_FL(a) = fl & 0xfffeffff;
                V_TSPEED(a) = V_CRUISE(a);
            }
        }
        else if (PHASE(fl) != 0)
        {
            int dy = (V_YAWT(a) - V_YAW0(a)) & 0xfff;
            if (dy)
            {
                if (!(fl & 2))
                {
                    if (dy > 0x800)
                        dy -= 0x1000;
                    dy = abs(dy);
                    if (dy > 0x400)
                        dy = 0x400;
                    int k = 0x1000 - V_TURNK(a);
                    if (k > 0)
                        V_TSPEED(a) = (int16_t)((V_CRUISE(a) * (0x10000 - ((k * dy) >> 6))) >> 16);
                }
                else
                    V_TSPEED(a) = (int16_t)(V_CRUISE(a) >> 2);
                V_FL(a) |= 0x10000;
            }
        }
    }
    if (V_TSPEED(a) < (V_CRUISE(a) >> 3) && V_BUMP(a) == 0)
        V_TSPEED(a) = V_CRUISE(a);
    Track_FindSurface(&a->tpos, a->obj.pos, NULL, 1);
    return res;
}

/* 8004ba74: 0x10 en el suelo, sube hasta la altura de vuelo y arranca; 0x40 parada, baja hasta el suelo */
static uint32_t Veh_Rise(Actor* a, int dt)
{
    uint32_t m = V_FL(a) & 0x50;
    int y;
    if (m == 0x10)
    {
        int len = V_SEGLEN(a);
        int t = len > 1 ? (int)(uint32_t)(((uint64_t)(uint32_t)V_DIST(a) * (0x80000000u / (uint32_t)(len << 7))) >> 16) : 0x10000;
        const uint8_t* net = V_NET(a);
        int y0 = NodeC(net, V_CUR(a), 1);
        int want = V_HEIGHT(a) * 0x10 + y0 * 0x100 + MulHi16((NodeC(net, V_NEXT(a), 1) - y0) * 0x100, t);
        if (want <= a->obj.pos[1] || want <= (y = a->obj.pos[1] + (V_CRUISE(a) * dt >> 1)))
        {
            V_BUMP(a) = 0;
            V_FL(a) &= ~0x10u;
            V_TSPEED(a) = V_CRUISE(a);
            a->speed = 1;
            return 1;
        }
    }
    else
    {
        if (m != 0x40)
            return 0;
        int ground = Track_GroundHeight(&a->tpos, a->obj.pos, -1);
        if (a->obj.model && a->obj.model->bbox)
        {
            const uint8_t* bb = a->obj.model->bbox + a->obj.frame * 0x10 + 8;
            ground += *(const int16_t*)(bb + 8) - *(const int16_t*)(bb + 2);
        }
        y = a->obj.pos[1];
        if (y <= ground || (y -= V_CRUISE(a) * dt >> 1) <= ground)
        {
            V_FL(a) |= 0x10;
            y = ground;
        }
    }
    a->obj.pos[1] = y;
    return 1;
}

/* 8004a200: ordenes del guion (1-7 y 10-15 modo de ruta y nodo objetivo, 0x13 parar / seguir, 0x14 / 0x15
 * encender / apagar un nodo de su red, 0x16 velocidad de crucero) */
static void Veh_Commands(Actor* a)
{
    for (int i = 0; i < 4;)
    {
        int c = a->cmds[i * 2], arg = a->cmds[i * 2 + 1];
        if (c == 0)
            return;
        if ((c >= 1 && c <= 7) || (c >= 10 && c <= 15))
        {
            V_TARGET(a) = (int16_t)arg;
            V_FL(a) = (V_FL(a) & 0xf0ffffff) | (uint32_t)(c - 1) << 24;
        }
        else if (c == 0x13)
        {
            if (arg == 0)
            {
                V_FL(a) |= 0x40;
                a->speed = 0;
            }
            else
            {
                V_FL(a) &= ~0x40u;
                V_TSPEED(a) = V_CRUISE(a);
            }
        }
        else if (c == 0x14 || c == 0x15)
        {
            uint8_t* n = (uint8_t*)Node(V_NET(a), arg);
            n[0] = (uint8_t)(c == 0x15 ? n[0] | 2 : n[0] & 0xfd);
        }
        else if (c == 0x16)
            V_CRUISE(a) = (int16_t)((arg * 0x2900) / 300 >> 4);
        else
        {
            i++;
            continue;
        }
        Actor_RemoveCmd(a, i);
    }
}

/* 8004bfac: se engancha a la red de su aRSL en el nodo de su posicion y apunta al siguiente */
static void Veh_InitNet(Actor* a, const ActorDef* d)
{
    const NrAsset* na = d->res && d->res[0] >= 3 ? (const NrAsset*)(uintptr_t)d->res[6] : NULL;
    V_NET(a) = NULL;
    if (!na)
        return;
    const uint8_t* net = na->data + 0xc;
    int start = Veh_StartNode(net, a->obj.pos);
    if (start < 0)
        start = Cnet_FindNearest3D(net, a->obj.pos);   /* solo en el port: el original lee fuera de la red */
    if (start < 0)
        return;
    a->flags2c |= 0x80000000;
    const uint8_t* t = d->def;
    V_NET(a) = net;
    V_PREV(a) = V_CUR(a) = (int16_t)start;
    V_DIST(a) = 0;
    V_TARGET(a) = *(const int16_t*)(t + 0x34);
    V_FL(a) = *(const uint16_t*)(t + 0x24) | (uint32_t)t[0x36] << 24;
    V_HEIGHT(a) = *(const int16_t*)(t + 0x28);
    int h = V_HEIGHT(a) * 0x10;
    if (h > 0 && (V_FL(a) & 0x10))
        h = 0;
    a->speed = 0;
    a->obj.pos[1] = h + NodeC(net, start, 1) * 0x100;
    V_BASESPD(a) = V_CRUISE(a) = V_TSPEED(a) = (int16_t)((*(const int16_t*)(t + 0x26) << 8) / 300 >> 4);
    V_ACCEL(a) = (int16_t)((*(const int16_t*)(t + 0x2c) << 8) / 300);
    V_DECEL(a) = (int16_t)((*(const int16_t*)(t + 0x2e) << 8) / 300);
    V_TURNK(a) = *(const int16_t*)(t + 0x2a);
    V_CHANCE(a) = *(const int16_t*)(t + 0x30);
    V_BUMP(a) = 0;
    V_ROLL(a) = V_ROLLT(a) = 0;
    V_U112(a) = *(const int16_t*)(t + 0x32);
    V_NEXT(a) = (int16_t)Veh_ChooseNode(a, start);
    V_NEXT2(a) = -1;
    if (V_NEXT(a) < 0)
    {
        V_FL(a) |= 0x40;
        return;   /* solo en el port: el original lee el nodo -1 */
    }
    for (int k = 0; k < 3; k++)
        a->vel[k] = (NodeC(net, V_NEXT(a), k) - NodeC(net, start, k)) * 0x100;
    int len = Vec_ApproxLen3(a->vel);
    V_SEGLEN(a) = (int16_t)((uint32_t)len >> 8);
    Vec_Scale(abs(len) < 2 ? 0 : (int)(0x80000000u / (uint32_t)(len >> 1)), a->vel);
    V_YAWT(a) = V_YAW(a) = V_YAW0(a) = (int16_t)(Math_Atan2(a->vel[0], a->vel[2]) & 0xfff);
    Math_SinCos(V_YAWT(a), &V_HEAD(a)[0], &V_HEAD(a)[2]);
    V_PITT(a) = V_PIT(a) = V_PIT0(a) = (int16_t)((0xfff - Math_Atan2(a->vel[1], FlatLen(a->vel[0], a->vel[2]))) & 0xfff);
    V_HEAD(a)[1] = a->vel[1];
    a->tpos.seg = 0xff;
    a->tpos.curve = -1;
    Track_FindSurface(&a->tpos, a->obj.pos, NULL, 1);
}

/* 8003496c */
static void Veh_Create(Actor* a, const ActorDef* d)
{
    Actor_DefaultHandler(2, a, (intptr_t)d, 0);
    Veh_InitNet(a, d);
    Veh_Orient(a);
    V_DYING(a) = 0;
    if (a->obj.model && a->obj.model->anim)
    {
        int seq = 0;
        if (a->type == 5)
        {
            V_DEATHSEQ(a) = d->def[0x3a];
            V_ASK10(a) = d->def[0x39] | 1;
            seq = d->def[0x38];
        }
        Anim_Init(a->obj.model->anim, V_ANIM(a), 1, seq);
    }
    Actor_UpdateCenter(a);
}

/* 80034a50: choque. Un vehiculo que cae se estrella; entre dos vehiculos de la misma red en el mismo tramo, el
 * de detras empuja al de delante (le pasa la mitad de su velocidad) */
static void Veh_Hit(Actor* a, intptr_t p1, intptr_t p2)
{
    uint8_t* o = (uint8_t*)p1;
    uint32_t fl = V_FL(a);
    if (fl >> 30 == 1)
    {
        a->hp = -1;
        V_FL(a) = fl | 0xc0000000;
    }
    if ((fl & 8) || !o || !(*(uint32_t*)(o + 0x2c) & 0x80000000))
    {
        Actor_DefaultHandler(0xb, a, p1, p2);
        return;
    }
    if (!(fl & 0x40) && A16(o, 0x124) >= 0)
    {
        uint8_t *back = NULL, *front = NULL;
        if (A16(o, 0x128) == V_NEXT(a))
        {
            /* mismo tramo: delante va el que tiene menos por recorrer */
            if (A16(o, 0x104) * 0x100 - A32(o, 0x100) < V_SEGLEN(a) * 0x100 - V_DIST(a))
            {
                back = (uint8_t*)a;
                front = o;
            }
            else
            {
                back = o;
                front = (uint8_t*)a;
            }
        }
        else if (A16(o, 0x12c) == V_NEXT(a))
        {
            back = (uint8_t*)a;
            front = o;
        }
        else if (A16(o, 0x128) == A16(a, 0x12c))
        {
            back = o;
            front = (uint8_t*)a;
        }
        if (back)
        {
            int v = A32(front, 0xcc) + (A32(back, 0xcc) >> 1), lim = A16(front, 0x114) << 1;
            A32(front, 0xcc) = v > lim ? lim : v;
            A16(front, 0x124) = -300;
            A16(back, 0x124) = 300;
            A32(back, 0xcc) >>= 1;
        }
    }
    if (fl & 0x80)
        Actor_DefaultHandler(0xb, a, p1, p2);
}

/* 80034dd8: sin vida y con 0x2000, 3 de cada 4 veces empieza a caer (bit 30) y sigue viva mientras cae */
static void Veh_StartFall(Actor* a)
{
    uint32_t st = V_FL(a) >> 30;
    if (st == 0)
    {
        if ((Rand_A() & 0xf) > 0xb)
            return;
        V_FL(a) |= 0x40000000;
        Rand_A();
        V_FL(a) &= 0xfffffffa;
    }
    else if (st != 1)
        return;
    a->hp = 0;
}

/* 80034d18 */
static void Veh_Update(Actor* a, int dt)
{
    uint32_t r = 0;
    if (V_NET(a))
    {
        /* 8004bc80 */
        if (a->cmds[0])
            Veh_Commands(a);
        if (!(V_FL(a) & 0x50))
            r = Veh_Follow(a, dt);
        else if (V_HEIGHT(a) > 0)
            r = Veh_Rise(a, dt) & 0xff;
        if (!a->handle)
            return;
    }
    if (V_FL(a) >> 30 == 1)
    {
        r = 2;
        V_HEIGHT(a) = (int16_t)(V_HEIGHT(a) - 0x1000);
        if (V_HEIGHT(a) <= 0)
        {
            /* solo en el port: el original lo estrella al chocar con algo; aqui, al llegar al suelo */
            a->hp = -1;
            V_FL(a) |= 0xc0000000;
        }
    }
    if (r)
    {
        if ((r & 2) || (V_FL(a) & 0x200))
            Veh_Orient(a);
        Actor_UpdateCenter(a);
    }
}

/* Animated_Handler 80034e6c */
int Vehicle_Handler(int msg, Actor* a, intptr_t p1, intptr_t p2)
{
    int anim = a->obj.model && a->obj.model->anim;
    switch (msg)
    {
    case 1:
        return 0x15c;
    case 2:
        Veh_Create(a, (const ActorDef*)p1);
        return 0;
    case 4:
        /* muere con su secuencia de animacion (si la tiene): parada y sin vida hasta que acaba */
        if (a->type == 5 && anim && V_DEATHSEQ(a) != 0xff)
        {
            if (V_DYING(a) == 0)
            {
                a->hp = 1;
                V_DYING(a) = 1;
                V_FL(a) |= 0x40;
                a->flags2c |= 1;
                Anim_SetSeq(V_ANIM(a), V_DEATHSEQ(a), 1, 1);
                if (a->objClass)
                    Fx_ObjDie(a->objClass, a);   /* 800354d4 */
                return 0;
            }
            if (V_DYING(a) == 1)
            {
                a->hp = 1;
                return 0;
            }
        }
        return Actor_DefaultHandler(msg, a, p1, p2);
    case 6:
        if (anim)
        {
            int dt = (int)p1;
            AnimState* s = V_ANIM(a);
            if (s->cur && s->cur[1] != 1)
                dt = a->speed * dt;
            if (a->type == 5 && V_DYING(a) == 1)
            {
                /* muriendo: fuera las ordenes de animacion */
                for (int i = 0; i < 4 && a->cmds[i * 2];)
                {
                    if ((unsigned)(a->cmds[i * 2] - 0x32) < 3)
                        Actor_RemoveCmd(a, i);
                    else
                        i++;
                }
            }
            uint32_t ev = Anim_Step(a, s, dt, 0);
            Obj_SetFrame(&a->obj, s->frame);
            if (a->type == 5 && ev >> 1 == 3 && V_DYING(a) == 1)
            {
                a->hp = -1;
                V_DYING(a) = 2;
                return 0;
            }
        }
        Veh_Update(a, (int)p1);
        return 0;
    case 9:
    case 0xe:
    case 0xb:
        if (a->type == 5 && V_DYING(a) == 1)
            return 0;
        if (msg == 0xb)
            Veh_Hit(a, p1, p2);
        else
            Actor_DefaultHandler(msg, a, p1, p2);
        if ((V_FL(a) & 0x2000) && a->hp < 0 && V_HEIGHT(a) > 0)
            Veh_StartFall(a);
        return 0;
    case 0xc:
        if (p1 == 6)
            return V_TARGET(a);
        return Actor_DefaultHandler(msg, a, p1, p2);
    case 0xf:
        return V_YAW(a);
    case 0x10:
        return a->type == 5 && anim ? V_ASK10(a) : 1;
    default:
        return Actor_DefaultHandler(msg, a, p1, p2);
    }
}

/* 80071f10 */
Actor* Vehicle_First(void)
{
    for (Actor* a = g_Actors; a; a = a->next)
        if (a->handle > 0 && a->type == 5)
            return a;
    return NULL;
}
