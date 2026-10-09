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
#include "actor.h"
#include "powerup.h"
#include "carphys.h"
#include "carphys_int.h"
#include "ai.h"
#include "collide.h"
#include "hud.h"
#include "globals.h"

RaceCar g_Cars[RACE_MAX_CARS];
int     g_NumCars;
int     g_RaceTime;
int     g_StartPos[3];
int     g_StartCurve;
int     g_HaveStart;
int     g_PrevRaceTime;
int     g_NumHumans;
RaceCar* g_Humans[2];
uint8_t* g_CarPhys[RACE_MAX_CARS];

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

static ModelHdr* Cobj_Model(int id)
{
    NrAsset* a = Asset_Find(ASSET_Cobj, id);
    return a ? (ModelHdr*)a->obj : NULL;
}

/* modelos de las ruedas en el controlador: humano +0x2AC (delante y detras, detalle 0 y 1) y +0x2BC (giro
 * rapido, delante y detras); IA +0x290 / +0x2A0 */
static ModelHdr** Car_WheelModels(RaceCar* c, int blur)
{
    int off = c->type == 1 ? (blur ? 0x2bc : 0x2ac) : (blur ? 0x2a0 : 0x290);
    return (ModelHdr**)(c->ctrl + off);
}

/* 8002cae0: ruedas segun la clase del coche (Rookie / Pro 20002, Elite 20003; el carrito de golf, piloto
 * 0x35, 20001); +3 = menos detalle, +6 = borrosa al girar deprisa */
static void Car_LoadWheels(RaceCar* c, const GridCar* g, GfxObj* wheels)
{
    int front, rear, cls = g->car;
    if (g->driver == 0x35)
    {
        if (cls == 0)
            front = rear = 0x4e21;
        else
        {
            rear = 0x4e22;
            front = cls == 1 ? 0x4e21 : 0x4e22;
        }
    }
    else
        front = rear = cls < 2 ? 0x4e22 : 0x4e23;
    ModelHdr** m = Car_WheelModels(c, 0);
    ModelHdr** b = Car_WheelModels(c, 1);
    m[0] = Cobj_Model(front);
    m[1] = Cobj_Model(front + 3);
    b[0] = Cobj_Model(front + 6);
    m[2] = Cobj_Model(rear);
    m[3] = Cobj_Model(rear + 3);
    b[1] = Cobj_Model(rear + 6);
    Obj_Init(&wheels[1], m[0]);
    Obj_Init(&wheels[0], m[0]);
    Obj_Init(&wheels[2], m[2]);
    Obj_Init(&wheels[3], m[2]);
}

/* Ruedas del coche: dentro del controlador (humano +0x6C, IA +0x44) */
static GfxObj* Car_Wheels(RaceCar* c)
{
    if (!c->ctrl)
        return NULL;
    return (GfxObj*)(c->ctrl + (c->type == 1 ? 0x6c : 0x44));
}

/* 8005f824: botones asignados del humano {mascara, estado} (2 = suelto) */
static void Human_InitCtrl(uint8_t* ctrl)
{
    static const uint32_t binds[][2] = {
        { 0x10, 0x40000 }, { 0x18, 0x100000 }, { 0x20, 0x2000000 }, { 0x2c, 0x8000000 },
        { 0x34, 0x200000 }, { 0x3c, 0x800000 }, { 0x44, 0x40000000 }, { 0x4c, 0x40000000 },
    };
    for (size_t i = 0; i < sizeof(binds) / sizeof(binds[0]); i++)
    {
        *(uint32_t*)(ctrl + binds[i][0]) = binds[i][1];
        *(uint32_t*)(ctrl + binds[i][0] + 4) = 2;
    }
    *(uint32_t*)(ctrl + 8) = 0;
    *(int*)(ctrl + 0x5c) = 0;
    *(int*)(ctrl + 0x60) = 0;
    *(int*)(ctrl + 0x68) = -1;
    *(int*)(ctrl + 4) = -1;                                      /* 8005fca4: tiempo en contra direccion */
    ctrl[0x58] = (uint8_t)(g_GameInfo->isRestart ? 0 : Hud_GetSetting(ctrl[0x59]));   /* nivel del marcador */
}

/* 8003132c: parrilla en dos columnas (+-0xA00 a los lados, 13 * 0x200 hacia atras por coche) a partir de la
 * direccion de la salida (del nodo de la red mas cercano al siguiente). Cada coche: HumanCar_Create 8005fca4
 * o AiCar_Create 8002cfbc (controlador, ruedas y fisica 8004e830, que lo orienta sobre la pista). */
