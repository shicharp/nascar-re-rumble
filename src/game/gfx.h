/* Graficos: doble buffer de OT y primitivas.
 * Original: Gfx_InitBuffers 8001a0b4 (OT de 0xB01 entradas + pool de primitivas), Gfx_FreeBuffers 8001a190,
 * Gfx_Flip 8001a24c (DrawOTag con area de dibujo fija 320x240 / 320x120 en pantalla partida),
 * FUN_8001a1b4 (reparte las primitivas en la OT segun la profundidad de su etiqueta, tabla 800aa4c8).
 * Framebuffers en VRAM (0,0) y (0,256) como el original (DAT_800af5e4 = 0x100 - y del buffer de dibujo).
 * WIDESCREEN: el area de dibujo y el centro de proyeccion se fijan aqui y en Camera_ApplyProjection. */
#ifndef NR_GFX_H
#define NR_GFX_H

#define GFX_SCREEN_W 320
#define GFX_SCREEN_H 240
#define GFX_OT_LEN   0xB01

/* Modos de Gfx_Flip (parametro & 0xF en el original) */
#define GFX_FLIP_HALF  0   /* mitad de pantalla partida */
#define GFX_FLIP_FULL  1   /* pantalla completa */
#define GFX_FLIP_SMALL 2   /* rectangulo 0x70 x 0x2A (retrovisor / icono) */
#define GFX_FLIP_RACE  3   /* fin de frame de carrera */

void  Gfx_Init(void);
void  Gfx_InitBuffers(int numPrims);   /* reserva numPrims * 0x68 + 0x5818 bytes por buffer, como el original */
void  Gfx_FreeBuffers(void);

void  Gfx_BeginFrame(int clearR, int clearG, int clearB);
void* Gfx_AllocPrim(int bytes);
void  Gfx_AddPrim(int otIndex, void* prim);     /* indice directo en la OT */
void  Gfx_AddPrimDepth(int depth, void* prim);  /* profundidad del juego (etiqueta & 0xFFFFFF) */
void  Gfx_Flip(int mode);
int   Gfx_GetDrawBufferY(void);                 /* y del buffer que se esta dibujando (0 o 256) */
int   Gfx_GetDisplayBufferY(void);              /* y del buffer que se ve */

/* Guarda el ultimo frame mostrado (buffer frontal de la ventana) como BMP de 24 bits */
int   Gfx_SaveScreenshot(const char* path);
/* Captura automatica: guarda el frame numero 'frame' en 'path' y termina el programa */
void  Gfx_SetAutoShot(const char* path, int frame);

#endif
