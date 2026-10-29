#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "libetc.h"
#include "nr.h"
#include "mem.h"
#include "gfx.h"
#include "lsc.h"
#include "globals.h"
#include "frontend.h"
#include "net.h"
#include "fe_internal.h"
#include "savegame.h"
#include "snd.h"
#include "hud.h"
#include "pad.h"

FrontendState* g_Frontend;
short g_FadeDir;
short g_FadeLevel;
unsigned g_MenuFrame;
GameSettings g_Settings;
RaceGrid g_RaceGrid;
CarRecord g_CarRecords[8];
uint8_t g_DemoCount;            /* DAT_800b711a: demostraciones seguidas sin tocar nada (a la cuarta, la intro) */
static const uint8_t* s_crle;   /* solo en el port: Crle 0 en RAM (el original lo relee de VRAM) */
static int s_bmdcSize[13];   /* solo en el port: tamano de cada Bmdc para acotar la lectura del MDEC */
static int s_gmdcSize[14];
static int s_lmdcSize[36];

static_assert(sizeof(FrontendState) == 0x101c, "FrontendState debe medir 0x101C");
static_assert(sizeof(MenuItem) == 0x20, "MenuItem debe medir 0x20");
static_assert(offsetof(FrontendState, pad) == 0xc8, "botones en +0xC8");
static_assert(offsetof(FrontendState, bmdc) == 0xe4, "fondos Bmdc en +0xE4");
static_assert(offsetof(FrontendState, text) == 0x1e0, "tabla de textos en +0x1E0");
static_assert(offsetof(FrontendState, mainMenu) == 0xa9c, "menu principal en +0xA9C");
static_assert(offsetof(FrontendState, raceMenu1P) == 0xb7c, "menu de carrera en +0xB7C");
static_assert(offsetof(FrontendState, gameOptions) == 0xddc, "opciones en +0xDDC");
static_assert(offsetof(FrontendState, showcaseMenu) == 0xf9c, "showcase en +0xF9C");
static_assert(offsetof(FrontendState, portPopup) == 0x94c, "aviso de mandos en +0x94C");
static_assert(offsetof(FrontendState, modelAngle) == 0xc4, "giro del coche en +0xC4");
static_assert(offsetof(FrontendState, carInfo) == 0x1fa, "tabla de coches en +0x1FA");
static_assert(sizeof(CarSlot) == 0x14, "CarSlot debe medir 0x14");
static_assert(offsetof(FrontendState, carSlots) == 0x5ec, "coches cargados en +0x5EC");
static_assert(offsetof(FrontendState, carModel) == 0x640, "modelos de los paneles en +0x640");
static_assert(offsetof(FrontendState, driver) == 0x678, "piloto elegido en +0x678");
static_assert(offsetof(FrontendState, carousel) == 0x688, "carrusel en +0x688");
static_assert(offsetof(FrontendState, thumbClut) == 0x6ac, "CLUT de miniaturas en +0x6AC");
static_assert(offsetof(FrontendState, teamMode) == 0x9f2, "parametros de carrera en +0x9F2");
static_assert(sizeof(GridCar) == 8, "GridCar debe medir 8");
static_assert(sizeof(CarRecord) == 0x18, "CarRecord debe medir 0x18");
static_assert(sizeof(VenueEntry) == 0x10, "VenueEntry debe medir 0x10");
static_assert(offsetof(FrontendState, venues) == 0x6cc, "sedes en +0x6CC");
static_assert(offsetof(FrontendState, preview) == 0x914, "trazado en +0x914");
static_assert(offsetof(FrontendState, vcar) == 0x924, "carrusel de sedes en +0x924");
static_assert(offsetof(FrontendState, vScrollAgain) == 0x946, "+0x946");
static_assert(offsetof(FrontendState, volumeSound) == 0x948, "+0x948");
static_assert(offsetof(FrontendState, trophies) == 0xa48, "copas en +0xA48");

