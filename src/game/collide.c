/* Choques entre coches.
 * Original: tras dibujar, 800268c0 -> 80061ba8 reparte las entidades en celdas por su punto de TCRV (>> 2) y
 * 80061808 / 80061608 / 80061708 prueban cada celda con ella misma y la siguiente; 800613b4 compara las esferas
 * (entidad +0x64 centro, +0x70 radio, +0x74 / +0x76 alto y ancho) y manda el mensaje 0xB (8002c894 ->
 * 8002c6a0 -> 8005cdb8) que resuelve 8005c05c: separacion de las cajas (80059f0c), impulso y giro (8005a534),
 * trompos y vuelcos en los golpes fuertes (8005ba30 -> 8005b408 / 8005b660 / 8005b848).
 * Pendiente: actores (objetos de la pista), chispas, sonidos y mensajes (8007734c, 8002b920), restos que
 * suelta el coche al golpearse (80052100 -> 8003da74) y la prueba con las cajas de colision del modelo
 * (80060f88, bit 27 de las banderas del modelo). */
#include <string.h>
#include "nr.h"
#include "track.h"
#include "vecmath.h"
#include "model.h"
#include "racecar.h"
#include "carphys.h"
#include "carphys_int.h"
#include "collide.h"

/* semiextensiones de la caja del modelo (3DBB +0xE / +0x10 / +0x12) */
static void Car_Half(RaceCar* e, int* h)
{
    const uint8_t* bb = e->obj.model ? e->obj.model->bbox : NULL;
    h[0] = bb ? I16(bb, 0xe) : 0x100;
    h[1] = bb ? I16(bb, 0x10) : 0x100;
    h[2] = bb ? I16(bb, 0x12) : 0x100;
}

/* 80059f0c: solape de las cajas de dos coches. Cada caja se reduce a dos puntos (delante y detras sobre su eje
 * Z) y se toma el par mas cercano; los puntos de contacto (en ejes de cada coche) salen del corte de esa
 * direccion con cada caja. Devuelve 1 y el empuje para separarlos si el choque es lento (< 0x79); 0 sin
 * empuje y -1 si no hay puntos de contacto (en el original quedan sin escribir). */
static int Car_BoxOverlap(int speed, RaceCar* a, int* ptA, RaceCar* b, int* ptB, int* push)
{
    int halfA[3], halfB[3];
    Car_Half(a, halfA);
    Car_Half(b, halfB);
    int hz[4] = { halfA[2] >> 1, -(halfA[2] >> 1), halfB[2] >> 1, -(halfB[2] >> 1) };
    int pa[2][3], pb[2][3];
    for (int k = 0; k < 3; k++)
    {
        int oa = M16(a->obj.rot[6 + k], hz[0]), ob = M16(b->obj.rot[6 + k], hz[2]);
        pa[0][k] = a->obj.pos[k] + oa;
        pa[1][k] = a->obj.pos[k] - oa;
        pb[0][k] = b->obj.pos[k] + ob;
        pb[1][k] = b->obj.pos[k] - ob;
    }
    int diff[2][3], dist[2][2];
    int slot = 0, best = 0x10000000, bi = 0, bj = 0;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 3; k++)
                diff[slot][k] = pb[j][k] - pa[i][k];
            int d = Vec_LenSq16(diff[slot]);
            dist[i][j] = d;
            if (d < best)
            {
                bi = i;
                slot ^= 1;
                best = d;
                bj = j;
            }
        }
    int dir[3], local[3], baseB;
    int* v;
    if (Abs_(dist[bi ^ 1][bj ^ 1] - dist[bi][bj]) < 0x80)
    {
        v = diff[slot];
        for (int k = 0; k < 3; k++)
            v[k] = b->obj.pos[k] - a->obj.pos[k];
        Mat_Mul(a->obj.rot, v, local);
        if (!Box_RayHit(halfA, local, 0, ptA))
            return -1;
        baseB = 0;
    }
    else
    {
        slot ^= 1;
        v = diff[slot];
        Mat_Mul(a->obj.rot, v, local);
        if (!Box_RayHit(halfA, local, hz[bi], ptA))
            return -1;
        baseB = hz[bj];
    }
    for (int k = 0; k < 3; k++)
        dir[k] = -v[k];
    memcpy(v, dir, sizeof(dir));
    Mat_Mul(b->obj.rot, v, local);
    if (!Box_RayHit(halfB, local, baseB, ptB))
        return -1;
    if (speed >= 0x79)
        return 0;
    int wa[3], wb[3];
    Mat_MulTAdd(a->obj.rot, ptA, wa);
    Mat_MulTAdd(b->obj.rot, ptB, wb);
    for (int k = 0; k < 3; k++)
        push[k] = wb[k] - wa[k];
    if (Vec_Dot16(v, push) <= 0)
        return 0;
    for (int k = 0; k < 3; k++)
        push[k] >>= 1;
    return 1;
}

