/* Widgets y mando del front-end compartidos entre las pantallas (fe_menu.c, fe_select.c). */
#ifndef NR_FE_INTERNAL_H
#define NR_FE_INTERNAL_H

#include <stdint.h>
#include "frontend.h"
#include "snd.h"

int      Fe_BaseDepth(void);
void     Fe_DrawBox(int r, int g, int b, int x0, int y0, int x1, int y1, int semi);  /* 8008e614 */
void     Fe_DrawLine(uint32_t color, int x0, int y0, int x1, int y1, int thick);      /* 80079784 */
uint32_t Rgb(int r, int g, int b);
void     Fe_DrawOutline(int r, int g, int b, int x0, int y0, int x1, int y1, int thick); /* 8008e51c */
void     Fe_DrawFrame(int r, int g, int b, int x0, int y0, int x1, int y1);           /* 8008e75c */
void     Fe_ItemColor(int* r, int* g, int* b, int mode);                               /* 8008ac5c */
void     Fe_DrawHelp(int port, const char* text, int x, int y, int clipX0, int clipX1, int edges); /* 8008d5f4 */
int      Fe_MsgBox(int x, int y, const char* text, uint8_t* yesNo, int port);         /* 8008cb88 */
void     Fe_DrawTextBox(int style, const char* text, int x, int y);                   /* 8008ec2c */

void     Fe_ReadPads(void);                                    /* 8008a7f4 */
int      Fe_PadPressed(uint32_t mask, int port, int repeat);   /* 8008a994 */
void     Fe_PadRepeatReset(void);                              /* 8008aa90 */
int      Fe_Clamp(int v, int lo, int hi);                      /* 8008aad8 */

/* Cinta de la pista generada a partir de los puntos del TkFE (0x6C bytes + vertices y normales) */
typedef struct
{
    short    count;          /* +0x00 puntos */
    short    start;          /* +0x02 punto de la linea de salida */
    short    startVerts[6][4];/* +0x04 cuadros de la linea de salida */
    int16_t* verts;          /* +0x34 por punto: borde izquierdo y derecho (x, y, z, -) */
    int16_t* normals;        /* +0x38 por punto (x, y, z, -) */
    int      rot[9];         /* +0x3C */
    int      pos[3];         /* +0x60 */
} TrackPreview;

/* fe_track.c */
TrackPreview* TrackPreview_Create(const NrAsset* a, int w, int h);                 /* 8001893c */
void        TrackPreview_Draw(TrackPreview* tp, uint32_t color, const SVECTOR* light);   /* 80018ef0 */
void        Fe_LoadVenueThumb(int image, int slot);                                       /* 8008e13c */
int         Fe_WrapVenue(int v);                                                          /* 80090860 */
void        Fe_DrawVenueThumb(int slot, int x, int y, int depth, int mode);               /* 8008c308 */

/* fe_select.c */
void        Fe_RequestCar(int p, int driver, int car);                                    /* 800923cc */
void        Fe_ResetCars(void);                                                           /* 80090a88(1) */
void        Fe_UpdateCars(void);                                                          /* 80090a88(0) */
void        Fe_DrawCarModel(int p, int x, int y, int dist, int reflection);              /* 8008bd58 */
void        Fe_DrawSpinningModel(int x, int y, int dist, int model, int speed);   /* 8008de60 */
void        Fe_DrawCallout(int x, int y, int w, int h, int down, int left, int down2, int right); /* 8008dc1c */
int         Fe_DrawTextRgb(int r, int g, int b, const char* text, int x, int y, int align);    /* 8008efec */
const char* HelpText(int i);                                                      /* DAT_800b6e78[i] */

/* Sonidos del front-end (FUN_8003f6fc(id, 0)) */
static inline void Fe_PlaySound(int id) { Snd_PlayUi(id, NULL); }

#endif
