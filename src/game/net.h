/* Solo en el port: partidas en red local (LAN) con ENet, hasta 8 jugadores, uno por maquina.
 *
 * Red por entradas (lockstep): cada maquina simula la carrera entera y por la red solo viajan las pulsaciones de
 * cada humano (ver NOTAS, "Multijugador"). Estrella: los clientes solo hablan con el anfitrion, que reenvia.
 *  - Sala: el cliente se presenta (HELLO: nombre, piloto, cambio); el anfitrion contesta con su numero de maquina
 *    y manda la lista de jugadores (LOBBY) cada vez que cambia.
 *  - Carrera: el anfitrion elige pista y parrilla y manda START (ajustes, parrilla, semilla, que humano es de que
 *    maquina); todos cargan y corren a paso fijo, y cada frame mandan las pulsaciones de su humano para el frame
 *    actual + el retardo de la carrera (NetRaceSetup.inputDelay: lo elige el anfitrion segun el ping). Ningun
 *    frame se simula sin las pulsaciones de todos. Las pulsaciones van sin garantia de entrega y cada paquete
 *    lleva tambien las NET_REDUNDANT anteriores: perder uno no para a nadie.
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
    uint8_t car;                  /* clase del coche (g_Settings.lastCar[0]: 0 rookie, 1 pro, 2 elite) */
    uint8_t transmission;         /* esquema de botones y cambio (g_Settings.transmission[0], 0-5) */
    uint8_t inRace;               /* donde esta: NET_P_* */
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

/* donde esta cada maquina (NetPlayer.inRace; los clientes lo dicen desde el front-end con Net_SetStage) */
#define NET_P_LOBBY      0        /* en la sala: entra en la siguiente carrera */
#define NET_P_RACING     1
#define NET_P_STANDINGS  2        /* campeonato: en la clasificacion, lista para la siguiente carrera */
#define NET_P_BUSY       3        /* despues de una carrera: resultados, records, podio, guardar... */
void Net_SetStage(int stage);                  /* cliente: se manda al anfitrion si cambia */

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
#define NET_INPUT_DELAY  3        /* frames minimos entre leer el mando y usarlo (60 Hz: 50 ms) */
#define NET_MAX_DELAY    12       /* y maximos (200 ms) */
#define NET_REDUNDANT    10       /* pulsaciones anteriores que repite cada paquete */

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
    int32_t  gameInfo[43];                     /* g_GameInfo entero (los guiones leen y guardan cosas en el) */
    uint8_t  inputDelay;                       /* frames de retardo de las pulsaciones (segun el ping) */
    uint8_t  assists;                          /* NET_ASSIST_*: las ayudas que ha puesto el anfitrion */
    /* campeonato (champType != 0): la copa, la carrera que toca y lo que lleva cada coche */
    uint8_t  champType, cupRaces, raceIndex, cupSelVenue, cupSelTrack;
    uint8_t  cupVenue[6], cupTrack[6];
    uint8_t  records[8][0x18];                 /* g_CarRecords en el orden de salida (puntos y tiempo de la copa) */
} NetRaceSetup;

/* ayudas para los humanos que elige el anfitrion en la sala (racecar.c, Race_Assists) */
#define NET_ASSIST_DRAFT    1      /* rebufo: detras de otro coche, mas potencia */
#define NET_ASSIST_CATCHUP  2      /* rezagado: lejos del primer humano, mas potencia */
int  Net_Assists(void);                        /* las de la carrera en curso (0 sin red) */
int  Net_LobbyAssists(void);                   /* las de la sala */
void Net_SetAssists(int assists);              /* anfitrion: cambiarlas (se mandan con la sala) */

int  Net_InRace(void);                         /* esta maquina corre una carrera en red */
int  Net_HumanView(int human);                 /* vista de esta maquina (0 / 1) del humano o -1 (remoto, IA); sin
                                                  red, la del original: human si es 0 o 1 */
const char* Net_HumanName(int human);          /* nombre del jugador en red (NULL sin red) */
int  Net_HumanGone(int human);                 /* su maquina ha dejado la carrera (desde el frame que se simula) */
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

/* ---- campeonato (como mucho NET_CUP_MAX humanos: la clasificacion del original es de 6 coches) ----
 * El anfitrion lo empieza con la primera salida; cada maquina sigue la copa en su front-end con su humano como
 * jugador 1 (records, podio y desbloqueos en su partida). Entre carrera y carrera los de la copa esperan en la
 * clasificacion y el anfitrion da la siguiente salida cuando estan todos; el coche de quien se va lo lleva la IA. */
#define NET_CUP_MAX 6
int  Net_CupActive(void);                      /* esta maquina sigue en un campeonato en red */
int  Net_CupAllReady(void);                    /* anfitrion: todos los de la copa estan en la clasificacion */
int  Net_HostCupHuman(int human);              /* anfitrion: numero en la siguiente carrera del humano de la ultima
                                                  (-1 si se ha ido) */
void Net_LeaveCup(void);                       /* cliente: la deja; anfitrion: la acaba para todos */
void Net_CupFinished(void);                    /* despues de la ultima carrera: ya no hay copa */
int  Net_LocalHuman(void);                     /* numero de humano de esta maquina en la ultima carrera */
const char* Net_ViewHumanName(int player);     /* nombre del humano de la parrilla del front-end tras la carrera
                                                  (ver Fe_NetAfterRace; NULL si no es de la red) */

#endif
