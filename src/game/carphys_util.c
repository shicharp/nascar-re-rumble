#include <string.h>
#include "nr.h"
#include "gte.h"
#include "model.h"
#include "carphys_int.h"
#include "snd.h"

/* ---------------------------------------------------------------- aleatorios (800182ec) */

static uint32_t s_seed;             /* DAT_800af0e4 */
static int      s_randTab[2][20];   /* DAT_800afa40 / DAT_800afa90 */
static int      s_randIdx[2];       /* DAT_800af5a0 / DAT_800af5a4 */

/* 80018264: Park-Miller (16807) */
static int Rand_Seed(void)
{
    uint32_t hi = (s_seed >> 16) * 0x41a7;
    s_seed = (s_seed & 0xffff) * 0x41a7 + (hi & 0x7fff) * 0x10000 + (hi >> 15);
    if ((int32_t)s_seed < 0)
        s_seed -= 0x7fffffff;
    return (int)s_seed;
}

/* 1/raiz: tabla de 0x400 entradas {mantisa << 5 | exponente} (8001836c) */
static uint16_t s_invSqrt[0x400];   /* DAT_800af5a8 */

static int Lzc(uint32_t v)
{
    int n = 0;
    if (v == 0)
        return 32;
    while (!(v & 0x80000000u))
    {
        v <<= 1;
        n++;
    }
    return n;
}

/* 80017a20: 2^33 / i con la division por restas del original */
static uint32_t Div2Pow33(uint32_t d)
{
    uint32_t r = 2, q = 0, carry = 0;
    for (int k = 0x20; k > 0; k--)
    {
        q <<= 1;
        if (carry || d < r)
        {
            r -= d;
            q |= 1;
        }
        carry = r & 0x80000000u;
        r <<= 1;
    }
    return q;
}

/* 800179d0: raiz entera bit a bit */
static uint32_t ISqrt(uint32_t v)
{
    uint32_t rem = 0, root = 0;
    for (int k = 0x17; k != -1; k--)
    {
        rem = rem << 2 | v >> 30;
        uint32_t r2 = root * 2, t = root * 4 + 1;
        v <<= 2;
        if (t <= rem)
        {
            rem -= t;
            r2++;
        }
        root = r2;
    }
    return root;
}

void Rand_Init(void)
{
    for (int i = 0; i < 0x400; i++)
    {
        uint32_t s = ISqrt(Div2Pow33((uint32_t)i));
        int e = 0x15;
        if (s != 0)
            e = 0x15 - Lzc(s);
        if (e > 0)
            s >>= e;
        s_invSqrt[i] = (uint16_t)(s << 5 | (uint32_t)(e & 0x1f));
    }
    Rand_SetSeed(0x457);
}

/* 800182ec: rellena las dos tablas de aleatorios desde la semilla (Race_Load la pone con DAT_800af17c, los frames del
 * front-end, o con la de la repeticion) */
void Rand_SetSeed(int seed)
{
    s_randIdx[0] = s_randIdx[1] = 0x14;
    s_seed = (uint32_t)seed;
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < 0x14; i++)
            s_randTab[t][i] = Rand_Seed();
}

static int Rand_Next(int t)
{
    int i = s_randIdx[t] - 1, j = s_randIdx[t] + 2;
    if (i < 0)
        i = 0x13;
    else if (j > 0x13)
        j = s_randIdx[t] - 0x12;
    s_randIdx[t] = i;
    s_randTab[t][i] += s_randTab[t][j];
    return s_randTab[t][i];
}

int Rand_A(void)
{
    return Rand_Next(0);
}

int Rand_B(void)
{
    return Rand_Next(1);
}

/* 80018448 */
int Math_InvSqrt(int x)
{
    int sh = 0x16 - (Lzc((uint32_t)x) & ~1);
    if (sh < 1)
        sh = 0;
    int idx = x >> sh;
    if (idx < 0 || idx >= 0x400)
        idx = 0x3ff;
    int e = s_invSqrt[idx];
    return (int)((((uint32_t)e >> 5) << (e & 0x1f)) >> (sh >> 1));
}

/* ---------------------------------------------------------------- matrices (columnas 16.16) */

void Mat_MulT(const int* m, const int* v, int* out)
{
    int x = v[0], y = v[1], z = v[2];
    out[0] = M16(x, m[0]) + M16(y, m[3]) + M16(z, m[6]);
    out[1] = M16(x, m[1]) + M16(y, m[4]) + M16(z, m[7]);
    out[2] = M16(x, m[2]) + M16(y, m[5]) + M16(z, m[8]);
}

void Mat_MulTAdd(const int* m, const int* v, int* out)
{
    Mat_MulT(m, v, out);
    out[0] += m[9];
    out[1] += m[10];
    out[2] += m[11];
}

void Mat_Mul(const int* m, const int* v, int* out)
{
    int x = v[0], y = v[1], z = v[2];
    int a = M16(x, m[0]) + M16(y, m[1]) + M16(z, m[2]);
    int b = M16(x, m[3]) + M16(y, m[4]) + M16(z, m[5]);
    int c = M16(x, m[6]) + M16(y, m[7]) + M16(z, m[8]);
    out[0] = a;
    out[1] = b;
    out[2] = c;
}

/* ---------------------------------------------------------------- GTE */