/* Definiciones de menus del EXE: texto, pantalla con X, con triangulo, nº valores, ayuda, tipo, ancho */
static const MenuDef s_mainMenuDefs[7] = {          /* 800ab7b4 */
    { 100, 200, -1, 1, 0, 0, 0 },     /* 1 Player       -> 200 */
    { 110, 200, -1, 1, 0, 0, 0 },     /* 2 Players      -> 200 */
    { 120, -1, -1, 1, 1, 2, 84 },     /* Racing Class   (ajuste) */
    { 130, -1, -1, 1, 1, 2, 84 },     /* Power-Ups      (ajuste) */
    { 140, 900, -1, 1, 2, 1, 84 },    /* Hall of Champs -> 900 */
    { 150, 400, -1, 1, 0, 0, 0 },     /* Game Options   -> 400 */
    { 160, 600, -1, 1, 0, 0, 0 },     /* Showcase       -> 600 */
};
static const MenuDef s_raceMenu1PDefs[5] = {        /* 800ab7fc */
    { 200, 1200, 100, 2, 7, 1, 84 },  /* Single Race */
    { 210, 1200, 100, 3, 7, 1, 84 },  /* Championship */
    { 220, 1200, 100, 1, 6, 0, 0 },   /* Time Trial */
    { 230, 1200, 100, 1, 6, 0, 0 },   /* Showdown */
    { 240, 300, 100, 1, 6, 0, 0 },    /* Race Options */
};
static const MenuDef s_raceMenu2PDefs[5] = {        /* 800ab830 */
    { 300, 1200, 100, 3, 7, 1, 84 },
    { 310, 1200, 100, 3, 7, 1, 84 },
    { 320, 1200, 100, 1, 6, 0, 0 },
    { 330, 1200, 100, 1, 6, 0, 0 },
    { 340, 300, 100, 1, 6, 0, 0 },
};
static const MenuDef s_raceOpts1PDefs[4] = {        /* 800ab864 */
    { 400, -1, 200, 1, 7, 2, 40 },    /* Vehicles */
    { 410, -1, 200, 1, 7, 2, 40 },    /* Teams */
    { 420, -1, 200, 1, 7, 2, 40 },    /* Laps */
    { 430, -1, 200, 1, 7, 2, 40 },    /* Select Opponents */
};
static const MenuDef s_raceOpts2PDefs[5] = {        /* 800ab88c */
    { 500, -1, 200, 1, 7, 2, 40 },
    { 510, -1, 200, 1, 7, 2, 40 },
    { 520, -1, 200, 1, 7, 2, 40 },    /* Cyber Teams */
    { 530, -1, 200, 1, 7, 2, 40 },
    { 540, -1, 200, 1, 7, 2, 40 },
};
static const MenuDef s_gameOptionsDefs[6] = {       /* 800ab8c0 */
    { 600, 500, 100, 1, 6, 0, 0 },    /* Audio Options -> 500 */
    { 610, 1100, 100, 1, 8, 1, 84 },  /* Set Name -> 1100 */
    { 620, -1, 100, 1, 8, 1, 91 },    /* Load and Save (-> 800/700) */
    { 630, -1, 100, 1, 8, 1, 84 },    /* Clear Records */
    { 640, 1000, 100, 1, 6, 0, 0 },   /* Controller Config -> 1000 */
    { 650, 1900, 100, 1, 6, 0, 0 },   /* Hints and Tips -> 1900 */
};
static const MenuDef s_audioDefs[5] = {             /* 800ab8fc */
    { 700, -1, 400, 1, 7, 0, 0 },     /* Effects Volume */
    { 710, -1, 400, 1, 7, 0, 0 },     /* Engine Volume */
    { 720, -1, 400, 1, 7, 0, 0 },     /* Music Volume */
    { 730, -1, 400, 1, 7, 0, 0 },     /* Voice Volume */
    { 740, -1, 400, 1, 7, 2, 50 },    /* Mode */
};
static const MenuDef s_loadSaveDefs[3] = {          /* 800ab930 */
    { 800, -1, 400, 0, 24, 0, 0 },
    { 810, -1, 400, 0, 25, 0, 0 },
    { 820, 400, 400, 0, 6, 0, 0 },
};
static const MenuDef s_showcaseDefs[4] = {          /* 800ab950 */
    { 900, -1, 100, 0, 26, 0, 0 },
    { 910, -1, 100, 0, 26, 0, 0 },
    { 920, -1, 100, 0, 26, 0, 0 },
    { 930, -1, 100, 0, 26, 0, 0 },
};