/* 8005a534 (solo coches, sin actores): impulso en la normal del punto de contacto y giro por el brazo de
 * palanca, repartidos por las masas (+0x134, escalada por +0x311 / 16) */
static void Car_Impulse(uint8_t* c1, const int* pt1, int* torque1, uint8_t* c2, const int* pt2, int* torque2)
{
    RaceCar* e1 = (RaceCar*)ENT(c1);
    RaceCar* e2 = (RaceCar*)ENT(c2);
    int v1[3], v2[3], rel[3], loc[3], n[3];
    Mat_MulT(e1->obj.rot, VEC(c1, 0x154), v1);
    Mat_MulT(e2->obj.rot, VEC(c2, 0x154), v2);
    for (int k = 0; k < 3; k++)
        rel[k] = v1[k] - v2[k];
    Mat_Mul(e2->obj.rot, rel, loc);
    memcpy(n, pt2, sizeof(n));
    Vec_Normalize(n);
    for (int k = 0; k < 3; k++)
        n[k] >>= 8;
    int dot = ((n[0] * loc[0]) >> 8) + ((n[1] * loc[1]) >> 8) + ((n[2] * loc[2]) >> 8);
    int m1 = I32(c1, 0x134), m2 = I32(c2, 0x134);
    if (U8(c1, 0x311) != 0x10)
        m1 = (int)(m1 * (uint32_t)U8(c1, 0x311)) >> 4;
    if (U8(c2, 0x311) != 0x10)
        m2 = (int)(m2 * (uint32_t)U8(c2, 0x311)) >> 4;
    if (dot < 0)
    {
        int imp = (Div_(dot * m1, m2) * 3) >> 2;
        if (e1->type == 1)
            Car_Shake(e1, imp);
        if (e2->type == 1)
            Car_Shake(e2, imp);
        I32(c2, 0x154) += (n[0] * imp) >> 8;
        I32(c2, 0x158) += (n[1] * imp) >> 8;
        I32(c2, 0x15c) += (n[2] * imp) >> 8;
    }
    {
        const uint8_t* d = PTR(c2, 0x1b4);
        int sh = (d && d[1] == '1') ? (pt2[2] > 0 ? 0xe : 0xd) : (pt2[2] > 0 ? 0xc : 0xb);
        int t0 = (loc[2] * pt2[1] - loc[1] * pt2[2]) >> sh;
        int t1 = (loc[0] * pt2[2] - loc[2] * pt2[0]) >> sh;
        int t2 = (loc[1] * pt2[0] - loc[0] * pt2[1]) >> sh;
        torque2[0] = Div_(t0 * m1, m2);
        torque2[1] = Div_(t1 * m1, m2);
        torque2[2] = Div_(t2 * m1, m2);
        I32(c2, 0x148) += torque2[0];
        I32(c2, 0x14c) += torque2[1];
        I32(c2, 0x150) += torque2[2];
    }
    for (int k = 0; k < 3; k++)
        rel[k] = v2[k] - v1[k];
    Mat_Mul(e1->obj.rot, rel, loc);
    memcpy(n, pt1, sizeof(n));
    Vec_Normalize(n);
    dot = (((n[0] >> 8) * loc[0]) >> 8) + (((n[1] >> 8) * loc[1]) >> 8) + (((n[2] >> 8) * loc[2]) >> 8);
    if (dot < 0)
    {
        int imp = (Div_(dot * m2, m1) * 3) >> 2;
        I32(c1, 0x154) += ((n[0] >> 8) * imp) >> 8;
        I32(c1, 0x158) += ((n[1] >> 8) * imp) >> 8;
        I32(c1, 0x15c) += ((n[2] >> 8) * imp) >> 8;
    }
    {
        const uint8_t* d = PTR(c1, 0x1b4);
        int sh = (d && d[1] == '1') ? (pt1[2] > 0 ? 0xe : 0xd) : (pt1[2] > 0 ? 0xc : 0xb);
        int t0 = (loc[2] * pt1[1] - loc[1] * pt1[2]) >> sh;
        int t1 = (loc[0] * pt1[2] - loc[2] * pt1[0]) >> sh;
        int t2 = (loc[1] * pt1[0] - loc[0] * pt1[1]) >> sh;
        torque1[0] = Div_(t0 * m2, m1);
        torque1[1] = Div_(t1 * m2, m1);
        torque1[2] = Div_(t2 * m2, m1);
        I32(c1, 0x148) += torque1[0];
        I32(c1, 0x14c) += torque1[1];
        I32(c1, 0x150) += torque1[2];
    }
}

