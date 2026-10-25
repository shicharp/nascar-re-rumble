/* Variables globales del juego (nombre del port <- direccion en SLUS_010.68). */
#ifndef NR_GLOBALS_H
#define NR_GLOBALS_H

#include "nr.h"

/* Estado de nivel (DAT_800af6e4): 1 cargando, 2 Cctr id 0, 4 en juego / menu (Cctr id 1),
 * 0xD pausa?, 5/6/0xB/0xC/0xE salidas que interpreta Level_Unload. */
enum
{
    GAMESTATE_LOADING = 1,
    GAMESTATE_LOADED_PART = 2,
    GAMESTATE_RUNNING = 4,
    GAMESTATE_RESULTS = 0xd,    /* resultados al terminar la carrera */
};

/* Struct global de 0xAC bytes reservado en Game_Init (DAT_800af744) */
typedef struct
{
    int unk00;
    int unk04;
    int racingClass;    /* +0x08: clase de la carrera (0 Kid Play, 1 Rookie, 2 Pro, 3 Elite; 8002cf7c) */
    int numPlayers;     /* +0x0C */
    int nextMode;       /* +0x10: modo siguiente (0 carrera, 3 arranque, 4 cargando...) */
    int unk14[14];
    int unlockMask;     /* +0x4C: especiales desbloqueados (bits de DAT_800abeec) */
    int unk50[2];
    int powerUps;       /* +0x58 */
    int unk5c;
    int isReplay;       /* +0x60: 1 durante la repeticion (Level_Run 80029be4) */
    int unk64[13];
    int unk98;          /* main: 1 */
    int unk9c[4];
} GameInfo;

extern int       g_GameState;       /* DAT_800af6e4 */
extern int       g_IsFrontend;      /* DAT_800af6c8 */
extern GameInfo* g_GameInfo;        /* DAT_800af744 */
extern int       g_FrameDelta;      /* DAT_800af6e0: ticks (1/300 s) del ultimo frame, maximo 30 */
extern int       g_FrameCount;      /* DAT_800af728 */
extern int       g_FrontendFrames;  /* DAT_800af17c */
extern char      g_LevelPath[128];  /* DAT_800b0d70 */
extern char      g_SelectedTrack[16]; /* DAT_800ab7a0: pista elegida en el front-end */
extern int       g_RaceExit;        /* DAT_800af940: g_GameInfo->nextMode al acabar la carrera (0 terminada) */
extern int       g_BootState;       /* DAT_800b7068: 0 primer arranque, 1 ya inicializado, 2 tras memcard */
extern short     g_Venue;           /* DAT_800af386: sede de la carrera (directorio CW\\Loc<codigo>) */
extern char      g_TrackName[32];   /* DAT_800b711d */
extern char      g_SplitScreen;     /* DAT_800af708 */
extern uint8_t   g_PlayerTeam[2];   /* DAT_800af972/973 */

/* Tiempo. El callback de VSync del original (~8001dcd4) suma 5 por vblank (unidades de 1/300 s):
 * DAT_800af714 siempre y DAT_800af738 solo en los estados 4 y 13 (tiempo de juego, se para en pausa).
 * Frame_Wait copia DAT_800af738 en DAT_800af748. */
extern int g_Ticks;       /* DAT_800af714 */
extern int g_GameTime;    /* DAT_800af738 */
extern int g_FrameTime;   /* DAT_800af748 */
void Timer_Update(void);  /* hace el trabajo del callback de VSync a partir del reloj de PsyCross */
int  Timer_GetVBlank(void);

#endif
