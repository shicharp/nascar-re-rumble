/* Solo en el port: partidas en red local (LAN) con ENet, hasta 8 jugadores, uno por maquina.
 *
 * Red por entradas (lockstep): cada maquina simula la carrera entera y por la red solo viajan las pulsaciones de
 * cada humano (ver NOTAS, "Multijugador"). Estrella: los clientes solo hablan con el anfitrion, que reenvia.
 *  - Sala: el cliente se presenta (HELLO: nombre, piloto, cambio); el anfitrion contesta con su numero de maquina
 *    y manda la lista de jugadores (LOBBY) cada vez que cambia.
 *  - Carrera: el anfitrion elige pista y parrilla y manda START (ajustes, parrilla, semilla, que humano es de que
 *    maquina); todos cargan y corren a paso fijo, y cada frame mandan las pulsaciones de su humano para el frame
 *    actual + NET_INPUT_DELAY. Ningun frame se simula sin las pulsaciones de todos.
 *  - Sumas de la carrera (Race_StateChecksum) cada 30 frames para detectar desincronizaciones. */
#ifndef NR_NET_H
#define NR_NET_H

#include <stdint.h>

#define NET_MAX_MACHINES 8
#define NET_DEFAULT_PORT 37120
#define NET_NAME_LEN     11       /* como g_Settings.playerName */

typedef struct
{
    uint8_t used;
    uint8_t driver;               /* piloto elegido (g_Settings.lastDriver[0]) */
    uint8_t transmission;         /* esquema de botones y cambio (g_Settings.transmission[0], 0-5) */
    uint8_t inRace;               /* sigue en la carrera (manda pulsaciones) */
    char    name[NET_NAME_LEN + 1];
} NetPlayer;

void Net_Init(void);
void Net_Shutdown(void);

int  Net_Host(int port);                       /* empieza una partida como anfitrion (maquina 0) */
int  Net_Join(const char* host, int port);     /* se une por IP o nombre; la conexion llega en Net_Poll */
void Net_Leave(void);                          /* sale de la partida (avisa a los demas) */
void Net_Poll(void);                           /* atiende la red sin esperar (lo llama Pad_ReadAll) */

int  Net_Active(void);                         /* hay partida en red (anfitrion, o cliente conectado o conectando) */
int  Net_IsHost(void);
int  Net_Connected(void);                      /* cliente: ya tiene numero de maquina */
int  Net_MachineId(void);                      /* la de esta maquina (-1 sin numero todavia) */
const NetPlayer* Net_Player(int machine);      /* NULL si el hueco esta libre */
int  Net_NumPlayers(void);
const char* Net_Status(void);                  /* texto del ultimo suceso (conectando, rechazado...) */

void Net_UpdateLocalPlayer(void);              /* el piloto o el nombre de esta maquina han cambiado: a la sala */

/* ---- busqueda de partidas en la red local (difusion UDP en NET_FIND_PORT; el anfitrion contesta) ---- */

#define NET_FIND_PORT 37121
#define NET_FIND_MAX  8

typedef struct
{
    char    addr[32];             /* IP del anfitrion */
    char    name[NET_NAME_LEN + 1];
    uint8_t players;
    uint32_t seen;                /* enet_time_get de la ultima respuesta */
} NetFound;

void Net_FindStart(void);
void Net_FindStop(void);
int  Net_FindCount(void);
const NetFound* Net_Found(int i);

/* ---- carrera ---- */

#define NET_MAX_HUMANS   8
#define NET_INPUT_DELAY  3        /* frames entre leer el mando y usarlo (60 Hz: 50 ms) */

/* Lo que manda el anfitrion al empezar (MSG_START): basta para que cada maquina cargue la misma carrera */
typedef struct
{
    uint32_t seed;                             /* Rand_SetSeed */
    uint32_t unlockMask;                       /* g_GameInfo->unlockMask */
    uint8_t  venue, track, laps, racingClass, powerUps, teamRace, gameMode, numHumans;
    uint8_t  humanMachine[NET_MAX_HUMANS];     /* maquina de cada humano (GridCar.player = numero de humano) */
    uint8_t  transmission[NET_MAX_HUMANS];     /* cambio de cada humano (< 3 automatico) */
    uint8_t  gridCount;
    uint8_t  grid[8][8];                       /* g_RaceGrid.car, ya en el orden de salida */
} NetRaceSetup;

int  Net_InRace(void);                         /* esta maquina corre una carrera en red */
int  Net_HumanView(int human);                 /* vista de esta maquina (0 / 1) del humano o -1 (remoto, IA); sin
                                                  red, la del original: human si es 0 o 1 */
const char* Net_HumanName(int human);          /* nombre del jugador en red (NULL sin red) */
int  Net_HumanTransmission(int human);         /* cambio del humano (sin red, g_Settings.transmission[human & 1]) */
int  Net_RaceSeed(void);
const NetRaceSetup* Net_Setup(void);

/* anfitrion (Fe_StartRace): las maquinas que entran en la carrera; Net_HostStartRace manda la salida */
int  Net_HostRacers(int* machines);            /* devuelve cuantas (sin el anfitrion) y sus numeros */
void Net_HostStartRace(void);
/* cliente: la salida ha llegado y hay que dejar el front-end (Frontend_MainLoop) */
int  Net_TakeStart(void);

void Net_RaceFrame(void);                      /* Race_UpdateTime, tras leer el mando: pulsaciones en lockstep */
void Net_RaceStep(void);                       /* tras simular el frame: sumas de control */
void Net_RaceEnd(void);                        /* al acabar la carrera en esta maquina */
int  Net_DesyncFrame(void);                    /* primer frame desincronizado (0 ninguno) */

#endif
