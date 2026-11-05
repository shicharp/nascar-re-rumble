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
#include "net.h"
#include "actor.h"
#include "powerup.h"
#include "snd.h"
#include "fx.h"
#include "carphys.h"
#include "carphys_int.h"
#include "ai.h"
#include "collide.h"
#include "hud.h"
#include "globals.h"
#include "tvcam.h"
#include "pause.h"

RaceCar g_Cars[RACE_MAX_CARS];
int     g_NumCars;
int     g_RaceTime;
int     g_StartPos[3];
int     g_StartCurve;
int     g_HaveStart;
int     g_PrevRaceTime;
int     g_NumHumans;
RaceCar* g_Humans[RACE_MAX_HUMANS];
RaceCar* g_ViewHumans[2];
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

/* Modelos de la carroceria por nivel de detalle en el controlador (humano +0x29C, IA +0x280; Entity_InitCommon
 * 8002c2a0: Cobj 10000 + hueco * 10 + 1 + nivel) */
static ModelHdr** Car_LodModels(RaceCar* c)
{
    return (ModelHdr**)(c->ctrl + (c->type == 1 ? 0x29c : 0x280));
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
    ctrl[0x58] = (uint8_t)(g_GameInfo->isReplay ? 0 : Hud_GetSetting(ctrl[0x59]));   /* nivel del marcador */
}

/* 8003132c: parrilla en dos columnas (+-0xA00 a los lados, 13 * 0x200 hacia atras por coche) a partir de la
 * direccion de la salida (del nodo de la red mas cercano al siguiente). Cada coche: HumanCar_Create 8005fca4
 * o AiCar_Create 8002cfbc (controlador, ruedas y fisica 8004e830, que lo orienta sobre la pista). */
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
        c->unk23 = c->type == 1 ? 0xff : 0;   /* grupo (+0x23) del tACT de la parrilla: 0xFF humanos, 0 IA */
        c->handle = Ent_NewHandle();   /* (los efectos pegados al coche lo usan para saber si sigue vivo) */
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
        ModelHdr** lod = Car_LodModels(c);
        for (int l = 0; l < 4; l++)
            lod[l] = Cobj_Model(10000 + i * 10 + 1 + l);
        GfxObj* wheels = Car_Wheels(c);
        Car_LoadWheels(c, &g_RaceGrid.car[i], wheels);
        if (human)
        {
            int p = c->player >= 0 && c->player < RACE_MAX_HUMANS ? c->player : 0;
            while (g_Humans[p] && p < RACE_MAX_HUMANS - 1)
                p++;
            g_Humans[p] = c;
            g_NumHumans++;
            /* +0x59: la vista (y el mando) de esta maquina; 0xFF el de otra maquina en red */
            int view = Net_HumanView(p);
            c->ctrl[0x59] = (uint8_t)(view < 0 ? 0xff : view);
            if (view >= 0)
                g_ViewHumans[view] = c;
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
        if (c->phys)
        {
            for (int k = 0; k < 4; k++)
                Marks_Free(c->phys, k);   /* 80075c08 */
            Car_PowerSkinFree(c->phys);
            /* 8004de8c: el roce de los golpes (y el turbo) */
            for (int o = 0; o < 2; o++)
            {
                int off = o ? 0x490 : 0x178;
                if (*(int*)(c->phys + off) != -1)
                    Snd_SourceKill(c->handle, *(int*)(c->phys + off));
                *(int*)(c->phys + off) = -1;
            }
            Engine_Free(c->phys);   /* 800417ec */
        }
        free(c->phys);
        free(c->ctrl);
        c->phys = c->phys2 = c->ctrl = NULL;
        g_CarPhys[i] = NULL;
    }
    memset(g_Humans, 0, sizeof(g_Humans));
    memset(g_ViewHumans, 0, sizeof(g_ViewHumans));
    memset(g_HumanDraft, 0, sizeof(g_HumanDraft));
    g_NumHumans = 0;
    g_NumCars = 0;
    Ai_Reset();
}

