/* Despachador de assets: original Asset_Dispatch 80024710 (switch por tipo al terminar de llegar). */
#ifndef NR_ASSET_H
#define NR_ASSET_H

#include "stream.h"

#define ASSET_Cpag NR_TAG('C', 'p', 'a', 'g')
#define ASSET_Cobj NR_TAG('C', 'o', 'b', 'j')
#define ASSET_Ctrk NR_TAG('C', 't', 'r', 'k')
#define ASSET_Cact NR_TAG('C', 'a', 'c', 't')
#define ASSET_Cctr NR_TAG('C', 'c', 't', 'r')

/* Procesa todos los assets que haya en la cola del stream */
void Asset_DispatchAll(void);

/* Resumen por tipo de lo despachado hasta ahora */
void Asset_LogSummary(void);

#endif
