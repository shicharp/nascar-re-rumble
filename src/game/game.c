#include "pause.h"
#include "frontend.h"
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
#include "snd.h"
#include "savegame.h"
#include "script.h"
#include "race.h"
#include "render.h"
#include "texslot.h"
#include "model.h"

GameOptions g_Options;

static int s_lastFrameTime;   /* DAT_800af718 */

/* 80079470. Pendiente: InitHeap3 (malloc del host), FUN_80074ee8, FUN_80016dac, CdInit (no hay CD),
 * callback de VSync (FUN_8001ce28(FUN_8008b160)), FUN_8001a884, memoria. */
int Game_Init(void)
{
    g_GameInfo = (GameInfo*)Mem_Alloc(sizeof(GameInfo), 3);
    memset(g_GameInfo, 0, sizeof(GameInfo));
    Pad_Init();
    Snd_Init();                     /* 80022330 */
    return 1;
}

/* 800795b4 */
void Game_Shutdown(void)
{
    Frontend_Free();
    Mem_Free(g_GameInfo);
    g_GameInfo = NULL;
}

/* 80090d98. El original deja la imagen en pantalla y espera 1500 vblanks (con param 0 vigila la
 * tarjeta de memoria y no deja saltar). En el port se redibuja cada frame (PsyCross necesita
 * presentar) y siempre se puede saltar con Start/X. */
