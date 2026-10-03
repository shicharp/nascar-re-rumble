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
};

/* Struct global de 0xAC bytes reservado en Game_Init (DAT_800af744) */
typedef struct
{
    int unk00;
    int unk04;
    int unk08;          /* main: 1 */
    int unk0c;
    int nextMode;       /* +0x10: modo siguiente (0 front-end, 3 arranque, 4 cargando...) */
    int unk14[17];
    int unk58;
    int unk5c;
    int isRestart;      /* +0x60: Level_Run lo pone a 1 al reiniciar */
    int unk64[13];
    int unk98;          /* main: 1 */
    int unk9c[4];
} GameInfo;

extern int       g_GameState;       /* DAT_800af6e4 */
extern int       g_IsFrontend;      /* DAT_800af6c8 */
extern GameInfo* g_GameInfo;        /* DAT_800af744 */
extern int       g_FrameDelta;      /* DAT_800af6e0: vblanks del ultimo frame (max 30) */
extern int       g_FrameCount;      /* DAT_800af728 */
extern int       g_FrontendFrames;  /* DAT_800af17c */
extern char      g_LevelPath[128];  /* DAT_800b0d70 */
extern char      g_SelectedTrack[16]; /* DAT_800ab7a0: pista elegida en el front-end */
extern int       g_BootState;       /* DAT_800b7068: 0 primer arranque, 1 ya inicializado, 2 tras memcard */

/* Tiempo. El callback de VSync del original (~8001dcd4) suma 5 por vblank (unidades de 1/300 s):
 * DAT_800af714 siempre y DAT_800af738 solo en los estados 4 y 13 (tiempo de juego, se para en pausa).
 * Frame_Wait copia DAT_800af738 en DAT_800af748. */
extern int g_Ticks;       /* DAT_800af714 */
extern int g_GameTime;    /* DAT_800af738 */
extern int g_FrameTime;   /* DAT_800af748 */
void Timer_Update(void);  /* hace el trabajo del callback de VSync a partir del reloj de PsyCross */
int  Timer_GetVBlank(void);

#endif
