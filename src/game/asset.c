#include "asset.h"
#include "cpag.h"
#include "globals.h"
#include "frontend.h"
#include "font.h"
#include "model.h"
#include "texslot.h"
#include "race.h"
#include "cnet.h"
#include "racecar.h"
#include "actor.h"
#include "snd.h"
#include "fx.h"

#define MAX_TYPES 48

static struct
{
    uint32_t type;
    int      count;
    int      bytes;
} s_types[MAX_TYPES];
static int s_numTypes;
int g_AssetViewerMode;

static void CountType(const NrAsset* a)
{
    int i;
    for (i = 0; i < s_numTypes; i++)
        if (s_types[i].type == a->type)
            break;
    if (i == s_numTypes)
    {
        if (s_numTypes == MAX_TYPES)
            return;
        s_types[s_numTypes++].type = a->type;
    }
    s_types[i].count++;
    s_types[i].bytes += a->size;
}

/* 8006710c: pagina al slot id - 1; en el garaje las libreias (id >= 10000) van al slot 9 + hueco */
static int Cpag_OnLoadedGame(NrAsset* a)
{
    int slot = a->id - 1;
    if (a->id > 9999)
    {
        if (!g_IsFrontend)
            slot = g_RaceGrid.car[g_CarLoadOrder[g_CarsLoaded]].playerNum + 0xe;   /* libreia del coche */
        else
        {
            if (Fe_IsFading() || !Fe_MarkReceiving())
                return 0;
            slot = g_FeLoadingSlot + 9;
        }
    }
    Cpag_UploadToSlot(a->data, a->size, slot);
    return 0;
}

/* 80024710. Devuelve 1 si el manejador se queda con el asset. */
static int Asset_Dispatch(NrAsset* a)
{
    CountType(a);
    switch (a->type)
    {
    case ASSET_Cpag:
        return g_AssetViewerMode ? Cpag_OnLoaded(a) : Cpag_OnLoadedGame(a);
    case ASSET_Cobj:
        return Cobj_OnLoaded(a);   /* 80013374 */
    case ASSET_TkFE:
        /* 800188f8: trazado de la pista para la pantalla 1300; +4 pasa de tamano a numero de puntos */
        {
            uint32_t* d = (uint32_t*)a->data;
            d[1] = (d[1] - 0xc) / 6;
            Asset_Register(a);
            return 1;
        }
    case ASSET_Ctos:
        return Stream_OnCtos(a);   /* 8002b810 */
    case ASSET_rscB:
        return 0;                  /* DAT_800b0db4 = id */
    case ASSET_rscE:
        Stream_SectionDone();      /* DAT_800b0db0 = -1 */
        g_CarsLoaded++;            /* DAT_800b0dbc */
        return 0;
    case ASSET_Ctrk:
        return Ctrk_OnLoaded(a);   /* 80062680 */
    case ASSET_Cnet:
        return g_IsFrontend ? 0 : Cnet_OnLoaded(a);   /* 8004c368 */
    case ASSET_Cact:
        return g_IsFrontend ? 0 : Cact_OnLoaded(a);   /* 8004942c */
    case NR_TAG('R', 'P', 'N', 'S'):
        return Actors_OnScriptPool(a);                /* 80049628 */
    case NR_TAG('M', 'c', 'I', 'c'):
        Asset_Register(a);         /* icono de la tarjeta (front-end +0x1DC) */
        return 1;
    case NR_TAG('C', 's', 'a', 'c'):
        return g_IsFrontend ? 0 : Csac_OnLoaded(a);   /* 800605f0 */
    case NR_TAG('C', 's', 'h', 'd'):
    case NR_TAG('C', 'v', 'k', 'h'):
    case NR_TAG('C', 'v', 'k', 'b'):
    case NR_TAG('C', 'e', 'n', 'g'):
        return Snd_OnAsset(a);     /* 80022908 */
    case NR_TAG('C', 's', 'f', 'x'):
        return Fx_OnCsfx(a);       /* 80035adc */
    case ASSET_Cctr:
        /* cambia el estado del nivel: id 0 -> 2, id 1 -> 4 (en juego). Port: solo durante la carga; el front-end
         * sigue leyendo su stream en el menu y, si ya ha salido (la salida de una partida en red puede llegar
         * antes de que acabe), no debe volver a "en juego" */
        if (g_GameState != GAMESTATE_LOADING && g_GameState != GAMESTATE_LOADED_PART)
            return 0;
        if (a->id == 0)
            g_GameState = GAMESTATE_LOADED_PART;
        else if (a->id == 1)
            g_GameState = GAMESTATE_RUNNING;
        return 0;
    case ASSET_Crle:
        return Fe_OnCrle(a);       /* 80092e08 */
    case ASSET_TxtR:
        return Fe_OnTxtR(a);       /* 80092dec */
    case ASSET_Cfnt:
        return Font_OnCfnt(a);     /* 80044690 */
    case ASSET_Bmdc:
    case ASSET_Lmdc:
    case ASSET_Tmdc:
    case ASSET_Gmdc:
        return Fe_OnMdc(a);
    default:
        return 0;
    }
}

void Asset_DispatchAll(void)
{
    NrAsset* a;
    while ((a = Stream_PopAsset()) != NULL)
    {
        if (!Asset_Dispatch(a))
            Asset_Free(a);   /* Pool_Free(asset) al final del switch original */
    }
}

void Asset_LogSummary(void)
{
    for (int i = 0; i < s_numTypes; i++)
    {
        char name[5];
        NR_TagToStr(s_types[i].type, name);
        NR_Log("  %-4s %5d assets %10d bytes\n", name, s_types[i].count, s_types[i].bytes);
    }
}
