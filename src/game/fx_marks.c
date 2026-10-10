/* Marcas de las ruedas en el asfalto y humo / tierra que levantan.
 * Original (0x80075000-0x80077000): cada rueda tiene un registro de 100 bytes en la fisica (+0x4A8 + k * 100):
 *  +0 escala (0x80), +1 tipo de marca (0 nada, 1 derrape, 2..6 segun la superficie), +2 el del frame anterior,
 *  +3 segmento de la pista (+0x208 + k * 4), +4 intensidad (la usa tambien el sonido del derrape), +5 espera del
 *  humo, +6 rastro de la superficie, +8 ultima marca, +0x14 posicion actual, +0x20 siguiente marca (anillo de 50
 *  de 12 bytes en +0x5C), +0x22 tramo actual y +0x24 el mas viejo (anillo de 8 de 0x1C bytes en +0x60), +0x26
 *  activa, +0x30 instancia del humo (aviso en +0x50: 80074fd0 la borra al morir), +0x59 superficie del humo.
 *  Solo el coche del jugador tiene anillos (80075a9c, 600 + 0xE0 bytes); los demas solo levantan humo.
 *  Marca (12 bytes): +0 posicion respecto al tramo >> 4, +6 medio ancho (cruz de la normal y la direccion >> 10),
 *  +0xA intensidad, +0xB segmento de la pista. Tramo: +0 base, +0xC primera, +0xD ultima, +0xE 1 usado /
 *  2 cerrado / 4 con direccion, +0xF tipo, +0x10 5 x {segmento, marcas en el} para saber si se ve.
 *  Generar 80076500 (cada paso de la fisica): tipo e intensidad por rueda; empezar 800763dc, alargar 80075fa0
 *  (una marca cada 0x400 de recorrido, 80075dac / 8007585c, reciclando las viejas 80075c5c), cerrar 800761b0.
 *  Humo: 80075130 (la cola de impactos con espera por superficie, 80075000) -> 80075330 (clase segun la
 *  superficie: 2 humo, 0x14 humo oscuro, 3 / 0x11 chispas, 9 / 10 / 0x31 / 0x32 tierra; una instancia por
 *  rueda que se vuelve a emitir mientras vive).
 *  Dibujar 80076a1c: tiras de POLY_GT4 sustractivas con la textura 800ab560 (pagina 8 de la pista), vertices a
 *  1/8 del mundo proyectados con 800688c8. */
#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "gte.h"
#include "exe.h"
#include "gfx.h"
#include "globals.h"
#include "texslot.h"
#include "track.h"
#include "vecmath.h"
#include "fx_int.h"
#include "carphys_int.h"

#define WHEEL(c, k)   (B_(c) + 0x4a8 + (k) * 100)
#define W_MARKS(w)    PTR(w, 0x5c)
#define W_SEGS(w)     PTR(w, 0x60)
#define SEG(w, i)     (W_SEGS(w) + (i) * 0x1c)
#define MARK(w, i)    (W_MARKS(w) + (i) * 0xc)
#define NEXT_MARK(i)  ((i) > 0x30 ? 0 : (i) + 1)
#define NEXT_SEG(i)   ((i) > 6 ? 0 : (i) + 1)

/* 80075a9c (desde Car_InitPhys) */
void Marks_Init(uint8_t* c, int k)
{
    uint8_t* w = WHEEL(c, k);
    RaceCar* e = (RaceCar*)ENT(c);
    /* (tambien el coche que sigue la repeticion sin humanos: DAT_800af744 / DAT_800b0e40) */
    if (e->type == 1)
    {
        SETPTR(w, 0x5c, calloc(1, 600));
        SETPTR(w, 0x60, calloc(1, 0xe0));
    }
    else
    {
        SETPTR(w, 0x5c, NULL);
        SETPTR(w, 0x60, NULL);
    }
    SETPTR(w, 0x30, NULL);
    I16(w, 0x20) = I16(w, 0x22) = I16(w, 0x24) = 0;
    w[0] = 0x80;
    w[1] = w[2] = w[3] = w[5] = w[6] = 0;
    w[0x26] = w[0x27] = 0;
    if (!W_SEGS(w))
    {
        free(W_MARKS(w));
        SETPTR(w, 0x5c, NULL);
        return;
    }
    for (int i = 0; i < 8; i++)
    {
        uint8_t* s = SEG(w, i);
        s[0xc] = s[0xd] = 0xff;
        s[0xe] = 0;
        for (int j = 0; j < 5; j++)
            s[0x11 + j * 2] = 0;
    }
}

