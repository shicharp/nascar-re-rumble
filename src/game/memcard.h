/* Tarjeta de memoria (puerto 1) emulada sobre una imagen .mcd de 128 KB con el formato de la PS1.
 * Original: el driver del juego (estado DAT_800aa598, 0x1BB8 bytes) va por los eventos de la BIOS (_card_info,
 * _card_read / _card_write por sectores de 128 bytes, VSyncCallback 8001edd0). El port mantiene su interfaz y sus
 * codigos de estado (+0x900): 0 lista, -1 tarjeta nueva, -2 sin formatear, -3 sin tarjeta o error, -4 leyendo,
 * -5 escribiendo. Las lecturas y escrituras se hacen en memoria y en el fichero; el estado pasa por -4 / -5 una
 * consulta antes de volver a 0, como en la consola, porque la logica del juego espera ese cambio. */
#ifndef NR_MEMCARD_H
#define NR_MEMCARD_H

#include <stdint.h>

void Card_Init(int port);                       /* 8001f3c4 + 8001f47c: abre la imagen */
void Card_Stop(void);                           /* 8001f538 */
int  Card_Status(void);                         /* 8001f560 */
int  Card_FindFile(const char* name);           /* 8001fa3c: bloque del fichero o 0 */
int  Card_ReadFile(const char* name, void* buf, int bytes);    /* 8001fae8: sectores pedidos o 0 */
int  Card_WriteFile(const char* name, const void* buf, int bytes);   /* 8001fd88: 0x2000 si va bien */
int  Card_Format(void);                         /* 8001f910: 0 si lo empieza */
int  Card_BlockState(int block);                /* 8001fba4: 1 ocupado, 2 libre */
int  Card_AllocBlocks(int bytes);               /* 8001fc14: primer bloque de un hueco libre o 0 */
int  Card_Result(void);                         /* 8001e128: 1 si la ultima peticion fallo */
const char* Card_BlockName(int block);          /* nombre del fichero del bloque (+0x44 + bloque * 4) */

#endif
