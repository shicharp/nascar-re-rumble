/* Camaras de TV (ver tvcam.h).
 * Original (0x80071F50-0x800746A0):
 *  - Emitter_Create 80071f50: camara de 0x48 bytes desde el tACT tipo 0x54 (+0 id, +8 posicion, +0x14 / +0x16 rumbo
 *    e inclinacion, +0x18 preset de la proyeccion, +0x1A tipo, +0x1C radio de la zona (0: 0x1800), +0x1E altura
 *    sobre el suelo / 16, +0x20 banderas, +0x24 / +0x26 medio ancho / 8 de la zona en z / x (0: circulo con el
 *    radio), +0x28 / +0x2A desplazamiento / 16 de la camara en z / x). Banderas: 1 (invertida al guardar) no
 *    cambia nunca a una virtual, 2 rumbo relativo a la direccion del desplazamiento, 4 lateral por el lado
 *    contrario (tipo 6), 0x100 << k excluye la virtual k + 2, 0x1000000 virtual.
 *  - 80072278 + 80071e4c: 8 virtuales en DAT_800af928 (la lista se llena al reves): 0 tipo 8 (en el coche),
 *    1 tipo 9 (helicoptero: sigue a la primera entidad de tipo 5, sin uso), 2 tipo 11 (lateral fija), 3 tipo 10
 *    (perseguidora con muelle; tambien cuando el coche vuela o vuelca), 4 tipo 7 (grua), 5 tipo 3 (rueda
 *    delantera), 6 tipo 6 (lateral), 7 tipo 5 (orbita, la de la llegada).
 *  - 80074118 elige: la camara de pista cuya zona contiene al coche mas cercana (800746a0 / 800739bc / 80074628)
 *    o, sin ninguna (o por azar), una virtual (80073bf8 / 80073a9c). 8007401c la activa (0x640 tics, estado de
 *    su tipo) y TvCam_Apply 80073ecc coloca la camara. 80074264 la actualiza cada frame (80073d64 por tipo) y
 *    cambia cuando el tipo lo pide: las de pista al salir el coche de su zona, las virtuales al acabar su tiempo
 *    o al entrar el coche en una zona.
 *  - Las virtuales que mueven la camara con el coche le anaden el traqueteo de 8005d2c8 en la superficie 0xF.
 * Las llamadas a RaceCam_AvoidWalls 8005d6b8 de los tipos 3, 6, 7 y 11 no tienen efecto (80074264 pisa el punto
 * corregido con la posicion de la camara); solo cuenta la del tipo 10. */
#include <stdlib.h>
#include <string.h>
#include "nr.h"
#include "render.h"
#include "track.h"
#include "cnet.h"
#include "vecmath.h"
#include "racecar.h"
#include "ai.h"
#include "carphys_int.h"
#include "tvcam.h"

#define TV_VIRTUAL 0x1000000u
#define TV_NUM_VIRTUAL 8

typedef struct TvCam
{
    struct TvCam* next;
    TrackPos tp;              /* +0x04 */
    int      pos[3];          /* +0x08 */
    int16_t  yaw, pitch;      /* +0x14 / +0x16 */
    int16_t  id;              /* +0x18 */
    uint8_t  preset, type;    /* +0x1A / +0x1B */
    int      r2;              /* +0x1C radio al cuadrado >> 8 */
    uint32_t flags;           /* +0x20 */
    int16_t  sizeX, sizeZ;    /* +0x24 / +0x26 */
    int16_t  offZ, offX;      /* +0x28 / +0x2A */
    int16_t  timer;           /* +0x2C */
    union                     /* +0x30: caja de la zona (pista) o estado del tipo (virtuales) */
    {
        int32_t i[6];
        int16_t s[12];
    } u;
    void*    ent;             /* +0x30 del tipo 9 (entidad de tipo 5; el port no las tiene) */
} TvCam;

typedef struct
{
    TvCam*   cur;             /* DAT_800af930 */
    uint8_t  active;          /* DAT_800af8c8 */
    int      eye[3];          /* camara de la vista +0x1C (DAT_800b617c) */
    int      shake, shakeVel; /* +0x38 / +0x3C (DAT_800b6198 / DAT_800b619c) */
    TrackPos tpos;            /* DAT_800af6d4 +0x6C */
} TvView;

static TvCam*  s_list;                    /* DAT_800af938 */
static TvCam*  s_virtual[TV_NUM_VIRTUAL]; /* DAT_800af928 */
static int     s_numVirtual;              /* DAT_800af92c */
static TvView  s_tv[2];
static int     s_lastTrackId = -1;        /* DAT_800af32c */

static int TvCam_LogOn(void)
{
    static int s_on = -1;
    if (s_on < 0)
        s_on = getenv("NR_TVLOG") != NULL;
    return s_on;
}

/* ---------------------------------------------------------------- creacion */

