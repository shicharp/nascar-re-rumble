/* Front-end (menus). Maquina de estados de pantallas (100 menu principal, 200, 300... 2000 pantalla
 * inicial) en Frontend_Update 8007a994; estado global de 0x101C bytes en DAT_800af950. */
#ifndef NR_FRONTEND_H
#define NR_FRONTEND_H

#include <stdint.h>
#include "stream.h"

/* Definicion de opcion en el EXE (10 bytes, tablas 800ab7b4...) */
typedef struct
{
    uint16_t text;       /* id de texto; los valores son text+1... */
    int16_t  onSelect;   /* pantalla con X */
    int16_t  onBack;     /* pantalla con triangulo */
    uint8_t  numValues;
    uint8_t  help;       /* indice en los textos de ayuda (3000 + help) */
    uint8_t  type;
    uint8_t  width;
} MenuDef;

/* Valor/descripcion de una opcion (8 bytes, FUN_8007a780) */
typedef struct
{
    const char* text;
    short       w;      /* ancho de la caja de descripcion */
    short       h;      /* alto (texto envuelto + 6) */
} MenuValue;

/* Opcion de menu (0x20 bytes, FUN_8007a684) */
typedef struct
{
    const char* text;       /* +0x00 */
    short       x, y;       /* +0x04 */
    const char* help;       /* +0x08 texto de ayuda de la barra inferior */
    MenuValue*  values;     /* +0x0C */
    int         unk10;
    short       onSelect;   /* +0x14 pantalla con X (-1 = ninguna) */
    short       onBack;     /* +0x16 pantalla con triangulo */
    char        type;       /* +0x18 0 normal, 1 con caja que se despliega, 2 ajuste */
    uint8_t     width;      /* +0x19 ancho de la caja de valor */
    short       anim;       /* +0x1A 0..10000 */
    char        active;     /* +0x1C */
    char        pad1d[3];
} MenuItem;

typedef struct
{
    short    screen;        /* +0x000 pantalla actual */
    short    prevScreen;    /* +0x002 */
    short    nextScreen;    /* +0x004 -1 = ninguna, 0 = volver a la anterior */
    char     firstFrame;    /* +0x006 primer frame en la pantalla */
    char     fadeDone;      /* +0x007 fundido a negro terminado */
    uint8_t  cursor;        /* +0x008 opcion seleccionada */
    uint8_t  prevCursor;    /* +0x009 */
    short    slideX;        /* +0x00A desplazamiento de entrada de las opciones */
    char     valueDir;      /* +0x00C -1/+1 al cambiar un ajuste */
    uint8_t  bgIndex;       /* +0x00D fondo Bmdc cargado en VRAM (0xFF = ninguno) */
    char     pad0e[2];
    int      idleTimer;     /* +0x010 */
    char     textBuf[0xa0]; /* +0x014 copia del texto de ayuda */
    char     marquee;       /* +0x0B4 */
    char     pad0b5;
    short    popup;         /* +0x0B6 mensaje emergente (1/2) */
    short    descAnim;      /* +0x0B8 zoom de la caja de descripcion (-0x1000..0x400) */
    char     descAnimOn;    /* +0x0BA */
    char     pad0bb[0xc8 - 0xbb];
    uint32_t pad[2];        /* +0x0C8 botones de cada mando este frame */
    int      repeatTime[2]; /* +0x0D0 ultimo disparo (autorrepeticion) */
    char     held[2];       /* +0x0D8 */
    char     latch[2];      /* +0x0DA X/triangulo/cuadrado solo al pulsar */
    char     lastPort;      /* +0x0DC */
    char     padDisabled;   /* +0x0DD */
    uint8_t  numBmdc;       /* +0x0DE */
    uint8_t  numLmdc;       /* +0x0DF */
    uint8_t  numGmdc;       /* +0x0E0 */
    char     pad0e1[3];
    const uint8_t* bmdc[13];/* +0x0E4 fondos de menu (MDEC, como los .LSC) */
    const uint8_t* lmdc[36];/* +0x118 Lmdc/Tmdc */
    const uint8_t* gmdc[14];/* +0x1A8 */
    const char* text;       /* +0x1E0 tabla de textos TxtR ("<id> <texto>\0"...) */
    int      unk1e4;
    unsigned short textSize;/* +0x1E8 */
    short    crleRows;      /* +0x1EA filas subidas del Crle 0 */
    short    crleRows2;     /* +0x1EC */
    char     pad1ee[0x948 - 0x1ee];
    int      volumeSound;   /* +0x948 sonido de prueba de la pantalla de audio */
    char     portPopup;     /* +0x94C aviso de mandos (1: puerto 1, 2: ambos) */
    char     pad94d[0xa9c - 0x94d];
    MenuItem mainMenu[7];   /* +0xA9C pantalla 100 (tabla 800ab7b4) */
    MenuItem raceMenu1P[5]; /* +0xB7C pantalla 200, 1 jugador (800ab7fc) */
    MenuItem raceMenu2P[5]; /* +0xC1C pantalla 200, 2 jugadores (800ab830) */
    MenuItem raceOpts1P[4]; /* +0xCBC pantalla 300, 1 jugador (800ab864) */
    MenuItem raceOpts2P[5]; /* +0xD3C pantalla 300, 2 jugadores (800ab88c) */
    MenuItem gameOptions[6];/* +0xDDC pantalla 400 (800ab8c0) */
    MenuItem audioMenu[5];  /* +0xE9C pantalla 500 (800ab8fc) */
    MenuItem loadSaveMenu[3]; /* +0xF3C pantalla 700 (800ab930) */
    MenuItem showcaseMenu[4]; /* +0xF9C pantalla 600 (800ab950) */
} FrontendState;

