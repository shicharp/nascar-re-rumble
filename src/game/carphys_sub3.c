#include <string.h>
#include "nr.h"
#include "track.h"
#include "vecmath.h"
#include "frontend.h"
#include "carphys.h"
#include "carphys_int.h"

#define GEAR_RATIO(c, g) I16(PTR(c, 0x3e8), (g) * 2 + 0x18)

static int s_noSlope;   /* DAT_800af302 (0: la pendiente empuja el coche) */

/* 80052dbc: fuerzas con las ruedas en el suelo. comp = compresion de cada rueda (DAT_800b6118). */
void Car_GroundForces(uint8_t* c, const int16_t* comp, int dt)
{
    RaceCar* e = (RaceCar*)ENT(c);
    const int* rot = e->obj.rot;
    int top = Car_TopSpeed(c);
    I16(c, 0x1a6) = 0;
    int roll = IA(c, 0xab) + IA(c, 0xac);
    if (U8(c, 0x431) < 0xcd)
    {
        if (U8(c, 0x431) < 0xcc)
            roll += roll >> 2;
    }
    else
        roll -= roll >> 2;
    if (IA(c, 0x10a) == 0 && top < e->speed)
        roll += 0x6000;
    if (I16(c, 0x184) != 0)
        roll += 0x6000;
    int grip = 0xa0, gripMax;
    if (I16(c, 0x2b4) < 0x81)
        gripMax = 0x200;
    else if (IA(c, 0x59) < 0xf200 && Abs_(I16(c, 0x2d8)) < (I16(c, 0x312) >> 3))
        gripMax = 0x180;
    else
    {
        grip = 0x90;
        gripMax = 0xf0;
    }
    int spin = IA(c, 0x57) * dt;
    int wheelF = Div_(spin, (IA(c, 0xa2) * 0x648) >> 8);
    int wheelR = wheelF;
    if (IA(c, 0xa2) != IA(c, 0xa3))
        wheelR = Div_(spin, (IA(c, 0xa3) * 0x648) >> 8);
    int susp[4];
    for (int k = 0; k < 4; k++)
    {
        int v = IA(c, 0x100 + k) + IA(c, 0x104 + k);
        int lim = IA(c, 0xbe);
        susp[k] = v < -lim ? -lim : (v > lim ? lim : v);
    }
    uint8_t onGround = U8(c, 0x638);
    if ((onGround & 0xc) == 0)
    {
        if (onGround & 3)
        {
            if (Abs_(IA(c, 0x52)) < 0x1000)
                IA(c, 0xfe) |= 0x20000;
        }
    }
    else if ((onGround & 3) == 0)
    {
        if (Abs_(IA(c, 0x52)) < 0x1000)
            IA(c, 0xfe) |= 0x20000;
    }
    else if (IA(c, 0xfe) & 0x20000)
    {
        IA(c, 0xfe) &= ~0x20000;
        IA(c, 0x52) >>= 1;
    }
    onGround = U8(c, 0x638);
    if ((onGround & 6) == 0)
    {
        if (onGround & 9)
            goto side;
    }
    else if ((onGround & 9) == 0)
    {
    side:
        if (Abs_(IA(c, 0x54)) < 0x1000)
            IA(c, 0xfe) |= 0x40000;
    }
    else if (IA(c, 0xfe) & 0x40000)
    {
        IA(c, 0xfe) &= ~0x40000;
        IA(c, 0x54) >>= 1;
    }
    int64_t l4 = (int64_t)IA(c, 0x4d) * (int64_t)(IA(c, 0x57) << 8);
    int64_t l5 = (int64_t)IA(c, 0x57) * (int64_t)(I16(c, 0x2d8) << 4);
    int centri = (int)(l4 >> 16) * (int)(l5 >> 16);
    int turn = (Div_(centri, IA(c, 0xbf)) * -3) / 2;            /* local_38 */
    int grav = -((IA(c, 0x4d) * rot[1]) >> 0xb);                /* local_34 */
    int lat = grav + turn;                                       /* local_3c */
    int fr[4];
    for (int k = 0; k < 4; k++)
        fr[k] = ((grip + ((comp[k] * (gripMax - grip)) >> 0xc)) * 0x10) / 0xc;
    int load, loadPrev, gripLim;
    int rear, front;
    int under;
    if (IA(c, 0x1a9) == 0)
    {
        rear = ((IA(c, 0x102) >> 4) * fr[2] + (IA(c, 0x103) >> 4) * fr[3]) >> 4;
        gripLim = (IA(c, 0x4c) * grip) >> 8;
        if (rear < gripLim)
            rear = gripLim;
        loadPrev = IA(c, 0xaf);
        IA(c, 0xaf) = rear;
        load = (rear + loadPrev) >> 1;
        front = ((IA(c, 0x100) >> 4) * fr[0] + (IA(c, 0x101) >> 4) * fr[1]) >> 4;
        under = front < gripLim;
    }
    else
    {
        rear = ((IA(c, 0x102) >> 4) * fr[2] + (IA(c, 0x103) >> 4) * fr[3]) >> 4;
        if (IA(c, 0xb3) == 0 || IA(c, 0xb4) == 0)
            rear += (Div_(IA(c, 0x1a9) * IA(c, 0x4d), dt) * ((comp[2] * comp[3]) >> 8)) >> 6;
        gripLim = (IA(c, 0x4c) * grip) >> 8;
        if (rear < gripLim)
            rear = gripLim;
        loadPrev = IA(c, 0xaf);
        IA(c, 0xaf) = rear;
        load = (rear + loadPrev) >> 1;
        front = ((IA(c, 0x100) >> 4) * fr[0] + (IA(c, 0x101) >> 4) * fr[1]) >> 4;
        under = front < gripLim;
        if (IA(c, 0xb1) == 0 || IA(c, 0xb2) == 0)
        {
            front += (Div_(IA(c, 0x1a9) * IA(c, 0x4d), dt) * ((comp[0] * comp[1]) >> 8)) >> 6;
            under = front < gripLim;
        }
    }
    if (under)
        front = gripLim;
    int fPrev = IA(c, 0xb0);
    IA(c, 0xb0) = front;
    int fsum = front + fPrev;
    int frontLoad = fsum >> 1;                                   /* local_30 */
    int frontHalf = fsum >> 2;                                   /* iVar9 */
    int slope = Vec_Dot16(VEC(c, 0x160), rot + 3);
    int half = load >> 1;                                        /* iVar7 */
    int tqZ, tqX;                                                /* iVar10, iVar20 */
    if (slope < 0xf000)
    {
        int limX = (I16(c, 0x19e) * 0x533a) >> 4, limZ = (I16(c, 0x19e) * 0x44cf) >> 4;
        int sx = Vec_Dot16(VEC(c, 0x160), rot);
        int sz = Vec_Dot16(VEC(c, 0x160), rot + 6);
        int v = sz << 7;
        tqZ = v > limZ ? limZ : (v < -limZ ? -limZ : v);
        v = sx * -0x80;
        tqX = v > limX ? limX : (v < -limX ? -limX : v);
    }
    else
    {
        tqX = 0;
        tqZ = 0;
    }
    tqZ += M16(susp[3] + susp[2], IA(c, 0xc0) << 8) - M16(susp[0] + susp[1], IA(c, 0xc1) << 8);
    tqX += M16(susp[3] + susp[0] - susp[1] - susp[2], IA(c, 0xc2) << 8);
    int arm = (IA(c, 0xc3) * 0x18) >> 4;
    int gear = I8(c, 0x3f4);
    if (IA(c, 0x57) == 0 || IA(c, 0x109) == 0)
    {
        int force, div;
        I16(c, 0x2ba) = 0;
        if (gear < 0)
        {
            int v = IA(c, 0x57);
            int dv = (-(roll * v) >> 8) * dt;
            if ((unsigned)(v + 0x1f) < 0x3f)
                IA(c, 0x57) = 0;
            else
                IA(c, 0x57) = v + Div_(dv, IA(c, 0x4d));
            force = IA(c, 0xff);
            I16(c, 0x2b4) = 0;
            I16(c, 0x2ba) = 0;
            I16(c, 0x2b6) = 0;
            I16(c, 0x2b8) = 0;
            IA(c, 0xbb) = wheelR;
            div = I32(PTR(c, 0x3e8), 0x10) >> 1;
        }
        else
        {
            int drive = (IA(c, 0xff) * GEAR_RATIO(c, gear) * 0x100) / 0x130 - ((roll * IA(c, 0x57)) >> 8);
            int ad = Abs_(drive), imp;
            if (ad < half)
            {
                imp = drive * dt;
                if (I16(c, 0x2b4) > 0)
                    I16(c, 0x2b4) = 0;
            }
            else
            {
                int16_t prevSlip = I16(c, 0x2b4);
                if (load < ad)
                {
                    int neg = drive < 1;
                    I16(c, 0x2b4) = 0x100;
                    drive = neg ? -load : load;
                }
                else if (load < 0x11)
                    I16(c, 0x2b4) = 0x100;
                else
                    I16(c, 0x2b4) = (int16_t)Div_((ad - half) * 0x100, load - half);
                imp = drive * dt;
                if (prevSlip == 0)
                    IA(c, 0xfe) |= 0x4001;
            }
            tqZ -= (drive * arm) >> 8;
            IA(c, 0x57) += Div_(imp, IA(c, 0x4d));
            if (IA(c, 0x108) == 0)
                I16(c, 0x2b4) = 0;
            if (I16(c, 0x2b4) < 0x21)
            {
                int rpm = ((IA(c, 0x57) << 8) / 0x775) * GEAR_RATIO(c, gear);
                IA(c, 0xfc) = rpm - IA(c, 0xfb);
                IA(c, 0xfb) = rpm;
                IA(c, 0xbb) = wheelR;
                goto lateral;
            }
            IA(c, 0xbb) = Div_(IA(c, 0xfb), GEAR_RATIO(c, gear));
            force = IA(c, 0xff);
            div = I32(PTR(c, 0x3e8), 0x10);
        }
        int d = Div_(force * dt, div);
        IA(c, 0xfc) = d;
        IA(c, 0xfb) += d;
    }
    else
    {
        uint32_t f = (uint32_t)IA(c, 0xfe);
        int brake = half;
        if (!(f & 0x200))
        {
            if (!(f & 1))
            {
                IA(c, 0xbb) = wheelR;
                brake = IA(c, 0x109) + ((roll * Abs_(IA(c, 0x57))) >> 8);
                if (half < brake)
                {
                    I16(c, 0x2b4) = 0x100;
                    IA(c, 0xfe) |= 0x4001;
                }
            }
            else
                IA(c, 0xbb) = 0;
            if (!(IA(c, 0xfe) & 0x80))
            {
                if (!(IA(c, 0xfe) & 2))
                {
                    IA(c, 0xbb) = wheelR;
                    int b2 = IA(c, 0x109) + ((roll * Abs_(IA(c, 0x57))) >> 8);
                    if ((frontLoad >> 1) < b2)
                    {
                        I16(c, 0x2ba) = 0x100;
                        IA(c, 0xfe) |= 0x4002;
                    }
                    brake += b2;
                }
                else
                {
                    brake += fsum >> 3;
                    IA(c, 0xbb) = 0;
                }
            }
        }
        else if (!(f & 1))
        {
            IA(c, 0xbb) = wheelR;
            brake = IA(c, 0x109) + ((roll * Abs_(IA(c, 0x57))) >> 8);
            if (half < brake)
            {
                I16(c, 0x2b4) = 0x100;
                IA(c, 0xfe) |= 0x4001;
            }
        }
        else
            IA(c, 0xbb) = 0;
        int v = IA(c, 0x57);
        int dv = (int16_t)Div_(brake * dt, IA(c, 0x4d));
        if (v < 1)
        {
            if (v < 0)
            {
                if (-v < dv)
                    IA(c, 0x57) = 0;
                else
                {
                    IA(c, 0x57) = v + dv;
                    tqZ -= (brake * arm) >> 8;
                }
            }
        }
        else if (v < dv)
            IA(c, 0x57) = 0;
        else
        {
            IA(c, 0x57) = v - dv;
            if (!(IA(c, 0xfe) & 0x80))
                tqZ += (brake * arm) >> 9;
            else
                tqZ += (brake * arm) >> 8;
        }
        if (IA(c, 0x57) == 0)
        {
            IA(c, 0xfb) = 0;
            I8(c, 0x3f4) = -1;
        }
        else
            IA(c, 0xfb) = ((IA(c, 0x57) << 8) / 0x775) * GEAR_RATIO(c, I8(c, 0x3f4));
    }
lateral:
    {
        int al = Abs_(lat);
        if (al == 0)
        {
            I16(c, 0x2b6) = 0;
            I16(c, 0x2b8) = 0;
        }
        else
        {
            if (al < half)
            {
                if (I16(c, 0x2b6) > 0)
                    I16(c, 0x2b6) = 0;
            }
            else
            {
                int16_t prev = I16(c, 0x2b6);
                if (load < al || load < 0x11)
                    I16(c, 0x2b6) = 0x100;
                else
                    I16(c, 0x2b6) = (int16_t)Div_((al - half) * 0x100, load - half);
                if (prev == 0)
                    IA(c, 0xfe) |= 0x4001;
            }
            if (al < frontHalf)
            {
                if (I16(c, 0x2b8) > 0)
                    I16(c, 0x2b8) = 0;
            }
            else
            {
                int16_t prev = I16(c, 0x2b8);
                if (frontLoad < al || frontLoad < 0x11)
                    I16(c, 0x2b8) = 0x100;
                else
                    I16(c, 0x2b8) = (int16_t)Div_((al - frontHalf) * 0x100, frontLoad - frontHalf);
                if (prev == 0)
                    IA(c, 0xfe) |= 0x4002;
            }
        }
    }
    int g = 0;
    if (!s_noSlope)
    {
        g = (U16(c, 0x33a) & 0x80) ? dt << 1 : dt * -0x40;
        int v = g * rot[1] * 3;
        int s = v >> 0x11;
        if (Abs_(s) > 8 && (IA(c, 0xfe) & 3))
            IA(c, 0x55) += s;
        v = g * rot[7];
        s = v >> 16;
        if (IA(c, 0x109) == 0 && Abs_(s) > 8)
            IA(c, 0x57) += s;
    }
    if ((IA(c, 0xfe) & 3) == 0)
    {
        turn = (turn * 0x14) >> 4;
        IA(c, 0x55) = 0;
        IA(c, 0x53) = 0;
        tqX -= ((turn + (grav >> 1)) * IA(c, 0xc3)) >> 8;
        goto integrate;
    }
    {
        int armR = IA(c, 0xc0), armF = IA(c, 0xc1);
        int yawv = M16(IA(c, 0x53), 0x6487e) >> 4;
        int vf = IA(c, 0x55) + ((armF * yawv) >> 0xc);
        int sr = (lat * I16(c, 0x2b6)) >> 8;
        int sf = (lat * I16(c, 0x2b8)) >> 8;
        int tr, tf, dvx, txs;   /* iVar18, iVar17, iVar9, iVar7 */
        int a7 = half, a9 = frontHalf;
        if (IA(c, 0x55) - ((armR * yawv) >> 0xc) < 1)
        {
            tr = -(armR * ((a7 + sr) >> 8));
            if (vf > 0)
            {
                a7 = a7 - a9;
                int t16 = (a7 + sr + sf) * IA(c, 0xc3);
                tf = -(armF * ((a9 - sf) >> 8));
                dvx = Div_(a7 * dt, IA(c, 0x4d));
                txs = -(t16 >> 9);
            }
            else
            {
                tf = armF * ((a9 + sf) >> 8);
                int t16 = (a7 + a9) * dt;
                txs = ((a7 + a9 + sr + sf) * IA(c, 0xc3)) >> 9;
                dvx = Div_(t16, IA(c, 0x4d));
            }
        }
        else
        {
            tr = armR * ((a7 - sr) >> 8);
            if (vf < 1)
            {
                tf = armF * ((a9 + sf) >> 8);
                a7 = a9 - a7;
                int t16 = (a7 + sr + sf) * IA(c, 0xc3);
                dvx = Div_(a7 * dt, IA(c, 0x4d));
                txs = -(t16 >> 9);
            }
            else
            {
                int t16 = (a7 + a9) * dt;
                tf = -(armF * ((a9 - sf) >> 8));
                txs = -((((a7 + a9) - sr) - sf) * IA(c, 0xc3) >> 9);
                dvx = -Div_(t16, IA(c, 0x4d));
            }
        }
        if (I16(c, 0x2b4) > 0)
            dvx <<= 1;
        if (IA(c, 0xfe) & 0x40)
            dvx >>= 2;
        int vx = IA(c, 0x55);
        if (vx < 1)
        {
            if (vx < 0)
            {
                IA(c, 0x55) = vx + dvx;
                if (vx + dvx >= 0)
                    IA(c, 0x55) = 0;
                else
                    tqX += txs;
            }
        }
        else
        {
            IA(c, 0x55) = vx + dvx;
            if (vx + dvx < 1)
                IA(c, 0x55) = 0;
            else
                tqX += txs;
        }
        int w = Div_((tr + (tf >> 3)) * dt, IA(c, 0x4e)) * 0x29;
        int dw = w >> 6;
        if (IA(c, 0xfe) & 0x40)
            dw = w >> 7;
        int yw;
        if (IA(c, 0x53) < 1 || dw < 0x101)
        {
            yw = IA(c, 0x53);
            if (yw < 0 && dw < -0x100)
                dw = -0x100;
        }
        else
        {
            dw = 0x100;
            yw = IA(c, 0x53);
        }
        if (yw < 1)
        {
            if (yw < 0)
            {
                IA(c, 0x53) = yw + dw;
                if (yw + dw >= 0)
                    IA(c, 0x53) = 0;
            }
        }
        else
        {
            IA(c, 0x53) = yw + dw;
            if (yw + dw < 1)
                IA(c, 0x53) = 0;
        }
    }
integrate:
    if (I8(c, 0x3f4) == -1)
        tqX += IA(c, 0xff) * 4;
    int vy = Div_((susp[0] + susp[1] + susp[2] + susp[3]) * dt, IA(c, 0x4d));
    if (!s_noSlope)
        vy += (g * rot[4]) >> 16;
    if ((unsigned)(vy + 0x10) > 0x20)
        IA(c, 0x56) += vy;
    if ((unsigned)(IA(c, 0x56) + 0x1f) < 0x3f)
        IA(c, 0x56) = 0;
    int ax = Div_((tqX - M16(IA(c, 0x54), 0x300000)) * dt, IA(c, 0x4f));
    if (ax > 2000)
        ax = 2000;
    else if (ax < -2000)
        ax = -2000;
    int az = Div_((tqZ - M16(IA(c, 0x52), 0x300000)) * dt, IA(c, 0x50));
    IA(c, 0x54) += ax;
    if (az > 2000)
        az = 2000;
    else if (az < -2000)
        az = -2000;
    IA(c, 0x52) += az;
    if (e->speed < 1)
    {
        if ((unsigned)(IA(c, 0x54) + 0x9c3) < 4999)
            IA(c, 0x54) = M16(IA(c, 0x54), 0x1000);
        if ((unsigned)(IA(c, 0x52) + 0x9c3) < 4999)
            IA(c, 0x52) = M16(IA(c, 0x52), 0x1000);
        if (Abs_(IA(c, 0x55)) < 0x19 && Abs_(IA(c, 0x56)) < 0x19 && Abs_(IA(c, 0x57)) < 0x19)
        {
            IA(c, 0x55) = IA(c, 0x56) = IA(c, 0x57) = 0;
            IA(c, 99) = IA(c, 100) = IA(c, 0x65) = 0;
            U16(c, 0x198) |= 0x80;
        }
        else
            U16(c, 0x198) &= 0xff7f;
    }
    if ((IA(c, 0xfe) & 3) == 0 || Abs_(IA(c, 0x53)) > 0x9c3 || Abs_(IA(c, 0x55)) > 0x7f || I16(c, 0x2b4) != 0)
    {
        int s = Abs_(IA(c, 0x55));
        if (s > 0x100)
        {
            int16_t d = (int16_t)(s >> 6);
            I16(c, 0x2b8) += d;
            I16(c, 0x2b6) += d;
            if (I16(c, 0x2b8) > 0x100)
                I16(c, 0x2b8) = 0x100;
            if (I16(c, 0x2b6) > 0x100)
                I16(c, 0x2b6) = 0x100;
        }
    }
    else
        Car_ClearFlags(c, 3);
    if (g_Settings.racingClass == 0 && e->type == 1)
    {
        /* ayuda del nivel Rookie: si el coche mira contra la pista, lo gira hacia ella */
        int pos[3], frame[9], links = 0;
        Car_TrackFrame(e->obj.pos, pos, frame, &links);
        if (Vec_Dot16(rot + 6, frame + 6) < 0x2000)
        {
            int s = Vec_Dot16(rot + 6, frame) < 0 ? 0x200000 : -0x200000;
            IA(c, 0xfe) |= 0x4003;
            IA(c, 0x53) += Div_(s * dt, IA(c, 0x4e));
        }
    }
    IA(c, 0xba) = wheelF;
}