static TvCam* TvCam_New(const uint8_t* def)
{
    TvCam* c = (TvCam*)calloc(1, sizeof(TvCam));
    if (!c)
        return NULL;
    c->next = s_list;
    s_list = c;
    uint32_t flags = NR_Read32(def + 0x20);
    c->tp.seg = 0xff;
    c->tp.curve = -1;
    if (!(flags & TV_VIRTUAL))
    {
        int p[3] = { (int)NR_Read32(def + 8), (int)NR_Read32(def + 0xc), (int)NR_Read32(def + 0x10) };
        c->pos[0] = p[0];
        c->pos[2] = p[2];
        c->sizeZ = (int16_t)NR_Read16(def + 0x24);
        c->sizeX = (int16_t)NR_Read16(def + 0x26);
        c->offZ = (int16_t)NR_Read16(def + 0x28);
        c->offX = (int16_t)NR_Read16(def + 0x2a);
        /* esquina de la caja: la zona va de -medio a +medio ancho (en / 8) alrededor de la posicion */
        c->u.i[0] = c->pos[2] - c->sizeZ * 8;
        c->u.i[1] = c->pos[0] - c->sizeX * 8;
        int h = (int16_t)NR_Read16(def + 0x1e) * 0x10;
        c->pos[1] = Track_GroundHeight(&c->tp, p, 0) + h;
        if (Track_FindSurface(&c->tp, p, NULL, 1) < 0)
        {
            /* 800655a0: fuera de la pista, la altura del punto de TCRV mas cercano */
            int k = Track_NearestCurvePoint(c->pos), cp[3];
            Track_GetCurvePoint(k, cp);
            c->tp.curve = (int16_t)k;
            c->tp.seg = (uint8_t)Track_FindSegmentByCurve(k);
            c->pos[1] = cp[1] + h;
        }
    }
    int16_t yaw = (int16_t)NR_Read16(def + 0x14);
    if (flags & 2)
        yaw = (int16_t)((Math_Atan2(c->offX << 4, c->offZ << 4) & 0xfff) + yaw);
    c->yaw = yaw;
    c->pitch = (int16_t)NR_Read16(def + 0x16);
    c->preset = def[0x18];
    c->type = def[0x1a];
    c->id = (int16_t)NR_Read16(def);
    int r = (int16_t)NR_Read16(def + 0x1c);
    if (r == 0)
        r = 0x1800;
    c->r2 = r * r >> 8;
    c->flags = flags ^ 1;
    switch (c->type)
    {
    case 7:   /* grua: altura de -0x1400 a 0, 0x280 por tic; alcance 0xA00 / 0x200 */
        c->u.s[3] = -0x1400;
        c->u.s[4] = 0;
        c->u.s[6] = (int16_t)((-c->u.s[3] * 0x100) >> 11);
        c->u.s[7] = 0xa00;
        c->u.s[8] = 0x200;
        c->u.s[9] = 0x200;
        c->u.s[0] = -0x1000;
        c->u.s[1] = -0x800;
        break;
    case 5:   /* orbita */
        c->u.i[0] = 0x2000;
        c->u.i[1] = 0x100;
        c->yaw = 0x400;
        c->u.i[2] = 0;
        c->pitch = -0x300;
        break;
    case 9:
        c->ent = NULL;   /* 80071f10: primera entidad de tipo 5 */
        break;
    }
    return c;
}

/* 80071f50 */
void TvCam_AddFromDef(const uint8_t* def)
{
    TvCam* c = TvCam_New(def);
    if (c && TvCam_LogOn())
        NR_Log("[tv] camara %d tipo %d en (%d, %d, %d) zona %d x %d radio^2 %d desp (%d, %d) preset %d banderas %x\n",
               c->id, c->type, c->pos[0], c->pos[1], c->pos[2], c->sizeX, c->sizeZ, c->r2, c->offX, c->offZ,
               c->preset, c->flags);
}

/* 80072320: virtuales (80072278) y su tabla (80071e4c) */
void TvCam_PostLoad(void)
{
    static const uint8_t types[TV_NUM_VIRTUAL] = { 5, 6, 3, 7, 10, 11, 9, 8 };
    uint8_t def[0x2c];
    memset(def, 0, sizeof(def));
    def[0x18] = 1;
    def[0x23] = 1;   /* banderas 0x1000000 */
    for (int k = 0; k < TV_NUM_VIRTUAL; k++)
    {
        def[0x1a] = types[k];
        TvCam_New(def);
    }
    s_numVirtual = 0;
    for (TvCam* c = s_list; c; c = c->next)
        if ((c->flags & TV_VIRTUAL) && s_numVirtual < TV_NUM_VIRTUAL)
            s_virtual[s_numVirtual++] = c;
}

/* 80072254 / RaceCam_ResetPresets 80072348 */
void TvCam_Free(void)
{
    while (s_list)
    {
        TvCam* n = s_list->next;
        free(s_list);
        s_list = n;
    }
    s_numVirtual = 0;
    memset(s_tv, 0, sizeof(s_tv));
    s_lastTrackId = -1;
}

/* ---------------------------------------------------------------- utilidades */

