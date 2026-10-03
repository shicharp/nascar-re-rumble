#include <stdlib.h>
#include <string.h>
#include "nr.h"
#include "pad.h"
#include "model.h"
#include "asset.h"
#include "render.h"
#include "track.h"
#include "cnet.h"
#include "vecmath.h"
#include "frontend.h"
#include "racecar.h"

RaceCar g_Cars[RACE_MAX_CARS];
int     g_NumCars;
int     g_RaceTime;
int     g_StartPos[3];
int     g_StartCurve;
int     g_HaveStart;

/* ---------------------------------------------------------------- salida y parrilla */

/* 800317a0: posicion de salida (actor tACT tipo 0x55), su altura y su punto de TCRV */
void Race_SetStart(const int* pos)
{
    TrackPos tp = { 0xff, 0, -1 };
    g_StartPos[0] = pos[0];
    g_StartPos[2] = pos[2];
    g_StartPos[1] = Track_GroundHeight(&tp, pos, 0);
    g_StartCurve = tp.curve;
    g_HaveStart = 1;
}

/* 8004942c (parcial): de los actores solo se usa por ahora la salida. Cada Cact es una lista de bloques
 * [tag][tamano]: tACT {id, tipo, x, y, z, ...}, aRSL, NULL. */
int Cact_OnLoaded(NrAsset* a)
{
    const uint8_t* d = a->data;
    for (const uint8_t* c = d; c + 0x1c <= d + a->size;)
    {
        uint32_t tag = NR_Read32(c), len = NR_Read32(c + 4);
        if (len < 8)
            break;
        if (tag == NR_TAG('t', 'A', 'C', 'T') && NR_Read32(c + 0xc) == 0x55)
        {
            int pos[3] = { (int)NR_Read32(c + 0x10), (int)NR_Read32(c + 0x14), (int)NR_Read32(c + 0x18) };
            Race_SetStart(pos);
        }
        c += len;
    }
    Asset_Register(a);
    return 1;
}

static void Car_SetYaw(RaceCar* c, int yaw)
{
    c->yaw = yaw & 0xfff;
    Math_RotMatrix(c->yaw, 0, 0, c->obj.rot);
}

static void Car_PlaceWheels(RaceCar* c, int spin)
{
    for (int k = 0; k < 4; k++)
    {
        GfxObj* w = &c->wheels[k];
        if (!w->model || !c->obj.model)
            continue;
        Math_RotMatrix(c->yaw + ((k & 1) ? 0x800 : 0), (k & 1) ? -spin : spin, 0, w->rot);
        Obj_GetPoint(&c->obj, k, w->pos);
    }
}

/* 8003132c: direccion de la salida = del nodo de la red mas cercano al siguiente; parrilla en dos columnas
 * (+-0xA00 a los lados, 13 * 0x200 hacia atras por coche). La orientacion la pondra la fisica
 * (FUN_8004e830); de momento se mira hacia la direccion de la salida. */
