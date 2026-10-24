/* Front-end (menus). Maquina de estados de pantallas (100 menu principal, 200, 300... 2000 pantalla
 * inicial) en Frontend_Update 8007a994; estado global de 0x101C bytes en DAT_800af950. */
#ifndef NR_FRONTEND_H
#define NR_FRONTEND_H

#include <stdint.h>
#include "libgte.h"
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

/* Coche del garaje cargado (0x14 bytes en +0x5EC) */
typedef struct
{
    uint8_t  users[2];      /* +0x00 lo muestra el panel 0 / 1 */
    char     inUse;         /* +0x02 */
    char     receiving;     /* +0x03 es el hueco al que va la seccion que esta llegando */
    char     state;         /* +0x04 0 libre o cargado, 1 pedido, 2 cargando */
    uint8_t  driver;        /* +0x05 */
    uint8_t  car;           /* +0x06 */
    char     pad07;
    unsigned lastUsed;      /* +0x08 g_MenuFrame del ultimo uso */
    void*    data;          /* +0x0C NrAsset del Cobj cargado (id 10000 + (equipo+1)*100 + clase*10 + 1) */
    void*    model;         /* +0x10 GfxObj */
} CarSlot;

/* Miniatura del carrusel de la pantalla 1200 */
typedef struct
{
    uint8_t  car;
    uint8_t  driver;
    int8_t   slot;          /* hueco en VRAM (0x200, slot*20) y CLUT (0x200, 0xF0+slot) */
} CarouselEntry;

/* Tabla de pilotos del EXE (800aa5ec, 12 bytes) */
typedef struct
{
    const char* name;
    uint16_t number;
    uint8_t  team;          /* coche: miniaturas y seccion = team*3 + clase */
    uint8_t  stat7;
    uint32_t stat8;
} DriverInfo;

/* Sede o pista de la pantalla 1300 (16 bytes en +0x6CC) */
typedef struct
{
    const char* name;       /* textos 5000 + sede*10 + k */
    const char* file;       /* 800abdd4: codigo de sede / fichero .trk */
    short    venue;
    short    unk6;
    short    tkfe;          /* id del asset TkFE con el trazado */
    short    image;         /* miniatura Lmdc/Tmdc (sede*4 + k) */
} VenueEntry;

typedef struct
{
    uint8_t venue;
    uint8_t k;              /* 0 la sede, 1..3 pista */
    int8_t  slot;           /* hueco en VRAM (0x200, slot*32) */
} VenueCarousel;

