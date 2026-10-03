/* Mandos. Original: Pad_GetButtons 8001dac0 (mascara del juego = ~palabra del buffer del mando),
 * FUN_8005fc54 (lee los dos puertos a DAT_800af700), init en FUN_8001dd94. */
#ifndef NR_PAD_H
#define NR_PAD_H

#include <stdint.h>

/* Bits de la mascara que usa el juego: ~(estado | tipo << 8 | botones << 16) */
#define PADB_SELECT   (1u << 16)
#define PADB_START    (1u << 19)
#define PADB_UP       (1u << 20)
#define PADB_RIGHT    (1u << 21)
#define PADB_DOWN     (1u << 22)
#define PADB_LEFT     (1u << 23)
#define PADB_L2       (1u << 24)
#define PADB_R2       (1u << 25)
#define PADB_L1       (1u << 26)
#define PADB_R1       (1u << 27)
#define PADB_TRIANGLE (1u << 28)
#define PADB_CIRCLE   (1u << 29)
#define PADB_CROSS    (1u << 30)
#define PADB_SQUARE   (1u << 31)

void     Pad_Init(void);
uint32_t Pad_GetButtons(int port);
void     Pad_ReadAll(void);

extern uint32_t g_PadButtons[2];   /* DAT_800af700 */

#endif