void Gte_LoadObjMatrix(const int* m)
{
    Gte_SetRot(m[0] >> 2, m[3] >> 2, m[6] >> 2, -m[1] >> 2, -m[4] >> 2, -m[7] >> 2, m[2] >> 2, m[5] >> 2,
               m[8] >> 2);
    Gte_SetTrans(m[9] << 2, -m[10] * 4, m[11] << 2);
}

void Gte_SetTransShift(const int* pos, int shift)
{
    Gte_SetTrans(pos[0] >> shift, -pos[1] >> shift, pos[2] >> shift);
}

void Gte_Rt(const short* v, int* out, int shift)
{
    MTC2((uint32_t)(uint16_t)v[0] | (uint32_t)v[1] << 16, 0);
    MTC2((uint32_t)(int32_t)v[2], 1);
    doCOP2(GTE_OP_RTV0TR);
    out[0] = MFC2_S(25) >> shift;
    out[1] = -(MFC2_S(26) >> shift);
    out[2] = MFC2_S(27) >> shift;
}

static const short* Obj_ModelVertex(uint8_t* obj, int idx)
{
    ModelHdr* m = (ModelHdr*)PTR(obj, 0);
    int frame = I16(obj, 0x48);
    if (!((m->flags >> 25) & 1))
    {
        int n;
        const uint8_t* verts = Model_FrameVerts(m, frame, &n);
        int hp = m->hardpoint[idx];
        if (hp == 0xff)
            return NULL;
        return (const short*)(verts + hp * 8);
    }
    if (!m->points)
        return NULL;
    return (const short*)(m->points + frame * m->points[0] * 2 + idx * 2 + 1);
}

/* Obj_GetPoint 80013c20: punto 'idx' del modelo en el mundo con la GTE (load = cargar antes la matriz) */
void Obj_GetPointGte(uint8_t* obj, int idx, int* out, int load)
{
    if (!PTR(obj, 0))
    {
        out[0] = I32(obj, 0x28);
        out[1] = I32(obj, 0x2c);
        out[2] = I32(obj, 0x30);
        return;
    }
    if (load)
        Gte_LoadObjMatrix(VEC(obj, 4));
    const short* v = Obj_ModelVertex(obj, idx);
    static const short zero[3] = { 0, 0, 0 };
    Gte_Rt(v ? v : zero, out, 2);
}

/* 8004d36c: punto 'idx' del modelo sin transformar */
void Obj_GetModelPoint(uint8_t* obj, int idx, int* out)
{
    out[0] = out[1] = out[2] = 0;
    if ((unsigned)idx >= 0xc || !PTR(obj, 0))
        return;
    const short* v = Obj_ModelVertex(obj, idx);
    if (!v)
        return;
    out[0] = v[0];
    out[1] = v[1];
    out[2] = v[2];
}

/* ---------------------------------------------------------------- caja del coche */

/* 80059b04: corte del rayo 'dir' (desde el centro, en ejes del coche; se normaliza) con la caja de
 * semiextensiones 'half' desplazada 'base' en z. 1 = corte en out. */
int Box_RayHit(const int* half, int* dir, int base, int* out)
{
    int r = Math_InvSqrt((int)((uint32_t)dir[0] * (uint32_t)dir[0] + (uint32_t)dir[1] * (uint32_t)dir[1] +
                               (uint32_t)dir[2] * (uint32_t)dir[2]));
    dir[0] = M16(dir[0], r);
    dir[1] = M16(dir[1], r);
    dir[2] = M16(dir[2], r);
    int hx = dir[0] < 1 ? -half[0] : half[0];
    int hy = dir[1] < 1 ? -half[1] : half[1];
    int hz = dir[2] < 1 ? -half[2] : half[2];
    int ax = Abs_(dir[0]), ay = Abs_(dir[1]), az = Abs_(dir[2]);
    int t;
    if (ay < 8 && az < 8)
        goto faceX;
    if (ax < 8)
    {
        if (az >= 8)
        {
            if (ay >= 8)
                goto diag;
            goto faceZ;
        }
        goto faceY;
    }
diag:
    {
        int zz = dir[2] < 1 ? half[2] + base : half[2] - base;
        if (zz * ax - half[0] * az < 0)
        {
            if (half[1] * az - zz * ay > 0)
                goto faceZ;
            goto faceY;
        }
        if (half[0] * ay < half[1] * ax)
            goto faceX;
        goto faceY;
    }
faceX:
    if (ax < 8)
        return 0;
    t = Div_(hx << 8, dir[0]);
    out[0] = hx;
    out[1] = (t * dir[1]) >> 8;
    out[2] = base + ((t * dir[2]) >> 8);
    return 1;
faceY:
    if (ay < 8)
        return 0;
    t = Div_(hy << 8, dir[1]);
    out[1] = hy;
    out[2] = base + ((t * dir[2]) >> 8);
    out[0] = (t * dir[0]) >> 8;
    return 1;
faceZ:
    if (az < 8)
        return 0;
    t = Div_((hz - base) * 0x100, dir[2]);
    out[2] = hz;
    out[1] = (t * dir[1]) >> 8;
    out[0] = (t * dir[0]) >> 8;
    return 1;
}

/* Cola de sonidos de la carrera (DAT_800b52f0, 20 entradas {0, id, x, z, -1}; la toca 80042234 en 8003e848) */
void Fx_Push(int kind, const int* pos, int life)
{
    (void)life;
    Snd_Queue(kind, pos[0], pos[2]);
}