static RaceCar* TvCam_Target(int view)
{
    return RaceCam_GetTarget(view);
}

/* 8005d258: fisica del coche que sigue la vista */
static uint8_t* TvCam_Phys(int view)
{
    RaceCar* t = TvCam_Target(view);
    return t && (t->type == 1 || t->type == 0x11) ? t->phys : NULL;
}

/* La camara con la base de la matriz m (columnas = ejes de la camara en el mundo) en pos (80017a68) */
static void TvCam_SetMatrix(const int* m, const int* pos)
{
    for (int k = 0; k < 3; k++)
        for (int j = 0; j < 3; j++)
            g_Camera.view[3 * k + j] = m[3 * j + k];
    memcpy(g_Camera.pos, pos, sizeof(g_Camera.pos));
}

/* 800194a0 */
static int TvCam_ApproxLen(const int* v)
{
    int a = Abs_(v[0]), b = Abs_(v[1]), d = Abs_(v[2]);
    if (a < b)
    {
        int t = a;
        a = b;
        b = t;
    }
    if (a < d)
    {
        int t = a;
        a = d;
        d = t;
    }
    return a + ((b + d) >> 2);
}

/* 80074628: mayor * 15/16 + (los otros) * 6/16 */
static unsigned TvCam_Dist(int x, int y, int z)
{
    unsigned a = (unsigned)Abs_(x), b = (unsigned)Abs_(y), c = (unsigned)Abs_(z);
    if (a < b)
    {
        unsigned t = a;
        a = b;
        b = t;
    }
    if (a < c)
    {
        unsigned t = a;
        a = c;
        c = t;
    }
    return (a * 0xf + (b + c) * 6) >> 4;
}

/* 800739bc: el coche (x, z) dentro de la zona (caja o circulo) */
static int TvCam_Contains(int x, int z, const RaceCar* t, const TvCam* c)
{
    if (c->sizeX == 0 || c->sizeZ == 0)
    {
        int d[3] = { t->obj.pos[0] - c->pos[0], t->obj.pos[1] - c->pos[1], t->obj.pos[2] - c->pos[2] };
        return Vec_LenSq16(d) < c->r2;
    }
    return (unsigned)(z - c->u.i[0]) <= (unsigned)(c->sizeZ << 4) &&
           (unsigned)(x - c->u.i[1]) <= (unsigned)(c->sizeX << 4);
}

/* 800746a0: la camara de pista mas cercana cuya zona contiene al coche (el original mide la distancia con los
 * desplazamientos cambiados: z en x y x en z) */
static TvCam* TvCam_Nearest(int view)
{
    RaceCar* t = TvCam_Target(view);
    if (!t)
        return NULL;
    int x = t->obj.pos[0], z = t->obj.pos[2];
    TvCam* best = NULL;
    unsigned bd = 0x7fffffff;
    for (TvCam* c = s_list; c; c = c->next)
    {
        if ((c->flags & TV_VIRTUAL) || !TvCam_Contains(x, z, t, c))
            continue;
        unsigned d = TvCam_Dist(x - c->pos[0] - c->offZ * 0x10, t->obj.pos[1] - c->pos[1],
                                z - c->pos[2] - c->offX * 0x10);
        if (d < bd)
        {
            bd = d;
            best = c;
        }
    }
    return best;
}

/* 80073a9c: indice de una virtual (2..7) al azar, sin las que excluye la camara de pista y sin repetir */
static int TvCam_PickVirtual(int view, const TvCam* tc, unsigned r)
{
    if (tc)
    {
        int cand[8], n = 0;
        for (int k = 2; k < 8; k++)
            if (!(tc->flags & (1u << (k + 6))))
                cand[n++] = k;
        if (n)
        {
            int k = cand[r % (unsigned)n];
            if (s_virtual[k] != s_tv[view].cur)
                return k;
            return cand[(r + 1) % (unsigned)n];
        }
    }
    unsigned m = (unsigned)(s_numVirtual - 2);
    if (m == 0)
        return 2;
    int k = (int)(r % m) + 2;
    if (s_virtual[k] == s_tv[view].cur)
        k = (int)((r + 1) % m) + 2;
    return k;
}

/* 80073bf8 */
static TvCam* TvCam_RandomVirtual(const TvCam* tc, int view)
{
    unsigned r = (unsigned)Rand_B();
    if (s_numVirtual < TV_NUM_VIRTUAL)
        return NULL;
    return s_virtual[TvCam_PickVirtual(view, tc, r & 0x7fff)];
}

/* 8005d2c8: traqueteo vertical de la camara en la superficie 0xF (por encima de 0x33 de velocidad); copia el
 * punto en la camara de la vista y le suma el traqueteo */
