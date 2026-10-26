#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "libetc.h"
#include "nr.h"
#include "mem.h"
#include "gfx.h"
#include "pad.h"
#include "font.h"
#include "globals.h"
#include "asset.h"
#include "model.h"
#include "render.h"
#include "texslot.h"
#include "frontend.h"
#include "game.h"
#include "race.h"
#include "track.h"
#include "sprite.h"
#include "cnet.h"
#include "racecar.h"
#include "script.h"
#include "tvcam.h"
#include "hud.h"
#include "pause.h"
#include "actor.h"
#include "powerup.h"
#include "snd.h"
#include "vecmath.h"
#include "fx.h"
#include "carphys_int.h"
#include "carphys.h"
#include "gte.h"
#include "replay.h"

uint8_t g_CarLoadOrder[8];
int     g_CarsLoaded;

/* 80024284. Port: aqui lo que hace falta antes de leer el stream (actores, efectos, pista, aleatorios, render);
 * los coches, las camaras, el marcador y el sonido se preparan al empezar Race_MainLoop. */
void Race_Load(const char* path)
{
    g_CarsLoaded = 0;              /* FUN_80023e70 */
    Actors_Free();                 /* 80048d5c */
    Engine_ResetTypes();           /* 8003cfd0: tipos de motor antes de que lleguen los Ceng */
    Fx_Reset();                    /* 80038d54 */
    TvCam_Free();                  /* 80072254 */
    Track_Free();
    Cnet_Reset();                  /* FUN_8002cf7c */
    g_GameInfo->racingClass = g_Settings.racingClass;   /* 8002cf7c: la clase de la carrera (+0x08) */
    Script_ResetRace();            /* 80067680 */
    {
        /* solo en el port (depuracion): NR_POWERUPS / NR_CLASS fuerzan los poderes (0..3) y la clase (0..3) */
        const char* pu = getenv("NR_POWERUPS");
        const char* cl = getenv("NR_CLASS");
        if (pu)
            g_GameInfo->powerUps = atoi(pu) & 3;
        if (cl)
            g_GameInfo->racingClass = atoi(cl) & 3;
    }
    /* 80074d90 + 8001836c + 800182ec: aleatorios con los frames del front-end como semilla (en la repeticion, la
     * grabada; tambien vuelve a poner la clase de la carrera grabada) */
    int seed = Replay_Begin(g_FrontendFrames);
    Physics_Init();
    Rand_SetSeed(seed);
    g_HaveStart = 0;
    strcpy(g_LevelPath, path);
    Gfx_InitBuffers(0x9c4);
    Render_Init();
    Render_SetPreset(1);           /* camaras de carrera: H = 180 */
    Stream_Open(g_LevelPath);      /* abre GlblData.psx; el .trk despues del ultimo coche */
    Font_Init();
    Font_SetDeferUpload(1);
    TexSlot_Init();

    /* los coches se leen en el orden en que estan en GlblData (por modelo) */
    int n = g_RaceGrid.count;
    for (int i = 0; i < n; i++)
        g_CarLoadOrder[i] = (uint8_t)i;
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (g_RaceGrid.car[g_CarLoadOrder[j]].model < g_RaceGrid.car[g_CarLoadOrder[i]].model)
            {
                uint8_t t = g_CarLoadOrder[i];
                g_CarLoadOrder[i] = g_CarLoadOrder[j];
                g_CarLoadOrder[j] = t;
            }
}

/* 80023ef4 */
int Race_NextCar(void)
{
    if (g_IsFrontend || g_CarsLoaded >= g_RaceGrid.count)
        return 0;
    int last = g_CarsLoaded == g_RaceGrid.count - 1;
    Stream_StartSection(g_RaceGrid.car[g_CarLoadOrder[g_CarsLoaded]].model, last);
    return 1;
}

static void Bar(int x, int y0, int y1, int w, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3)
{
    POLY_G4* p = (POLY_G4*)Gfx_AllocPrim(sizeof(POLY_G4));
    if (!p)
        return;
    setPolyG4(p);
    setXY4(p, x, y0, x + w, y0, x, y1, x + w, y1);
    setRGB0(p, c0 & 0xff, (c0 >> 8) & 0xff, (c0 >> 16) & 0xff);
    setRGB1(p, c1 & 0xff, (c1 >> 8) & 0xff, (c1 >> 16) & 0xff);
    setRGB2(p, c2 & 0xff, (c2 >> 8) & 0xff, (c2 >> 16) & 0xff);
    setRGB3(p, c3 & 0xff, (c3 >> 8) & 0xff, (c3 >> 16) & 0xff);
    Gfx_AddPrimDepth(1, p);
}