static void InitMenu(MenuItem* items, const MenuDef* defs, int count, int x, int y)
{
    for (int i = 0; i < count; i++)
        Fe_InitMenuItem(&items[i], &defs[i], x, y + i * 0x11);
}

static void FreeMenu(MenuItem* items, int count)
{
    for (int i = 0; i < count; i++)
    {
        Pool_Free(items[i].values);
        items[i].values = NULL;
    }
}

/* 80079684 (parte): el estado del front-end. Port: la proyeccion de los modelos 3D la pone cada pantalla
 * (SetGeomOffset en fe_select.c / fe_records.c) */
void Frontend_InitCamera(void)
{
    if (!g_Frontend)
        g_Frontend = (FrontendState*)Pool_Alloc(sizeof(FrontendState), 2);
    memset(g_Frontend, 0, sizeof(FrontendState));
    g_Frontend->bgIndex = 0xff;
    g_GameInfo->unk5c = 0;
    g_GameInfo->numPlayers = 1;
    g_GameInfo->racingClass = 1;
    g_GameInfo->powerUps = 1;
}

void Frontend_Free(void)
{
    if (g_Frontend)
    {
        FrontendState* fe = g_Frontend;
        FreeMenu(fe->mainMenu, 7);
        FreeMenu(fe->raceMenu1P, 5);
        FreeMenu(fe->raceMenu2P, 5);
        FreeMenu(fe->raceOpts1P, 4);
        FreeMenu(fe->raceOpts2P, 5);
        FreeMenu(fe->gameOptions, 6);
        FreeMenu(fe->audioMenu, 5);
        FreeMenu(fe->loadSaveMenu, 3);
        FreeMenu(fe->showcaseMenu, 4);
        Fe_FreeGarageObjects();
    }
    Pool_Free(g_Frontend);
    g_Frontend = NULL;
}

/* 80079814. Port: los menus se preparan en sus pantallas (Fe_ResetMenu), los coches del garaje en
 * Fe_CreateGarageObjects y la vuelta de una carrera es Fe_AfterRace (80090fb4). */