static void TvCam_Shake(uint8_t* phys, int amp, int view, int* pos, int dt)
{
    TvView* v = &s_tv[view];
    unsigned r = (unsigned)Rand_B();
    if (!phys || U8(phys, 0x638) != 0xf || ((RaceCar*)ENT(phys))->speed < 0x33)
    {
        v->shake = v->shakeVel = 0;
        memcpy(v->eye, pos, sizeof(v->eye));
        return;
    }
    if ((r & 0xe) == 0xe)
        v->shake = (r & 1) ? amp : -amp;   /* 8005d2b0 */
    /* muelle hacia 0 con su amortiguacion, en 8.8 por tic */
    int a = ((v->shake * -0x80000) >> 8) * dt >> 8;
    if (a > 1000)
        a = 1000;
    else if (a < -1000)
        a = -1000;
    v->shakeVel += a;
    int damp = ((v->shakeVel * -0x400) >> 8) * dt >> 8;
    if (v->shakeVel < 1)
    {
        v->shakeVel += damp;
        if (v->shakeVel > 0)
            v->shakeVel = 0;
    }
    else
    {
        v->shakeVel += damp;
        if (v->shakeVel < 0)
            v->shakeVel = 0;
    }
    v->shake += v->shakeVel * dt >> 8;
    if (v->shake > amp)
    {
        v->shakeVel = 0;
        v->shake = amp;
    }
    else if (v->shake < -amp)
    {
        v->shakeVel = 0;
        v->shake = -amp;
    }
    memcpy(v->eye, pos, sizeof(v->eye));
    pos[1] += v->shake;
}

/* RaceCam_AvoidWalls 8005d6b8 (con limite >= 5000): aparta el ojo de las paredes que cruza un triangulo de
 * 0x400 a su alrededor */
static int TvCam_AvoidWalls(int view, int lim)
{
    TvView* v = &s_tv[view];
    RaceCar* t = TvCam_Target(view);
    if (t)
    {
        int d[3] = { t->obj.pos[0] - v->eye[0], t->obj.pos[1] - v->eye[1], t->obj.pos[2] - v->eye[2] };
        if (TvCam_ApproxLen(d) > 0x6000)
            return 0;
    }
    int e[3][3] = {
        { v->eye[0], v->eye[1], v->eye[2] + 0x400 },
        { v->eye[0] + 0x400, v->eye[1], v->eye[2] - 0x400 },
        { v->eye[0] - 0x400, v->eye[1], v->eye[2] - 0x400 },
    };
    for (int i = 0; i < 3; i++)
    {
        int a = i, b = i == 2 ? 0 : i + 1;
        int n[3], hit[3];
        if (!Track_RayWall(e[a], e[b], n, hit))
            continue;
        int d[3] = { hit[0] - v->eye[0], hit[1] - v->eye[1], hit[2] - v->eye[2] };
        unsigned dot = (unsigned)Vec_Dot16(d, n);
        TrackPos tp = v->tpos;
        if (lim < 0x1389 || Track_FindSurface(&tp, v->eye, NULL, 0) != TRACK_NO_SURFACE)
            dot = ~dot;
        if ((int)dot < 0)
            for (int k = 0; k < 3; k++)
                n[k] = -n[k];
        int d2[3] = { hit[0] - e[a][0], hit[1] - e[a][1], hit[2] - e[a][2] };
        int k2 = Vec_Dot16(d2, n);
        if (k2 < 0)
        {
            int d3[3] = { hit[0] - e[b][0], hit[1] - e[b][1], hit[2] - e[b][2] };
            k2 = Vec_Dot16(d3, n);
        }
        Vec_Scale(k2, n);
        for (int k = 0; k < 3; k++)
            v->eye[k] += n[k];
        return 1;
    }
    return 0;
}

static void TvCam_Activate(int view, TvCam* c);

/* La camara cae fuera de la pista: pasa a la virtual k */
static void TvCam_Fallback(int view, int k)
{
    if (k >= s_numVirtual)
        return;
    s_tv[view].cur = s_virtual[k];
    TvCam_Activate(view, s_virtual[k]);
    s_tv[view].shake = s_tv[view].shakeVel = 0;
}

static int TvCam_TimeUp(const TvCam* c)
{
    return c->timer < 0;
}

/* ---------------------------------------------------------------- tipos */

/* 80073c64 (tipo 0): fija, mirando al coche; acaba cuando el coche sale de la zona */
static int TvCam_Fixed(int view, TvCam* c, int look)
{
    RaceCar* t = TvCam_Target(view);
    if (look)
    {
        int eye[3] = { c->pos[0] + c->offX * 0x10, c->pos[1], c->pos[2] + c->offZ * 0x10 };
        Camera_LookAt(eye, t->obj.pos);
    }
    return !TvCam_Contains(t->obj.pos[0], t->obj.pos[2], t, c);
}

