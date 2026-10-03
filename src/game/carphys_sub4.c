#include <string.h>
#include "nr.h"
#include "track.h"
#include "vecmath.h"
#include "carphys.h"
#include "carphys_int.h"

static int Vec_Len16b(const int* v)
{
    int s0 = v[0], s1 = v[1], s2 = v[2];
    if ((unsigned)(s0 + 0x67fff) < 0xcffff && s1 < 0x68000 && s1 > -0x68000 && s2 < 0x68000 && s2 > -0x68000)
        return Math_SquareRoot12(Vec_LenSq16(v)) << 2;
    int64_t a = (int64_t)(s0 >> 4) * (s0 >> 4), b = (int64_t)(s1 >> 4) * (s1 >> 4), c = (int64_t)(s2 >> 4) * (s2 >> 4);
    return Math_SquareRoot12((int)(a >> 16) + (int)(b >> 16) + (int)(c >> 16)) << 6;
}

/* a * 2^31 / (|b| / 2) >> 16 con el signo del cociente */
static int DivScale(int a, int b)
{
    if (b == 0)
        return 0;
    unsigned den = (unsigned)Abs_(b) >> 1;
    if (den == 0)
        return 0;
    int v = (int)(((uint64_t)(unsigned)Abs_(a) * (0x80000000u / den)) >> 16);
    return ((a < 0) != (b < 0)) ? -v : v;
}

/* 80060540: sacudida de la camara del jugador al chocar (controlador +0x28) */
void Car_Shake(RaceCar* e, int impact)
{
    int u = Abs_(impact);
    if (u < 0x200)
        return;
    int v = u - 0x4000;
    v = (v >> 31) & v;
    int k = v + 0x4000;
    if (k < 0)
        k = v + 0x401f;
    int16_t cur = I16(e->ctrl, 0x28);
    int16_t t = (int16_t)(k >> 5);
    int d = cur - t;
    I16(e->ctrl, 0x28) = (int16_t)(cur - ((d >> 31) & (cur - t)));
}

/* 80050210: respuesta a un contacto. rec = registro de contacto (+0 n, +4 punto local, +0x10 normal);
 * ground: 1 = contra el suelo, 0 = contra un muro; self: el coche es el suyo propio (no otro coche). */
