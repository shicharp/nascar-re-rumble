/* Flujo principal del juego: main 800792a8 y la cadena de carga/ejecucion de niveles. */
#ifndef NR_GAME_H
#define NR_GAME_H

typedef struct
{
    int skipLegal;      /* no esperar los 1500 vblanks de la pantalla legal (ni poner los videos de arranque) */
    const char* movie;  /* --movie: poner ese video y salir */
} GameOptions;

extern GameOptions g_Options;

int  Game_Main(void);                 /* 800792a8 */
int  Game_Init(void);                 /* 80079470 */
void Game_Shutdown(void);             /* 800795b4 */
void Legal_Show(int skippable);       /* 80090d98 */
void Movie_Play(const char* name, int a, int b, int y, int w, int h);   /* 800991e4 (movie.c) */

int  Level_Run(const char* name, int isFrontend);   /* 80029be4 */
int  Level_Load(const char* name);                  /* 80024684 */
const char* Level_GetDataDir(void);                 /* 8009233c */
void Frontend_Load(const char* path);               /* 80024080 */
int  Level_WaitLoaded(void);                        /* 800252b0 */
void Frontend_MainLoop(void);                       /* 8002865c */
void Level_Unload(const char* name);                /* 80025448 */
void Frame_Wait(void);                              /* 800285e4 */
void Frame_WaitUntil(int ticks);                    /* 800269c0 (repeticion): el frame dura 'ticks' */

#endif
