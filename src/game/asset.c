#include "asset.h"
#include "cpag.h"
#include "globals.h"
#include "frontend.h"
#include "font.h"

#define MAX_TYPES 48

static struct
{
    uint32_t type;
    int      count;
    int      bytes;
} s_types[MAX_TYPES];
static int s_numTypes;

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

/* 80024710. Devuelve 1 si el manejador se queda con el asset. */
static int Asset_Dispatch(NrAsset* a)
{
    CountType(a);
    switch (a->type)
    {
    case ASSET_Cpag:
        return Cpag_OnLoaded(a);   /* 8006710c */
    case ASSET_Cctr:
        /* cambia el estado del nivel: id 0 -> 2, id 1 -> 4 (en juego) */
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
    /* TODO: Cobj 80013374, Ctrk 80062680, Cact 8004942c, Cnet 8004c368, Cfnt 80044690,
     *       sonido 80022908, menus del front-end (Bmdc/Lmdc/Gmdc/Tmdc/McIc/TkFE)... */
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