void Car_Collide(uint8_t* c, uint8_t* rec, int dt, int ground, int self)
{
    RaceCar* e = (RaceCar*)ENT(c);
    const int* rot = e->obj.rot;
    uint8_t* other = self ? c : NULL;
    int impactSnd = 0, flip = 0;
    if (U16(c, 0x198) & 0x20)
        return;
    int vel[3] = { IA(c, 0x55), IA(c, 0x56), IA(c, 0x57) };
    int ang[3] = { M16(IA(c, 0x52), 0x6487e), M16(IA(c, 0x53), 0x6487e), M16(IA(c, 0x54), 0x6487e) };
    int* nW = VEC(rec, 0x10);
    int n[3] = { Vec_Dot16(nW, rot), Vec_Dot16(nW, rot + 3), Vec_Dot16(nW, rot + 6) };
    int a19a = Abs_(n[I8(c, 0x19a)]);
    int a19b = Abs_(n[I8(c, 0x19b)]);
    int u = a19b - a19a;
    int a19c = Abs_(n[I8(c, 0x19c)]);
    int u2 = a19c - (a19b - ((u >> 31) & u));
    int maxAxis = a19c - ((u2 >> 31) & u2);       /* local_30 */
    int pt[3] = { I32(rec, 4), I32(rec, 8), I32(rec, 0xc) };
    int wr[3];
    Vec_Cross(ang, pt, wr);
    int pv[3] = { vel[0] + wr[0], vel[1] + wr[1], vel[2] + wr[2] };
    int towards = Vec_Dot16(n, wr) < 0;           /* local_48 (signo) */
    if (Vec_Dot16(n, pv) > 0)
    {
        Vec_Scale(0x400, n);
        IA(c, 0x55) += n[0];
        IA(c, 0x56) += n[1];
        IA(c, 0x57) += n[2];
        return;
    }
    int lim = (other && (U16(other, 0x33a) & 2)) ? 0x1e : 0x96;
    if (lim < I16(c, 0x146))
    {
        I16(c, 0x146) = 0;
        impactSnd = 1;
    }
    int pn[3] = { pt[0], pt[1], pt[2] };
    int plen = Vec_NormalizeLen(pn);
    int p2 = M16(plen, plen);
    int invArm = p2 < 2 ? 0x10000 : (int)(0x80000000u / (unsigned)(p2 >> 1));
    int cosA = (pn[0] >> 8) * vel[0] + (pn[1] >> 8) * vel[1] + (pn[2] >> 8) * vel[2];
    int vlen = Math_SquareRoot12((vel[0] >> 4) * (vel[0] >> 4) + (vel[1] >> 4) * (vel[1] >> 4) +
                                 (vel[2] >> 4) * (vel[2] >> 4)) * 0x40;
    cosA = vlen == 0 ? 0 : DivScale(cosA, vlen);
    int head = -Vec_Dot16(n, pn);                 /* local_54 */
    int an = Vec_Dot16(n, ang);                   /* local_58 */
    int angN[3] = { n[0], n[1], n[2] };           /* local_160 */
    Vec_Scale(an, angN);
    int angT[3] = { ang[0] - angN[0], ang[1] - angN[1], ang[2] - angN[2] };   /* local_150 */
    int angTl = Vec_Len16b(angT);
    int vn = Vec_Dot16(vel, n);                   /* iVar9 */
    if (e->type == 1)
        Car_Shake(e, vn);
    int velN[3] = { M16(vn, n[0]), M16(vn, n[1]), M16(vn, n[2]) };   /* local_130 */
    int velT[3] = { vel[0] - velN[0], vel[1] - velN[1], vel[2] - velN[2] };   /* local_140 */
    int velTl = Vec_Len16b(velT);
    if (ground && U8(c, 0x181) == 7 && I16(c, 0x184) == 0 && (Abs_(velTl) > 3000 || Abs_(vn) > 3000))
        I16(c, 0x184) = 600;   /* agua: chispas y sonido (800773d4, cola de efectos): pendientes */
    if (U8(c, 0x189) == 0 && (U16(c, 0x198) & 0x10) && ground && U8(c, 0x181) != 7)
    {
        if (!other || (U8(other, 0x638) == 0 && (!(U16(other, 0x33a) & 2) || maxAxis < 0xfc01)))
            Vec_Scale(0xf000, velT);
        if (Abs_(vn) < 0xe00 && Abs_(angTl) < 0x10000)
        {
            uint16_t f = U16(c, 0x198);
            U16(c, 0x198) = f | 8;
            if (Abs_(velTl) < 0xe00 && Abs_(an) < 0x10000)
            {
                if (!other)
                {
                    if (0xe000 < maxAxis)
                        U16(c, 0x198) |= 0x20;
                }
                else
                {
                    IA(other, 0xfe) = (IA(other, 0xfe) & 0xffffffdf) | 0x10;
                    U16(c, 0x198) = (U16(c, 0x198) & 0xfff7) | 0x20;
                    if (U8(other, 0x188) != 0)
                        flip = 1;
                }
            }
        }
    }
    (void)impactSnd;   /* chispas / golpes (8007734c): pendientes */
    int kT = 0, kA = 0, kN = 0, kR = 0;
    int nUp = 0, slide = 0;
    if (self && (U16(c, 0x198) & 8) && ground)
    {
        /* ya apoyado: empuje fijo hacia fuera del contacto, sin giro */
        int k = 0xf000 < maxAxis ? 0x380 : 0x700;
        velN[0] = -M16(k, n[0]);
        velN[1] = -M16(k, n[1]);
        velN[2] = -M16(k, n[2]);
        angN[0] = angN[1] = angN[2] = 0;
        goto torque;
    }
    if (!ground)
    {
        kT = 0xe000;
        kA = 0xe000;
    }
    else
    {
        nUp = Vec_Dot16(rot + 3, nW);
        if (U8(c, 0x181) == 7)
        {
            kT = 0x5000;
            kA = 0xe000;
        }
        else if ((cosA < 0xd4fd && (kT = 0xf000, 0xe000 < head)) || (kT = 0xa000, !other))
            kA = kT >> 1;
        else
        {
            kA = 0x5000;
            if (0xb000 < nUp)
            {
                kT = 0x10000;
                kA = kT >> 1;
            }
        }
    }
    if ((!ground || (kR = 0x8000, U8(c, 0x181) != 7)) &&
        (!other || !(U16(other, 0x33a) & 2) || (kR = 0xec00, maxAxis < 0xfc01)))
        kR = M16(kT - 0xf000, cosA) + 0xf000;
    if (!ground || (kN = 0xa000, U8(c, 0x181) != 7))
    {
        if (!other)
        {
            if (!ground || (kN = -0x5000, maxAxis < 0xfc01))
                kN = M16(head, -0x15000) + 0x10000;
        }
        else
        {
            if (!ground)
                kN = -0x15000;
            else
            {
                kN = -0x5000;
                if (0xfc00 < maxAxis)
                    goto bounceDone;
                kN = nUp < 0xe000 ? -0x15000 : -0x10a00;
            }
            kN = M16(kN, head) + 0x10000;
            if (kN > 0)
                kN = 0;
        }
    }
bounceDone:
    {
        int kS;
        if (!ground || (kS = 0xe000, U8(c, 0x181) != 7))
            kS = !towards ? 0x10000 : M16(head, 0x11000) - 0x5000;
        if ((!self || !(U16(c, 0x198) & 8)) && U8(c, 0x189) == 0)
        {
            Vec_Scale(kA, angN);
            Vec_Scale(kS, angT);
        }
    }
    if (!(U16(c, 0x198) & 0x10) && !ground)
        slide = 0x6500 < Abs_(vn);
    if (!self && !(e->flags2c & 0x80) && Abs_(vn) > 0x400)
        e->unk20--;
    if (!(U16(c, 0x198) & 8))
    {
        if (maxAxis < 0xf800 && (slide || (I32(c, 0x198) & 0x18) == 0x10))
        {
            int doSpin = 0;
            if (!ground)
                doSpin = slide || (cosA < 0xd4fd && 0xe000 < head);
            else
                doSpin = nUp < 0xb000 || cosA < 0;
            if (doSpin)
            {
                int t[3];
                Vec_Cross(pt, velT, t);
                int k = (self && cosA < 0) ? 0x1400 : 0xa000;
                t[0] = -M16(k, M16(invArm, t[0]));
                t[1] = -M16(k, M16(invArm, t[1]));
                t[2] = -M16(k, M16(invArm, t[2]));
                if (Abs_(t[0]) > 0xc0000 || Abs_(t[1]) > 0xc0000 || Abs_(t[2]) > 0xc0000)
                {
                    int l = Vec_NormalizeLen(t);
                    Vec_Scale(DivScale(0xc0000, l), t);
                }
                angN[0] += t[0];
                angN[1] += t[1];
                angN[2] += t[2];
            }
        }
        else if (!ground && self)
        {
            int lim2 = (int)((uint32_t)U16(other, 0x2da) << 16) >> 0x11;
            int k = 0x4000;
            if (lim2 < I16(other, 0x2d8) || (k = -0x4000, I16(other, 0x2d8) < -lim2))
                IA(other, 0xbd) = k;
            else if (n[0] < 0)
            {
                if (n[2] < -0x800)
                    IA(other, 0xbd) = -0x4000;
                else if (0x800 < n[2])
                    IA(other, 0xbd) = 0x4000;
            }
            else if (n[2] < -0x800)
                IA(other, 0xbd) = 0x4000;
            else if (0x800 < n[2])
                IA(other, 0xbd) = -0x4000;
        }
        if (maxAxis < 0xf800 && ((U16(c, 0x198) & 0x10) || slide))
        {
            int k = 0xc000;
            if (ground)
            {
                k = 0xc00;
                if (U8(c, 0x181) != 7)
                {
                    k = 0x1800;
                    if (nUp < 0xe000)
                        k = 0xc000;
                }
            }
            int t[3];
            Vec_Cross(pt, velN, t);
            t[0] = M16(invArm, t[0]);
            t[1] = M16(invArm, t[1]);
            t[2] = M16(invArm, t[2]);
            if (cosA < 1)
                k >>= 1;
            t[0] = -M16(k, t[0]);
            t[1] = -M16(k, t[1]);
            t[2] = -M16(k, t[2]);
            int cap = (other || ((U16(c, 0x198) & 8) == 0 && a19a < 0xfc01)) ? 0xc0000 : 0xc000;
            if (Abs_(t[0]) > cap || Abs_(t[1]) > cap || Abs_(t[2]) > cap)
            {
                int l = Vec_NormalizeLen(t);
                Vec_Scale(cap == 0 ? 0 : DivScale(cap, l), t);
            }
            angT[0] += t[0];
            angT[1] += t[1];
            angT[2] += t[2];
        }
    }
    else if (kN < 0)
        kN = 0;
    Vec_Scale(kN, velN);
    if (cosA > 0)
    {
        if (kR > 0x10000)
            kR = 0x10000;
        if (ground && U8(c, 0x189) == 0)
            Vec_Scale(kR, velT);
    }
torque:
    if ((I32(c, 0x198) & 0x48) == 8 && ground)
    {
        int k = (self && maxAxis < 0xf000) ? 0x40000 : 0x8000;
        int tq[3];
        int i0 = I8(c, 0x19a), i1 = I8(c, 0x19b), i2 = I8(c, 0x19c);
        if (n[i0] < 1)
        {
            tq[i2] = -M16(n[i1], k);
            tq[i1] = M16(n[i2], k);
        }
        else
        {
            tq[i2] = M16(n[i1], k);
            tq[i1] = -M16(n[i2], k);
        }
        tq[i0] = 0;
        if (a19a < 0xfc00)
        {
            if (tq[0])
                angT[0] += Div_(tq[0] * dt, IA(c, 0x50)) * 0x100;
            if (tq[1])
                angT[1] += Div_(tq[1] * dt, IA(c, 0x4e)) * 0x100;
            if (tq[2])
                angT[2] += Div_(tq[2] * dt, IA(c, 0x4f)) * 0x100;
        }
        if (!self)
        {
            int kk = other ? k >> 0xb : k >> 9;
            int dx = Div_(-M16(kk, angT[0]) * dt, IA(c, 0x50));
            int dy = Div_(-M16(kk, angT[1]) * dt, IA(c, 0x4e));
            int dz = Div_(-M16(kk, angT[2]) * dt, IA(c, 0x4f));
            if (angT[0] > 0 && (angT[0] += dx * 0x100) < 0)
                angT[0] = 0;
            if (angT[1] > 0 && (angT[1] += dy * 0x100) < 0)
                angT[1] = 0;
            if (angT[2] > 0 && (angT[2] += dz * 0x100) < 0)
                angT[2] = 0;
        }
    }
    if (slide && self)
        IA(other, 0xfe) |= 0x40;
    int vz = velT[2] + velN[2];
    IA(c, 0x52) = M16(angN[0] + angT[0], 0x28be);
    IA(c, 0x53) = M16(angN[1] + angT[1], 0x28be);
    IA(c, 0x54) = M16(angN[2] + angT[2], 0x28be);
    IA(c, 0x55) = velT[0] + velN[0];
    IA(c, 0x56) = velT[1] + velN[1];
    IA(c, 0x57) = vz;
    if (flip)
    {
        int s0 = IA(other, 0x9e), s1 = IA(other, 0x9f), s2 = IA(other, 0xa0), s3 = IA(other, 0xa1);
        if (Abs_(vz) < 0x1000)
            IA(c, 0x57) = (s2 + s3 <= s0 + s1) ? vz - 0x400 : vz + 0x400;
        int vx = IA(c, 0x55);
        if (Abs_(vx) < 0x1000)
            IA(c, 0x55) = (s3 + s0 <= s1 + s2) ? vx + 0x400 : vx - 0x400;
        IA(c, 0x56) += 0x400;
    }
    if (!(U16(c, 0x198) & 0x10) && !ground && self)
        IA(other, 0xfe) |= 0x4003;
}

