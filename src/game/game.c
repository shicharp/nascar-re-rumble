#include <stdio.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "libetc.h"
#include "nr.h"
#include "mem.h"
#include "file.h"
#include "stream.h"
#include "asset.h"
#include "gfx.h"
#include "pad.h"
#include "lsc.h"
#include "globals.h"
#include "frontend.h"
#include "font.h"
#include "game.h"

GameOptions g_Options;

static int s_lastFrameTime;   /* DAT_800af718 */

/* 80079470. Pendiente: InitHeap3 (malloc del host), FUN_80074ee8, FUN_80016dac, CdInit (no hay CD),
 * callback de VSync (FUN_8001ce28(FUN_8008b160)), sonido (FUN_8001a884, FUN_80022330), memoria. */
int Game_Init(void)
{
    g_GameInfo = (GameInfo*)Mem_Alloc(sizeof(GameInfo), 3);
    memset(g_GameInfo, 0, sizeof(GameInfo));
    Pad_Init();
    return 1;
}

/* 800795b4 */
void Game_Shutdown(void)
{
    Frontend_Free();
    Mem_Free(g_GameInfo);
    g_GameInfo = NULL;
}

/* 800991e4. TODO: decodificador de video WVE de EA (y FSV) */
void Movie_Play(const char* name, int a, int b, int c, int w, int h)
{
    (void)a; (void)b; (void)c; (void)w; (void)h;
    NR_Log("Movie_Play: '%s' omitido (video WVE/FSV sin portar)\n", name);
}

/* 80090d98. El original deja la imagen en pantalla y espera 1500 vblanks (con param 0 vigila la
 * tarjeta de memoria y no deja saltar). En el port se redibuja cada frame (PsyCross necesita
 * presentar) y siempre se puede saltar con Start/X. */
void Legal_Show(int skippable)
{
    (void)skippable;
    Lsc_LoadToVram("CW\\Opening\\Legal.lsc", 1);
    Timer_Update();
    int end = g_Ticks + (g_Options.skipLegal ? 150 : 1500);   /* 1500 ticks = 5 s */
    while (g_Ticks < end)
    {
        Timer_Update();
        Pad_ReadAll();
        if ((g_PadButtons[0] | g_PadButtons[1]) & (PADB_START | PADB_CROSS))
            break;
        Gfx_BeginFrame(0, 0, 0);
        Fe_DrawBackground(0);
        Gfx_Flip(GFX_FLIP_FULL);
    }
}

/* 800285e4: espera a que avance el tiempo de juego y calcula el delta del frame (maximo 30 ticks) */
void Frame_Wait(void)
{
    int delta;
    do
    {
        Pad_ReadAll();
        Timer_Update();
        g_FrameTime = g_GameTime;
        delta = g_GameTime - s_lastFrameTime;
    } while (delta < 1);
    if (delta > 30)
        delta = 30;
    g_FrameDelta = delta;
    s_lastFrameTime = g_GameTime;
}

/* 8009233c */
const char* Level_GetDataDir(void)
{
    static char dir[32];
    if (g_IsFrontend)
        snprintf(dir, sizeof(dir), "%s%s", "CW\\", "FEnd\\");
    else
        snprintf(dir, sizeof(dir), "%sLoc%s\\", "CW\\", "BB");   /* TODO: tabla de sedes (DAT_800af386) */
    return dir;
}

/* 80024080 (parte imprescindible). Pendiente: FUN_80023e70, pools (FUN_80016e2c), FUN_8001836c/182ec,
 * FUN_8001930c, FUN_800185fc, FUN_8003cfd0, FUN_80067680, camaras (FUN_800167c8), FUN_80046978,
 * FUN_800137d8, FUN_80061cd0/80012be0, FUN_80060b98, FUN_800446b8, FUN_80066fe0. */
void Frontend_Load(const char* path)
{
    strcpy(g_LevelPath, path);
    Gfx_InitBuffers(0x478);
    Stream_Open(g_LevelPath);       /* Stream_Open 8002baa8: en el front-end se abre el propio .trk */
    Font_Init();                    /* FUN_800446b8 */
    Frontend_InitCamera();
}

