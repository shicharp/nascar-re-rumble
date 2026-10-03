#include <string.h>
#include "nr.h"
#include "track.h"
#include "vecmath.h"
#include "carphys.h"
#include "carphys_int.h"

/* 8004cdb4: gira la matriz m (3 columnas 16.16) por el vector de rotacion w (angulo = |w|) */
void Car_RotateMatrix(int* m, const int* w)
{
    int len;
    int s0 = w[0] * 0x100, s1 = w[1] << 8, s2 = w[2] << 8;
    int sv[3] = { s0, s1, s2 };
    if ((unsigned)(s0 + 0x67fff) < 0xcffff && s1 < 0x68000 && s1 > -0x68000 && s2 < 0x68000 && s2 > -0x68000)
        len = Math_SquareRoot12(Vec_LenSq16(sv)) << 2;
    else
    {
        int64_t a = (int64_t)(s0 >> 4) * (s0 >> 4), b = (int64_t)(s1 >> 4) * (s1 >> 4),
                c = (int64_t)(s2 >> 4) * (s2 >> 4);
        len = Math_SquareRoot12((int)(a >> 16) + (int)(b >> 16) + (int)(c >> 16)) << 6;
    }
    if (Abs_(len >> 8) < 2)
        return;
    int inv = (int)(0x80000000u / (unsigned)(len >> 9));
    int d[3];
    d[0] = M16(Vec_Dot16(m, w), inv);
    d[1] = M16(Vec_Dot16(m + 3, w), inv);
    d[2] = M16(Vec_Dot16(m + 6, w), inv);
    int sn, cs;
    Math_SinCos(len >> 12, &sn, &cs);
    int ord[3];
    int a0 = Abs_(d[0]), a1 = Abs_(d[1]), a2 = Abs_(d[2]);
    if (a0 < a1)
    {
        if (a2 <= a1)
        {
            ord[0] = 2;
            ord[1] = 0;
            ord[2] = 1;
            goto rot;
        }
    }
    else if (a2 <= a0)
    {
        ord[0] = 1;
        ord[1] = 2;
        ord[2] = 0;
        goto rot;
    }
    ord[0] = 0;
    ord[1] = 1;
    ord[2] = 2;
rot:
    for (int k = 0; k < 2; k++)
    {
        int dd = d[ord[k]];
        int px = M16(M16(dd, w[0]), inv), py = M16(M16(dd, w[1]), inv), pz = M16(M16(dd, w[2]), inv);
        int* col = m + ord[k] * 3;
        int r[3] = { col[0] - px, col[1] - py, col[2] - pz };
        int x[3];
        Vec_Cross(r, w, x);
        x[0] = -M16(x[0], inv);
        x[1] = -M16(x[1], inv);
        x[2] = -M16(x[2], inv);
        col[0] = M16(r[0], cs) + M16(x[0], sn) + px;
        col[1] = M16(r[1], cs) + M16(x[1], sn) + py;
        col[2] = M16(r[2], cs) + M16(x[2], sn) + pz;
        Vec_Normalize(col);
    }
    Vec_Cross(m + ord[0] * 3, m + ord[1] * 3, m + ord[2] * 3);
    Vec_Normalize(m + ord[2] * 3);
}