void Frontend_InitScreens(void)
{
    FrontendState* fe = g_Frontend;

    InitMenu(fe->mainMenu, s_mainMenuDefs, 7, 0x46, 0x5b);
    InitMenu(fe->raceMenu1P, s_raceMenu1PDefs, 5, 0x46, 0x5e);
    InitMenu(fe->raceMenu2P, s_raceMenu2PDefs, 5, 0x46, 0x5e);
    InitMenu(fe->raceOpts1P, s_raceOpts1PDefs, 4, 0x54, 0x5e);
    InitMenu(fe->raceOpts2P, s_raceOpts2PDefs, 5, 0x54, 0x5e);
    InitMenu(fe->gameOptions, s_gameOptionsDefs, 6, 0x46, 0x5b);
    InitMenu(fe->audioMenu, s_audioDefs, 5, 0x46, 0x5e);
    InitMenu(fe->loadSaveMenu, s_loadSaveDefs, 3, 0x31, 0x5e);
    InitMenu(fe->showcaseMenu, s_showcaseDefs, 4, 0x51, 0x5b);

    fe->screen = 2000;
    fe->prevScreen = -1;
    if (g_BootState == 0)
    {
        /* primer arranque: ajustes por defecto (DAT_800b7158) */
        g_Settings.numPlayers = 1;
        g_Settings.racingClass = 1;
        g_Settings.powerUps = 1;
        g_Settings.hallType = 0;
        g_Settings.maxClassUnlocked = 1;   /* DAT_800b7134[1]: viene de la tarjeta de memoria */
        g_Settings.setNamePlayers = 0;
        g_Settings.loadSaveMode = 0;
        g_Settings.clearRecords = 0;
        static const int8_t opts1P[4] = { 6, 3, 4, 0 }, opts2P[5] = { 6, 3, 3, 4, 0 };
        memcpy(g_Settings.raceOpts1P, opts1P, sizeof(opts1P));
        memcpy(g_Settings.raceOpts2P, opts2P, sizeof(opts2P));
        /* volumenes: los del motor de sonido (8003d520, 8003d6f0, 8003d5d0, 8003d668 tras 8003cf48) */
        g_Settings.volume[0] = 0x40;
        g_Settings.volume[1] = 0x40;
        g_Settings.volume[2] = 0x40;
        g_Settings.volume[3] = 0x7f;
        g_Settings.audioMode = 1;
        Save_ResetExtra();   /* DAT_800b711c, 715f / 7160, 797d, 7981, 7995 / 7996 y 800931d8(-1) */
        /* pilotos iniciales al azar entre los 33 de la Winston Cup (FUN_800181ac % 33) */
        for (int p = 0; p < 2; p++)
        {
            g_Settings.lastDriver[p] = (uint8_t)(rand() % 33);
            g_Settings.lastCar[p] = 0;
            g_Settings.mateDriver[p] = (uint8_t)(rand() % 33);
            g_Settings.mateCar[p] = 0;
            g_Settings.transmission[p] = 0;
        }
        for (int i = 1; i <= 5; i++)
        {
            g_Settings.oppDriver[i] = (uint8_t)(rand() % 33);
            g_Settings.oppCar[i] = 0;
        }
        memset(g_Settings.progress, 0, sizeof(g_Settings.progress));
    }
    if (g_BootState != 1)
    {
        /* LAB_80079c94: primer arranque o partida recien cargada de la tarjeta */
        g_FeSaveReturn = 0;
        g_Settings.savedTrack = 0xff;
        g_DemoCount = 0;
        g_LoadHelp = 1;            /* DAT_800b7117: la ayuda de los mandos en la primera carga de carrera */
        g_BootState = 1;
        fe->nextScreen = 100;
    }
    else if (!g_Settings.demo)
    {
        /* vuelta de una carrera: nivel del marcador (DAT_800b715e + jugadores) y lo que sigue (80090fb4) */
        g_SaveExtra.hudPref[(g_Settings.numPlayers - 1) & 1] = (uint8_t)Hud_GetSetting(0);
        fe->screen = 2000;
        fe->nextScreen = Net_Active() ? 2100 : (short)Fe_AfterRace();   /* port: en red, a la sala */
    }
    else
    {
        g_Settings.demo = 0;
        g_Settings.venue = g_Settings.curVenue;
        g_Settings.track = g_Settings.curTrack;
        fe->nextScreen = 100;
    }

    /* 8007a3e4: seccion del coche y miniatura de cada piloto y clase */
    for (int c = 0; c < 3; c++)
        for (int d = 0; d < NUM_DRIVERS; d++)
            for (int k = 0; k < 3; k++)
                fe->carInfo[d][c][k] = (short)(g_Drivers[d].team * 3 + c);

    Fe_InitVenues();
    Fe_CreateGarageObjects();

    fe->firstFrame = 1;
    fe->valueDir = 0;
    fe->prevCursor = 0;
    fe->cursor = 0;
    g_FadeDir = -1;
    g_FadeLevel = 0;
    fe->slideX = 0;
    fe->descAnim = (short)0xf000;
    fe->descAnimOn = 1;
    fe->idleTimer = 0;
    Snd_ApplySettings(g_Settings.volume, g_Settings.audioMode);
}

/* 80089240: la primera vez copia lo que se esta viendo (la pantalla de carga) como fondo */
static void Fe_Screen2000(void)
{
    FrontendState* fe = g_Frontend;
    if (fe->firstFrame)
    {
        /* 8008e480: MoveImage del framebuffer mostrado a (0x140,0)/(0x140,0x100). En el port la
         * pantalla de carga ya esta en esa zona de VRAM (LoadScreen_Show), asi que no hace falta. */
    }
    Fe_DrawBackground(0x800);
}