/* 80024f08: barra de progreso bajo la pantalla de carga (de rojo a verde) */
void Race_DrawProgress(void)
{
    int prog = Stream_GetLoadProgress(g_CarsLoaded, g_RaceGrid.count);
    if (prog > 0x400)
        prog = 0x400;
    int w = prog * 0xa1 >> 10, t = prog / 10;
    TILE* frame = (TILE*)Gfx_AllocPrim(sizeof(TILE));
    TILE* back = (TILE*)Gfx_AllocPrim(sizeof(TILE));
    if (!frame || !back)
        return;
    setTile(frame);
    setRGB0(frame, 0x60, 0x60, 0x40);
    setXY0(frame, 0x4d, 0xbd);
    setWH(frame, 0xa3, 0xc);
    Gfx_AddPrimDepth(3, frame);
    setTile(back);
    setRGB0(back, 0, 0, 0);
    setXY0(back, 0x4e, 0xbe);
    setWH(back, 0xa1, 10);
    Gfx_AddPrimDepth(2, back);
    uint32_t dark = 0x080820, light = 0x202080;
    uint32_t endDark = (uint32_t)(((0x80 - t) >> 3) | (t >> 3) << 8);
    uint32_t endLight = (uint32_t)((0x80 - t) | t << 8);
    Bar(0x4e, 0xbe, 0xc3, w, dark, endDark, light, endLight);
    Bar(0x4e, 0xc3, 200, w, light, endLight, dark, endDark);
}

/* 800134f0: los 4 modelos de cada coche (10000 + hueco*10 + 1..4) usan la libreia de su slot
 * (0xE + numero de jugador; solo el del jugador 1 con la pagina a resolucion completa) y despues los
 * modelos comunes 20001-20009 */
static void Cars_RegisterModels(void)
{
    for (int i = 0; i < g_RaceGrid.count; i++)
    {
        uint8_t* quad = NULL;   /* 8001309c con la caja del primer modelo, para los que no traen 3DSD */
        for (int k = 1; k < 5; k++)
        {
            NrAsset* a = Asset_Find(ASSET_Cobj, 10000 + i * 10 + k);
            if (!a || a->obj)
                continue;
            ModelHdr* m = Model_Register(a->data, a->size, -0xe - g_RaceGrid.car[i].playerNum);
            a->obj = m;
            if (!m)
                continue;
            if (!quad)
                quad = Model_MakeShadowQuad(m);
            if (!m->shadow)
                m->shadow = quad;
        }
    }
    for (int id = 20001; id < 20010; id++)
    {
        NrAsset* a = Asset_Find(ASSET_Cobj, id);
        if (a && !a->obj)
            a->obj = Model_Register(a->data, a->size, id);
    }
}

/* 80062680 */
int Ctrk_OnLoaded(NrAsset* a)
{
    Asset_Register(a);
    Track_Parse(a->data, a->size); /* 80062088 */
    Cars_RegisterModels();
    return 1;
}

/* 80033724: sobre cada companero de equipo del humano de la vista, un triangulo del color del equipo apuntando abajo
 * (0x560 por encima del coche, de 0x12 * H / z + 2 de medio ancho) */
static void Race_DrawTeamMarkers(int view, const RaceCar* h)
{
    const RenderProj* pr = Render_GetProjection();
    Render_LoadCameraRotation();
    for (int i = 0; i < g_NumCars; i++)
    {
        const RaceCar* c = &g_Cars[i];
        if (c == h || !c->phys || c->team != h->team)
            continue;
        Gte_SetV0((c->obj.pos[0] - g_Camera.pos[0]) >> 4, ((c->obj.pos[1] - g_Camera.pos[1]) >> 4) + 0x56,
                  (c->obj.pos[2] - g_Camera.pos[2]) >> 4);
        doCOP2(GTE_OP_RTPS);
        if (CFC2(31) & 0x1c66000)
            continue;
        uint32_t sxy = MFC2(14);
        int sz = (int)MFC2(19);
        if (sz < 0x20)
            continue;
        POLY_F3* p = (POLY_F3*)Gfx_AllocPrim(sizeof(POLY_F3));
        if (!p)
            return;
        int x = (int16_t)sxy, y = (int16_t)(sxy >> 16), w = pr->h * 0x12 / sz + 2;
        unsigned col = Hud_TeamColor(c->team);   /* 15 bits con el rojo arriba */
        setPolyF3(p);
        setRGB0(p, (col >> 7) & 0xf8, (col >> 2) & 0xf8, (col << 3) & 0xf8);
        setXY3(p, x - w, y - w, x + w, y - w, x, y + w);
        Gfx_AddPrimDepth(sz, p);
    }
    (void)view;
}