/* 80055010: esquinas de la caja del coche en el mundo (+0x84: 8 x 3; con 4 puntos, solo las ruedas) */
void Car_BoxCorners(uint8_t* c)
{
    RaceCar* e = (RaceCar*)ENT(c);
    const int* r = e->obj.rot;
    const int* k = (const int*)PTR(c, 0xe4);
    U8(c, 0x180) = 0;
    IA(c, 0x42) = 0;
    IA(c, 0x4a) = 0;
    U8(c, 0xee) = 0;
    I16(c, 0x10c) = 0;
    I16(c, 0xec) = 0;
    int px = e->obj.pos[0], py = e->obj.pos[1], pz = e->obj.pos[2];
    int x0 = (r[0] * k[0]) >> 16, x1 = (r[1] * k[0]) >> 16, x2 = (r[2] * k[0]) >> 16;
    int dx0 = x0 * -2, dx1 = x1 * -2, dx2 = x2 * -2;
    int y0 = (r[3] * k[7]) >> 16, y1 = (r[4] * k[7]) >> 16, y2 = (r[5] * k[7]) >> 16;
    int z0 = (r[6] * k[2]) >> 16, z1 = (r[7] * k[2]) >> 16, z2 = (r[8] * k[2]) >> 16;
    int w0 = (r[6] * k[8]) >> 16, w1 = (r[7] * k[8]) >> 16, w2 = (r[8] * k[8]) >> 16;
    int* p = VEC(c, 0x84);
    if (I16(c, 0x144) == 4)
    {
        p[0] = px + x0 + y0;
        p[1] = py + x1 + y1;
        p[2] = pz + x2 + y2;
        p[9] = p[0] + w0;
        p[10] = p[1] + w1;
        p[11] = p[2] + w2;
        p[0] += z0;
        p[1] += z1;
        p[2] += z2;
    }
    else
    {
        int b0 = (r[3] * k[1]) >> 16, b1 = (r[4] * k[1]) >> 16, b2 = (r[5] * k[1]) >> 16;
        int t0 = (r[3] * k[13]) >> 16, t1 = (r[4] * k[13]) >> 16, t2 = (r[5] * k[13]) >> 16;
        p[12] = px + x0 + t0;
        p[13] = py + x1 + t1;
        p[14] = pz + x2 + t2;
        p[21] = p[12] + w0;
        p[22] = p[13] + w1;
        p[23] = p[14] + w2;
        p[12] += z0;
        p[13] += z1;
        p[14] += z2;
        p[15] = p[12] + dx0;
        p[16] = p[13] + dx1;
        p[17] = p[14] + dx2;
        p[18] = p[21] + dx0;
        p[19] = p[22] + dx1;
        p[20] = p[23] + dx2;
        p[0] = px + x0 + b0 + z0;
        p[1] = py + x1 + b1 + z1;
        p[2] = pz + x2 + b2 + z2;
        p[9] = px + x0 + y0 + w0;
        p[10] = py + x1 + y1 + w1;
        p[11] = pz + x2 + y2 + w2;
    }
    p[3] = p[0] + dx0;
    p[4] = p[1] + dx1;
    p[5] = p[2] + dx2;
    p[6] = p[9] + dx0;
    p[7] = p[10] + dx1;
    p[8] = p[11] + dx2;
}

/* 8004f650: aristas de la caja contra los muros de la pista: acumula el contacto en +0xEC (n, punto, normal)
 * y la penetracion maxima en +0x108 */