void Race_CreateCars(void)
{
    static int s_physInit;
    if (!s_physInit)
    {
        Physics_Init();
        s_physInit = 1;
    }
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
    Race_FreeCars();
    g_NumCars = g_RaceGrid.count < RACE_MAX_CARS ? g_RaceGrid.count : RACE_MAX_CARS;
    for (int i = 0; i < g_NumCars; i++)
    {
        RaceCar* c = &g_Cars[i];
        memset(c, 0, sizeof(*c));
        c->slot = i;
        c->id = (uint16_t)(1000 + i);
        c->player = (int8_t)g_RaceGrid.car[i].player;
        c->type = c->player < 0 ? 0x11 : 1;
        c->team = g_RaceGrid.car[i].team;
        int back = g_NumCars == 2 ? 0 : (i * 13) << 9;
        int ox = Fix_Mul16(dir[2], 0xa00), oz = Fix_Mul16(dir[0], 0xa00);
        if (!(i & 1))
            ox = -ox;
        else
            oz = -oz;
        NrAsset* m = Asset_Find(ASSET_Cobj, 10000 + i * 10 + 1);
        Obj_Init(&c->obj, m ? (ModelHdr*)m->obj : NULL);
        c->obj.pos[0] = g_StartPos[0] - Fix_Mul16(dir[0], back) + ox;
        c->obj.pos[2] = g_StartPos[2] - Fix_Mul16(dir[2], back) + oz;
        c->tpos.seg = 0xff;
        c->tpos.curve = -1;
        c->obj.pos[1] = Track_GroundHeight(&c->tpos, c->obj.pos, 0) + 0x200;
        c->obj.flags |= 0x20;
        c->flags2c = (c->flags2c & 0xfeffffae) | 0x40800308;
        int human = c->type == 1;
        c->ctrl = (uint8_t*)calloc(1, human ? 0x2c4 : 0x2a8);
        GfxObj* wheels = Car_Wheels(c);
        Car_LoadWheels(c, &g_RaceGrid.car[i], wheels);
        if (human)
        {
            int p = c->player < 2 && !g_Humans[c->player] ? c->player : (g_Humans[0] ? 1 : 0);
            g_Humans[p] = c;
            g_NumHumans++;
            c->ctrl[0x59] = (uint8_t)p;
            Human_InitCtrl(c->ctrl);
        }
        else
            Ai_InitCar(c);
        uint8_t* phys = Car_CreatePhys(c, wheels, &g_RaceGrid.car[i]);
        *(uint8_t**)(c->ctrl + (human ? 0 : 0x40)) = phys;
        c->phys2 = phys;
        g_CarPhys[i] = phys;
        if (!human)
            Ai_InitPhys(c);
    }
}

void Race_FreeCars(void)
{
    for (int i = 0; i < RACE_MAX_CARS; i++)
    {
        RaceCar* c = &g_Cars[i];
        free(c->phys);
        free(c->ctrl);
        c->phys = c->phys2 = c->ctrl = NULL;
        g_CarPhys[i] = NULL;
    }
    g_Humans[0] = g_Humans[1] = NULL;
    g_NumHumans = 0;
    g_NumCars = 0;
    Ai_Reset();
}

/* Race_UpdateSim 80030cac: posiciones (8002fc9c), mando de los humanos (8005ffbc -> 8004cae8), vueltas
 * (8003014c), poderes (8002ec00), IA (80030928, 8002de7c, 8002f4ac) y fisica (80056c6c). */
void Race_UpdateCars(int dt)
{
    if (g_NumCars <= 0)
        return;
    Race_UpdatePositions(dt);
    for (int i = 0; i < g_NumHumans; i++)
    {
        RaceCar* c = g_Humans[i];
        if (!c)
            continue;
        Human_ReadPad(c, c->ctrl[0x59]);
        Human_UpdateButtons(c, dt);
        Human_Control(c);
        Human_Buttons(c);
    }
    Ai_UpdateLaps();
    for (int i = 0; i < g_NumCars; i++)   /* Car_UpdatePowerUps 8002ec00: IA con algo en la mano, humano con R2 */
    {
        RaceCar* c = &g_Cars[i];
        if (!c->phys || !c->ctrl)
            continue;
        int use = c->type == 1 ? I32(c->ctrl, 0x38) == 3 : I8(c->phys, 0x338) >= 0;
        Car_UpdatePowerUps(c->phys, dt, use);
    }
    Ai_UpdateAll(dt);
    Physics_StepAll(dt);
    Race_CollideCars();            /* 800268c0 (tras dibujar en el original) */
    Actors_CollideCars();
}

void Race_AddCarsToDrawList(void)
{
    Physics_PlaceWheelsAll(0);
    for (int i = 0; i < g_NumCars; i++)
    {
        RaceCar* c = &g_Cars[i];
        if (!c->obj.model)
            continue;
        GfxObj* wheels = Car_Wheels(c);
        /* 80060190 / 80030f84: ruedas borrosas si giran deprisa (+0x2E8 delante, +0x2EC detras); el nivel de
         * detalle (80013db0) queda en 0 */
        ModelHdr** m = Car_WheelModels(c, 0);
        ModelHdr** b = Car_WheelModels(c, 1);
        for (int k = 0; wheels && k < 4; k++)
        {
            int rear = k >> 1;
            int spin = c->phys ? *(int*)(c->phys + 0x2e8 + rear * 4) : 0;
            ModelHdr* want = (spin > 0x40 || spin < -0x40) && b[rear] ? b[rear] : m[rear * 2];
            if (want && wheels[k].model != want)
            {
                wheels[k].model = want;
                wheels[k].radius = *(int16_t*)(want->bbox + 0x14);
            }
            if (wheels[k].model)
                Render_Add(&wheels[k]);
        }
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

int RaceCam_GetViewMode(int view)
{
    return s_viewMode[view & 1];
}

RaceCar* RaceCam_GetTarget(int view)
{
    return s_views[view & 1].target;
}

/* 8005ed10 (parte): siguiente vista de persecucion (la de dentro del coche, tipo 2, pendiente) */
void RaceCam_CycleView(int view, int dir)
{
    int m = s_viewMode[view & 1];
    for (int i = 0; i < 4; i++)
    {
        m = (m + dir + 4) % 4;
        if (s_presets[m].type == 3)
            break;
    }
    s_viewMode[view & 1] = m;
    memset(s_views[view & 1].vel, 0, sizeof(s_views[view & 1].vel));
}

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
    /* solo en el port (depuracion): NR_CAMCAR=n sigue al coche del hueco n */
    const char* cam = getenv("NR_CAMCAR");
    if (cam && atoi(cam) >= 0 && atoi(cam) < g_NumCars)
        s_views[0].target = &g_Cars[atoi(cam)];
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
    v->ground = Track_FindSurface(&v->tpos, v->eye, NULL, 1);

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
