/* Front-end (menus). Maquina de estados de pantallas (100 menu principal, 200, 300... 2000 pantalla
 * inicial) en Frontend_Update 8007a994; estado global de 0x101C bytes en DAT_800af950. */
#ifndef NR_FRONTEND_H
#define NR_FRONTEND_H

#include "stream.h"

typedef struct
{
    short   screen;        /* +0x00 pantalla actual */
    short   prevScreen;    /* +0x02 */
    short   nextScreen;    /* +0x04 -1 = ninguna, 0 = volver a la anterior */
    char    firstFrame;    /* +0x06 primer frame en la pantalla */
    char    fadeDone;      /* +0x07 fundido a negro terminado */
    char    unk08;
    char    unk09;
    short   unk0a;
    char    captureBg;     /* +0x0C copiar el frame mostrado como fondo (pantalla 2000) */
    char    unk0d[3];
    int     idleTimer;     /* +0x10 */
    char    unk14[0x1e0 - 0x14];
    const char* text;      /* +0x1E0 tabla de textos TxtR ("<id> <texto>\0"...) */
    int     unk1e4;
    unsigned short textSize; /* +0x1E8 */
    short   crleRows;      /* +0x1EA filas subidas del Crle 0 */
    short   crleRows2;     /* +0x1EC */
    char    unk1ee[0x101c - 0x1ee];
} FrontendState;

extern FrontendState* g_Frontend;   /* DAT_800af950 */
extern short g_FadeDir;             /* DAT_800af976: -1 a negro, 1 desde negro, 0 nada */
extern short g_FadeLevel;           /* DAT_800af974: 0..0x100 */

void Frontend_InitCamera(void);      /* 80079684: reserva el estado del front-end */
void Frontend_InitScreens(void);     /* 80079814 */
void Frontend_Update(void);          /* 8007a994 */
void Frontend_DrawFade(void);        /* parte de Frontend_MainLoop 8002865c */
void Frontend_Free(void);

const char* Fe_GetString(int id);    /* 80090984 */
void Fe_DrawVramRect(int vramX, int vramY, int w, int h, int x, int y, int depth, int semi); /* 8008c86c */
void Fe_DrawBackground(int depth);   /* 8008b79c */

/* Manejadores de assets del front-end */
int  Fe_OnCrle(NrAsset* a);          /* 80092e08 */
int  Fe_OnTxtR(NrAsset* a);          /* 80092dec */

#endif