#define NUM_DRIVERS 56
extern const DriverInfo g_Drivers[NUM_DRIVERS];

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
    char     pad0bb;
    int      unk0bc;
    int      unk0c0;
    int      modelAngle;    /* +0x0C4 giro del coche del garaje (suma 2 * delta por frame) */
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
    const uint8_t* atlas;   /* +0x1E4 atlas de miniaturas (Crle 0 leido de VRAM, FUN_80092f5c) */
    unsigned short textSize;/* +0x1E8 */
    short    crleRows;      /* +0x1EA filas subidas del Crle 0 */
    short    crleRows2;     /* +0x1EC */
    char     pad1ee[2];
    void*    lockModel[2];  /* +0x1F0 modelos Cobj 3 y 4 (candado de coche bloqueado) */
    char     selectMode;    /* +0x1F8 pantalla 1200: 0 jugadores, 1 eligiendo rivales */
    uint8_t  numPanels;     /* +0x1F9 paneles de la pantalla 1200 (1 o 2) */
    short    carInfo[56][3][3]; /* +0x1FA por piloto y clase: equipo*3+clase (seccion, miniatura...) */
    char     pad5ea[2];
    CarSlot  carSlots[3];   /* +0x5EC coches del garaje cargados por streaming */
    char     pad628[2];
    char     pendingLoad;   /* +0x62A peticion sin hueco libre */
    char     pad62b;
    char     pendingPlayer; /* +0x62C */
    uint8_t  pendingDriver; /* +0x62D */
    uint8_t  pendingCar;    /* +0x62E */
    char     pad62f[0x640 - 0x62f];
    void*    carModel[2];   /* +0x640 modelo que muestra cada panel */
    void*    garageObjA[4]; /* +0x648 objetos Cobj 0, 1 y 2 del garaje */
    void*    garageObjB[4]; /* +0x658 */
    void*    garageObjC[4]; /* +0x668 */
    uint8_t  driver[2];     /* +0x678 piloto elegido (0..55) */
    uint8_t  car[2];        /* +0x67A clase del coche (0..2) */
    char     modelAnim[2];  /* +0x67C 1 entra, -1 sale, -2 sale y vuelve a entrar */
    char     pad67e[2];
    int      modelOffset[2];/* +0x680 0 = en su sitio, 15000 = fuera de pantalla */
    CarouselEntry carousel[2][4]; /* +0x688 miniaturas del carrusel (la 1 es la elegida) */
    char     scrollDir[2];  /* +0x6A0 */
    short    scrollX[2];    /* +0x6A2 */
    char     scrollAgain[2];/* +0x6A6 */
    char     stage[2];      /* +0x6A8 0 piloto, -1 compañero, 1..5 rival N, 100 listo */
    char     pad6aa[2];
    int      thumbClut[8];  /* +0x6AC CLUT de cada miniatura */
    VenueEntry venues[9][4];/* +0x6CC por sede: [0] la sede, [1..3] sus pistas */
    SVECTOR  previewLight;  /* +0x90C luz del trazado 3D */
    void*    preview;       /* +0x914 trazado 3D (TrackPreview) */
    char     previewAnim;   /* +0x918 1 entra, -1 sale, -2 sale y vuelve */
    char     pad919;
    short    previewOffset; /* +0x91A 0 en su sitio, 15000 fuera */
    short    previewDX;     /* +0x91C */
    short    previewDY;     /* +0x91E */
    int      previewZoom;   /* +0x920 */
    VenueCarousel vcar[10]; /* +0x924 [0..3] carrusel de sedes, [4..9] lista de pistas */
    char     vScrollDir;    /* +0x942 */
    char     pad943;
    short    vScrollX;      /* +0x944 */
    char     vScrollAgain;  /* +0x946 */
    char     pad947;
    int      volumeSound;   /* +0x948 sonido de prueba de la pantalla de audio */
    char     portPopup;     /* +0x94C aviso de mandos; en la pantalla 1300, empezar la carrera; en la 1100,
                               cuantos jugadores escriben su nombre */
    char     champReady[2]; /* +0x94D pantalla 1100: texto (2000 + n) de cada jugador */
    uint8_t  namePlayer[2]; /* +0x94F pantalla 1100: jugador de cada recuadro */
    char     pad951[0x9f2 - 0x951];
    char     teamMode;      /* +0x9F2 cada jugador elige tambien compañero de equipo */
    char     pickOpponents; /* +0x9F3 el jugador elige los rivales */
    uint8_t  numOpponents;  /* +0x9F4 */
    char     pad9f5[0xa48 - 0x9f5];
    void*    trophies[12];  /* +0xA48 modelos Cobj 5-16 (copas) */
    char     pada78[0xa9c - 0xa78];
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
    uint8_t transmission[2];   /* DAT_800b797f: <3 automatico, >=3 manual (cuadrado en la pantalla 1200) */
    uint8_t lastDriver[2];     /* DAT_800b7182: ultima eleccion de cada jugador */
    uint8_t lastCar[2];        /* DAT_800b7184 */
    uint8_t mateDriver[2];     /* DAT_800b7186: compañero de equipo */
    uint8_t mateCar[2];        /* DAT_800b7188 */
    uint8_t oppDriver[6];      /* DAT_800b7189[1..5]: rivales elegidos */
    uint8_t oppCar[6];         /* DAT_800b718e[1..5] */
    uint8_t progress[0x38];    /* DAT_800b7121..7158: desbloqueos y copas ganadas (ver PROGRESS) */
    /* pantalla 1300 y carrera */
    uint8_t venue;             /* DAT_800b7194: sede elegida (0-6 pistas, 7 WC, 8 EA) */
    uint8_t track;             /* DAT_800b7195: pista de la sede (0-2) */
    uint8_t savedVenue;        /* DAT_800b7196: sede y pista elegidas antes de la copa (80090fb4) */
    uint8_t savedTrack;        /* DAT_800b7197 */
    uint8_t teamRace;          /* DAT_800b7167 */
    uint8_t champType;         /* DAT_800b7168: 0 carrera, 1 copa de sede, 2 WC, 3 EA */
    uint8_t curVenue;          /* DAT_800b7169 */
    uint8_t curTrack;          /* DAT_800b716a */
    uint8_t cupRaces;          /* DAT_800b716b */
    uint8_t raceIndex;         /* DAT_800b716c */
    uint8_t cupVenue[6];       /* DAT_800b716d */
    uint8_t cupTrack[6];       /* DAT_800b7173 */
    uint8_t laps;              /* DAT_800b7116 */
    uint8_t demo;              /* DAT_800b711b */
    char    playerName[2][11]; /* DAT_800b70fc / 800b7107 */
    uint8_t playerNameSet[2];  /* DAT_800b7105 / 800b7110 */
} GameSettings;

