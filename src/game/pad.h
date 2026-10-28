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
int      Pad_GetType(int port);   /* FUN_8001d840: 0 sin mando, 4 digital, 7 analogico, 2 neGcon */
uint32_t Pad_GetButtons(int port);
void     Pad_ReadAll(void);
int      Pad_Kind(int port);      /* 8001d840: el tipo (2, 4, 7), 0 sin mando, -1 uno que el juego no admite */
int      Pad_GetStick(int port, int* x, int* y);   /* 8001d10c: stick izquierdo del analogico (0x80 centro) */

/* Botones de cada jugador en carrera (DAT_800b0570, 14 mascaras: arriba, abajo, izquierda, derecha, cuadrado,
 * cruz, circulo, triangulo, R1, L1, L2, R2, SELECT, START); 8001cedc los pone por defecto y 8008b160
 * (Pad_SetupControls, en fe_pads.c) los copia del esquema elegido en la pantalla 1000. */
extern uint32_t g_PadBinds[2][14];
void     Pad_ResetBinds(int player);            /* 8001cedc */
int      Pad_HasVibration(int port);            /* 8001d448: el mando tiene motores (analogico) */
void     Pad_SetVibration(int port, int on);    /* 8001d520 */
void     Pad_TestVibration(int port);           /* 8001d2c8: el motor pequeño un momento */

/* Pruebas: "frame:boton,frame:boton..." (up, down, left, right, cross, triangle, start...).
 * Cada pulsacion simulada dura 3 frames en el mando 1 ("boton*n": n frames; "2boton": el mando 2). "lx=v" / "ly=v"
 * mueven el stick izquierdo (0..255) y hacen el mando simulado analogico. */
void     Pad_SetScript(const char* script);
void     Pad_SetRaceFrame(int frame);   /* frame de la carrera para las entradas "rN" (-1 fuera de ella) */

extern uint32_t g_PadButtons[2];   /* DAT_800af700 */

#endif