/* Race_UpdateTime 800269c0: mando de cada humano (8005fc54 + 8005f928), cada frame y tambien en pausa */
void Race_ReadPads(void)
{
    for (int i = 0; i < RACE_MAX_HUMANS; i++)
        if (g_Humans[i] && g_Humans[i]->ctrl && g_Humans[i]->ctrl[0x59] < 2)
            Human_ReadPad(g_Humans[i], g_Humans[i]->ctrl[0x59]);
}

/* ---------------------------------------------------------------- solo en el port: ayudas de las partidas en red */

/* Opciones del anfitrion (NET_ASSIST_*), solo para los humanos. Suben el factor de potencia +0x431 (0xCC normal;
 * escala el par y la velocidad punta como (1 + v / 0xCC) / 2: 0xFF = +12,5 %), el mismo que usa la goma elastica
 * de la IA (Ai_Drive). Salen del estado de la carrera, asi que dan lo mismo en todas las maquinas. */
#define DRAFT_MIN    0x800     /* rebufo: de 0x800 a 0x5000 por delante (unos 3 coches; la parrilla los pone */
#define DRAFT_DIST   0x5000    /* a 0x1A00 uno de otro), con mas fuerza cuanto mas cerca, */
#define DRAFT_WIDTH  0x700     /* y a menos de 0x700 de lado (las columnas de la parrilla van a 0x1400) */
#define DRAFT_BOOST  0x20      /* hasta +7,8 % */
#define CATCH_START  40        /* rezagado: a partir de 40 puntos de TCRV (unos 3 s a fondo) del primer humano */
#define CATCH_RANGE  160       /* hasta el maximo 160 puntos despues (unos 15 s) */
#define CATCH_BOOST  0x24      /* +8,8 % */

uint8_t g_HumanDraft[RACE_MAX_HUMANS];   /* fuerza del rebufo de cada humano (0..255; el marcador lo avisa) */

/* fuerza del rebufo (0..256): el coche mas cercano por delante en la trazada, si los dos van deprisa */
static int Assist_Draft(const RaceCar* e)
{
    if (e->speed < 0x40)
        return 0;
    int s, c;
    Math_SinCos(I16(e->phys, 0x432), &s, &c);   /* rumbo: adelante = (sin, cos) en x, z (16.16) */
    int best = 0;
    for (int i = 0; i < g_NumCars; i++)
    {
        const RaceCar* o = &g_Cars[i];
        if (o == e || !o->phys || o->speed < 0x40)
            continue;
        int dx = o->obj.pos[0] - e->obj.pos[0], dz = o->obj.pos[2] - e->obj.pos[2];
        if (Abs_(dx) > DRAFT_DIST || Abs_(dz) > DRAFT_DIST)
            continue;
        int along = (int)(((int64_t)dx * s + (int64_t)dz * c) >> 16);
        int side = (int)(((int64_t)dx * c - (int64_t)dz * s) >> 16);
        if (along <= DRAFT_MIN || along >= DRAFT_DIST || Abs_(side) >= DRAFT_WIDTH)
            continue;
        int k = (DRAFT_DIST - along) * 256 / (DRAFT_DIST - DRAFT_MIN);
        if (k > best)
            best = k;
    }
    return best;
}