/* 800723f8 (tipo 3): junto a la rueda delantera mirando hacia delante; distancias por modelo de coche */
static int TvCam_Wheel(int view, TvCam* c, int dt)
{
    uint8_t* ph = TvCam_Phys(view);
    RaceCar* t = (RaceCar*)ENT(ph);
    const uint8_t* desc = PTR(ph, 0x1b4);
    unsigned b = desc ? desc[1] : 0;
    int back = 0x700, side = 0x500, up = 0x380, ahead = 0x900;
    if (b >= 0x27 && b < 0x2e)
    {
        back = 0x800;
        ahead = 0x1000;
    }
    else if (b == 0x32)
    {
        side = 0x800;
        ahead = 0x1000;
    }
    else if (b == 0x31)
    {
        back = 0x900;
        side = 0x700;
        ahead = 0xf00;
    }
    else if (b == 0x34)
    {
        back = 0x800;
        side = 0x700;
        ahead = 0x1000;
    }
    else if (b == 0x36)
    {
        back = 0x400;
        up = 0x280;
        ahead = 0x1200;
    }
    int l[3], m[3], eye[3], at[3];
    Obj_GetLocalPoint(&t->obj, 0, l);
    l[0] -= side;
    l[1] += up;
    l[2] -= back;
    m[0] = l[0] + ahead;
    m[1] = l[1] - 0x100;
    m[2] = l[2] + 0x2000;
    Mat_MulTAdd(t->obj.rot, m, at);
    Mat_MulTAdd(t->obj.rot, l, eye);
    if (Track_FindSurface(&c->tp, eye, NULL, 0) < 0)
    {
        TvCam_Fallback(view, 0);
        return 0;
    }
    TvCam_Shake(ph, 5, view, eye, dt);
    Camera_LookAt(eye, at);
    return TvCam_TimeUp(c);
}

/* 8007264c (tipo 8): con la orientacion del coche, 0x100 por encima de su centro */
static int TvCam_OnCar(int view, TvCam* c)
{
    RaceCar* t = TvCam_Target(view);
    Track_FindSurface(&c->tp, t->obj.pos, NULL, 0);
    int p[3] = { t->obj.pos[0], t->obj.pos[1] + 0x100, t->obj.pos[2] };
    TvCam_SetMatrix(t->obj.rot, p);
    return TvCam_TimeUp(c);
}

/* 800727bc (tipo 6): por detras del coche a 0x800 de su punto 0, mirandolo; con la bandera 4 de la camara de
 * pista que la eligio, mas alta y mirando a un lado (el de la red de la IA si el coche va en su sentido) */
static int TvCam_Side(int view, TvCam* c, int dt)
{
    uint8_t* ph = TvCam_Phys(view);
    RaceCar* t = (RaceCar*)ENT(ph);
    int l[3], at[3] = { t->obj.pos[0], t->obj.pos[1], t->obj.pos[2] }, eye[3];
    Obj_GetLocalPoint(&t->obj, 0, l);
    l[0] = 0;
    l[2] += 0x800;
    int y = l[1] + 0x200;
    if (c->flags & 4)
    {
        y = l[1] + 0x400;
        int side[3] = { -M16(t->obj.rot[0], 0x800), -M16(t->obj.rot[1], 0x800), -M16(t->obj.rot[2], 0x800) };
        int sg = 1;
        const uint8_t* net = Ai_FirstNet();
        if (net)
        {
            int n0 = Cnet_FindNearest3D(net, t->obj.pos);
            int n1 = Cnet_Next(net, n0);
            int a[3], b[3];
            Cnet_NodePos(net, n0, a);
            Cnet_NodePos(net, n1, b);
            if (M16(b[0] - a[0], t->obj.rot[6]) + M16(b[2] - a[2], t->obj.rot[8]) < 1)
                sg = -1;
        }
        for (int k = 0; k < 3; k++)
            at[k] += sg * side[k];
    }
    l[1] = y;
    Mat_MulTAdd(t->obj.rot, l, eye);
    if (Track_FindSurface(&c->tp, eye, NULL, 0) < 0)
    {
        TvCam_Fallback(view, 0);
        return 0;
    }
    TvCam_Shake(ph, 0x20, view, eye, dt);
    Camera_LookAt(eye, at);
    return TvCam_TimeUp(c);
}

/* 80072ae8 (tipo 11): a un lado del coche (+-0x600, +-0xE00 al azar, 0x400 de alto) mirando a su centro */
static int TvCam_Fixed2(int view, TvCam* c, int dt)
{
    uint8_t* ph = TvCam_Phys(view);
    RaceCar* t = (RaceCar*)ENT(ph);
    int l[3] = { c->u.i[0], 0x400, c->u.i[1] }, eye[3];
    Mat_MulTAdd(t->obj.rot, l, eye);
    if (Track_FindSurface(&c->tp, eye, NULL, 0) < 0)
    {
        TvCam_Fallback(view, 0);
        return 0;
    }
    TvCam_Shake(ph, 0x20, view, eye, dt);
    int a[3] = { 0, 0, c->u.i[2] }, at[3];
    Mat_MulTAdd(t->obj.rot, a, at);
    Camera_LookAt(eye, at);
    return TvCam_TimeUp(c);
}

/* 80073808 (tipo 5): orbita alrededor del coche (elipse de 0x1000 x 0xC00, 0x500 de alto) del rumbo 0x800 al
 * 0xE00 */