static void Car_WallContacts(uint8_t* c)
{
    RaceCar* e = (RaceCar*)ENT(c);
    int n = I16(c, 0x144);
    if (n == 4)
        n = 2;
    else if (n == 8)
        n = 4;
    int* pts = VEC(c, 0x84);
    uint8_t bit = 1;
    int a = 0, b = 0;
    for (int i = 0; i < n; i++, bit <<= 1)
    {
        if (n == 2)
        {
            if (i == 0)
            {
                b = 2;
                a = 0;
            }
            else if (i == 1)
            {
                b = 3;
                a = 1;
            }
        }
        else if (n == 4)
        {
            if (i == 1)
            {
                b = 7;
                a = 1;
            }
            else if (i == 2)
            {
                b = 4;
                a = 2;
            }
            else if (i == 3)
            {
                b = 5;
                a = 3;
            }
            else if (i == 0)
            {
                b = 6;
                a = 0;
            }
        }
        else
        {
            if (i == 3)
            {
                b = 0;
                a = 3;
            }
            else if (i == 7)
            {
                b = 4;
                a = 7;
            }
            else
            {
                b = i + 1;
                a = i;
            }
        }
        int* pa = pts + a * 3;
        int* pb = pts + b * 3;
        int nrm[3], edge[6];
        int hit = Track_RayWall(pa, pb, nrm, edge);
        if (!hit)
            continue;
        U8(c, 0x19d) = (uint8_t)hit;
        int rel[3] = { edge[0] - e->obj.pos[0], edge[1] - e->obj.pos[1], edge[2] - e->obj.pos[2] };
        if (Vec_Dot16(rel, nrm) > 0)
        {
            nrm[0] = -nrm[0];
            nrm[1] = -nrm[1];
            nrm[2] = -nrm[2];
        }
        int along = Vec_Dot16(VEC(c, 0x160), nrm);
        rel[0] = edge[0] - pa[0];
        rel[1] = edge[1] - pa[1];
        rel[2] = edge[2] - pa[2];
        int depth = Vec_Dot16(rel, nrm);
        int which = a;
        if (depth < 0)
        {
            rel[0] = edge[0] - pb[0];
            rel[1] = edge[1] - pb[1];
            rel[2] = edge[2] - pb[2];
            depth = Vec_Dot16(rel, nrm);
            which = b;
        }
        if (along > 0x80)
        {
            int t[3] = { nrm[0] - M16(IA(c, 0x58), along), nrm[1] - M16(IA(c, 0x59), along),
                         nrm[2] - M16(IA(c, 0x5a), along) };
            Vec_Normalize(t);
            nrm[0] = t[0];
            nrm[1] = t[1];
            nrm[2] = t[2];
        }
        if (IA(c, 0x42) < depth)
            IA(c, 0x42) = depth;
        const int* local = (const int*)PTR(c, 0xe4) + which * 3;
        if (I16(c, 0xec) == 0)
        {
            IA(c, 0x3c) = local[0];
            IA(c, 0x3d) = local[1];
            IA(c, 0x3e) = local[2];
            IA(c, 0x3f) = nrm[0];
            IA(c, 0x40) = nrm[1];
            IA(c, 0x41) = nrm[2];
        }
        else
        {
            IA(c, 0x3c) += local[0];
            IA(c, 0x3d) += local[1];
            IA(c, 0x3e) += local[2];
            IA(c, 0x3f) += nrm[0];
            IA(c, 0x40) += nrm[1];
            IA(c, 0x41) += nrm[2];
        }
        I16(c, 0xec)++;
        U8(c, 0xee) |= bit;
    }
}

/* 8004fb10 */
void Car_WallContactsAll(void)
{
    for (int i = 0; i < g_NumCars; i++)
        Car_WallContacts(g_CarPhys[i]);
}

/* 8004fb74: esquinas bajo el suelo: contacto en +0x10C (n, punto local, normal) y penetracion en +0x128 */
static void Car_GroundContacts(uint8_t* c, const int* heights, const int* normals)
{
    int forced = 0;
    for (;;)
    {
        uint32_t bit = 1;
        int skipped = 0;
        for (int i = 0; i < I16(c, 0x144); i++)
        {
            int a, b;
            if (i == 3)
            {
                b = 0;
                a = 3;
            }
            else if (i == 7)
            {
                b = 4;
                a = 7;
            }
            else
            {
                b = i + 1;
                a = i;
            }
            if (U8(c, 0xee) & bit)
            {
                bit <<= 1;
                continue;
            }
            int ha = heights[a], hb;
            if (ha == TRACK_NO_SURFACE || (hb = heights[b]) == TRACK_NO_SURFACE)
                continue;
            int da = I32(c, a * 0xc + 0x88) - ha;
            int db = I32(c, b * 0xc + 0x88) - hb;
            int which = a, d = da;
            if (da < 0)
            {
                if (db < da)
                {
                    which = b;
                    d = db;
                }
            }
            else
            {
                if (db >= 0)
                {
                    bit <<= 1;
                    continue;
                }
                if (db <= da)
                {
                    which = b;
                    d = db;
                }
            }
            d = -d;
            if (!forced && d >= 0x2001)
            {
                skipped++;
                continue;
            }
            if (I32(c, 0x128) < d)
                I32(c, 0x128) = d;
            const int* local = (const int*)PTR(c, 0xe4) + which * 3;
            const int* nn = normals + which * 3;
            if (I16(c, 0x10c) == 0)
            {
                I32(c, 0x110) = local[0];
                I32(c, 0x114) = local[1];
                I32(c, 0x118) = local[2];
                I32(c, 0x11c) = nn[0];
                I32(c, 0x120) = nn[1];
                I32(c, 0x124) = nn[2];
            }
            else
            {
                I32(c, 0x110) += local[0];
                I32(c, 0x114) += local[1];
                I32(c, 0x118) += local[2];
                I32(c, 0x11c) += nn[0];
                I32(c, 0x120) += nn[1];
                I32(c, 0x124) += nn[2];
            }
            I16(c, 0x10c)++;
            bit <<= 1;
        }
        forced = 1;
        if (skipped < 8)
            return;
    }
}