/* 80075c08 */
void Marks_Free(uint8_t* c, int k)
{
    uint8_t* w = WHEEL(c, k);
    free(W_MARKS(w));
    free(W_SEGS(w));
    SETPTR(w, 0x5c, NULL);
    SETPTR(w, 0x60, NULL);
}

static void Seg_Clear(uint8_t* s)
{
    s[0xd] = s[0xc] = 0xff;
    s[0xe] = 0;
    for (int j = 0; j < 5; j++)
        s[0x11 + j * 2] = 0;
}

/* Baja la cuenta del segmento de la pista de la marca (solo mira la primera entrada no vacia) */
static void Seg_Uncount(uint8_t* s, int trackSeg)
{
    for (int j = 0; j < 5; j++)
        if (s[0x11 + j * 2])
        {
            if (s[0x10 + j * 2] == (uint8_t)trackSeg)
                s[0x11 + j * 2]--;
            return;
        }
}

/* 80075c5c: si el anillo alcanza al tramo mas viejo, este pierde su primera marca */
static void Marks_Recycle(uint8_t* w)
{
    uint8_t* s = SEG(w, I16(w, 0x24));
    if (!(s[0xe] & 1))
        return;
    int next = NEXT_MARK(I16(w, 0x20));
    if (next != (int8_t)s[0xc])
        return;
    s[0xc] = (uint8_t)NEXT_MARK((int8_t)s[0xc]);
    Seg_Uncount(s, MARK(w, next)[0xb]);
    if ((s[0xe] & 2) && s[0xc] == s[0xd])
    {
        Seg_Clear(s);
        I16(w, 0x24) = (int16_t)NEXT_SEG(I16(w, 0x24));
    }
}

/* 8007585c: mode 0 posicion, 1 solo el ancho, 2 las dos cosas */
static void Marks_Write(uint8_t* w, uint8_t* m, const int* pos, const int* normal, const int* dir, int mode)
{
    m[9] = 0xff;
    m[0xa] = w[4];
    if (mode != 1)
    {
        uint8_t* s = SEG(w, I16(w, 0x22));
        for (int k = 0; k < 3; k++)
            *(int16_t*)(m + k * 2) = (int16_t)((pos[k] - I32(s, k * 4)) >> 4);
        int idx = -1, prev = -1, found = 0;
        for (int j = 0; j < 5; j++)
        {
            idx = j;
            if (s[0x11 + j * 2])
            {
                idx = prev;
                if (s[0x10 + j * 2] == w[3])
                {
                    s[0x11 + j * 2]++;
                    found = 1;
                    break;
                }
            }
            prev = idx;
        }
        if (!found && idx != -1)
        {
            s[0x10 + idx * 2] = w[3];
            s[0x11 + idx * 2]++;
        }
        m[0xb] = w[3];
    }
    if (mode != 0)
    {
        int x[3];
        Vec_Cross(normal, dir, x);
        m[6] = (uint8_t)(x[0] >> 10);
        m[7] = (uint8_t)(x[1] >> 10);
        m[8] = (uint8_t)(x[2] >> 10);
    }
}

/* 80075dac */
static void Marks_AddPoint(uint8_t* w, const int* pos, const int* normal, const int* dir)
{
    uint8_t* s = SEG(w, I16(w, 0x22));
    Marks_Recycle(w);
    if (!(s[0xe] & 4))
    {
        Marks_Write(w, MARK(w, (int8_t)s[0xc]), pos, normal, dir, 1);
        s[0xe] |= 4;
    }
    Marks_Write(w, MARK(w, I16(w, 0x20)), pos, normal, dir, 2);
    I16(w, 0x20) = (int16_t)NEXT_MARK(I16(w, 0x20));
}

/* 80075ec0 */
static void Marks_NextSeg(uint8_t* w)
{
    int i = NEXT_SEG(I16(w, 0x22));
    I16(w, 0x22) = (int16_t)i;
    if (SEG(w, i)[0xe] & 1)
    {
        I16(w, 0x24) = (int16_t)NEXT_SEG(i);
        Seg_Clear(SEG(w, i));
    }
}

