/* Redes de trazadas Cnet (para la IA, la salida y la direccion de la pista).
 * Asset: cabecera de 0xC bytes ('ODtN', tamano, ?) y la red: +2 s16 n de nodos, nodos de 12 bytes desde +4:
 * u32 enlaces (3 x 10 bits: >> 22, >> 12, >> 2), u16 banderas (0x1000 = no vale como salida), s16 x, y, z
 * (mundo >> 8; en el fichero z, x, y >> 4 y Cnet_OnLoaded 8004c368 las pasa a x, altura del suelo, z).
 * Original: redes DAT_800b5ce8[DAT_800af888] (la primera es la principal), nodo mas cercano 8002cc50,
 * siguiente nodo 8004bd74. */
#ifndef NR_CNET_H
#define NR_CNET_H

#include <stdint.h>
#include "stream.h"

#define CNET_MAX 4

extern uint8_t* g_Nets[CNET_MAX];      /* DAT_800b5ce8 */
extern int      g_NumNets;             /* DAT_800af888 */

int  Cnet_OnLoaded(NrAsset* a);                         /* 8004c368 */
void Cnet_Reset(void);
int  Cnet_Count(const uint8_t* net);
void Cnet_NodePos(const uint8_t* net, int node, int* out);   /* mundo */
int  Cnet_FindNearest(const uint8_t* net, const int* pos, int skipFlagged);   /* 8002cc50 */
int  Cnet_Next(const uint8_t* net, int node);           /* 8004bd74: primer enlace valido o -1 */
int  Cnet_FindNearest3D(const uint8_t* net, const int* pos);   /* 8004becc */

#endif