static int Team(uint8_t* c)
{
    return ((RaceCar*)ENT(c))->team;
}

/* golpe hacia arriba (fila Y de la matriz, en ejes del coche) */
static void Hit_Lift(uint8_t* v, int shift13)
{
    const int* r = ((RaceCar*)ENT(v))->obj.rot;
    if (shift13)
    {
        I32(v, 0x154) += ((r[1] >> 8) << 0xd) >> 8;
        I32(v, 0x158) += ((r[4] >> 8) << 0xd) >> 8;
        I32(v, 0x15c) += ((r[7] >> 8) << 0xd) >> 8;
    }
    else
    {
        I32(v, 0x154) += r[1] >> 8;
        I32(v, 0x158) += r[4] >> 8;
        I32(v, 0x15c) += r[7] >> 8;
    }
}

static void Hit_Flags(uint8_t* v)
{
    U8(v, 0x189) = 0xff;
    U16(v, 0x198) &= 0xffdf;
    IA(v, 0xfe) = (IA(v, 0xfe) & 0xffffffcf) | 0x40;
}

/* 8005b408: trompo (giro alrededor de Y) */
static void Hit_Spin(uint8_t* att, uint8_t* vic, const int* pt, int* torque, int soft)
{
    if ((U16(vic, 0x33a) & 0x200) || Team(vic) == Team(att))
        return;
    int k = soft ? 0x2000 : 0x10000;
    torque[1] += torque[1] < 1 ? -k : k;
    I32(vic, 0x148) += torque[0];
    I32(vic, 0x14c) += torque[1];
    I32(vic, 0x150) += torque[2];
    Hit_Lift(vic, 0);
    int dv = soft ? 0x300 : 0x2000;
    I32(vic, 0x154) += pt[0] < 1 ? dv : -dv;
    Hit_Flags(vic);
}

/* 8005b660: vuelta de campana (giro alrededor de X) */
static void Hit_Flip(uint8_t* att, uint8_t* vic, int flags)
{
    if ((U16(vic, 0x33a) & 0x200) || Team(vic) == Team(att))
        return;
    Hit_Lift(vic, 1);
    if (!(flags & 1))
    {
        I32(vic, 0x148) += 0x8000;
        I32(vic, 0x15c) += 0x2000;
    }
    else
    {
        I32(vic, 0x148) -= 0x8000;
        I32(vic, 0x15c) -= 0x2000;
    }
    Hit_Flags(vic);
}

/* 8005b848: vuelco lateral (giro alrededor de Z) */
static void Hit_Roll(uint8_t* att, uint8_t* vic, int flags)
{
    if ((U16(vic, 0x33a) & 0x200) || Team(vic) == Team(att))
        return;
    Hit_Lift(vic, 1);
    if (!(flags & 4))
    {
        I32(vic, 0x150) += 0x10000;
        I32(vic, 0x154) -= 0x2000;
    }
    else
    {
        I32(vic, 0x150) -= 0x10000;
        I32(vic, 0x154) += 0x2000;
    }
    Hit_Flags(vic);
}