/* Race_DrawView 8006422c + Fx_Draw + Sprite_DrawAll de una vista con la camara en g_Camera; el retrovisor (mirror) es
 * la misma escena con la camara mirando atras (sin el destello del sol y, en el port, sin el coche del jugador) */
static void Race_DrawScene(int view, int mirror, int seg)
{
    Sprite_Clear();
    if (!mirror)
        Render_DrawLensFlare(view);    /* 8001551c (con la marca del retrovisor no hace nada) */
    Track_DrawView(view, seg, mirror);
    Track_DrawHorizon(view, mirror);
    Track_DrawSegments(view, mirror);
    Render_ClearList();
    Race_AddCarsToDrawList(view, mirror);
    Actors_AddToDrawList(view);        /* 80061a0c + Actors_DrawView 80061b28 */
    Render_DrawListView(view, mirror);
    Race_RestoreCarModels();
    if (g_Humans[view])
        Race_DrawTeamMarkers(view, g_Humans[view]);   /* 80033724 */
    Fx_Draw(view, mirror);             /* 80038ccc */
    Sprite_DrawAll();                  /* 80045268 */
}

/* la vista entera con el marcador (80043520) */
static void Race_DrawViewAll(int view)
{
    int seg = RaceCam_GetSegment(view);
    if (seg == 0xff)
        seg = Track_FindSegment(g_Camera.pos);
    Race_DrawScene(view, 0, seg);
    Render_EndLensFlare(view);
    Hud_Draw(view);                    /* 80043520 */
}

/* Pantalla partida (8001a6fc por vista): la vista 'view' se dibuja en su mitad (y 0 o 120) con el area de dibujo
 * recortada y el desplazamiento de dibujo de la mitad, como el original; cada una en su capa de la OT para que no
 * se mezclen por profundidad. half < 0: vuelve a la pantalla entera (para la pausa y los resultados). */
static void Race_SetDrawHalf(int half, int otIndex)
{
    int y0 = Gfx_GetDrawBufferY();
    DR_AREA* area = (DR_AREA*)Gfx_AllocPrim(sizeof(DR_AREA));
    DR_OFFSET* ofs = (DR_OFFSET*)Gfx_AllocPrim(sizeof(DR_OFFSET));
    if (!area || !ofs)
        return;
    int y = half < 0 ? 0 : half * (GFX_SCREEN_H / 2);
    RECT16 rc = { 0, (short)(y0 + y), GFX_SCREEN_W, (short)(half < 0 ? GFX_SCREEN_H : GFX_SCREEN_H / 2) };
    SetDrawArea(area, &rc);
    /* SetDrawOffset (PsyCross no la trae): comando 0xE5 con x en los bits 0-10 e y en los 11-21. Una sola palabra:
     * PsyCross para en un 0 dentro del paquete y lee lo que siga en memoria como otra primitiva */
    setlen(ofs, 1);
    ofs->code[0] = 0xe5000000u | (uint32_t)((y0 + y) & 0x7ff) << 11;
    Gfx_AddPrim(otIndex, area);
    Gfx_AddPrim(otIndex, ofs);
}

/* Race_Frame1P 80026f28 (parte): retrovisor con R1 mantenido (un jugador, despues de la salida y sin haber
 * terminado). Camara en el coche + 0x200 de altura con su matriz y el eje Z invertido (imagen de espejo, por
 * eso se dibuja con las caras invertidas), proyeccion del retrovisor y rectangulo 0x70 x 0x2A en (0x68, 10)
 * (Gfx_Flip(2)) con un marco negro. En el port va en una segunda capa de la OT recortada con DR_AREA. */