/* 8004fdd4: alturas del suelo bajo las esquinas (DAT_800b5d68 / normales DAT_800b5e28) */
void Car_GroundContactsAll(void)
{
    static int heights[RACE_MAX_CARS][8];
    static int normals[RACE_MAX_CARS][8 * 3];
    for (int i = 0; i < g_NumCars; i++)
    {
        uint8_t* c = g_CarPhys[i];
        if (U8(c, 0x638) != 0)
            continue;
        for (int k = 0; k < I16(c, 0x144); k++)
            heights[i][k] = Track_FindSurface((TrackPos*)(B_(c) + 4 + k * 4), VEC(c, 0x84 + k * 0xc), normals[i] + k * 3, 0);
    }
    for (int i = 0; i < g_NumCars; i++)
        if (U8(g_CarPhys[i], 0x638) == 0)
            Car_GroundContacts(g_CarPhys[i], heights[i], normals[i]);
}

/* 800646e4: suelo bajo el centro de cada coche (y su tipo y cara) */
void Car_GroundAll(int* ground)
{
    for (int i = 0; i < g_NumCars; i++)
    {
        uint8_t* c = g_CarPhys[i];
        RaceCar* e = (RaceCar*)ENT(c);
        ground[i] = Track_FindSurface(&e->tpos, e->obj.pos, VEC(c, 0x160), 1);
        const uint8_t* face;
        U8(c, 0x181) = (uint8_t)Track_SurfaceType(&e->tpos, &face);
        I16(c, 0x18a) = face ? (int16_t)NR_Read16(face + 8) : 0;
        if (ground[i] == TRACK_NO_SURFACE)
        {
            ground[i] = IA(c, 0x125);
            U8(c, 0x180) |= 0x20;
        }
        else
        {
            IA(c, 0x125) = ground[i];
            U8(c, 0x180) &= 0xdf;
        }
        if (U8(c, 0x181) == 7)
            ground[i] -= 0x2000;
    }
}

/* 80013b20: puntos de las ruedas en el mundo (+0x248, orden 1 0 2 3) */
void Car_WheelPointsAll(void)
{
    static const int order[4] = { 1, 0, 2, 3 };
    for (int i = 0; i < g_NumCars; i++)
    {
        uint8_t* c = g_CarPhys[i];
        RaceCar* e = (RaceCar*)ENT(c);
        Gte_LoadObjMatrix(e->obj.rot);
        for (int k = 0; k < 4; k++)
            Obj_GetPointGte(B_(&e->obj), order[k], VEC(c, 0x248 + k * 0xc), 0);
    }
}