/* lado de la caja tocado: 1 delante (+Z), 2 detras, 8 derecha (+X), 4 izquierda */
static int Hit_Side(RaceCar* e, const int* pt)
{
    int h[3];
    Car_Half(e, h);
    int f = pt[2] == h[2] ? 1 : pt[2] == -h[2] ? 2 : 0;
    if (pt[0] == h[0])
        f |= 8;
    else if (pt[0] == -h[0])
        f |= 4;
    return f;
}

static int Speed(uint8_t* c)
{
    return ((RaceCar*)ENT(c))->speed;
}

/* 8005ba30: golpes fuertes (> 0x10): trompos, vueltas y vuelcos segun las caras que se tocan */
static void Car_BigHit(int speed, uint8_t* c1, uint8_t* c2, const int* pt1, const int* pt2, int* t1, int* t2)
{
    int f1 = Hit_Side((RaceCar*)ENT(c1), pt1), f2 = Hit_Side((RaceCar*)ENT(c2), pt2), f = f1 | f2;
    if (f == 3)
    {
        if (speed > 0x60)
        {
            if (f1 & 2)
                Hit_Flip(c2, c1, f1);
            else
                Hit_Flip(c1, c2, f2);
        }
        else
        {
            int soft = speed < 0x41;
            if (f1 & 2)
                Hit_Spin(c2, c1, pt1, t1, soft);
            else
                Hit_Spin(c1, c2, pt2, t2, soft);
        }
        return;
    }
    if (speed < 0x41)
        return;
    int p1 = (U16(c1, 0x33a) & 0x200) != 0, p2 = (U16(c2, 0x33a) & 0x200) != 0;
    if ((f & 3) == 0 || (f & 0xc) == 0)
    {
        if (f == 1)
        {
            if (speed > 0x60 || p1 || p2)
            {
                if (Speed(c2) > 0x60 || (p2 && !p1))
                    Hit_Flip(c2, c1, f1);
                if (Speed(c1) < 0x61 && (!p1 || p2))
                    return;
                Hit_Flip(c1, c2, f2);
                return;
            }
            if (Speed(c2) > 0x40)
                Hit_Spin(c2, c1, pt1, t1, 0);
            if (Speed(c1) < 0x41)
                return;
            Hit_Spin(c1, c2, pt2, t2, 0);
            return;
        }
        if (f != 0xc || Team(c1) == Team(c2))
            return;
        if (p1 && !(U16(c2, 0x33a) & 4))
        {
            Hit_Roll(c1, c2, f2);
            return;
        }
        if (!p2 || (U16(c1, 0x33a) & 4))
            return;
        Hit_Roll(c2, c1, f1);
        return;
    }
    if (!(f1 & 0xc))
    {
        if (!(f2 & 0xc))
            return;
        if (Speed(c1) < 0x31 && (!p1 || p2))
            return;
        Hit_Roll(c1, c2, f2);
        return;
    }
    if (Speed(c2) > 0x30)
    {
        Hit_Roll(c2, c1, f1);
        return;
    }
    if (!p2 || p1)
        return;
    Hit_Roll(c2, c1, f1);
}