/* 800763dc */
static void Marks_Start(uint8_t* w, const int* pos, const int* normal)
{
    w[5] = 0;
    memcpy(w + 8, pos, 12);
    memcpy(w + 0x14, pos, 12);
    uint8_t* s = SEG(w, I16(w, 0x22));
    Marks_Recycle(w);
    memcpy(s, pos, 12);
    s[0xf] = w[1];
    s[0xd] = 0xff;
    s[0xe] = 1;
    s[0xc] = (uint8_t)I16(w, 0x20);
    Marks_Write(w, MARK(w, I16(w, 0x20)), pos, normal, pos, 0);
    I16(w, 0x20) = (int16_t)NEXT_MARK(I16(w, 0x20));
}

/* 800761b0: cierra el tramo (como el original, la ultima posicion se pisa antes de medir: nunca anade otra) */
static void Marks_End(uint8_t* w, const int* pos, const int* normal)
{
    (void)normal;
    memcpy(w + 8, pos, 12);
    memcpy(w + 0x14, pos, 12);
    SETPTR(w, 0x30, NULL);
    uint8_t* s = SEG(w, I16(w, 0x22));
    if ((s[0xe] & 2) || (int8_t)s[0xc] == -1)
        return;
    if (!(s[0xe] & 4))
    {
        Seg_Clear(s);
        return;
    }
    int h = I16(w, 0x20);
    s[0xd] = (uint8_t)(h == 0 ? 0x31 : h - 1);
    Seg_Uncount(s, MARK(w, h)[0xb]);
    s[0xe] |= 2;
    Marks_NextSeg(w);
}

/* 80075000: espera entre bocanadas segun la superficie (800ab57c) y si toca */
static int Wheel_SmokeDue(uint8_t* c, int k, int dt)
{
    uint8_t* w = WHEEL(c, k);
    if ((int)w[5] >= dt)
    {
        w[5] = (uint8_t)(w[5] - dt);
        return 0;
    }
    int surf = U8(c, 0x63a + k);
    w[5] = Exe_U8(0x800ab57cu + surf * 4);
    RaceCar* e = (RaceCar*)ENT(c);
    switch (surf)
    {
    case 0:
    case 1:
    case 8:
        if (e->speed < 0x29)
        {
            if (k < 2)
                return 0;
            if (IA(c, 0x108) != 0 && I8(c, 0x3f4) != -1)
                return 1;
        }
        break;
    case 3:
        if (k < 2)
            return 0;
        return (unsigned)(IA(c, 0x57) + 0x400) >= 0x801;
    case 4:
    case 5:
    case 6:
    case 9:
    case 10:
    case 0xb:
        if (k < 2)
            return 0;
        if (IA(c, 0x57) > 0x800)
            return 1;
        break;
    }
    return 0;
}

/* 80075130 */
static void Wheel_QueueSmoke(uint8_t* c, int k, int dt)
{
    if (Wheel_SmokeDue(c, k, dt))
        Fx_QueueWheel(c, k);
}

/* 80075fa0: con la rueda marcando, una marca cada 0x400 de recorrido */
static void Marks_Extend(uint8_t* c, int k, int dt, int on)
{
    uint8_t* w = WHEEL(c, k);
    if (!on || !(SEG(w, I16(w, 0x22))[0xe] & 1))
        return;
    const int* cp = VEC(c, 0x644 + k * 0xc);
    memcpy(w + 0x14, cp, 12);
    Wheel_QueueSmoke(c, k, dt);
    int d[3] = { cp[0] - I32(w, 8), cp[1] - I32(w, 0xc), cp[2] - I32(w, 0x10) };
    int len = Vec_NormalizeLen(d);
    if (len <= 0x400)
        return;
    int step[3] = { M16(d[0], 0x400), M16(d[1], 0x400), M16(d[2], 0x400) };
    int p[3] = { I32(w, 8), I32(w, 0xc), I32(w, 0x10) };
    const int* normal = VEC(c, 0x674 + k * 0xc);
    do
    {
        len -= 0x400;
        for (int a = 0; a < 3; a++)
            p[a] += step[a];
        Marks_AddPoint(w, p, normal, d);
    } while (len > 0x400);
    memcpy(w + 8, p, 12);
}