/* 80065230: suelo bajo cada rueda (+0x648 altura, +0x674 normal, +0x63A tipo, sombra de la rueda) */
void Car_WheelGroundAll(const int* ground)
{
    for (int i = 0; i < g_NumCars; i++)
    {
        uint8_t* c = g_CarPhys[i];
        int allMissing = 1, noneMissing = 1;
        for (int k = 0; k < 4; k++)
        {
            TrackPos* tp = (TrackPos*)(B_(c) + 0x208 + k * 4);
            int h = Track_FindSurface(tp, VEC(c, 0x248 + k * 0xc), VEC(c, 0x674 + k * 0xc), 0);
            I32(c, 0x648 + k * 0xc) = h;
            int type = Track_SurfaceType(tp, NULL);
            U8(c, 0x63a + k) = (uint8_t)type;
            if (type == 7)
                I32(c, 0x648 + k * 0xc) -= 0x2000;
            if (I32(c, 0x648 + k * 0xc) == TRACK_NO_SURFACE)
            {
                int g = ground[i];
                if (g == TRACK_NO_SURFACE)
                {
                    g = I32(c, 0x278 + k * 4);
                    if (g == TRACK_NO_SURFACE)
                    {
                        I32(c, 0x648 + k * 0xc) = 0;
                        for (int j = 0; j < 4; j++)
                            if (I32(c, 0x278 + j * 4) != TRACK_NO_SURFACE)
                            {
                                I32(c, 0x648 + k * 0xc) = I32(c, 0x278 + j * 4);
                                break;
                            }
                    }
                    else
                        I32(c, 0x648 + k * 0xc) = g;
                }
                else
                    I32(c, 0x648 + k * 0xc) = g;
                noneMissing = 0;
            }
            else
                allMissing = 0;
            GfxObj* w = (GfxObj*)PTR(c, 0x1f8 + k * 4);
            if (w)
                w->shadowY = (short)(I32(c, 0x648 + k * 0xc) >> 4);
            I32(c, 0x644 + k * 0xc) = I32(c, 0x248 + k * 0xc);
            I32(c, 0x64c + k * 0xc) = I32(c, 0x250 + k * 0xc);
        }
        if (noneMissing && !(U8(c, 0x180) & 0x20))
            U8(c, 0x180) |= 8;
        if (allMissing && (U8(c, 0x180) & 0x20))
            U8(c, 0x180) |= 0x10;
    }
}

/* 800547b8: rigidez lateral de cada rueda (+0x294) segun la inclinacion del suelo */
void Car_WheelSlopeAll(void)
{
    for (int i = 0; i < g_NumCars; i++)
    {
        uint8_t* c = g_CarPhys[i];
        RaceCar* e = (RaceCar*)ENT(c);
        for (int k = 0; k < 4; k++)
        {
            int dot;
            if (U8(c, 0x208 + k * 4) == e->tpos.seg && U8(c, 0x209 + k * 4) == e->tpos.face)
            {
                if (I8(c, 0x639) != -1)
                {
                    IA(c, 0xa5 + k) = IA(c, I8(c, 0x639) + 0xa5);
                    continue;
                }
                dot = IA(c, 0x3a);
                I8(c, 0x639) = (int8_t)k;
                if ((unsigned)(dot + 0x200) <= 0x400)
                {
                    IA(c, 0xa5 + k) = dot < 1 ? -0x800000 : 0x800000;
                    continue;
                }
            }
            else
            {
                dot = Vec_Dot16(VEC(c, 0x674 + k * 0xc), e->obj.rot + 3);
                if ((unsigned)(dot + 0x200) < 0x401)
                {
                    IA(c, 0xa5 + k) = dot < 1 ? -0x800000 : 0x800000;
                    continue;
                }
            }
            int ny = IA(c, 0x19e + k * 3);
            unsigned num = (unsigned)Abs_(ny), den = (unsigned)Abs_(dot) >> 1;
            int v = den ? (int)(((uint64_t)num * (0x80000000u / den)) >> 16) : 0;
            if ((ny < 1) != (dot < 1))
                v = -v;
            IA(c, 0xa5 + k) = v;
        }
    }
}
