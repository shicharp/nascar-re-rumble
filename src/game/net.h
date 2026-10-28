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

#endif