/* 80076500 (cada paso de la fisica) */
void Car_WheelMarksFx(uint8_t* c, int dt)
{
    int mask, slip[2];
    if (U32(c, 0x3f8) & 1)
    {
        int v = I16(c, 0x2b4) + I16(c, 0x2b6);
        mask = 0xc;
        slip[1] = v < 0xff ? v : 0xff;
    }
    else
    {
        mask = 0;
        slip[1] = 0;
    }
    if (U32(c, 0x3f8) & 2)
    {
        int v = I16(c, 0x2b8) + I16(c, 0x2ba);
        slip[0] = v < 0xff ? v : 0xff;
        mask |= 3;
    }
    else
        slip[0] = 0;
    uint8_t ground = U8(c, 0x638);
    mask &= ground;
    for (int k = 0, bit = 1; k < 4; k++, bit <<= 1)
    {
        uint8_t* w = WHEEL(c, k);
        const int* cp = VEC(c, 0x644 + k * 0xc);
        const int* normal = VEC(c, 0x674 + k * 0xc);
        int surf = U8(c, 0x63a + k), add = 0;
        w[0x26] = 1;
        w[3] = U8(c, 0x208 + k * 4);
        if (!(ground & bit))
            w[6] = 0;
        else
        {
            if (surf == 2 || surf == 5 || surf == 6 || surf == 7)
                w[6] = 0xff;
            else if (surf == 0xb)
                w[6] = 0x40;
            else if (w[6] != 0)
            {
                int dec = (dt << 7) >> 8;
                if (dec <= w[6])
                    w[6] = (uint8_t)(w[6] - dec);
                else
                {
                    w[6] = 0;
                    w[0x26] = 0;
                }
            }
            switch (surf)
            {
            case 3:
            case 6:
                add = 1;
                break;
            case 4:
            case 9:
                add = 0x14;
                break;
            case 5:
                add = 0x19;
                break;
            case 10:
            case 0xb:
                add = 0x32;
                break;
            }
        }
        unsigned v = (unsigned)(add + slip[k >> 1] + w[6]);
        if (v > 0xff)
            v = 0xff;
        w[4] = (uint8_t)((v * w[0] >> 8) + 0x20);
        int marking = (mask & bit) || w[6] || add;
        if (!marking || !(ground & bit))
            w[0x26] = 0;
        int smoke = 0;
        if (w[0x26] == 0)
            w[1] = 0;
        else
        {
            if (add == 0)
                w[1] = (mask & bit) ? 1 : (w[6] ? 2 : 0);
            else
                switch (surf)
                {
                case 4:
                    w[1] = 3;
                    break;
                case 5:
                    w[1] = 4;
                    break;
                case 9:
                    w[1] = 5;
                    break;
                case 10:
                case 0xb:
                case 0xc:
                    w[1] = 6;
                    break;
                default:
                    w[1] = 2;
                    break;
                }
            if (!W_MARKS(w))
                smoke = 1;
            else if (w[2] != w[1])
                Marks_End(w, cp, normal);
        }
        if (W_MARKS(w))
        {
            if (w[2] == w[1])
            {
                if (SEG(w, I16(w, 0x24))[0xe] != 0)
                    Marks_Extend(c, k, dt, marking);
            }
            else if (w[1] == 0)
                Marks_End(w, cp, normal);
            else
                Marks_Start(w, cp, normal);
        }
        else if (w[0x26] == 0)
            smoke = 1;
        if (smoke && marking)
        {
            memcpy(w + 0x14, cp, 12);
            Wheel_QueueSmoke(c, k, dt);
        }
        w[2] = w[1];
    }
}

/* ---- humo ---- */

/* 80074fd0: al morir la instancia del humo la rueda la olvida */
static void Wheel_SmokeDone(int msg, FxInst* in, FxCallback* cb)
{
    (void)in;
    if (msg == 2)
        SETPTR((uint8_t*)cb->user, 8, NULL);
}