static void Race_Assists(void)
{
    int opt = Net_Assists();
    if (!(opt & (NET_ASSIST_DRAFT | NET_ASSIST_CATCHUP)))
        return;   /* (NET_ASSIST_CLASSCAR va en Car_GearsAddr, al crear el coche) */
    /* el primer humano que sigue corriendo (el de mejor puesto) */
    RaceCar* lead = NULL;
    for (int h = 0; h < RACE_MAX_HUMANS; h++)
    {
        RaceCar* e = g_Humans[h];
        if (e && e->phys && I32(e->phys, 0x330) == 0 && (!lead || U8(e->phys, 0x31a) < U8(lead->phys, 0x31a)))
            lead = e;
    }
    for (int h = 0; h < RACE_MAX_HUMANS; h++)
    {
        RaceCar* e = g_Humans[h];
        g_HumanDraft[h] = 0;
        if (!e || !e->phys)
            continue;
        uint8_t* c = e->phys;
        int v = 0xcc;
        if (I32(c, 0x330) == 0)
        {
            if ((opt & NET_ASSIST_CATCHUP) && lead && lead != e)
            {
                int gap = Car_TrackDeltaPublic(lead->phys, c) - CATCH_START;
                if (gap > 0)
                    v += (gap < CATCH_RANGE ? gap : CATCH_RANGE) * CATCH_BOOST / CATCH_RANGE;
            }
            if (opt & NET_ASSIST_DRAFT)
            {
                int k = Assist_Draft(e);
                g_HumanDraft[h] = (uint8_t)(k > 255 ? 255 : k);
                v += k * DRAFT_BOOST / 256;
            }
        }
        U8(c, 0x431) = (uint8_t)(v > 0xff ? 0xff : v);
    }
}

/* Solo en el port: en red la carrera acaba cuando han llegado todos los humanos (Ai_UpdateLaps, al cruzar la meta).
 * Los de una maquina que ha dejado la carrera ya no llegan: cuentan como llegados desde el frame en que van sin
 * mando (el mismo en todas las maquinas), y si el ultimo que faltaba se va, la carrera acaba igual. */
static void Race_NetCheckEnd(void)
{
    if (!Net_InRace() || g_RaceEndTimer != 0)
        return;
    int arrived = 0;
    for (int h = 0; h < RACE_MAX_HUMANS; h++)
    {
        RaceCar* e = g_Humans[h];
        if (!e || !e->phys)
            continue;
        if (I32(e->phys, 0x330) != 0)
            arrived++;
        else if (!Net_HumanGone(h))
            return;
    }
    if (arrived)
        g_RaceEndTimer = 5;
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
        Human_UpdateButtons(c, dt);   /* (el mando ya lo ha leido Race_ReadPads, o la repeticion) */
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
    Race_Assists();                /* solo en el port: rebufo y ayuda al rezagado en red */
    Race_NetCheckEnd();            /* solo en el port: en red, sin esperar a los que se han ido */
    Physics_StepAll(dt);
    Race_CollideCars();            /* 800268c0 (tras dibujar en el original) */
    Actors_CollideCars();
}

/* Solo en el port: tras dibujar, la carroceria vuelve al modelo completo. En el original el modelo del nivel de
 * detalle se queda en el objeto y la fisica lee de el los puntos de las ruedas (80013b20) y la caja de choque; los
 * modelos de los niveles 2 y 3 no tienen esos puntos (0xFF: el original lee memoria de fuera del modelo, el port
 * las pondria en el centro y el coche se levantaria) */
void Race_RestoreCarModels(void)
{
    for (int i = 0; i < g_NumCars; i++)
    {
        RaceCar* c = &g_Cars[i];
        ModelHdr* m = c->ctrl ? Car_LodModels(c)[0] : NULL;
        if (m)
            c->obj.model = m;
    }
}

/* 8005e6f4: el coche que sigue la camara no se ve con la vista dentro del coche (tipo 2) ni con la camara de TV
 * pegada a el (tipo 8) */
int RaceCam_CarVisible(int view, const RaceCar* c)
{
    if (c != RaceCam_GetTarget(view))
        return 1;
    if (TvCam_IsActive(view))
        return TvCam_CurrentType(view) != 8;
    return !RaceCam_IsInCar(view);
}

/* Port: en el retrovisor (mirror) no se dibuja el coche del jugador; con la camara en su centro se veian trozos de
 * la carroceria */