/* 80055748: contactos acumulados con los muros (+0xEC) y con el suelo (+0x10C) */
void Car_HandleContacts(uint8_t* c, int dt, uint8_t* self, int fromStep)
{
    RaceCar* e = (RaceCar*)ENT(c);
    int nrm[3], edge[6];
    int decTimer = 0;
    (void)self;
    (void)fromStep;
    int hit = Track_RayWall(e->obj.pos, VEC(c, 0x16c), nrm, edge);
    if (hit == 0)
    {
        if (U8(c, 0x180) & 3)
        {
            if (U8(c, 0x180) & 1)
            {
                /* (golpe 80052100 y chispas 8007734c: pendientes) */
                if (IA(c, 0x42) < 0x30)
                    IA(c, 0x42) = 0x30;
                e->obj.pos[0] += M16(IA(c, 0x42), IA(c, 0x3f));
                e->obj.pos[1] += M16(IA(c, 0x42), IA(c, 0x40));
                e->obj.pos[2] += M16(IA(c, 0x42), IA(c, 0x41));
            }
            if (U8(c, 0x180) & 2)
            {
                if (I16(c, 0x184) == 0)
                {
                    if (U8(c, 0x181) != 7)
                        e->obj.pos[1] += IA(c, 0x4a);
                }
                else
                    decTimer = 1;
            }
            else
                decTimer = I16(c, 0x184) != 0;
        }
        else
            decTimer = I16(c, 0x184) != 0;
    }
    else
    {
        /* el centro ha cruzado un muro desde la ultima posicion buena: se recoloca en la caja */
        int d[3] = { e->obj.pos[0] - IA(c, 0x5b), e->obj.pos[1] - IA(c, 0x5c), e->obj.pos[2] - IA(c, 0x5d) };
        const uint8_t* bb = e->obj.model ? e->obj.model->bbox : NULL;
        int half[3] = { bb ? I16(bb, 0xe) : 0, bb ? I16(bb, 0x10) : 0, bb ? I16(bb, 0x12) : 0 };
        int local[3];
        Mat_Mul(e->obj.rot, d, local);
        if (Box_RayHit(half, local, 0, VEC(c, 0xf0)))
        {
            int w[3];
            Mat_MulTAdd(e->obj.rot, VEC(c, 0xf0), w);
            IA(c, 0x3f) = nrm[0];
            IA(c, 0x40) = nrm[1];
            IA(c, 0x41) = nrm[2];
            int rel[3] = { edge[0] - w[0], edge[1] - w[1], edge[2] - w[2] };
            int depth = Vec_Dot16(rel, nrm);
            IA(c, 0x42) = depth;
            Vec_Scale(depth, nrm);
            e->obj.pos[0] += nrm[0];
            e->obj.pos[1] += nrm[1];
            e->obj.pos[2] += nrm[2];
            Car_Collide(c, B_(c) + 0xec, dt, 0, 1);
        }
        decTimer = I16(c, 0x184) != 0;
    }
    if (decTimer)
    {
        if ((int)U16(c, 0x184) > dt)
            U16(c, 0x184) = (uint16_t)(U16(c, 0x184) - dt);
        else
        {
            I16(c, 0x184) = 0;
            if (e->type == 1 || e->type == 0x11)
                Car_Reset(e, 0);
        }
    }
    if (U8(c, 0x182) != 0)
    {
        int v = (int)U8(c, 0x182) - dt;
        U8(c, 0x182) = (uint8_t)v;
        if ((int32_t)((uint32_t)v << 24) < 1)
            U8(c, 0x182) = 0;
    }
}
