#include <stdio.h>
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

uint8_t g_CarLoadOrder[8];
int     g_CarsLoaded;
static NrAsset* s_track;            /* Ctrk (Track_Parse 80062680, pendiente) */

/* 80024284 (parte imprescindible). Pendiente: los ~30 subsistemas de la carrera (sonido, IA, red de
 * trazadas Cnet, actores Cact, camaras, HUD, power-ups...). */
void Race_Load(const char* path)
{
    g_CarsLoaded = 0;              /* FUN_80023e70 */
    s_track = NULL;
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
        for (int k = 1; k < 5; k++)
        {
            NrAsset* a = Asset_Find(ASSET_Cobj, 10000 + i * 10 + k);
            if (!a || a->obj)
                continue;
            a->obj = Model_Register(a->data, a->size, -0xe - g_RaceGrid.car[i].playerNum);
            /* TODO: Model_MakeShadowQuad 8001309c para los que no traen 3DSD */
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
    s_track = a;                   /* TODO: Track_Parse */
    Cars_RegisterModels();
    return 1;
}

/* 800299a8. Pendiente: todo el bucle de carrera. De momento muestra los coches de la parrilla con sus
 * ruedas girando delante de la camara; START o triangulo vuelven al front-end. */
void Race_MainLoop(void)
{
    Font_SetDeferUpload(0);
    Font_FlushUploads();
    int n = g_RaceGrid.count;
    GfxObj cars[8], wheels[8][4];
    for (int i = 0; i < n; i++)
    {
        NrAsset* a = Asset_Find(ASSET_Cobj, 10000 + i * 10 + 1);
        Obj_Init(&cars[i], a ? (ModelHdr*)a->obj : NULL);
        NrAsset* w = Asset_Find(ASSET_Cobj, 20001 + g_RaceGrid.car[i].car * 3);
        for (int k = 0; k < 4; k++)
            Obj_Init(&wheels[i][k], w ? (ModelHdr*)w->obj : NULL);
    }
    int angle = 0;
    while (g_GameState == GAMESTATE_RUNNING)
    {
        Frame_Wait();
        Gfx_BeginFrame(0x20, 0x30, 0x50);
        angle += g_FrameDelta * 4;
        int eye[3] = { 0, 0x500, -0x2400 }, target[3] = { 0, 0, 0 };
        Camera_LookAt(eye, target);
        Render_ClearList();
        for (int i = 0; i < n; i++)
        {
            GfxObj* o = &cars[i];
            if (!o->model)
                continue;
            Math_RotMatrix(angle + i * 0x155, 0, 0, o->rot);
            o->pos[0] = (i % 3 - 1) * 0x1000;
            o->pos[1] = 0;
            o->pos[2] = (i / 3) * 0x1400;
            for (int k = 0; k < 4; k++)
            {
                GfxObj* w = &wheels[i][k];
                if (!w->model)
                    continue;
                Math_RotMatrix(angle + i * 0x155 + ((k & 1) ? 0x800 : 0), 0, 0, w->rot);
                Obj_GetPoint(o, k, w->pos);
                Render_Add(w);
            }
            Render_Add(o);
        }
        Render_DrawList(0);
        /* la fuente de depuracion de PsyCross queda bajo las paginas de la pista: se usan las del juego */
        char line[64];
        Font_Select(0);
        snprintf(line, sizeof(line), "%s (%s), %d vueltas", g_TrackName, g_SelectedTrack, g_Settings.laps);
        Font_DrawString(line, 12, 10, 9);
        Font_DrawString("Bucle de carrera sin portar - START para volver", 12, 22, 9);
        for (int i = 0; i < n; i++)
        {
            snprintf(line, sizeof(line), "%d  %s", i + 1, g_Drivers[g_RaceGrid.car[i].driver].name);
            Font_DrawString(line, 12, 160 + i * 11, 9);
        }
        Asset_DispatchAll();
        Gfx_Flip(GFX_FLIP_FULL);
        if ((g_PadButtons[0] | g_PadButtons[1]) & (PADB_START | PADB_TRIANGLE))
            g_GameState = 0xc;
    }
}