void Race_CreateCars(void)
{
    const uint8_t* net = g_NumNets ? g_Nets[0] : NULL;
    int dir[3] = { 0, 0, 0x10000 };
    if (net && Cnet_Count(net) > 0)
    {
        int n0 = Cnet_FindNearest(net, g_StartPos, 1);
        int n1 = Cnet_Next(net, n0);
        if (n1 >= 0)
        {
            int a[3], b[3];
            Cnet_NodePos(net, n0, a);
            Cnet_NodePos(net, n1, b);
            for (int k = 0; k < 3; k++)
                dir[k] = b[k] - a[k];
            int len = Vec_ApproxLen3(dir);
            Vec_Scale(len < 2 ? 0 : (int)(0x80000000u / (unsigned)(len >> 1)), dir);
        }
    }
    int yaw = Math_Atan2(dir[0], dir[2]);
    g_NumCars = g_RaceGrid.count < RACE_MAX_CARS ? g_RaceGrid.count : RACE_MAX_CARS;
    for (int i = 0; i < g_NumCars; i++)
    {
        RaceCar* c = &g_Cars[i];
        memset(c, 0, sizeof(*c));
        c->slot = i;
        c->player = (int8_t)g_RaceGrid.car[i].player;
        c->type = c->player < 0 ? 0x11 : 1;
        int back = g_NumCars == 2 ? 0 : (i * 13) << 9;
        int ox = Fix_Mul16(dir[2], 0xa00), oz = Fix_Mul16(dir[0], 0xa00);
        if (!(i & 1))
            ox = -ox;
        else
            oz = -oz;
        int pos[3] = { g_StartPos[0] - Fix_Mul16(dir[0], back) + ox, 0, g_StartPos[2] - Fix_Mul16(dir[2], back) + oz };
        c->tpos.seg = 0xff;
        c->tpos.curve = -1;
        int ground = Track_GroundHeight(&c->tpos, pos, 0);
        NrAsset* m = Asset_Find(ASSET_Cobj, 10000 + i * 10 + 1);
        Obj_Init(&c->obj, m ? (ModelHdr*)m->obj : NULL);
        NrAsset* w = Asset_Find(ASSET_Cobj, 20001 + g_RaceGrid.car[i].car * 3);
        for (int k = 0; k < 4; k++)
            Obj_Init(&c->wheels[k], w ? (ModelHdr*)w->obj : NULL);
        c->obj.pos[0] = pos[0];
        c->obj.pos[1] = ground + 0x200;    /* 8005fca4 */
        c->obj.pos[2] = pos[2];
        c->obj.shadowY = (short)(ground >> 4);
        Car_SetYaw(c, yaw);
        Car_PlaceWheels(c, 0);
    }
}

/* Provisional (hasta portar 80056c6c): el coche del jugador acelera con X, frena con cuadrado, gira con la
 * cruceta y se pega al suelo con la colision de la pista. Los demas esperan en la parrilla. */
void Race_UpdateCars(int dt)
{
    for (int i = 0; i < g_NumCars; i++)
    {
        RaceCar* c = &g_Cars[i];
        if (c->type != 1 || g_RaceTime < 900)
        {
            Car_PlaceWheels(c, 0);
            continue;
        }
        uint32_t b = g_PadButtons[c->player >= 0 && c->player < 2 ? c->player : 0];
        if (b & PADB_CROSS)
            c->speed += 33 * dt;
        else if (b & PADB_SQUARE)
            c->speed -= 120 * dt;
        else
            c->speed -= 8 * dt;
        if (c->speed < 0)
            c->speed = 0;
        if (c->speed > 0xc000)
            c->speed = 0xc000;
        int steer = ((b & PADB_LEFT) ? -1 : 0) + ((b & PADB_RIGHT) ? 1 : 0);
        if (steer)
            Car_SetYaw(c, c->yaw + steer * dt * (1 + (c->speed >> 14)));
        int s, co;
        Math_SinCos(c->yaw, &s, &co);
        int step = (c->speed >> 8) * dt;
        int pos[3] = { c->obj.pos[0] + Fix_Mul16(s, step), c->obj.pos[1], c->obj.pos[2] + Fix_Mul16(co, step) };
        TrackPos tp = c->tpos;
        int ground = Track_FindSurface(&tp, pos, 1);
        if (ground == TRACK_NO_SURFACE || ground > c->obj.pos[1] + 0x400)
        {
            c->speed = 0;   /* fuera de la pista o contra un muro */
            continue;
        }
        c->tpos = tp;
        c->obj.pos[0] = pos[0];
        c->obj.pos[1] = ground + 0x200;
        c->obj.pos[2] = pos[2];
        c->obj.shadowY = (short)(ground >> 4);
        c->obj.frame = 0;
        static int spin;
        spin += (c->speed >> 10) * dt;
        Car_PlaceWheels(c, spin);
    }
}

void Race_AddCarsToDrawList(void)
{
    for (int i = 0; i < g_NumCars; i++)
    {
        RaceCar* c = &g_Cars[i];
        if (!c->obj.model)
            continue;
        for (int k = 0; k < 4; k++)
            if (c->wheels[k].model)
                Render_Add(&c->wheels[k]);
        Render_Add(&c->obj);
    }
}

/* ---------------------------------------------------------------- camara de persecucion */