static int TvCam_Orbit(int view, TvCam* c, int dt)
{
    uint8_t* ph = TvCam_Phys(view);
    RaceCar* t = (RaceCar*)ENT(ph);
    TvView* v = &s_tv[view];
    int s, co;
    Math_SinCos(c->yaw, &s, &co);
    s >>= 8;
    co >>= 8;
    int l[3] = { (s << 12) >> 8, 0x500, co * 0xc00 >> 8 };
    Mat_MulTAdd(t->obj.rot, l, v->eye);
    if (Track_FindSurface(&c->tp, v->eye, NULL, 0) < 0)
    {
        TvCam_Fallback(view, 3);
        return 0;
    }
    TvCam_Shake(ph, 0x20, view, v->eye, dt);
    Camera_LookAt(v->eye, t->obj.pos);
    c->yaw = (int16_t)(c->yaw + (int16_t)((unsigned)(c->u.i[1] * dt) >> 8));
    c->pitch = (int16_t)(c->pitch + (int16_t)((unsigned)(c->u.i[2] * dt) >> 8));
    int done = c->yaw > 0xe00;
    c->u.i[0] += c->u.i[3] * dt >> 8;
    return done;
}

/* 80073500 (tipo 7): grua que sube de detras del coche hasta su altura, acercandose de lado */
static int TvCam_Crane(int view, TvCam* c, int dt)
{
    uint8_t* ph = TvCam_Phys(view);
    RaceCar* t = (RaceCar*)ENT(ph);
    const uint8_t* desc = PTR(ph, 0x1b4);
    unsigned b = desc ? desc[1] : 0;
    int16_t* s = c->u.s;
    int x = s[7], wy = s[9];
    if (b == 0x33 || b == 0x34)
        x = s[7] + 0x200;
    else if (b == 0x31)
        x = s[7] + 0x100;
    else if (b == 0x35)
    {
        x = s[7] - 0x200;
        wy = s[9] - 0x200;
    }
    int cur = (int16_t)(uint16_t)((uint16_t)s[5] + (s[6] * dt >> 8));
    s[5] = (int16_t)cur;
    if (s[4] < cur)
        s[5] = s[4];
    else if (cur <= s[1])
        /* de s0 a s1 el lado pasa del minimo (s8) a x */
        x = s[0] < cur ? (x - s[8]) * (cur - s[0]) / (s[1] - s[0]) + s[8] : s[8];
    int y = 0;
    if (s[9] != 0)
        y = wy * (s[5] - s[3]) / (s[4] - s[3]);
    int l[3] = { x, y, s[5] }, eye[3];
    Mat_MulTAdd(t->obj.rot, l, eye);
    if (Track_FindSurface(&c->tp, eye, NULL, 0) < 0)
    {
        TvCam_Fallback(view, 0);
        return 0;
    }
    TvCam_Shake(ph, 0x20, view, eye, dt);
    int a[3] = { s[8], y, s[5] + 0x800 }, at[3];
    Mat_MulTAdd(t->obj.rot, a, at);
    Camera_LookAt(eye, at);
    return 0;
}

/* 80073054 (tipo 10): muelle que persigue al coche a 0x1800 en el plano y 0xC00 sobre el suelo, sin pasar de
 * +0x1A0 de la fisica por encima del suelo */
static int TvCam_Chase(int view, TvCam* c, int dt)
{
    uint8_t* ph = TvCam_Phys(view);
    RaceCar* t = (RaceCar*)ENT(ph);
    TvView* v = &s_tv[view];
    int* p = c->u.i;   /* +0x30 posicion, +0x3C velocidad */
    int g = Track_FindSurface(&c->tp, t->obj.pos, NULL, 0);
    int dx = t->obj.pos[0] - p[0], dz = t->obj.pos[2] - p[2];
    int len = Math_SquareRoot12(M16(dx, dx) + M16(dz, dz));
    int d = len * 4;
    if (g >= 1 && d > 0x100)
    {
        /* direccion en 16.16 (dx * 2^31 / (d / 2) >> 16) */
        unsigned h = (unsigned)(d >> 1);
        int nx = (int)(((long long)dx * (long long)(0x80000000u / h)) >> 16);
        int nz = (int)(((long long)dz * (long long)(0x80000000u / h)) >> 16);
        int k = M16(d - 0x1800, 0xc0000);
        int ax = M16(nx, k) - M16(p[3], 0x40000);
        int ay = M16(g - (p[1] - 0xc00), 0x20000) - M16(p[4], 0x20000);
        int az = M16(nz, k) - M16(p[5], 0x40000);
        p[3] += ax * dt >> 8;
        p[4] += ay * dt >> 8;
        p[5] += az * dt >> 8;
        p[0] += p[3] * dt >> 8;
        p[1] += p[4] * dt >> 8;
        p[2] += p[5] * dt >> 8;
        if (I32(ph, 0x1a0) < p[1] - g)
            p[1] = g + I32(ph, 0x1a0);
    }
    memcpy(v->eye, p, sizeof(v->eye));
    TvCam_AvoidWalls(view, 10000);
    memcpy(p, v->eye, 3 * sizeof(int));
    Camera_LookAt(p, t->obj.pos);
    return TvCam_TimeUp(c);
}