void Race_AddCarsToDrawList(int view, int mirror)
{
    Physics_PlaceWheelsAll(view);
    for (int i = 0; i < g_NumCars; i++)
    {
        RaceCar* c = &g_Cars[i];
        if (!c->obj.model || !RaceCam_CarVisible(view, c) || (mirror && c == g_ViewHumans[0]))
            continue;
        GfxObj* wheels = Car_Wheels(c);
        /* 80060190 / 80030f84: modelo segun el nivel de detalle (80013db0); las ruedas solo en los niveles 0 y 1,
         * borrosas en el 0 si giran deprisa (+0x2E8 delante, +0x2EC detras) */
        int lvl = Obj_LodLevel(&c->obj);
        c->visBits |= (uint8_t)(lvl << 4);
        ModelHdr* body = Car_LodModels(c)[lvl];
        if (body)
            c->obj.model = body;
        ModelHdr** m = Car_WheelModels(c, 0);
        ModelHdr** b = Car_WheelModels(c, 1);
        for (int k = 0; wheels && k < 4; k++)
            wheels[k].flags &= 0xfc;
        if (lvl >= 2)
            wheels = NULL;
        for (int k = 0; wheels && k < 4; k++)
        {
            int rear = k >> 1;
            int spin = c->phys ? *(int*)(c->phys + 0x2e8 + rear * 4) : 0;
            ModelHdr* want = lvl == 0 && (spin > 0x40 || spin < -0x40) && b[rear] ? b[rear] : m[rear * 2 + lvl];
            if (want && wheels[k].model != want)
            {
                wheels[k].model = want;
                wheels[k].radius = *(int16_t*)(want->bbox + 0x14);
            }
            if (wheels[k].model)
                Render_Add(&wheels[k]);
        }
        Render_Add(&c->obj);
        if (c->phys)
            c->phys[0x49a] = 1;   /* HumanCar_Handler / AiCar_Handler: dibujado en la vista (0) */
    }
}

/* ---------------------------------------------------------------- camara de persecucion */

/* 800ab408: {eje de la matriz del coche, detras, tipo (2 dentro del coche, 3 persecucion), altura,
 * distancia, longitud de la cuerda, con +0x200, altura del punto mirado} (las cuerdas en RaceCam_Init) */
typedef struct { int axis, back, type, height, dist, rest, rest2, lookH; } CamPreset;
static CamPreset s_presetTab[2][4] = {
    {
        { 2, 1, 3, 0x700, 0xf00, 0, 0, 0 },
        { 2, 1, 3, 0x570, 0x1000, 0, 0, 0 },
        { 2, 1, 3, 0x700, 0xb80, 0, 0, 0 },
        { 2, 1, 2, 0, 0, 0, 0, 0 },
    },
    {   /* 800ab488: con dos jugadores (DAT_800af6e8 == 2), mas lejos y mirando mas alto para la vista mas baja */
        { 2, 1, 3, 0x600, 0x12c0, 0, 0, 0x300 },
        { 2, 1, 3, 0x500, 0x1000, 0, 0, 0x300 },
        { 2, 1, 3, 0x580, 0xe00, 0, 0, 0x300 },
        { 2, 1, 2, 0, 0, 0, 0, 0x300 },
    },
};
#define s_presets (s_presetTab[g_SplitScreen != 0])

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
static int         s_tvAtStart;     /* solo en el port: NR_TVCAM=1 (como la demostracion) o vN (virtual N) */

/* 8005e840: la camara del jugador va dentro del coche (preset de tipo 2) */
int RaceCam_IsInCar(int view)
{
    return s_presets[s_viewMode[view & 1] & 3].type == 2;
}

int RaceCam_GetViewMode(int view)
{
    return s_viewMode[view & 1];
}

RaceCar* RaceCam_GetTarget(int view)
{
    return s_views[view & 1].target;
}

/* Solo en el port (red): el que ya ha llegado mira a otro coche. dir +-1: el siguiente de la parrilla que no ha
 * llegado (si no queda ninguno, no cambia), con la camara de persecucion (sale de las de TV) */
void RaceCam_Spectate(int view, int dir)
{
    view &= 1;
    int n = g_NumCars;
    if (n <= 0)
        return;
    const RaceCar* cur = s_views[view].target;
    int start = cur ? cur->slot : 0;
    for (int k = 1; k <= n; k++)
    {
        RaceCar* c = &g_Cars[((start + dir * k) % n + n) % n];
        if (c->phys && I32(c->phys, 0x330) == 0)
        {
            s_views[view].target = c;
            s_views[view].init = 0;
            TvCam_Leave(view);
            return;
        }
    }
}