static void Race_DrawMirror(void)
{
    RaceCar* h = g_Humans[0];
    if (g_NumHumans != 1 || !h || !h->phys || !h->ctrl || g_RaceTime <= 0x4af || g_RacePaused)
        return;
    if (*(const int*)(h->phys + 0x330) != 0 || !(*(const uint32_t*)(h->ctrl + 8) & 0x800000))
        return;
    Camera3D saved = g_Camera;
    const int* r = h->obj.rot;
    for (int k = 0; k < 3; k++)
    {
        g_Camera.view[3 * k + 0] = r[k];
        g_Camera.view[3 * k + 1] = r[3 + k];
        g_Camera.view[3 * k + 2] = -r[6 + k];
    }
    g_Camera.pos[0] = h->obj.pos[0];
    g_Camera.pos[1] = h->obj.pos[1] + 0x200;
    g_Camera.pos[2] = h->obj.pos[2];

    Gfx_FlushPartial();
    int y0 = Gfx_GetDrawBufferY();
    DR_AREA* area = (DR_AREA*)Gfx_AllocPrim(sizeof(DR_AREA));
    TILE* bg = (TILE*)Gfx_AllocPrim(sizeof(TILE));
    if (!area || !bg)
    {
        g_Camera = saved;
        return;
    }
    RECT16 rc = { 0x68, (short)(y0 + 10), 0x70, 0x2a };
    SetDrawArea(area, &rc);
    Gfx_AddPrim(GFX_OT_LEN - 1, area);
    setTile(bg);
    setRGB0(bg, 0, 0, 0);
    setXY0(bg, 0x68, 10);
    setWH(bg, 0x70, 0x2a);
    Gfx_AddPrim(GFX_OT_LEN - 1, bg);

    Render_SetMirrorProjection(1);
    /* 80026980 con la marca del retrovisor (camara +7): la escena entera, efectos y destellos incluidos */
    Race_DrawScene(0, 1, h->tpos.seg != 0xff ? h->tpos.seg : Track_FindSegment(g_Camera.pos));
    Render_SetMirrorProjection(0);

    DR_AREA* full = (DR_AREA*)Gfx_AllocPrim(sizeof(DR_AREA));
    if (full)
    {
        RECT16 rf = { 0, (short)y0, GFX_SCREEN_W, GFX_SCREEN_H };
        SetDrawArea(full, &rf);
        Gfx_AddPrim(0, full);
    }
    Hud_Rect15(0, 0x68 * 2, 10 * 2, 0xe0, 2, 0);
    Hud_Rect15(0, 0x68 * 2, (10 + 0x29) * 2, 0xe0, 2, 0);
    Hud_Rect15(0, 0x68 * 2, (10 + 1) * 2, 2, 0x50, 0);
    Hud_Rect15(0, (0x68 + 0x6f) * 2, (10 + 1) * 2, 2, 0x50, 0);
    g_Camera = saved;
}

/* 800299a8: oyente del sonido para la vista (camara: posicion, rumbo, velocidad del coche que sigue y si esta
 * en un tunel, bit 14 de la cara de la pista bajo ella) */
static void Race_UpdateListener(int view)
{
    int heading = Math_Atan2(g_Camera.view[2], g_Camera.view[8]) & 0xfff;
    int vel[3] = { 0, 0, 0 };
    RaceCar* t = RaceCam_GetTarget(view);
    if (t && t->phys)
        memcpy(vel, t->phys + 0x154, sizeof(vel));
    TrackPos tp = { 0xff, 0, -1 };
    const uint8_t* face = NULL;
    int reverb = 0;
    if (Track_FindSurface(&tp, g_Camera.pos, NULL, 1) != TRACK_NO_SURFACE)
    {
        Track_SurfaceType(&tp, &face);
        reverb = face ? (*(const uint16_t*)(face + 8) >> 14) & 1 : 0;
    }
    Snd_SetListener(view, g_Camera.pos, heading, vel, reverb, tp.curve);
}

/* Solo en el port (depuracion): NR_ENDRACE=t da por terminada la carrera del jugador 1 a los t tics, como si
 * cruzara la meta (la vuelta rapida, si no tiene, es el tiempo total); con dos jugadores el 2 termina 300 tics
 * despues */