static void Fe_ScreenPending(void)
{
    static short warned = -1;
    if (warned != g_Frontend->screen)
    {
        NR_Log("Frontend: pantalla %d sin portar\n", g_Frontend->screen);
        warned = g_Frontend->screen;
    }
    Fe_DrawBackground(0x800);
    FntPrint("\n\n\n\n\n\n\n\n\n\n\n\n\n     PANTALLA %d SIN PORTAR\n", g_Frontend->screen);
}

/* 8007a994. Port: sin Camera_ApplyProjection(fe +0xC0) al empezar (el front-end usa el preset 1). */
void Frontend_Update(void)
{
    FrontendState* fe = g_Frontend;

    if (g_FadeDir != 0)
    {
        if (g_FadeLevel < 0x100)
        {
            g_FadeLevel = (short)(g_FadeLevel + g_FrameDelta * 4);
            if (g_FadeLevel >= 0x100)
            {
                g_FadeLevel = 0x100;
                if (g_FadeDir == -1)
                    fe->fadeDone = 1;
            }
        }
        else if (fe->nextScreen == -1)
        {
            g_FadeDir = 0;
        }
        else
        {
            if (fe->nextScreen == 0)
            {
                short s = fe->prevScreen;
                fe->prevScreen = -1;
                fe->screen = s;
            }
            else
            {
                fe->prevScreen = fe->screen;
                fe->screen = fe->nextScreen;
            }
            if (getenv("NR_SCREENLOG"))   /* solo en el port (depuracion) */
                NR_Log("[pantalla] %d -> %d (frame %d)\n", fe->prevScreen, fe->screen, Gfx_GetFlipCount());
            fe->firstFrame = 1;
            fe->nextScreen = -1;
            fe->prevCursor = 0;
            fe->cursor = 0;
            g_FadeDir = 1;
            g_FadeLevel = 0;
        }
    }

    int savePrompt = Fe_SavePrompt(0);   /* DAT_800b7118 */
    switch (fe->screen)
    {
    case 100:  Fe_Screen100(); break;
    case 200:  Fe_Screen200(); break;
    case 300:  Fe_Screen300(); break;
    case 400:  Fe_Screen400(); break;
    case 500:  Fe_Screen500(); break;
    case 600:  Fe_Screen600(); break;
    case 700:  Fe_Screen700(); break;
    case 800:  Fe_Screen800(); break;
    case 900:  Fe_Screen900(); break;
    case 1000: Fe_Screen1000(); break;
    case 1100: Fe_Screen1100(); break;
    case 1200: Fe_Screen1200(); break;
    case 1300: Fe_Screen1300(); break;
    case 1400: Fe_Screen1400(); break;
    case 1500: Fe_Screen1500(); break;
    case 1600: Fe_Screen1600(); break;
    case 1700: Fe_Screen1700(); break;
    case 1800: Fe_Screen1800(); break;
    case 1900: Fe_Screen1900(); break;
    case 2000: Fe_Screen2000(); break;
    case 2100: Fe_ScreenLan(); break;   /* solo en el port */
    default: Fe_ScreenPending(); break;
    }

    if (fe->firstFrame)
        fe->firstFrame = 0;
    /* 9000 tics (30 s) sin tocar nada en el menu principal: una demostracion (Fe_StartRace(1), que sale del
     * front-end); a la cuarta seguida, la intro (pantalla 600 con fe +0xA84 = -2) */
    if (fe->idleTimer > 0x2328)
    {
        g_DemoCount = (uint8_t)Fe_Clamp(g_DemoCount + 1, 0, 4);
        if (g_DemoCount < 4)
            Fe_StartRace(1);
        else
        {
            fe->idleTimer = 0;
            g_FeUnlockShow = -2;
            fe->nextScreen = 600;
            g_FadeDir = -1;
            g_FadeLevel = 0;
            fe->descAnim = (short)0xf000;
            fe->descAnimOn = 1;
        }
    }
    if (fe->fadeDone)
    {
        fe->fadeDone = 0;
        fe->idleTimer = 0;
    }
    if (savePrompt)
        Fe_SavePrompt(1);
    else if (g_FadeDir == 0 && fe->nextScreen == -1)
    {
        /* sonidos de navegacion: mover el cursor 0x46, cambiar un ajuste 0x47 */
        if (fe->cursor != fe->prevCursor)
        {
            fe->prevCursor = fe->cursor;
            Snd_PlayUi(0x46, NULL);
        }
        else if (fe->valueDir)
        {
            fe->valueDir = 0;
            Snd_PlayUi(0x47, NULL);
        }
    }
    if (!savePrompt && fe->nextScreen != -1 && g_FadeDir == 0)
    {
        /* LAB_8007afc4: empieza el fundido a negro hacia la siguiente pantalla (sonido 0x48; no al volver de la
         * pantalla 700 con +0xB6 puesto) */
        if (fe->screen != 700 || fe->nextScreen != 0 || fe->popup == 0)
            Snd_PlayUi(0x48, NULL);
        g_FadeDir = -1;
        g_FadeLevel = 0;
        fe->descAnim = (short)0xf000;
        fe->descAnimOn = 1;
    }

    /* LAB_8007b024: animaciones de entrada */
    if (fe->slideX > 0)
    {
        fe->slideX = (short)(fe->slideX - g_FrameDelta * 3);
        if (fe->slideX < 0)
            fe->slideX = 0;
    }
    if (fe->descAnim < 0x400)
    {
        fe->descAnim = (short)(fe->descAnim + g_FrameDelta * 0x14);
        if (fe->descAnim > 0x400)
            fe->descAnim = 0x400;
    }
    /* mando desconectado o no admitido (0xFA2 / 0xFA3 + numero): el del 1 en todas las pantallas menos la 1000 (que
     * tiene los suyos) y la 2000; el del 2, con dos jugadores, fuera de las pantallas de menus y records, y mientras
     * falte no se leen los mandos (fe +0xDD). En el podio (1700) los jugadores son los de la copa (fe +0xB6). */
    fe->padDisabled = 0;
    if (fe->screen != 1000)
    {
        int players = fe->screen == 1700 ? fe->popup : g_Settings.numPlayers;
        char msg[64];
        if (players > 0 && Pad_Kind(0) < 1 && fe->screen != 2000)
        {
            snprintf(msg, sizeof(msg), "%s1", Fe_GetString(Pad_Kind(0) == -1 ? 0xfa3 : 0xfa2));
            Fe_DrawTextBox(1, msg, 0xa0, 0x51);
        }
        static const short s_noPad2Warn[] = { 100, 400, 500, 600, 700, 800, 900, 1600, 1900, 2000 };
        int warn2 = players == 2 && Pad_Kind(1) < 1;
        for (size_t i = 0; warn2 && i < sizeof(s_noPad2Warn) / sizeof(s_noPad2Warn[0]); i++)
            if (fe->screen == s_noPad2Warn[i])
                warn2 = 0;
        if (warn2)
        {
            snprintf(msg, sizeof(msg), "%s2", Fe_GetString(Pad_Kind(1) == -1 ? 0xfa3 : 0xfa2));
            Fe_DrawTextBox(1, msg, 0xa0, 0x9a);
            fe->padDisabled = 1;
        }
    }
    if (fe->screen == 100)   /* el tiempo sin tocar nada solo cuenta en el menu principal */
        fe->idleTimer += g_FrameDelta;
    g_MenuFrame++;
}