/* 80024684 */
int Level_Load(const char* name)
{
    char path[128];
    g_GameState = GAMESTATE_LOADING;
    g_GameInfo->nextMode = 4;
    snprintf(path, sizeof(path), "%s%s", Level_GetDataDir(), name);
    if (g_IsFrontend)
    {
        Frontend_Load(path);
        return 1;
    }
    NR_Log("Level_Load: carrera '%s' sin portar (Race_Load 80024284)\n", path);
    return 0;
}

/* 800252b0. El original no presenta frames mientras carga (la pantalla queda fija); con PsyCross
 * hay que seguir presentando, asi que se redibuja la pantalla de carga en cada vuelta. */
int Level_WaitLoaded(void)
{
    Timer_Update();
    s_lastFrameTime = g_GameTime;
    g_FrameTime = 0;
    g_FrameDelta = 1;
    LoadScreen_Show();

    while (g_GameState != GAMESTATE_RUNNING)
    {
        Pad_ReadAll();
        if (Stream_Pump(1) == 0)
        {
            NR_Log("Level_WaitLoaded: fin del stream sin Cctr (estado %d)\n", g_GameState);
            return 0;
        }
        Asset_DispatchAll();
        Gfx_BeginFrame(0, 0, 0);
        Fe_DrawBackground(0);
        Gfx_Flip(GFX_FLIP_FULL);
    }
    g_GameInfo->nextMode = 1;
    NR_Log("Nivel cargado: %d bloques leidos\n", Stream_GetStats()->blocks);
    return 1;
}

/* 8002865c */
void Frontend_MainLoop(void)
{
    g_FrameCount = 0;
    Frontend_InitScreens();
    while (g_GameState == GAMESTATE_RUNNING)
    {
        g_FrameCount++;
        g_FrontendFrames++;
        Frame_Wait();
        Gfx_BeginFrame(0, 0, 0);
        Frontend_Update();
        Frontend_DrawFade();
        Asset_DispatchAll();        /* FUN_80028634: Asset_Dispatch + Gfx_Flip(1) */
        Gfx_Flip(GFX_FLIP_FULL);
    }
}

/* 80025448 */
void Level_Unload(const char* name)
{
    (void)name;
    DrawSync(0);
    switch (g_GameState)
    {
    case 5:  g_GameInfo->nextMode = 3; break;
    case 6:
    case 0xe: g_GameInfo->nextMode = 2; break;
    case 0xb:
    case 0xc: g_GameInfo->nextMode = 0; break;
    default: g_GameInfo->nextMode = 4; break;
    }
    Stream_Close();                 /* FUN_8002b7c8 / Stream_Close 8002ba0c */
    if (g_IsFrontend)
        Frontend_Free();            /* FUN_800241d4 */
}

/* 80029be4 (sin la logica de reinicio de carrera) */
int Level_Run(const char* name, int isFrontend)
{
    g_IsFrontend = isFrontend;
    if (!Level_Load(name))
        return 0;
    if (!Level_WaitLoaded())
        return 0;
    if (g_IsFrontend)
        Frontend_MainLoop();
    else
        NR_Log("Race_MainLoop 800299a8 sin portar\n");
    Level_Unload(name);
    return 0;
}

/* 800792a8 */
int Game_Main(void)
{
    g_BootState = 0;
    g_IsFrontend = 1;
    if (Game_Init())
    {
        g_GameInfo->unk08 = 1;
        g_GameInfo->unk98 = 1;
        g_GameInfo->nextMode = 3;
        Gfx_InitBuffers(200);

        char intro[64];
        snprintf(intro, sizeof(intro), "%s%sIntro.wve", "CW\\", "Opening\\");
        Legal_Show(0);
        Movie_Play(intro, 4, 0, 8, 0x140, 0xf0);
        Movie_Play("Ea_logo.fsv", 4, 0, 0, 0x140, 0xf0);
        Gfx_FreeBuffers();

        /* Bucle principal: front-end y carrera elegida. La carrera aun no existe en el port. */
        Level_Run("Fe.trk", 1);
        if (g_SelectedTrack[0])
            Level_Run(g_SelectedTrack, 0);
    }
    Game_Shutdown();
    return 0;
}