static void Race_DebugEnd(void)
{
    static int s_end = -1;
    if (s_end < 0)
        s_end = getenv("NR_ENDRACE") ? atoi(getenv("NR_ENDRACE")) : 0;
    if (s_end <= 0)
        return;
    int done = 0;
    for (int p = 0; p < 2; p++)
    {
        RaceCar* e = g_Humans[p];
        if (!e || !e->phys)
            continue;
        if (I32(e->phys, 0x330) != 0)
        {
            done++;
            continue;
        }
        if (g_RaceTime < s_end + p * 300)
            continue;
        I32(e->phys, 0x330) = g_RaceTime - 900;
        int slot = e->id - 1000;
        if (slot >= 0 && slot < 6 && g_CarRecords[slot].b == 0)
            g_CarRecords[slot].b = (g_RaceTime - 900) / 3;
        done++;
        NR_Log("[depuracion] NR_ENDRACE: el jugador %d termina a los %d tics\n", p + 1, g_RaceTime);
    }
    if (done == g_NumHumans && g_RaceEndTimer == 0)
        g_RaceEndTimer = 5;
}

/* Solo en el port (depuracion): NR_REPLAYLOG=1 apunta cada 150 tics de carrera una suma de la posicion y la
 * velocidad de todos los coches; la de la carrera y la de su repeticion deben coincidir en cada tiempo */
static void Race_DebugReplay(void)
{
    static int s_on = -1;
    if (s_on < 0)
        s_on = getenv("NR_REPLAYLOG") != NULL;
    if (!s_on || g_RaceTime / 150 == g_PrevRaceTime / 150)
        return;
    uint32_t sum = 0;
    for (int i = 0; i < g_NumCars; i++)
    {
        const RaceCar* e = &g_Cars[i];
        for (int k = 0; k < 3; k++)
            sum = sum * 31 + (uint32_t)e->obj.pos[k];
        if (e->phys)
            for (int k = 0; k < 3; k++)
                sum = sum * 31 + (uint32_t)I32(e->phys, 0x154 + k * 4);
    }
    NR_Log("[repeticion] %s t %d suma %08x\n", g_GameInfo->isReplay ? "rep" : "car", g_RaceTime, sum);
}

/* 800269c0: espera el frame y lee el mando de los humanos. Fuera de la repeticion graba el frame (80074ac8) si hay
 * humanos, no esta en pausa y el ultimo no ha terminado; en la repeticion pone los mandos y el dt grabados
 * (80074860) y el frame dura ese dt. Devuelve el dt del frame. */
static int Race_UpdateTime(void)
{
    Frame_Wait();
    Race_ReadPads();
    if (!g_GameInfo->isReplay)
    {
        if (g_NumHumans > 0 && !g_RacePaused && g_NumCars > 0 && I32(g_CarPhys[g_NumCars - 1], 0x330) == 0)
            Replay_RecordFrame(g_FrameDelta);
    }
    else if (!g_RacePaused)
    {
        int dt = Replay_PlayFrame();
        if (dt >= 0)
            Frame_WaitUntil(dt);
        else
            Pause_ReplayEnded();
    }
    else
        Pause_ReplayWait();
    return g_FrameDelta;
}

/* 800299a8 con Race_Frame1P 80026f28 o, con dos jugadores, Race_Frame2P 8002767c: cada vista con su camara
 * (8005f034), su proyeccion de media pantalla y su marcador, en su mitad (Race_SetDrawHalf). En la repeticion
 * (GameInfo +0x60) los mandos y el dt salen de la grabacion (Race_UpdateTime). */