/* Fundido de Frontend_MainLoop. El original redibuja el framebuffer como textura modulada por
 * (nivel / 2); aqui se aproxima con un TILE de mezcla sustractiva (B - F). */
void Frontend_DrawFade(void)
{
    if (g_FadeDir == 0)
        return;
    int level = g_FadeDir < 0 ? 0x100 - g_FadeLevel : g_FadeLevel;
    int bright = level / 2;                     /* 0x80 = imagen normal, 0 = negro */
    int sub = (0x80 - bright) * 2;
    if (sub <= 0)
        return;
    if (sub > 255)
        sub = 255;

    DR_TPAGE* tp = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
    TILE* t = (TILE*)Gfx_AllocPrim(sizeof(TILE));
    if (!t || !tp)
        return;
    setDrawTPage(tp, 1, 0, getTPage(0, 2, 0, 0));   /* abr 2 = resta */
    setTile(t);
    setSemiTrans(t, 1);
    setRGB0(t, sub, sub, sub);
    setXY0(t, 0, 0);
    setWH(t, GFX_SCREEN_W, GFX_SCREEN_H);
    Gfx_AddPrim(0, tp);   /* mismo indice: se dibujan en orden de creacion */
    Gfx_AddPrim(0, t);
}

/* 80090984 */
const char* Fe_GetString(int id)
{
    const FrontendState* fe = g_Frontend;
    if (!fe || !fe->text)
        return "";
    int i = 0;
    while (i < fe->textSize)
    {
        int n = 0;
        while (i < fe->textSize && fe->text[i] != ' ')
            n = n * 10 + (fe->text[i++] - '0');
        if (n == id)
            return fe->text + i + 1;
        while (i < fe->textSize && fe->text[i] != 0)
            i++;
        i++;
    }
    return "";
}