void Legal_Show(int skippable)
{
    Lsc_LoadToVram("CW\\Opening\\Legal.lsc", 1);
    Timer_Update();
    int end = g_Ticks + (g_Options.skipLegal ? 150 : 1500);   /* 1500 ticks = 5 s */
    while (g_Ticks < end)
    {
        Timer_Update();
        if (!skippable)
            Save_BootLoad();   /* 8009347c / 80093540 / 8009358c / 80093938: la partida de la tarjeta */
        Pad_ReadAll();
        if ((g_PadButtons[0] | g_PadButtons[1]) & (PADB_START | PADB_CROSS))
            break;
        Gfx_BeginFrame(0, 0, 0);
        Fe_DrawBackground(0);
        Gfx_Flip(GFX_FLIP_FULL);
    }
    if (!skippable)
        Save_BootEnd();
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

/* 800269c0 (repeticion): el frame dura los tics grabados; tras Frame_Wait espera a que pasen desde el frame anterior.
 * Si ya han pasado mas, el resto se pierde (como en el original: la repeticion va mas lenta, nunca mas deprisa) */
void Frame_WaitUntil(int ticks)
{
    int start = s_lastFrameTime - g_FrameDelta;
    while (g_GameTime - start < ticks)
    {
        Pad_ReadAll();
        Timer_Update();
    }
    g_FrameTime = g_GameTime;
    g_FrameDelta = ticks;
    s_lastFrameTime = g_GameTime;
}

/* 8009233c */
const char* Level_GetDataDir(void)
{
    static char dir[32];
    if (g_IsFrontend)
        snprintf(dir, sizeof(dir), "%s%s", "CW\\", "FEnd\\");
    else
        snprintf(dir, sizeof(dir), "%sLoc%s\\", "CW\\", Fe_VenueCode(g_Venue));   /* sede DAT_800af386 */
    return dir;
}

/* 80024080 (parte imprescindible). Pendiente: FUN_80023e70, pools (FUN_80016e2c), FUN_8001836c/182ec,
 * FUN_8001930c, FUN_800185fc, FUN_8003cfd0, FUN_80067680, camaras (FUN_800167c8), FUN_80046978,
 * FUN_800137d8, FUN_80061cd0/80012be0, FUN_80060b98, FUN_800446b8, FUN_80066fe0. */
void Frontend_Load(const char* path)
{
    strcpy(g_LevelPath, path);
    Gfx_InitBuffers(0x478);
    TexSlot_Init();                 /* FUN_80066fe0 */
    Render_Init();                  /* FUN_800137d8 */
    Render_SetPreset(3);            /* Camera_Create(3) + Camera_ApplyProjection: H = 360 */
    Script_ResetRace();             /* 80067680: los guiones de las cabeceras del FE.TRK */
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
        Frontend_Load(path);
    else
        Race_Load(path);
    return 1;
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

    int idle = 0;
    while (g_GameState != GAMESTATE_RUNNING)
    {
        Pad_ReadAll();
        int waitingCar = g_GameState == GAMESTATE_LOADED_PART && Race_NextCar();
        if (Stream_Pump(1) == 0 && !waitingCar && ++idle > 60)
        {
            NR_Log("Level_WaitLoaded: fin del stream sin Cctr (estado %d)\n", g_GameState);
            return 0;
        }
        Asset_DispatchAll();
        /* Las paginas de la pista (slots 0-12) pisan la pantalla de carga en VRAM: el original no la vuelve a
         * dibujar; aqui se deja de presentar cuando empieza a llegar el .trk */
        if (g_IsFrontend || Stream_GetLoadProgress(g_CarsLoaded, g_RaceGrid.count) < 0x200)
        {
            Gfx_BeginFrame(0, 0, 0);
            Fe_DrawBackground(g_IsFrontend ? 0 : 0x800);   /* en carrera, detras de la barra */
            if (!g_IsFrontend)
                Race_DrawProgress();
            Gfx_Flip(GFX_FLIP_FULL);
        }
    }
    g_GameInfo->nextMode = 1;
    Stream_SetIdle();               /* el resto (secciones de coches, musica) se lee bajo demanda */
    NR_Log("Nivel cargado: %d bloques leidos, %d assets, %d descartados por su guion\n", Stream_GetStats()->blocks,
           Stream_GetStats()->assets, Stream_GetStats()->skipped);
    return 1;
}

/* 8002865c */
void Frontend_MainLoop(void)
{
    g_FrameCount = 0;
    Frontend_InitScreens();
    {
        char p[64];
        snprintf(p, sizeof(p), "%sFE.TRK", Level_GetDataDir());
        Music_Play(p);              /* (en el original llega por el stream: VAGM de FE.TRK) */
    }
    while (g_GameState == GAMESTATE_RUNNING)
    {
        g_FrameCount++;
        g_FrontendFrames++;
        Frame_Wait();
        Gfx_BeginFrame(0, 0, 0);
        Frontend_Update();
        Frontend_DrawFade();
        Snd_SetListenerCount(0);    /* 8003d48c(0) */
        Snd_Update();               /* 8003e848 */
        Stream_Pump(1);             /* el original lee por DMA en segundo plano */
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
    Snd_Pause(0);                   /* 8003fe7c(0) */
    Music_Stop();                   /* 80023cf8 */
    Speech_Reset();                 /* 80023088 / 8002b4ac */
    Speech_SetToc(NULL, 0);
    Stream_Close();                 /* FUN_8002b7c8 / Stream_Close 8002ba0c */
    if (g_IsFrontend)
        Frontend_Free();            /* FUN_800241d4 */
    Snd_UnloadBanks();              /* 80022da4 */
    Asset_ClearRegistry();
}

/* 80029be4: "Show Replay" / "Restart Replay" (DAT_800af690) vuelven a cargar la pista con GameInfo +0x60 = 1;
 * "Restart Race" (DAT_800af691), sin la repeticion */
int Level_Run(const char* name, int isFrontend)
{
    for (;;)
    {
        g_RaceRestart = 0;
        g_ShowReplay = 0;
        g_IsFrontend = isFrontend;
        if (!Level_Load(name))
            return 0;
        if (!Level_WaitLoaded())
            return 0;
        if (g_IsFrontend)
            Frontend_MainLoop();
        else
            Race_MainLoop();
        Level_Unload(name);
        if (g_ShowReplay)
        {
            g_GameInfo->isReplay = 1;
            continue;
        }
        g_GameInfo->isReplay = 0;
        if (!g_RaceRestart)
            break;
        /* "Restart Race": borra la posicion final y el tiempo de cada registro y vuelve a cargar */
        for (int i = 0; i < g_RaceGrid.count && i < 6; i++)
        {
            g_CarRecords[i].a = 0;
            g_CarRecords[i].startPos = 0;
        }
    }
    return 0;
}

/* 800792a8 */
int Game_Main(void)
{
    g_BootState = 0;
    g_IsFrontend = 1;
    if (Game_Init())
    {
        g_GameInfo->racingClass = 1;
        g_GameInfo->unk98 = 1;
        g_GameInfo->nextMode = 3;
        Gfx_InitBuffers(200);

        char intro[64];
        snprintf(intro, sizeof(intro), "%s%sIntro.wve", "CW\\", "Opening\\");
        Legal_Show(0);
        if (g_Options.movie)
        {
            /* solo en el port (pruebas): --movie <fichero> pone ese video y sale */
            Movie_Play(g_Options.movie, 4, 0, 0xc, 0x140, 0xf0);
            Gfx_FreeBuffers();
            Game_Shutdown();
            return 0;
        }
        if (!g_Options.skipLegal)   /* (el port salta tambien los videos con --skip-legal) */
        {
            Movie_Play(intro, 4, 0, 8, 0x140, 0xf0);
            Movie_Play("Ea_logo.fsv", 4, 0, 0, 0x140, 0xf0);   /* (no esta en el disco) */
        }
        Gfx_FreeBuffers();

        /* Bucle principal: front-end y carrera elegida */
        for (;;)
        {
            g_SelectedTrack[0] = 0;
            Level_Run("Fe.trk", 1);
            if (!g_SelectedTrack[0])
                break;
            Level_Run(g_SelectedTrack, 0);
            g_RaceExit = g_GameInfo->nextMode;   /* 800793d8 */
            if (g_Settings.demo && g_RaceExit == 0)
            {
                /* la demostracion que acaba sola pone la pantalla legal (se puede saltar) */
                Gfx_InitBuffers(200);
                Legal_Show(1);
                Gfx_FreeBuffers();
            }
        }
    }
    Game_Shutdown();
    return 0;
}