/* 80075268: orientacion y velocidad del chorro segun el regimen (+0x3EC) */
static int Wheel_SprayParams(uint8_t* c, int surf, int* yaw, int* pitch, int* speed)
{
    int v;
    switch (surf)
    {
    default:
        *pitch = 600;
        v = IA(c, 0xfb) >> 4;
        break;
    case 5:
        *pitch = 0x29c;
        v = IA(c, 0xfb) >> 3;
        break;
    case 9:
        *pitch = 600;
        v = IA(c, 0xfb) >> 3;
        break;
    }
    *yaw = 0x800;
    if (v > 0x300)
        v = 0x300;
    else if (v < 0x100)
        v = 0x100;
    *speed = v;
    return v > 0x200;
}

/* 800751cc: cambia la emision de la plantilla (o de la instancia) */
static void Wheel_SetSpray(FxInst* in, int cls, int yaw, int pitch, int speed)
{
    uint8_t* t = in ? in->tmpl : Fx_Template(0, cls);
    if (!t || T8(t, TP_TYPE) != 0x5e)
        return;
    T16(t, 0x48) = (int16_t)yaw;
    T16(t, 0x4a) = (int16_t)pitch;
    if (!in)
    {
        T16(t, 0x3a) = (int16_t)speed;
        T16(t, 0x38) = (int16_t)(speed - 0x100);
    }
    else
        Psys_SetSpeed(in, speed - 0x100, speed);
}

/* 80038158: cerca del coche de alguna vista */
static int Fx_NearViews(const int* p)
{
    for (int v = 0; v < (g_SplitScreen ? 2 : 1); v++)
    {
        int f[3];
        Fx_Focus(v, f);
        int d[3] = { p[0] - f[0], p[1] - f[1], p[2] - f[2] };
        if (Vec_ApproxLen3(d) < 0x20000)
            return 1;
    }
    return 0;
}

/* 80075330: humo / tierra / chispas de la rueda k */
void Fx_WheelSmoke(uint8_t* c, int k)
{
    uint8_t* w = WHEEL(c, k);
    int surf = U8(c, 0x63a + k);
    const int* cp = VEC(c, 0x644 + k * 0xc);
    if (!Fx_NearViews(cp))
        return;
    FxSpawn sp;
    memset(&sp, 0, sizeof(sp));
    sp.flags = 4;
    memcpy(sp.pos, cp, sizeof(sp.pos));
    RaceCar* ent = NULL;
    RaceCar* e = (RaceCar*)ENT(c);
    if (surf > 3 && surf <= 0xb && !(surf == 7 || surf == 8))
    {
        /* en el marco del coche */
        int rel[3] = { cp[0] - e->obj.pos[0], cp[1] - e->obj.pos[1], cp[2] - e->obj.pos[2] };
        sp.pos[0] = Vec_Dot16(rel, e->obj.rot);
        sp.pos[1] = Vec_Dot16(rel, e->obj.rot + 3);
        sp.pos[2] = Vec_Dot16(rel, e->obj.rot + 6);
        ent = e;
    }
    else if (surf > 0xb)
        return;
    int cls;
    switch (surf)
    {
    case 0:
    case 1:
    case 2:
    case 8:
        cls = 2;
        break;
    case 3:
        cls = 0x14;
        break;
    case 4:
        cls = 0x11;
        break;
    case 5:
        cls = 3;
        break;
    case 6:
        cls = 9;
        break;
    case 9:
    case 10:
        cls = 10;
        break;
    case 0xb:
        cls = 0x31;
        break;
    default:
        return;
    }
    int yaw, pitch, speed;
    int sprayed = surf >= 4 && surf <= 0xb && surf != 7 && surf != 8;
    FxInst* in = (FxInst*)PTR(w, 0x30);
    if (in)
    {
        if (in->anchor)
            memcpy(in->anchor->pos, sp.pos, sizeof(sp.pos));
        if (sprayed)
        {
            Wheel_SprayParams(c, surf, &yaw, &pitch, &speed);
            Wheel_SetSpray(in, 0, yaw, pitch, speed);
        }
        in->handler(1, in, 1, &sp);
        return;
    }
    FxCallback* cb = (FxCallback*)(w + 0x50);
    cb->user = w + 0x28;
    cb->fn = Wheel_SmokeDone;
    SETPTR(w, 0x4c, c);
    w[0x59] = (uint8_t)surf;
    if (sprayed)
    {
        Wheel_SprayParams(c, surf, &yaw, &pitch, &speed);
        if (surf == 0xb && IA(c, 0xfb) > 0x4000)
            cls = 0x32;
        Wheel_SetSpray(NULL, cls, yaw, pitch, speed);
    }
    SETPTR(w, 0x30, Fx_Spawn(ent, cls, &sp, 0, cb));
}