void Race_MainLoop(void)
{
    Font_SetDeferUpload(0);
    Font_FlushUploads();
    g_RaceTime = g_PrevRaceTime = 0;
    Race_CreateCars();             /* 8003132c (desde Level_Run) */
    Fx_PostLoad();                 /* 8003913c (Level_Run) + 80075808 (Level_WaitLoaded) */
    TvCam_PostLoad();              /* 80072320 (Level_Run): camaras de TV virtuales */
    Actors_LogSummary();
    RaceCam_Init();
    Hud_Init();
    Pause_Init();
    const char* laps = getenv("NR_LAPS");   /* solo en el port (depuracion): numero de vueltas */
    if (laps && atoi(laps) > 0)
        g_Settings.laps = (uint8_t)atoi(laps);
    const char* power = getenv("NR_POWER");  /* solo en el port (depuracion): poder en la mano del jugador 1 */
    if (power && g_Humans[0] && g_Humans[0]->phys)
    {
        g_Humans[0]->phys[0x338] = (uint8_t)atoi(power);
        *(int16_t*)(g_Humans[0]->phys + 0x33e) = 0x7fff;
    }
    Snd_RaceInit();                    /* 8003cfd0 */
    {
        char p[64];
        snprintf(p, sizeof(p), "%s%s.AV", Level_GetDataDir(), Fe_VenueCode(g_Venue));
        Music_Play(p);                 /* musica de la sede (en el original, por el stream del .AV) */
    }
    Snd_ApplySettings(g_Settings.volume, g_Settings.audioMode);
    Snd_Pause(1);                      /* 8003fe7c(1) (Level_WaitLoaded) */
    Camera3D viewCam[2];               /* camara de cada vista (DAT_800b6160 + vista * 0x40) */
    memset(viewCam, 0, sizeof(viewCam));
    g_FrameCount = 0;                  /* DAT_800af728 */
    while (g_GameState == GAMESTATE_RUNNING || g_GameState == GAMESTATE_RESULTS)
    {
        g_FrameCount++;
        int dt = Race_UpdateTime();    /* 800269c0 */
        Gfx_BeginFrame(0, 0, 0);       /* el original no borra: el horizonte cubre el fondo */
        if (!g_RacePaused)
        {
            g_PrevRaceTime = g_RaceTime;
            g_RaceTime += dt;          /* Race_UpdateTime 800269c0 */
            Actors_RetryOrders(dt);    /* 800497f4 */
            Model_AnimateTextures(dt); /* 8001364c */
            Track_Animate(dt);         /* 80061e94 */
            Script_Tick(dt);           /* 80067834 */
            Actors_Activate();         /* 80049b74 */
            Actors_RunOrders();        /* 80048f78 */
            Power_ClearAlerts();       /* Race_Frame1P: aviso de clima */
            Actors_Update(dt);         /* 80048dec(6) */
            Race_UpdateCars(dt);
            Race_DebugEnd();
            Race_DebugReplay();
            Actors_PostUpdate();       /* 80048e84 */
            Fx_Update(dt);             /* 80038b50 */
            Hud_Update(dt);            /* 80042330 */
            for (int v = 0; v < (g_SplitScreen ? 2 : 1); v++)
            {
                RaceCam_Update(v, dt); /* 8005f034 */
                viewCam[v] = g_Camera;
            }
        }
        else if (g_PauseCamPreview > 0)
        {
            g_PauseCamPreview = g_PauseCamPreview < dt ? 0 : g_PauseCamPreview - dt;
            RaceCam_Update(0, dt);
            viewCam[0] = g_Camera;
        }

        if (!g_SplitScreen)            /* Race_Frame1P 80026f28 */
        {
            g_Camera = viewCam[0];
            Race_DrawViewAll(0);
            Race_DrawMirror();
            Race_UpdateListener(0);    /* 800299a8: 8003dd84 por vista */
        }
        else                           /* Race_Frame2P 8002767c */
        {
            for (int v = 0; v < 2; v++)
            {
                if (v == 1)
                    Gfx_FlushPartial();
                Race_SetDrawHalf(v, GFX_OT_LEN - 1);
                g_Camera = viewCam[v];
                Render_SetPreset(TvCam_CurrentPreset(v));   /* Camera_ApplyProjection(DAT_800b6162 + vista * 0x40) */
                Race_DrawViewAll(v);
                if (v == 0)
                    Hud_Rect15(1, 0, 0xee, 0x280, 2, 0);    /* raya negra entre las dos mitades */
                Race_UpdateListener(v);
            }
            Race_SetDrawHalf(-1, 0);
        }
        if (g_GameState == GAMESTATE_RESULTS)
            Race_Results(dt);          /* 8002896c */
        else if (g_RacePaused)
            Pause_Menu();              /* 80025ad4 */

        Snd_SetListenerCount(g_SplitScreen ? 2 : 1);   /* 8003d48c */
        Snd_Update();                  /* 8003e848 */

        if (!g_RacePaused)
            Csacs_Update(dt);          /* 80060a60 */
        Asset_DispatchAll();
        Gfx_Flip(GFX_FLIP_FULL);
        Race_UpdateState(dt);          /* 80027e90 */
    }
    Snd_StopRace();                    /* 8003d04c */
    Replay_End();                      /* 800747a4 (8002456c, fuera de la repeticion) */
    Script_Disable();                  /* 80067880 (8002456c) */
    Fx_FreeAll(2);                     /* 80038e28 (8002456c) */
    TvCam_Free();                      /* RaceCam_ResetPresets 80072348 (8002456c) */
    Render_SetPreset(1);
    Race_FreeCars();
    Actors_Free();
    Storm_FreeAll();                   /* 80070e1c (8002456c) */
    Track_Free();
}