/* Indice en progress[] de una direccion del original (base DAT_800b7121) */
#define PROGRESS(addr) g_Settings.progress[(addr) - 0x800b7121u]

/* Parrilla (DAT_800b0e40): numero de coches y 8 bytes por coche */
typedef struct
{
    uint8_t player;            /* +0 mando (0xFF = IA) */
    uint8_t driver;            /* +1 */
    uint8_t car;               /* +2 clase del coche */
    uint8_t team;              /* +3 equipo / orden (100 = libre en la pantalla 1200) */
    uint8_t cls;               /* +4 nivel de la IA (clase de carrera) */
    uint8_t model;             /* +5 equipo*3 + clase */
    uint8_t playerNum;         /* +6 */
    uint8_t flag;              /* +7 rival / leyenda */
} GridCar;
typedef struct
{
    uint8_t count;
    GridCar car[8];
} RaceGrid;
extern RaceGrid g_RaceGrid;

/* Registro de cada coche en la carrera / copa (DAT_800b706c, 0x18 bytes) */
typedef struct
{
    uint8_t startPos;
    uint8_t pad01[3];
    int     a, b, points, d;
    uint8_t taken;
    uint8_t pad15[3];
} CarRecord;
extern CarRecord g_CarRecords[6];



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
void Fe_Screen600(void);                                                  /* 80088edc: Showcase (videos) */
void Fe_Screen700(void);                                                  /* 8007cb6c: contrasena */
void Fe_Screen800(void);                                                  /* 8007d32c: tarjeta de memoria */
void Fe_Screen1100(void);                                                 /* 80080cbc: nombre de los jugadores */
int  Fe_SavePrompt(int ask);                                              /* parte de 8007a994 */

/* Vuelta de una carrera, records y copas (fe_records.c) */
extern short g_FeSaveReturn;          /* DAT_800b7118: 1 = preguntar si guardar al volver al menu; despues, la
                                         pantalla a la que se vuelve desde la 800 */
extern int8_t g_FeUnlockShow;        /* fe +0xA84: clase recien desbloqueada; -1 todo ganado en Elite (la 600 pone
                                         el final), -2 el video de la intro */
int  Fe_AfterRace(void);                                                  /* 80090fb4: siguiente pantalla */
int  Fe_DrawTime(int r, int g, int b, int x, int y, int t);               /* 8008ee98 */
void Fe_Screen1400(void);                                                 /* 800855a0: records de la pista */
void Fe_Screen900(void);                                                  /* 8007ddb4: Hall of Champs */
void Fe_Screen1500(void);                                                 /* 80085d90: clasificacion de la copa */
void Fe_Screen1600(void);                                                 /* 8007ea64: records de una copa */
void Fe_Screen1700(void);                                                 /* 80087130: podio de la copa */
void Fe_Screen1800(void);                                                 /* 80087fa8: desbloqueos */
int  Fe_DecodeGmdc(int index, int x, int y);                              /* 8008e21c */
void Fe_Screen1200(void);                                                 /* 80081a9c */
void Fe_Screen1300(void);                                                 /* 800838f8 */
void Fe_InitVenues(void);                                                 /* parte de 80079814 */
int  Fe_CarAvailable(int driver, int car);                                /* 80090674 */
void Fe_StartRace(int demo);                                              /* 8008927c */
const char* Fe_VenueCode(int venue);                                      /* 800abdd4[sede][0] */
int  Fe_DecodeLmdc(int index, int x, int y);
void Fe_LoadAtlas(void);                                                  /* 80092f5c */
void Fe_FreeAtlas(void);                                                  /* 80093050 */
void Fe_CreateGarageObjects(void);                                        /* parte de 80079814 */
void Fe_FreeGarageObjects(void);

/* Coches del garaje que llegan por secciones (fe_select.c) */
int  Fe_IsFading(void);                  /* DAT_800af388 */
int  Fe_CarReceiving(void);              /* 80092938 */
int  Fe_MarkReceiving(void);             /* 80092894 */
int  Fe_OnCarModel(NrAsset* a, int id);  /* 80092998: 1 si se queda con el asset */

/* Manejadores de assets del front-end */
int  Fe_OnCrle(NrAsset* a);          /* 80092e08 */
int  Fe_OnTxtR(NrAsset* a);          /* 80092dec */
int  Fe_OnMdc(NrAsset* a);           /* Bmdc 80092d2c, Lmdc 80092d58, Tmdc 80092d84, Gmdc 80092db0 */

#endif