/* ---- dibujo ---- */

typedef struct
{
    int16_t x, y, z, pad;
} MarkVert;

static MarkVert s_mv[0x66];          /* DAT_800b6a20 */
static uint8_t  s_mvShade[0x33];     /* DAT_800b6dc0 */

/* 800688c8: proyecta con la rotacion de la camara; si la division se desborda (vertice muy cerca) lo
 * proyecta 32 veces mas lejos en la misma direccion y divide la profundidad */
static void Marks_Project(int n, uint32_t* sxy, int* sz, uint8_t* fl)
{
    for (int i = 0; i < n; i++)
    {
        Gte_SetV0(s_mv[i].x, s_mv[i].y, s_mv[i].z);
        doCOP2(GTE_OP_RTPS);
        uint32_t flag = CFC2(31);
        int shift = 0;
        if (flag & 0x20000)
        {
            int m3 = MFC2_S(27);
            if (m3 < 0x20)
            {
                fl[i] = 1;
                continue;
            }
            int m1 = MFC2_S(25), m2 = MFC2_S(26);
            int tx = (int)CFC2(5), ty = (int)CFC2(6), tz = (int)CFC2(7);
            Gte_SetTrans(m1 << 5, m2 << 5, m3 << 5);
            Gte_SetV0(0, 0, 0);
            doCOP2(GTE_OP_RTPS);
            flag = CFC2(31);
            Gte_SetTrans(tx, ty, tz);
            shift = 5;
        }
        int z = (int)MFC2(19) >> shift;
        uint32_t xy = MFC2(14);
        if (flag & 0x1c66000)
        {
            fl[i] = 1;
            continue;
        }
        sxy[i] = xy;
        sz[i] = z;
        fl[i] = (uint8_t)(((unsigned)(xy & 0xffff) < 2u * 0xa0) << 6 | ((unsigned)(xy >> 16) < 2u * 0x78) << 7);
    }
}

/* Vertices de una marca a cada lado (mundo + medio ancho * 2, respecto a la camara a 1/8) */
static void Mark_Verts(int n, const int* base, const int8_t* x, int shade)
{
    for (int s = 0; s < 2; s++)
    {
        int sign = s ? -2 : 2;
        MarkVert* v = &s_mv[n + s];
        v->x = (int16_t)(((base[0] + x[0] * sign - g_Camera.pos[0]) >> 4) << 1);
        v->y = (int16_t)(((base[1] + x[1] * sign - g_Camera.pos[1]) >> 4) << 1);
        v->z = (int16_t)(((base[2] + x[2] * sign - g_Camera.pos[2]) >> 4) << 1);
    }
    s_mvShade[n >> 1] = (uint8_t)shade;
}