/* 8005c05c (coche contra coche) */
static int Car_CollidePair(uint8_t* c1, uint8_t* c2)
{
    RaceCar* e1 = (RaceCar*)ENT(c1);
    RaceCar* e2 = (RaceCar*)ENT(c2);
    if (I16(c1, 0x186) != 0 || I16(c2, 0x186) != 0)
        return 0;
    if ((U8(c1, 0x180) & 4) && (U8(c2, 0x180) & 4))
        return 0;
    int d[3] = { I32(c1, 0x18c) - I32(c2, 0x18c), I32(c1, 0x190) - I32(c2, 0x190), I32(c1, 0x194) - I32(c2, 0x194) };
    int speed = (Math_SquareRoot12(Vec_LenSq16(d)) << 2) >> 8;
    int pt1[3], pt2[3], push[3];
    int ov = Car_BoxOverlap(speed, e1, pt1, e2, pt2, push);
    if (ov < 0)
        return 0;
    if (ov)
    {
        GfxObj* w1 = (GfxObj*)PTR(c1, 0x1f8);
        GfxObj* w2 = (GfxObj*)PTR(c2, 0x1f8);
        for (int k = 0; k < 3; k++)
        {
            e1->obj.pos[k] += push[k];
            e2->obj.pos[k] -= push[k];
        }
        for (int w = 0; w < 4; w++)
            for (int k = 0; k < 3; k++)
            {
                if (w1)
                    w1[w].pos[k] += push[k];
                if (w2)
                    w2[w].pos[k] -= push[k];
            }
    }
    int t1[3], t2[3];
    Car_Impulse(c1, pt1, t1, c2, pt2, t2);
    if ((U16(c1, 0x33a) & 0x200) && !(U16(c2, 0x33a) & 4))
        speed += 0x40;
    if ((U16(c2, 0x33a) & 0x200) && !(U16(c1, 0x33a) & 4))
        speed += 0x40;
    U8(c1, 0x180) |= 4;
    U8(c2, 0x180) |= 4;
    U16(c1, 0x198) &= 0xffd1;
    U16(c2, 0x198) &= 0xffd1;
    if (speed < 0x16)
    {
        if (speed >= 3)
        {
            uint8_t* cs[2] = { c1, c2 };
            int fs[2] = { Hit_Side(e1, pt1), Hit_Side(e2, pt2) };
            for (int i = 0; i < 2; i++)
            {
                uint8_t* c = cs[i];
                if ((U16(c, 0x33a) & 0x200) || !(fs[i] & 0xc))
                    continue;
                if (fs[i] == 4)
                {
                    if (!(U16(c, 0x198) & 0x10))
                        I32(c, 0x154) += speed * 0x40;
                    I32(c, 0x150) -= 0x800;
                }
                else
                {
                    if (!(U16(c, 0x198) & 0x10))
                        I32(c, 0x154) -= speed * 0x40;
                    I32(c, 0x150) += 0x800;
                }
                U16(c, 0x198) &= 0xffdf;
                IA(c, 0xfe) |= 0x4043;
            }
        }
    }
    else
    {
        IA(c1, 0xfe) |= 0x4003;
        IA(c2, 0xfe) |= 0x4003;
    }
    if (e1->speed > 2 && e2->speed > 2)
    {
        U8(c1, 0x182) = 0x2f;   /* 80052100: restos y chispas pendientes */
        U8(c2, 0x182) = 0x2f;
    }
    if (speed > 0x10)
        Car_BigHit(speed, c1, c2, pt1, pt2, t1, t2);
    return 1;
}

/* 80060d34: caja 'box' de A (centro y semiextensiones, en ejes de A) llevada a los ejes de B como caja
 * alineada (extension = suma de |ejes|), contra cada caja de la lista de B. Devuelve una mascara. */
static uint32_t Box_VsList(const GfxObj* a, const GfxObj* b, const int16_t* box, const uint8_t* list, int count,
                           int stride)
{
    int c[3] = { box[0], box[1], box[2] }, w[3], d[3], cb[3];
    Mat_MulTAdd(a->rot, c, w);
    for (int k = 0; k < 3; k++)
        d[k] = w[k] - b->pos[k];
    Mat_Mul(b->rot, d, cb);
    int ext[3] = { 0, 0, 0 };
    for (int j = 0; j < 3; j++)
    {
        int ax[3] = { a->rot[j * 3], a->rot[j * 3 + 1], a->rot[j * 3 + 2] }, l[3];
        Mat_Mul(b->rot, ax, l);
        for (int k = 0; k < 3; k++)
            ext[k] += Abs_(M16(l[k], box[3 + j]));
    }
    int n = count;
    if (count > 1)
    {
        n = count - 1;
        list += stride;
    }
    uint32_t mask = 0;
    for (int i = 0; i < n; i++, list += stride)
    {
        const int16_t* o = (const int16_t*)list;
        mask <<= 1;
        if (o[0] - o[3] < ext[0] + cb[0] && cb[0] - ext[0] < o[0] + o[3] && o[1] - o[4] < ext[1] + cb[1] &&
            cb[1] - ext[1] < o[1] + o[4] && o[2] - o[5] < ext[2] + cb[2] && cb[2] - ext[2] < o[2] + o[5])
            mask |= 1;
    }
    return mask;
}

/* 80060f88 (los dos modelos con cajas de colision, bit 27): alguna caja de A solapa con alguna de B mirado
 * desde los dos lados. Sin cajas el original devuelve 0x10001 (siempre). */