/* 8005ed10: siguiente vista (3 de persecucion y la de dentro del coche). Desde las camaras de TV vuelve a la vista
 * sin avanzar si se pasaria del principio o del final de la lista. Con dir 0 (o en la repeticion al pasarse de un
 * extremo) prueba las camaras de TV; si no hay ninguna, sigue hacia delante */
void RaceCam_CycleView(int view, int dir)
{
    view &= 1;
    int tv = TvCam_IsActive(view), tried = 0, m;
    for (;;)
    {
        if (dir == 0)
        {
            if (!tried && TvCam_Enter(view))
                return;
            tried = 1;
            dir = 1;
        }
        m = s_viewMode[view];
        if (!tv || (dir == 1 && m != 0) || (dir == -1 && m != 3))
            m += dir;
        if (m >= 0 && m < 4)
            break;
        if (g_GameInfo->isReplay && !tv && !tried)
        {
            dir = 0;
            continue;
        }
        m = m < 0 ? 3 : 0;
        break;
    }
    if (tv)
        TvCam_Leave(view);
    s_viewMode[view] = m;
    memset(s_views[view].vel, 0, sizeof(s_views[view].vel));
}

int RaceCam_GetSegment(int view)
{
    return s_views[view].tpos.seg;
}

/* 8005d4c8 */
void RaceCam_Init(void)
{
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < 4; i++)
        {
            CamPreset* p = &s_presetTab[t][i];
            if (p->type != 3)
                continue;
            int h = p->height, d = p->dist;
            p->rest = Math_SquareRoot12(Fix_Mul16(h, h) + Fix_Mul16(d, d)) << 2;
            d += 0x200;
            p->rest2 = Math_SquareRoot12(Fix_Mul16(h, h) + Fix_Mul16(d, d)) << 2;
        }
    memset(s_views, 0, sizeof(s_views));
    s_viewMode[0] = s_viewMode[1] = 0;
    for (int v = 0; v < 2; v++)
    {
        s_views[v].tpos.seg = 0xff;
        s_views[v].tpos.curve = -1;
    }
    /* 8003132c: cada vista sigue a su jugador (DAT_800b6188 + jugador * 0x40); sin humanos (demostracion), la
     * primera de la lista de la IA (DAT_800af7c4: la ultima creada, el ultimo de la parrilla) */
    for (int v = 0; v < 2; v++)
        s_views[v].target = g_ViewHumans[v];
    if (!s_views[0].target && g_NumCars > 0)
        s_views[0].target = &g_Cars[g_NumCars - 1];
    /* solo en el port (depuracion): NR_CAMCAR=n sigue al coche del hueco n */
    const char* cam = getenv("NR_CAMCAR");
    if (cam && atoi(cam) >= 0 && atoi(cam) < g_NumCars)
        s_views[0].target = &g_Cars[atoi(cam)];
    /* solo en el port (depuracion): NR_TVCAM=1 empieza con las camaras de TV (8005f034: sin humanos o en la
     * repeticion, en los primeros frames); NR_TVCAM=vN con la virtual N */
    const char* tv = getenv("NR_TVCAM");
    s_tvAtStart = !tv ? 0 : tv[0] == 'v' ? 2 + atoi(tv + 1) : 1;
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

/* RaceCam_AvoidWalls 8005d6b8 con limite < 5000 (la cuerda de la persecucion): rayos del coche a dos puntos a
 * +-0x300 a los lados del ojo (perpendiculares a la direccion coche-ojo); si uno cruza un muro, el ojo avanza hacia
 * dentro lo que le falta hasta el. Nada si el ojo esta a mas de 0x6000 del coche. */