/* 80076a1c: las marcas de un coche */
void Marks_Draw(uint8_t* c, int view)
{
    uint32_t e0 = 0x800ab560u;   /* pagina, u0, v0, u1, v1, region, tpage */
    int slot = Exe_S32(e0);
    int u0 = Exe_S32(e0 + 4), v0 = Exe_S32(e0 + 8), u1 = Exe_S32(e0 + 12), v1 = Exe_S32(e0 + 16);
    const uint8_t* reg = Cpag_FindRegion(slot, u0, v0);
    int du, dv, tp = TexSlot_GetTPage(slot, 0, &du, &dv);
    uint16_t clut = reg ? NR_Read16(reg + 4) : 0;
    uint16_t tpage = (uint16_t)(((tp < 0 ? 0 : tp) & ~0x60) | 0x40);
    for (int k = 0; k < 4; k++)
    {
        uint8_t* w = WHEEL(c, k);
        if (!W_MARKS(w) || !W_SEGS(w))
            continue;
        int si = I16(w, 0x24);
        do
        {
            uint8_t* s = SEG(w, si);
            if (!(s[0xe] & 1))
                break;
            int draw = !(s[0xe] & 2);
            if (!draw)
                for (int j = 0; j < 5 && !draw; j++)
                    if (s[0x11 + j * 2] && Track_SegFlags(view, s[0x10 + j * 2]))
                        draw = 1;
            if (draw)
            {
                int n = 0;
                int end = (s[0xe] & 2) ? NEXT_MARK((int8_t)s[0xd]) : I16(w, 0x20);
                int i = (s[0xe] & 4) ? (int8_t)s[0xc] : NEXT_MARK((int8_t)s[0xc]);
                while (i != end && n < 0x62)
                {
                    const uint8_t* m = MARK(w, i);
                    int b[3] = { I32(s, 0) + *(const int16_t*)m * 0x10, I32(s, 4) + *(const int16_t*)(m + 2) * 0x10,
                                 I32(s, 8) + *(const int16_t*)(m + 4) * 0x10 };
                    Mark_Verts(n, b, (const int8_t*)(m + 6), s[0xf] == 1 ? m[0xa] : m[0xa] >> 1);
                    n += 2;
                    i = NEXT_MARK(i);
                }
                if (n && !(s[0xe] & 2))
                {
                    /* tramo abierto: hasta la rueda si se ha movido */
                    int d[3] = { I32(w, 0x14) - I32(w, 8), I32(w, 0x18) - I32(w, 0xc), I32(w, 0x1c) - I32(w, 0x10) };
                    if (Abs_(d[0]) > 0x3f || Abs_(d[1]) > 0x3f || Abs_(d[2]) > 0x3f)
                    {
                        int x[3];
                        Vec_Normalize(d);
                        Vec_Cross(VEC(c, 0x674 + k * 0xc), d, x);
                        Vec_Scale(0x80, x);
                        int8_t h[3] = { 0, 0, 0 };
                        int t = s[0xf];
                        int shade = (t == 1 || (t >= 3 && t <= 6)) ? w[4] : w[4] >> 1;
                        for (int s2 = 0; s2 < 2; s2++)
                        {
                            MarkVert* v = &s_mv[n + s2];
                            int sg = s2 ? -1 : 1;
                            v->x = (int16_t)(((I32(w, 0x14) + x[0] * sg - g_Camera.pos[0]) >> 4) << 1);
                            v->y = (int16_t)(((I32(w, 0x18) + x[1] * sg - g_Camera.pos[1]) >> 4) << 1);
                            v->z = (int16_t)(((I32(w, 0x1c) + x[2] * sg - g_Camera.pos[2]) >> 4) << 1);
                        }
                        (void)h;
                        s_mvShade[n >> 1] = (uint8_t)shade;
                        n += 2;
                    }
                }
                if (n)
                {
                    uint32_t sxy[0x66];
                    int sz[0x66];
                    uint8_t fl[0x66];
                    Render_LoadCameraRotation();
                    Marks_Project(n, sxy, sz, fl);
                    for (int q = 0; q < (n >> 1) - 1; q++)
                    {
                        int a = q * 2;
                        if ((fl[a] | fl[a + 1] | fl[a + 2] | fl[a + 3]) & 1)
                            continue;
                        POLY_GT4* p = (POLY_GT4*)Gfx_AllocPrim(sizeof(POLY_GT4));
                        if (!p)
                            return;
                        setPolyGT4(p);
                        setSemiTrans(p, 1);
                        int c0 = s_mvShade[q], c1 = s_mvShade[q + 1];
                        setRGB0(p, c0, c0, c0);
                        setRGB1(p, c0, c0, c0);
                        setRGB2(p, c1, c1, c1);
                        setRGB3(p, c1, c1, c1);
                        setXY4(p, (int16_t)sxy[a], (int16_t)(sxy[a] >> 16), (int16_t)sxy[a + 1], (int16_t)(sxy[a + 1] >> 16),
                               (int16_t)sxy[a + 2], (int16_t)(sxy[a + 2] >> 16), (int16_t)sxy[a + 3],
                               (int16_t)(sxy[a + 3] >> 16));
                        setUV4(p, u0, v1, u1, v1, u0, v0, u1, v0);
                        p->clut = clut;
                        p->tpage = tpage;
                        Gfx_AddPrimDepth((sz[a] + sz[a + 1] + sz[a + 2] + sz[a + 3]) >> 3, p);
                    }
                }
            }
            si = NEXT_SEG(si);
        } while (si != I16(w, 0x24));
    }
}
