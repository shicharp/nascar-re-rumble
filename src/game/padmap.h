/* Solo en el port: que tecla o boton del PC da cada boton del mando de la PS1. El teclado (un mapeo para los
 * puertos que lee, el 1 salvo que se cambie con F4) y el mando SDL de cada puerto, en PsyCross
 * (g_cfg_keyboardMapping, g_cfg_controllerMapping / 2); se cambian en la pantalla de mandos (1000) y se guardan en
 * controls.cfg, junto al .log. */
#ifndef NR_PADMAP_H
#define NR_PADMAP_H

#define PADMAP_BUTTONS 14   /* arriba, abajo, izquierda, derecha, cruz, circulo, cuadrado, triangulo, L1, R1, L2, R2,
                               START, SELECT */

enum { PADMAP_KEYBOARD, PADMAP_CONTROLLER };

void        PadMap_Init(void);   /* despues de PsyX_Initialise: guarda los de por defecto y lee controls.cfg */
void        PadMap_Save(void);
int         PadMap_HasKeyboard(int port);     /* el teclado da los botones de ese puerto */
int         PadMap_HasController(int port);   /* hay un mando SDL en ese puerto */
int         PadMap_Get(int port, int device, int button);
void        PadMap_Set(int port, int device, int button, int code);   /* si otro boton la tenia, se intercambian */
void        PadMap_Reset(int port);           /* los de por defecto (el teclado, si es de ese puerto) */
const char* PadMap_ButtonName(int button);    /* "cross"... (los de --press) */
const char* PadMap_CodeName(int device, int code);

/* Captura de la tecla o el boton nuevo: Poll da -1 mientras no se pulse nada nuevo y -2 con Esc */
void        PadMap_CaptureBegin(int port, int device);
int         PadMap_CapturePoll(void);

#endif