/* Ajustes de partida y del front-end (bloque DAT_800b7114..800b7181 y DAT_800b7978..7c) */
typedef struct
{
    uint8_t numPlayers;        /* DAT_800b7114 */
    uint8_t gameMode;          /* DAT_800b7115: modo de carrera elegido en la pantalla 200 */
    uint8_t racingClass;       /* DAT_800b7158 byte 1 */
    uint8_t powerUps;          /* byte 2 */
    uint8_t hallType;          /* byte 3 */
    uint8_t maxClassUnlocked;  /* DAT_800b7134 byte 1 */
    uint8_t setNamePlayers;    /* DAT_800b715c */
    uint8_t loadSaveMode;      /* DAT_800b715d: 0 tarjeta, 1 password */
    uint8_t clearRecords;      /* DAT_800b715e: 0 pista, 1 copas */
    uint8_t singleRace[3];     /* DAT_800b7161 + jugadores */
    uint8_t championship[3];   /* DAT_800b7163 + jugadores */
    uint8_t raceMenuCursor;    /* DAT_800b7166 */
    int8_t  raceOpts1P[4];     /* DAT_800b7179: vehiculos, equipos, vueltas, rivales */
    int8_t  raceOpts2P[5];     /* DAT_800b717d: vehiculos, equipos, equipos cyber, vueltas, rivales */
    uint8_t volume[4];         /* DAT_800b7978: efectos, motor, musica, voces (0..127) */
    uint8_t audioMode;         /* DAT_800b797c: mono, estereo, dolby */
    uint8_t yesNo;             /* DAT_800b8d1a: eleccion del cuadro Si/No */
} GameSettings;

extern FrontendState* g_Frontend;   /* DAT_800af950 */
extern short g_FadeDir;             /* DAT_800af976: -1 a negro, 1 desde negro, 0 nada */
extern short g_FadeLevel;           /* DAT_800af974: 0..0x100 */
extern unsigned g_MenuFrame;        /* DAT_800af38c */
extern GameSettings g_Settings;

void Frontend_InitCamera(void);      /* 80079684: reserva el estado del front-end */
void Frontend_InitScreens(void);     /* 80079814 */
void Frontend_Update(void);          /* 8007a994 */
void Frontend_DrawFade(void);        /* parte de Frontend_MainLoop 8002865c */
void Frontend_Free(void);

const char* Fe_GetString(int id);    /* 80090984 */
void Fe_DrawVramRect(int vramX, int vramY, int w, int h, int x, int y, int depth, int semi); /* 8008c86c */
void Fe_DrawBackground(int depth);   /* 8008b79c */
int  Fe_SetBackground(int index);    /* 8008dfd0 */

/* Widgets y menus (fe_menu.c) */
void Fe_InitMenuItem(MenuItem* item, const MenuDef* def, int x, int y);   /* 8007a684 */
void Fe_ResetMenu(MenuItem* items, int count);                            /* 8008abf8 */
void Fe_Screen100(void);                                                  /* 8007b2ac */
void Fe_Screen200(void);                                                  /* 8007b7fc */
void Fe_Screen300(void);                                                  /* 8007f4a0 */
void Fe_Screen400(void);                                                  /* 8007bf00 */
void Fe_Screen500(void);                                                  /* 8007c540 */
int  Fe_DecodeGmdc(int index, int x, int y);                              /* 8008e21c */

/* Manejadores de assets del front-end */
int  Fe_OnCrle(NrAsset* a);          /* 80092e08 */
int  Fe_OnTxtR(NrAsset* a);          /* 80092dec */
int  Fe_OnMdc(NrAsset* a);           /* Bmdc 80092d2c, Lmdc 80092d58, Tmdc 80092d84, Gmdc 80092db0 */

#endif
