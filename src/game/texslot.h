/* Slots de pagina de textura del juego: cada Cpag se sube a un slot de VRAM (slot = id - 1; en el garaje
 * los coches van a los slots 9-11) y los modelos Cobj traducen el slot de cada cara a tpage/CLUT reales.
 * Original: tabla de tpages DAT_800b6418 (FUN_80066fe0), tablas de regiones DAT_800b6448,
 * Cpag_UploadToSlot 80066d00, Cpag_FindRegion 80066c4c, posicion por nivel de mip 800669d4,
 * CLUTs 8006674c / 800668cc, pixeles 800666c8, parcheo de caras 800674fc. */
#ifndef NR_TEXSLOT_H
#define NR_TEXSLOT_H

#include <stdint.h>

#define TEXSLOT_COUNT 0x14

void TexSlot_Init(void);                                              /* 80066fe0 */
int  TexSlot_GetTPage(int slot, int level, int* x, int* y);          /* 800669d4: tpage o -1 */
void Cpag_UploadToSlot(const uint8_t* data, int size, int slot);      /* 80066d00 */
const uint8_t* Cpag_FindRegion(int slot, int u, int v);              /* 80066c4c */
void TexSlot_PatchFace(uint8_t* face, int level);                     /* 800674fc */

extern uint32_t g_EnvMapUV[2];     /* DAT_800af910/914: CLUT y uv del mapa de reflejos (pagina 8) */
extern int      g_FeLoadingSlot;   /* DAT_800af970: hueco del garaje que se esta cargando */

#endif