/* 80072c30 (tipo 9): entre la entidad de tipo 5 y el coche (sin ella no mueve la camara) */
static int TvCam_Heli(int view, TvCam* c)
{
    if (!c->ent)
        return 0;
    (void)view;
    return TvCam_TimeUp(c);
}

/* 80073d64: solo las fijas de pista (0 / 1) devuelven si han terminado; lo que devuelven las virtuales (el
 * tiempo, la orbita completa) se pierde y cambian solo por las zonas de las camaras de pista */
static int TvCam_Run(int view, TvCam* c, int dt)
{
    switch (c->type)
    {
    case 0:  return TvCam_Fixed(view, c, 1);
    case 1:  return TvCam_Fixed(view, c, 0);
    case 3:  TvCam_Wheel(view, c, dt); break;
    case 5:  TvCam_Orbit(view, c, dt); break;
    case 6:  TvCam_Side(view, c, dt); break;
    case 7:  TvCam_Crane(view, c, dt); break;
    case 8:  TvCam_OnCar(view, c); break;
    case 9:  TvCam_Heli(view, c); break;
    case 10: TvCam_Chase(view, c, dt); break;
    case 11: TvCam_Fixed2(view, c, dt); break;
    }
    return 0;
}

/* ---------------------------------------------------------------- activar y elegir */

/* 80072dd4: la perseguidora empieza donde esta la camara o, lejos, en el nodo de la red de la IA a 0x1800 por
 * detras del coche (si queda a menos de 0x6000), 0x600 mas arriba */
static void TvCam_ChaseInit(int view, TvCam* c)
{
    RaceCar* t = TvCam_Target(view);
    int* p = c->u.i;
    int d[3] = { g_Camera.pos[0] - t->obj.pos[0], g_Camera.pos[1] - t->obj.pos[1], g_Camera.pos[2] - t->obj.pos[2] };
    const uint8_t* net = Ai_FirstNet();
    if ((Math_SquareRoot12(Vec_LenSq16(d)) << 2) < 0x1801 || !net)
        memcpy(p, g_Camera.pos, 3 * sizeof(int));
    else
    {
        int back[3];
        for (int k = 0; k < 3; k++)
            back[k] = t->obj.pos[k] - M16(t->obj.rot[6 + k], 0x1800);
        Cnet_NodePos(net, Cnet_FindNearest3D(net, back), p);
        int e[3] = { p[0] - t->obj.pos[0], p[1] - t->obj.pos[1], p[2] - t->obj.pos[2] };
        if ((Math_SquareRoot12(Vec_LenSq16(e)) << 2) > 0x6000)
            memcpy(p, t->obj.pos, 3 * sizeof(int));
    }
    p[1] += 0x600;
    memcpy(s_tv[view].eye, p, sizeof(s_tv[view].eye));
    p[3] = p[4] = p[5] = 0;
    c->preset = 1;
}

/* TvCam_Apply 80073ecc: las de pista colocan la camara con su rumbo e inclinacion en su sitio mas el
 * desplazamiento; las virtuales se mueven una vez sin tiempo. Despues, el preset de la proyeccion */
static void TvCam_Apply(int view, TvCam* c)
{
    TvView* v = &s_tv[view];
    if (!(c->flags & TV_VIRTUAL))
    {
        int m[9];
        Math_RotMatrix(c->yaw, c->pitch, 0, m);
        int p[3] = { c->pos[0] + c->offX * 0x10, c->pos[1], c->pos[2] + c->offZ * 0x10 };
        TvCam_SetMatrix(m, p);
        v->tpos = c->tp;
        TvCam_Run(view, c, 0);
    }
    else
    {
        TvCam_Run(view, c, 0);
        if (Track_FindSurface(&v->tpos, g_Camera.pos, NULL, 1) == TRACK_NO_SURFACE)
            v->tpos.seg = 0xff;
    }
    Render_SetPreset(c->preset);   /* Camera_SetPreset 80016bac */
}

/* 8007401c */
static void TvCam_Activate(int view, TvCam* c)
{
    if (!c)
        return;
    c->timer = 0x640;
    switch (c->type)
    {
    case 5:   /* 80072d30 */
        c->preset = 1;
        c->u.i[0] = 0x2000;
        c->yaw = 0x800;
        c->pitch = -0x200;
        break;
    case 7:   /* 80072db4 */
        c->preset = 1;
        c->u.s[5] = c->u.s[3];
        break;
    case 8:   /* 80072dc8 */
        c->preset = 3;
        break;
    case 9:   /* 800734f4 */
        c->preset = 9;
        break;
    case 10:
        TvCam_ChaseInit(view, c);
        break;
    case 11:  /* 80072d54 */
        c->preset = 1;
        c->u.i[0] = (Rand_B() & 1) ? 0x600 : -0x600;
        c->u.i[1] = (Rand_B() & 1) ? 0xe00 : -0xe00;
        c->u.i[2] = 0;
        break;
    }
    if (TvCam_LogOn())
        NR_Log("[tv] t=%d vista %d: camara %s %d tipo %d\n", g_RaceTime, view,
               (c->flags & TV_VIRTUAL) ? "virtual" : "de pista", c->id, c->type);
    TvCam_Apply(view, c);
}