static int RaceCam_AvoidWalls(RaceCamView* v, const RaceCar* c)
{
    int n[3] = { c->obj.pos[0] - v->eye[0], c->obj.pos[1] - v->eye[1], c->obj.pos[2] - v->eye[2] };
    if (Vec_ApproxLenMax(n) > 0x6000)
        return 0;
    Vec_Normalize(n);
    int sx = Fix_Mul16(n[2], 0x300), sz = Fix_Mul16(n[0], 0x300);
    int p[2][3] = { { v->eye[0] - sx, v->eye[1], v->eye[2] + sz }, { v->eye[0] + sx, v->eye[1], v->eye[2] - sz } };
    int k = 0, w[3], hit[6];   /* Track_RayWall: normal del muro y sus dos extremos */
    if (!Track_RayWall(c->obj.pos, p[0], w, hit))
    {
        k = 1;
        if (!Track_RayWall(c->obj.pos, p[1], w, hit))
            return 0;
    }
    int e[3] = { hit[0] - c->obj.pos[0], hit[1] - c->obj.pos[1], hit[2] - c->obj.pos[2] };
    if (Vec_Dot16(e, w) > 0)
        for (int j = 0; j < 3; j++)
            w[j] = -w[j];
    int f[3] = { hit[0] - p[k][0], hit[1] - p[k][1], hit[2] - p[k][2] };
    Vec_Scale(Vec_Dot16(f, w), w);
    for (int j = 0; j < 3; j++)
        v->eye[j] += w[j];
    return 1;
}

/* clase del coche: byte 1 de su descriptor (fisica +0x1B4) */
static int Car_Class(const RaceCar* c)
{
    return c->phys && PTR(c->phys, 0x1b4) ? PTR(c->phys, 0x1b4)[1] : 0;
}

/* 8005dc98: muelle hacia la posicion deseada, a la distancia de la cuerda y nunca por debajo de suelo +
 * altura. Durante la cuenta atras (y lejos) gira alrededor del coche. Al final aparta el ojo de los muros
 * (8005d6b8). La altura sube con la clase del coche (descriptor +1: '0' y '3' 0x200, '/' 0x280, '4' 0x400) y 0x2000
 * mientras vuela (+0x184); casi parado (+0x15C < 10), si un muro corta la recta del coche al punto deseado (bajado la
 * altura) el ojo no se mueve. Volando y volcado (+0x198 & 0x11) tampoco. El tope de altura 80016d38 del original no
 * hace nada (devuelve la altura que recibe). */
static void RaceCam_Spring(RaceCamView* v, const RaceCar* c, int rest, int height, const int* desired, int dt)
{
    const uint8_t* ph = c->phys;
    int cls = Car_Class(c);
    height += cls == '0' || cls == '3' ? 0x200 : cls == '/' ? 0x280 : cls == '4' ? 0x400 : 0;
    int blocked = 0;
    if (ph && I32(ph, 0x15c) < 10)
    {
        int low[3] = { desired[0], desired[1] - height, desired[2] };
        blocked = Track_RayWall(c->obj.pos, low, NULL, NULL);
    }
    int flying = ph && I16(ph, 0x184) != 0;
    if (flying)
        height += 0x2000;
    unsigned air = ph ? U16(ph, 0x198) : 0;
    int start = g_RaceTime < 900;
    int d[3] = { v->eye[0] - c->obj.pos[0], v->eye[1] - c->obj.pos[1], v->eye[2] - c->obj.pos[2] };
    int len = Vec_NormalizeLen(d);
    int far = len >= rest + 0x80;
    if (!(far && start) && !flying)
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
    else if (c->speed < 2)
    {
        k0 = 0x9000;
        shift = 2;
    }
    else
    {
        k0 = 0x240000;      /* DAT_800af8b0[vista]: nadie lo pone a 1 */
        shift = 4;
    }
    if (!blocked || (far && start))
    {
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
        if (c->speed == 0 && abs(delta[0]) < 0x80 && abs(delta[1]) < 0x80 && abs(delta[2]) < 0x80)
        {
            memset(v->vel, 0, sizeof(v->vel));
            memcpy(pred, desired, sizeof(pred));
        }
        else
            for (int k = 0; k < 3; k++)
                v->vel[k] += acc[k];
        if ((far && start) || (air & 0x11) != 0x11)
            for (int k = 0; k < 3; k++)
                v->eye[k] = pred[k] + (v->vel[k] * dt >> 8);
        v->ground = Track_FindSurface(&v->tpos, v->eye, NULL, 1);
    }

    if (v->ground != TRACK_NO_SURFACE && (c->obj.shadowY << 4) < v->eye[1] && v->eye[1] < v->ground + height)
        v->eye[1] = v->ground + height;

    if (!(far && start) && !flying)
    {
        int r[3] = { v->eye[0] - c->obj.pos[0], v->eye[1] - c->obj.pos[1], v->eye[2] - c->obj.pos[2] };
        Vec_NormalizeLen(r);
        const int* fwd = c->obj.rot + 6;
        int dot = (air & 0x10) ? -0xd000 : Vec_Dot16(r, fwd);   /* en el aire no se fuerza a quedar detras */
        if (dot > -0xd000)
        {
            for (int k = 0; k < 3; k++)
                r[k] = r[k] - Fix_Mul16(dot, fwd[k]) + Fix_Mul16(fwd[k], -0xd000);
            Vec_Normalize(r);
        }
        for (int k = 0; k < 3; k++)
            v->eye[k] = c->obj.pos[k] + Fix_Mul16(rest, r[k]);
    }
    RaceCam_AvoidWalls(v, c);   /* 8005d6b8 (limite = la cuerda, < 5000) */
}

