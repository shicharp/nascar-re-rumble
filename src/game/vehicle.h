/* Vehiculos de la pista (tACT tipo 5: dirigibles, helicopteros y otros que siguen una red Cnet).
 * Original: Animated_Handler 80034e6c, Animated_Create 8003496c, orientacion 80034ba4, paso 80034d18, choque
 * entre vehiculos 80034a50, caida 80034dd8; seguimiento de la red: creacion 8004bfac, paso 8004bc80, ordenes
 * 8004a200, siguiente nodo 8004a3a8, preparar el giro 8004a69c, giro 8004af64, avance 8004b214, subir / bajar
 * 8004ba74, nodo de salida 8004bd14. Ver vehicle.c. */
#ifndef NR_VEHICLE_H
#define NR_VEHICLE_H

#include "actor.h"

int Vehicle_Handler(int msg, Actor* a, intptr_t p1, intptr_t p2);   /* 80034e6c */
Actor* Vehicle_First(void);                                         /* 80071f10: primera entidad de tipo 5 */

#endif