/* 8008c86c: rectangulo de VRAM de 16 bits como POLY_FT4 (tpage en modo 15 bits) */
void Fe_DrawVramRect(int vramX, int vramY, int w, int h, int x, int y, int depth, int semi)
{
    if (w <= 0 || h <= 0)
        return;
    POLY_FT4* p = (POLY_FT4*)Gfx_AllocPrim(sizeof(POLY_FT4));
    if (!p)
        return;
    setPolyFT4(p);
    setSemiTrans(p, semi);
    setRGB0(p, 0x80, 0x80, 0x80);
    int u = vramX & 0x3f, v = vramY & 0xff;
    setXY4(p, x, y, x + w, y, x, y + h, x + w, y + h);
    setUV4(p, u, v, u + w, v, u, v + h, u + w, v + h);
    p->tpage = getTPage(2, 0, vramX & ~0x3f, vramY & ~0xff);
    p->clut = 0;
    Gfx_AddPrimDepth(depth, p);
}

/* 8008b79c: las dos mitades de 160x240 de (0x140,0) y (0x140,0x100) */
void Fe_DrawBackground(int depth)
{
    Fe_DrawVramRect(0x140, 0, 0xa0, 0xf0, 0, 0, depth, 0);
    Fe_DrawVramRect(0x140, 0x100, 0xa0, 0xf0, 0xa0, 0, depth, 0);
}

/* 8008dfd0: decodifica el fondo Bmdc 'index' a VRAM si no es el que ya esta cargado */
int Fe_SetBackground(int index)
{
    FrontendState* fe = g_Frontend;
    if (index < 0 || index >= 13 || !fe->bmdc[index])
        return 0;
    if (fe->bgIndex != index)
    {
        const uint8_t* d = fe->bmdc[index];
        int size = s_bmdcSize[index];
        int second = (int)NR_Read32(d);
        DrawSync(0);
        /* el bitstream de la primera mitad acaba unos bytes despues de 'second' (el MDEC lee palabras
         * completas), asi que se acota con el final del asset y no con el inicio de la segunda imagen */
        Lsc_DecodeToVram(d + 8, size - 8, 0x140, 0);
        Lsc_DecodeToVram(d + second + 8, size - second - 8, 0x140, 0x100);
        fe->bgIndex = (uint8_t)index;
    }
    return 1;
}

/* 8008e21c: decodifica la imagen Gmdc 'index' (una sola imagen MDEC) en (x, y) de VRAM */
int Fe_DecodeGmdc(int index, int x, int y)
{
    FrontendState* fe = g_Frontend;
    if (index < 0 || index >= 14 || !fe->gmdc[index])
        return 0;
    DrawSync(0);
    Lsc_DecodeToVram(fe->gmdc[index], s_gmdcSize[index], x, y);
    return 1;
}

/* 8008e13c (parte del MDEC): miniatura de sede/pista 64x32 en (x, y) de VRAM */
int Fe_DecodeLmdc(int index, int x, int y)
{
    FrontendState* fe = g_Frontend;
    if (index < 0 || index >= 36 || !fe->lmdc[index])
        return 0;
    DrawSync(0);
    Lsc_DecodeToVram(fe->lmdc[index], s_lmdcSize[index], x, y);
    return 1;
}