/* 8005f034: vista de persecucion (muelle) o dentro del coche (tipo 2) */
void RaceCam_Update(int view, int dt)
{
    RaceCamView* v = &s_views[view];
    const RaceCar* c = v->target;
    if (!c)
        return;
    if (s_tvAtStart && view == 0)
    {
        if (s_tvAtStart == 1)
            TvCam_Enter(view);
        else
            TvCam_SelectIndex(view, s_tvAtStart - 2);
        s_tvAtStart = 0;
    }
    /* sin humanos o en la repeticion, en los primeros frames empieza con las camaras de TV (al final de 8005f034 en el
     * original: alli el primer frame aun es la de persecucion) */
    if (g_FrameCount < 3 && (g_NumHumans == 0 || g_GameInfo->isReplay) && !TvCam_IsActive(view))
        RaceCam_CycleView(view, 0);
    if (TvCam_IsActive(view))
    {
        TvCam_Update(view, dt, &v->tpos);
        memcpy(v->eye, g_Camera.pos, sizeof(v->eye));
        memset(v->vel, 0, sizeof(v->vel));
        return;
    }
    const CamPreset* p = &s_presets[s_viewMode[view]];
    if (p->type == 2)
    {
        /* dentro del coche: su matriz (la vista es la traspuesta, 8001802c) y su posicion 0x100 mas alta, nunca a
         * menos de 0x380 sobre el suelo */
        int y = c->obj.pos[1] + 0x100, floor = c->obj.shadowY * 0x10 + 0x380;
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++)
                g_Camera.view[3 * k + j] = c->obj.rot[3 * j + k];
        g_Camera.pos[0] = c->obj.pos[0];
        g_Camera.pos[1] = y < floor ? floor : y;
        g_Camera.pos[2] = c->obj.pos[2];
        /* el segmento de la camara (800299a8 lo busca cada frame con cualquier camara; sin esto la pista se dibujaba
         * alrededor del segmento en el que se entro en la vista) */
        memcpy(v->eye, g_Camera.pos, sizeof(v->eye));
        v->ground = Track_FindSurface(&v->tpos, v->eye, NULL, 1);
        return;
    }
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
    else   /* (ningun preset es de tipo 4, que pondria el ojo en la posicion deseada) */
        RaceCam_Spring(v, c, Car_Class(c) == '4' ? p->rest2 : p->rest, p->height, desired, dt);
    /* las clases '/' y '4' se miran 0x100 mas arriba */
    int look[3] = { c->obj.pos[0], c->obj.pos[1] + p->lookH + (Car_Class(c) == '/' || Car_Class(c) == '4' ? 0x100 : 0),
                    c->obj.pos[2] };
    Camera_LookAt(v->eye, look);
}
