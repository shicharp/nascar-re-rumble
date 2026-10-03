/* Despachador de assets: original Asset_Dispatch 80024710 (switch por tipo al terminar de llegar). */
#ifndef NR_ASSET_H
#define NR_ASSET_H

#include "stream.h"

#define ASSET_Cpag NR_TAG('C', 'p', 'a', 'g')
#define ASSET_Cobj NR_TAG('C', 'o', 'b', 'j')
#define ASSET_Ctrk NR_TAG('C', 't', 'r', 'k')
#define ASSET_Cact NR_TAG('C', 'a', 'c', 't')
#define ASSET_Cctr NR_TAG('C', 'c', 't', 'r')
#define ASSET_Crle NR_TAG('C', 'r', 'l', 'e')
#define ASSET_TxtR NR_TAG('T', 'x', 't', 'R')
#define ASSET_Cfnt NR_TAG('C', 'f', 'n', 't')
#define ASSET_Bmdc NR_TAG('B', 'm', 'd', 'c')
#define ASSET_Lmdc NR_TAG('L', 'm', 'd', 'c')
#define ASSET_Tmdc NR_TAG('T', 'm', 'd', 'c')
#define ASSET_Gmdc NR_TAG('G', 'm', 'd', 'c')
#define ASSET_Ctos NR_TAG('C', 't', 'o', 's')
#define ASSET_rscB NR_TAG('r', 's', 'c', 'B')
#define ASSET_rscE NR_TAG('r', 's', 'c', 'E')
#define ASSET_TkFE NR_TAG('T', 'k', 'F', 'E')

/* El visor de depuracion guarda las paginas Cpag en memoria en vez de subirlas a sus slots */
extern int g_AssetViewerMode;

/* Procesa todos los assets que haya en la cola del stream */
void Asset_DispatchAll(void);

/* Resumen por tipo de lo despachado hasta ahora */
void Asset_LogSummary(void);

#endif
