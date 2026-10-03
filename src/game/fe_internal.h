/* Widgets y mando del front-end compartidos entre las pantallas (fe_menu.c, fe_select.c). */
#ifndef NR_FE_INTERNAL_H
#define NR_FE_INTERNAL_H

#include <stdint.h>
#include "frontend.h"

int      Fe_BaseDepth(void);
void     Fe_DrawBox(int r, int g, int b, int x0, int y0, int x1, int y1, int semi);  /* 8008e614 */
void     Fe_DrawLine(uint32_t color, int x0, int y0, int x1, int y1, int thick);      /* 80079784 */
uint32_t Rgb(int r, int g, int b);
void     Fe_DrawOutline(int r, int g, int b, int x0, int y0, int x1, int y1, int thick); /* 8008e51c */
void     Fe_DrawFrame(int r, int g, int b, int x0, int y0, int x1, int y1);           /* 8008e75c */
void     Fe_ItemColor(int* r, int* g, int* b, int mode);                               /* 8008ac5c */
void     Fe_DrawHelp(int port, const char* text, int x, int y, int clipX0, int clipX1, int edges); /* 8008d5f4 */
int      Fe_MsgBox(int x, int y, const char* text, uint8_t* yesNo, int port);         /* 8008cb88 */

void     Fe_ReadPads(void);                                    /* 8008a7f4 */
int      Fe_PadPressed(uint32_t mask, int port, int repeat);   /* 8008a994 */
void     Fe_PadRepeatReset(void);                              /* 8008aa90 */
int      Fe_Clamp(int v, int lo, int hi);                      /* 8008aad8 */

/* fe_select.c */
void        Fe_DrawSpinningModel(int x, int y, int dist, int model, int speed);   /* 8008de60 */
void        Fe_DrawCallout(int x, int y, int w, int h, int down, int left, int down2, int right); /* 8008dc1c */
int         Fe_DrawTextRgb(int r, int g, int b, const char* text, int x, int y, int align);    /* 8008efec */
const char* HelpText(int i);                                                      /* DAT_800b6e78[i] */

/* Sonidos del front-end (FUN_8003f6fc): sin audio todavia */
static inline void Fe_PlaySound(int id) { (void)id; }

#endif