/* 800ab408: {eje de la matriz del coche, detras, tipo (2 dentro del coche, 3 persecucion), altura,
 * distancia, longitud de la cuerda, con +0x200, altura del punto mirado} (las cuerdas en RaceCam_Init) */
typedef struct { int axis, back, type, height, dist, rest, rest2, lookH; } CamPreset;
static CamPreset s_presets[4] = {
    { 2, 1, 3, 0x700, 0xf00, 0, 0, 0 },
    { 2, 1, 3, 0x570, 0x1000, 0, 0, 0 },
    { 2, 1, 3, 0x700, 0xb80, 0, 0, 0 },
    { 2, 1, 2, 0, 0, 0, 0, 0 },
};

/* DAT_800b6160 + vista * 0x40 */
typedef struct
{
    uint8_t  init;          /* +0x00 */
    int      vel[3];        /* +0x10 */
    int      eye[3];        /* +0x1C */
    RaceCar* target;        /* +0x28 (DAT_800b6188) */
    TrackPos tpos;          /* camara +0x6C */
    int      ground;        /* camara +0x70 */
} RaceCamView;

static RaceCamView s_views[2];
static int         s_viewMode[2];   /* DAT_800af8c0 */

int RaceCam_GetSegment(int view)
{
    return s_views[view].tpos.seg;
}

/* 8005d4c8 */
void RaceCam_Init(void)
{
    for (int i = 0; i < 4; i++)
        if (s_presets[i].type == 3)
        {
            int h = s_presets[i].height, d = s_presets[i].dist;
            s_presets[i].rest = Math_SquareRoot12(Fix_Mul16(h, h) + Fix_Mul16(d, d)) << 2;
            d += 0x200;
            s_presets[i].rest2 = Math_SquareRoot12(Fix_Mul16(h, h) + Fix_Mul16(d, d)) << 2;
        }
    memset(s_views, 0, sizeof(s_views));
    s_viewMode[0] = s_viewMode[1] = 0;
    for (int v = 0; v < 2; v++)
    {
        s_views[v].tpos.seg = 0xff;
        s_views[v].tpos.curve = -1;
    }
    /* 8003132c: la vista 0 sigue al primer humano */
    for (int i = 0; i < g_NumCars; i++)
        if (g_Cars[i].type == 1)
        {
            s_views[0].target = &g_Cars[i];
            break;
        }
    if (!s_views[0].target && g_NumCars > 0)
        s_views[0].target = &g_Cars[0];
}

/* 8005e8b0: detras del coche segun el eje del preset, a 'dist' en el plano y 'height' por encima */
static void RaceCam_Desired(const RaceCar* c, const CamPreset* p, int* out)
{
    const int* m = c->obj.rot + p->axis * 3;
    int off[2] = { 0, 0 };
    int l2 = Fix_Mul16(m[0], m[0]) + Fix_Mul16(m[2], m[2]);
    if (l2 >= 0x11)
    {
        int len = Math_SquareRoot12(l2) * 4;
        unsigned inv = 0x80000000u / (unsigned)abs(len >> 1);
        int ux = (int)(((int64_t)abs(m[0]) * inv) >> 16), uz = (int)(((int64_t)abs(m[2]) * inv) >> 16);
        if (m[0] < 0)
            ux = -ux;
        if (m[2] < 0)
            uz = -uz;
        if (p->back)
        {
            ux = -ux;
            uz = -uz;
        }
        off[0] = Fix_Mul16(ux, p->dist);
        off[1] = Fix_Mul16(uz, p->dist);
    }
    out[0] = c->obj.pos[0] + off[0];
    out[1] = c->obj.pos[1] + p->height;
    out[2] = c->obj.pos[2] + off[1];
}

/* 8005dc98: muelle hacia la posicion deseada, a la distancia de la cuerda y nunca por debajo de suelo +
 * altura. Durante la cuenta atras (y lejos) gira alrededor del coche. Pendiente: techo (80065844) y paredes
 * (8005d6b8). */