/* 80074574 */
void TvCam_SelectIndex(int view, int idx)
{
    if (idx < 0 || idx >= s_numVirtual || !TvCam_Phys(view))
        return;
    TvView* v = &s_tv[view];
    v->cur = s_virtual[idx];
    TvCam_Activate(view, v->cur);
    v->shake = v->shakeVel = 0;
    TvCam* tc = TvCam_Nearest(view);
    s_lastTrackId = tc ? tc->id : -1;
    v->active = 1;
}

/* 80074118: la camara de pista de la zona del coche o, sin ella (o por azar), una virtual */
static void TvCam_Choose(int view)
{
    TvView* v = &s_tv[view];
    TvCam* tc = TvCam_Nearest(view);
    if (!tc)
        v->cur = TvCam_RandomVirtual(NULL, view);
    else
    {
        unsigned r = 0;
        int virt = 1;
        if (tc->type == 2)
            r = (unsigned)Rand_B();
        else if (!(tc->flags & 1) || !((r = (unsigned)Rand_B()) & 1))
        {
            v->cur = tc;
            virt = 0;
        }
        if (virt)
        {
            int k = TvCam_PickVirtual(view, tc, r);
            uint32_t fl = tc->flags;
            TvCam_SelectIndex(view, k);
            v->cur->flags |= fl & 4;
        }
    }
    TvCam_Activate(view, v->cur);
    v->shake = v->shakeVel = 0;
}

/* RaceCam_CycleView 8005ed10 con 0 */
int TvCam_Enter(int view)
{
    TvView* v = &s_tv[view];
    if (!TvCam_Phys(view) || s_numVirtual < TV_NUM_VIRTUAL)
        return 0;
    TvCam_Choose(view);
    if (!v->cur)
        return 0;
    TvCam_Apply(view, v->cur);
    v->active = 1;
    return 1;
}

void TvCam_Leave(int view)
{
    s_tv[view].active = 0;
    Render_SetPreset(1);
}

int TvCam_IsActive(int view)
{
    return s_tv[view & 1].active;
}

/* 80074264 */
static void TvCam_Step(int view, int dt)
{
    TvView* v = &s_tv[view];
    TvCam* c = v->cur;
    if (!c || !TvCam_Phys(view))
        return;
    c->timer = (int16_t)(c->timer - dt);
    int done = TvCam_Run(view, c, dt);
    int g = Track_FindSurface(&v->cur->tp, g_Camera.pos, NULL, 1);
    /* nunca a menos de 0x380 del suelo */
    if (g != TRACK_NO_SURFACE && g_Camera.pos[1] - g < 0x380)
        g_Camera.pos[1] = g + 0x380;
    memcpy(v->eye, g_Camera.pos, sizeof(v->eye));
    if (done)
    {
        TvCam_Choose(view);
        return;
    }
    if (!(c->flags & TV_VIRTUAL))
        return;
    uint8_t* ph = TvCam_Phys(view);
    /* con el coche volando o volcando, la perseguidora */
    if (v->cur->type != 10 && ((I32(ph, 0x198) & 0x50) == 0x50 || I16(ph, 0x184) != 0))
    {
        TvCam_Fallback(view, 3);
        return;
    }
    TvCam* tc = TvCam_Nearest(view);
    if (!tc)
    {
        if (s_lastTrackId < 0)
            return;
        v->cur = TvCam_RandomVirtual(NULL, view);
    }
    else
    {
        if (s_lastTrackId == tc->id)
            return;
        if (tc->type == 2)
        {
            unsigned r = (unsigned)Rand_B();
            TvCam_SelectIndex(view, TvCam_PickVirtual(view, tc, r));
            return;
        }
        v->cur = tc;
    }
    TvCam_Activate(view, v->cur);
    v->shake = v->shakeVel = 0;
    s_lastTrackId = -1;
}

/* RaceCam_Update 8005f034 en modo TV. El punto de pista de la camara se pone al dia cada frame si la camara esta
 * sobre la pista (el original solo lo cambia al activar una camara) */
void TvCam_Update(int view, int dt, TrackPos* camPos)
{
    TvView* v = &s_tv[view];
    if (!TvCam_Phys(view))
        return;
    if (!v->cur)
        TvCam_Choose(view);
    TvCam_Step(view, dt);
    TrackPos tp = v->tpos;
    if (Track_FindSurface(&tp, g_Camera.pos, NULL, 1) != TRACK_NO_SURFACE)
        v->tpos = tp;
    *camPos = v->tpos;
}
