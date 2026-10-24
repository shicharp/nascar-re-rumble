/* Partida guardada en la tarjeta de memoria ("BASLUS-01068NASCRMBL", un bloque, 0x1DEC bytes).
 * Original: buffer DAT_800b8d2c (0x1E6C), estado DAT_800b8d19..8d28, titulo SJIS DAT_800b8d30 (80093748),
 * iniciar 8009347c / 80093540, parar 80093568, consultar 8009358c, leer 80093938, escribir 800937f8, empaquetar
 * 800939fc, desempaquetar 80093f6c, borrar records 800931d8. Ver savegame.c. */
#ifndef NR_SAVEGAME_H
#define NR_SAVEGAME_H

#include <stdint.h>

#define SAVE_FILE_NAME "BASLUS-01068NASCRMBL"

/* Datos de la partida que el port no tenia en otras estructuras (en el orden del original) */
typedef struct
{
    uint8_t  b711c;            /* DAT_800b711c */
    uint8_t  hudPref[2];       /* DAT_800b715f / 7160: nivel del marcador (Fe_StartRace lo copia a 7112) */
    int8_t   recordPos[2];     /* DAT_800b7106 / 7111 (ultimo byte de cada nombre): puesto del ultimo record de
                                  pista de cada jugador, +100 con el de la vuelta rapida (99: solo ese); -1 ninguno */
    uint8_t  padKind[2];       /* DAT_800b797d: tipo de mando de cada jugador (pantalla 1000: 0 analogico, 1 digital, 2 / 3 neGcon) */
    uint8_t  vibration[2];         /* DAT_800vibration */
    uint8_t  b7995, b7996;     /* DAT_800b7995 / 7996 */
    uint8_t  trackRecs[7 * 0x120];  /* DAT_800b7198: [sede][clase][6] x 0x10 (Clear Records "Track") */
    uint8_t  cupRecs[8 * 0x150];    /* DAT_800b7998: [sede][clase][4] x 0x1C (Clear Records "Cups") */
    uint8_t  cupRecs2[8 * 0x120];   /* DAT_800b8418: [sede][clase][4] x 0x18 (tambien "Cups") */
} SaveExtra;

extern SaveExtra g_SaveExtra;

/* Estado de la partida en la tarjeta (DAT_800b8d19..8d28) */
typedef struct
{
    int8_t on;          /* DAT_800b8d19 */
    int    last, prev;  /* DAT_800b8d1c / 8d20: ultimo estado de la tarjeta */
    int8_t mode;        /* DAT_800b8d24: 0, 1 tarjeta lista, -1 nueva (sin formato), -2 guardar al estar lista */
    int8_t res;         /* DAT_800b8d25: 1 / 2 leyendo / escribiendo, 3 / 4 confirmar sobrescribir / cargar,
                           -1 mirar el fichero, -2 llena, -3 / -4 error al escribir / leer, -5 escrita, -6 leida,
                           -7 hay partida, -8 no hay, -10 sin tarjeta al guardar */
    int8_t shown, shownPrev;   /* DAT_800b8d26 / 8d27 */
    int8_t exists;      /* DAT_800b8d28: 1 hay partida (o se acaba de escribir), -1 no */
} SaveState;

extern SaveState g_SaveState;

void Save_Init(void);                   /* 8009347c (+ 80093748) */
void Save_Start(void);                  /* 80093540 */
void Save_Stop(void);                   /* 80093568 */
int  Save_Poll(void);                   /* 8009358c: estado de la tarjeta */
void Save_Read(void);                   /* 80093938 */
void Save_Write(void);                  /* 800937f8 */
void Save_ClearRecords(int which);      /* 800931d8: 0 campeonatos, 1 records y copas, -1 todo */
void Save_ResetExtra(void);             /* parte de Frontend_InitScreens 80079814 (primer arranque) */
void Save_BootLoad(void);               /* Legal_Show 80090d98: carga la partida al arrancar (cada frame) */
void Save_BootEnd(void);

#endif