/* 80092e08: Crle id 0 = imagen cruda para VRAM (0x240,0) (+ resto en (0x2C0,0));
 * otros ids = tabla que se queda en memoria */
static const uint8_t* s_padAtlas;   /* fe +0x9D8 */

/* Sube a VRAM w x h pixeles leyendo como mucho avail bytes de src (el resto, a cero). Las filas que calcula el
 * original redondean hacia arriba y la parte de (0x2C0,0) pide el doble de lo que queda del asset: en la PS1 se lee
 * basura de la RAM contigua sin mas, pero aqui pasarse del bloque puede caer en una pagina sin mapear */
static void Fe_LoadImageClamped(int x, int y, int w, int h, const uint8_t* src, int avail)
{
    int need = w * h * 2;
    if (avail < 0)
        avail = 0;
    uint8_t* buf = (uint8_t*)malloc(need);
    if (!buf)
        return;
    int n = avail < need ? avail : need;
    memcpy(buf, src, n);
    memset(buf + n, 0, need - n);
    RECT16 r;
    setRECT(&r, x, y, w, h);
    LoadImage(&r, (u_long*)buf);
    DrawSync(0);
    free(buf);
}

int Fe_OnCrle(NrAsset* a)
{
    if (a->id == 0)
    {
        int rows = (a->size + 0xff) >> 8;
        int rows2 = 0;
        if (rows > 0x200)
        {
            rows2 = (a->size - 0x1ffc1) >> 6;
            rows = 0x200;
        }
        Fe_LoadImageClamped(0x240, 0, 0x80, rows, a->data, (int)a->size);
        if (rows2 > 0)
            Fe_LoadImageClamped(0x2c0, 0, 0x40, rows2, a->data + 0x20000, (int)a->size - 0x20000);
        s_crle = a->data;
        if (g_Frontend)
        {
            g_Frontend->atlas = NULL;
            g_Frontend->crleRows = (short)rows;
            g_Frontend->crleRows2 = (short)rows2;
        }
        return 1;   /* el original hace Pool_Free del asset y luego lo relee de VRAM */
    }
    /* otro id: atlas pequeño en memoria (los dibujos de los mandos de la pantalla 1000) en fe +0x9D8 */
    s_padAtlas = a->data;
    return 1;
}

const uint8_t* Fe_PadAtlas(void)
{
    return s_padAtlas;
}

/* 80092f5c: el original copia con StoreImage las filas subidas del Crle 0 y apunta las tablas de la
 * cabecera; aqui basta con el asset que se guardo en RAM */
void Fe_LoadAtlas(void)
{
    g_Frontend->atlas = s_crle;
}

/* 80093050 */
void Fe_FreeAtlas(void)
{
    g_Frontend->atlas = NULL;
}

/* 80092dec */
int Fe_OnTxtR(NrAsset* a)
{
    if (!g_Frontend)
        return 0;
    g_Frontend->text = (const char*)a->data;
    g_Frontend->textSize = (unsigned short)a->size;
    return 1;
}

/* Bmdc 80092d2c, Lmdc 80092d58, Tmdc 80092d84, Gmdc 80092db0: se guardan para decodificar despues */
int Fe_OnMdc(NrAsset* a)
{
    FrontendState* fe = g_Frontend;
    if (!fe)
        return 0;
    char t = (char)(a->type >> 24);
    if (t == 'B' && fe->numBmdc < 13)
    {
        s_bmdcSize[fe->numBmdc] = a->size;
        fe->bmdc[fe->numBmdc++] = a->data;
    }
    else if ((t == 'L' || t == 'T') && fe->numLmdc < 36)
    {
        s_lmdcSize[fe->numLmdc] = a->size;
        fe->lmdc[fe->numLmdc++] = a->data;   /* en orden de llegada: sede*4 + k */
    }
    else if (t == 'G' && fe->numGmdc < 14)
    {
        s_gmdcSize[fe->numGmdc] = a->size;
        fe->gmdc[fe->numGmdc++] = a->data;
    }
    else
        return 0;
    return 1;
}