static void RaceCam_Spring(RaceCamView* v, const RaceCar* c, int rest, int height, const int* desired, int dt)
{
    int start = g_RaceTime < 900;
    int d[3] = { v->eye[0] - c->obj.pos[0], v->eye[1] - c->obj.pos[1], v->eye[2] - c->obj.pos[2] };
    int len = Vec_NormalizeLen(d);
    int far = len >= rest + 0x80;
    if (!(far && start))
    {
        Vec_Scale(rest, d);
        for (int k = 0; k < 3; k++)
            v->eye[k] = c->obj.pos[k] + d[k];
    }
    int delta[3] = { desired[0] - v->eye[0], desired[1] - v->eye[1], desired[2] - v->eye[2] };
    int k0, shift;
    if (far && start)
    {
        static const int up[3] = { 0, 0x10000, 0 };
        int side[3];
        Vec_Cross(delta, up, side);
        Vec_Scale(0x5000, side);
        for (int k = 0; k < 3; k++)
            delta[k] += side[k];
        k0 = 0x2d000;
        shift = 2;
    }
    else if ((c->speed >> 8) < 2)
    {
        k0 = 0x9000;
        shift = 2;
    }
    else
    {
        k0 = 0x240000;      /* DAT_800af8b0[vista] == 0 */
        shift = 4;
    }
    int pred[3], acc[3];
    for (int k = 0; k < 3; k++)
    {
        pred[k] = v->eye[k] + (v->vel[k] * dt >> 8);
        acc[k] = ((Fix_Mul16(k0, delta[k]) * dt) >> 8) + ((v->eye[k] - pred[k]) << shift);
    }
    int m2 = Vec_LenSq16(acc);
    if (m2 > 0x80)
    {
        int l = (Math_SquareRoot12(m2) << 2) >> 8;
        if (l != 0)
            for (int k = 0; k < 3; k++)
                acc[k] = acc[k] * 0xb / l;
    }
    if ((c->speed >> 8) == 0 && abs(delta[0]) < 0x80 && abs(delta[1]) < 0x80 && abs(delta[2]) < 0x80)
    {
        memset(v->vel, 0, sizeof(v->vel));
        memcpy(pred, desired, sizeof(pred));
    }
    else
        for (int k = 0; k < 3; k++)
            v->vel[k] += acc[k];
    for (int k = 0; k < 3; k++)
        v->eye[k] = pred[k] + (v->vel[k] * dt >> 8);
    v->ground = Track_FindSurface(&v->tpos, v->eye, 1);

    if (v->ground != TRACK_NO_SURFACE && (c->obj.shadowY << 4) < v->eye[1] && v->eye[1] < v->ground + height)
        v->eye[1] = v->ground + height;

    if (!(far && start))
    {
        int r[3] = { v->eye[0] - c->obj.pos[0], v->eye[1] - c->obj.pos[1], v->eye[2] - c->obj.pos[2] };
        Vec_NormalizeLen(r);
        const int* fwd = c->obj.rot + 6;
        int dot = Vec_Dot16(r, fwd);
        if (dot > -0xd000)
        {
            for (int k = 0; k < 3; k++)
                r[k] = r[k] - Fix_Mul16(dot, fwd[k]) + Fix_Mul16(fwd[k], -0xd000);
            Vec_Normalize(r);
        }
        for (int k = 0; k < 3; k++)
            v->eye[k] = c->obj.pos[k] + Fix_Mul16(rest, r[k]);
    }
}

/* 8005f034 (vista de persecucion; la de dentro del coche, tipo 2, pendiente) */
void RaceCam_Update(int view, int dt)
{
    RaceCamView* v = &s_views[view];
    const RaceCar* c = v->target;
    if (!c)
        return;
    const CamPreset* p = &s_presets[s_viewMode[view]];
    int desired[3];
    RaceCam_Desired(c, p, desired);
    if (!v->init)
    {
        /* empieza delante del coche (0x8000 en la direccion de su eje Z) */
        for (int k = 0; k < 3; k++)
            v->eye[k] = desired[k] + Fix_Mul16(c->obj.rot[6 + k], 0x8000);
        v->init = 1;
    }
    else if (p->type == 1)
        memset(v->vel, 0, sizeof(v->vel));
    else
        RaceCam_Spring(v, c, p->rest, p->height, desired, dt);
    int look[3] = { c->obj.pos[0], c->obj.pos[1] + p->lookH, c->obj.pos[2] };
    Camera_LookAt(v->eye, look);
}
