/* Graficos: doble buffer de OT y primitivas.
 * Original: Gfx_InitBuffers 8001a0b4 (OT de 0xB01 entradas + pool de primitivas), Gfx_FreeBuffers 8001a190,
 * Gfx_Flip 8001a24c (DrawOTag con area de dibujo fija 320x240 / 320x120 en pantalla partida).
 * WIDESCREEN: el area de dibujo y el centro de proyeccion se fijan aqui y en Camera_ApplyProjection. */
#ifndef NR_GFX_H
#define NR_GFX_H


#define GFX_SCREEN_W 320
#define GFX_SCREEN_H 240
#define GFX_OT_LEN   0xB01

void  Gfx_Init(void);
void  Gfx_InitBuffers(int primBytes);
void  Gfx_FreeBuffers(void);

void  Gfx_BeginFrame(int clearR, int clearG, int clearB);
void* Gfx_AllocPrim(int bytes);
void  Gfx_AddPrim(int z, void* prim);
void  Gfx_Flip(void);

/* Guarda el ultimo frame mostrado (buffer frontal de la ventana) como BMP de 24 bits */
int   Gfx_SaveScreenshot(const char* path);

#endif