static int Car_ModelBoxes(const GfxObj* a, const GfxObj* b)
{
    if (!a->model || !b->model)
        return 1;
    int fa = (a->model->flags >> 27) & 1, fb = (b->model->flags >> 27) & 1;
    if (!fa || !fb)
        return 1;
    const uint8_t* ba = a->model->bbox;
    const uint8_t* bb = b->model->bbox;
    int na = (int)NR_Read32(ba), sa = (int)NR_Read32(ba + 4);
    int nb = (int)NR_Read32(bb), sb = (int)NR_Read32(bb + 4);
    const uint8_t* la = ba + 8 + a->frame * 16;
    const uint8_t* lb = bb + 8 + b->frame * 16;
    int ia0 = na > 1 ? 1 : 0, ib0 = nb > 1 ? 1 : 0;
    for (int i = ia0; i < na; i++)
    {
        uint32_t m = Box_VsList(a, b, (const int16_t*)(la + i * sa), lb, nb, sb);
        if (!m)
            continue;
        for (int j = ib0; j < nb; j++)
        {
            uint32_t bit = 1u << ((nb - ib0) - 1 - (j - ib0));
            if (!(m & bit))
                continue;
            uint32_t back = Box_VsList(b, a, (const int16_t*)(lb + j * sb), la, na, sa);
            uint32_t abit = 1u << ((na - ia0) - 1 - (i - ia0));
            if (back & abit)
                return 1;
        }
    }
    return 0;
}

/* 800613b4: esferas y cajas aproximadas de las entidades (centro +0x64, radio +0x70, alto +0x74 y ancho +0x76
 * en 1/16), con la distancia octogonal del original */
static int Approx2(int a, int b)
{
    a = Abs_(a);
    b = Abs_(b);
    int hi = a, lo = b;
    if (hi < lo)
    {
        hi = b;
        lo = a;
    }
    if ((hi >> 1) < lo)
    {
        int t = lo * 0x6a;
        lo = hi - lo;
        hi = (hi * 0x4b + t) >> 7;
    }
    return (hi + (lo >> 2)) - (lo >> 6);
}

int Ent_Near(const GfxObj* a, uint32_t fa, const GfxObj* b, uint32_t fb)
{
    int dxz = Approx2(a->unk34[2] - b->unk34[2], a->unk34[0] - b->unk34[0]);
    int rs = a->radius + b->radius;
    if (dxz - rs >= 0x4000)
        return 0;
    int dy = a->unk34[1] - b->unk34[1];
    int d = Approx2(dxz, dy) - rs;
    if (d >= 0x4000 || !((fa | fb) & 0x200) || d >= 0)
        return 0;
    if (dxz - (a->bb16 + b->bb16) * 0x10 >= 0)
        return 0;
    if (Abs_(dy) - (a->bb10 + b->bb10) * 0x10 >= 0)
        return 0;
    return Car_ModelBoxes(a, b);
}

static int Car_Near(RaceCar* a, RaceCar* b)
{
    return Ent_Near(&a->obj, a->flags2c, &b->obj, b->flags2c);
}

/* 80061808: cada coche con los de su celda de TCRV (curva >> 2) y la siguiente */
void Race_CollideCars(void)
{
    int cells = g_Track && g_Track->numCurve > 0 ? (g_Track->numCurve >> 2) + 1 : 1;
    for (int i = 0; i < g_NumCars; i++)
        for (int j = i + 1; j < g_NumCars; j++)
        {
            uint8_t* c1 = g_CarPhys[i];
            uint8_t* c2 = g_CarPhys[j];
            if (!c1 || !c2)
                continue;
            RaceCar* a = (RaceCar*)ENT(c1);
            RaceCar* b = (RaceCar*)ENT(c2);
            if (a->tpos.seg == 0xff || b->tpos.seg == 0xff)
                continue;
            int ca = a->tpos.curve >> 2, cb = b->tpos.curve >> 2;
            int dc = ca - cb;
            if (dc < 0)
                dc = -dc;
            if (dc > 1 && dc != cells - 1)
                continue;
            if (Car_Near(a, b))
                Car_CollidePair(c1, c2);
        }
}
