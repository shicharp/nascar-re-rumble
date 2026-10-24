/* Pantalla 1000: configuracion de mandos (8007f810) y Pad_SetupControls (8008b160), que pasa el esquema elegido a
 * los botones de cada jugador en carrera (g_PadBinds).
 *
 * Por jugador: tipo de mando (DAT_800b797d: 0 analogico, 1 digital, 2 / 3 neGcon, -1 ninguno), esquema
 * (DAT_800b797f: 0-2 cambio automatico, 3-5 manual; el mismo byte que elige el cambio del coche) y vibracion
 * (DAT_800b7981). Cada esquema tiene una tabla de etiquetas (15 bytes: la de arriba y 7 + 7 a los lados del
 * dibujo del mando, indices en la tabla de textos 800ab978) y una de botones (14 mascaras, 800abb34; neGcon
 * 800abc84). Izquierda / derecha cambian de esquema, cuadrado entre automatico y manual, L1 la vibracion (cuadro
 * 0x9C6), R1 calibra el neGcon y triangulo vuelve a la 400. El segundo jugador entra con X en su mando. */
#include <stdio.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "gfx.h"
#include "pad.h"
#include "font.h"
#include "exe.h"
#include "frontend.h"
#include "fe_internal.h"
#include "savegame.h"
#include "globals.h"
#include "lsc.h"

/* 8008b160. player -1: el tipo de cada mando conectado (y el esquema a 0 si no vale); si no, copia a g_PadBinds el
 * esquema 'preset' (-1 = el suyo) para el tipo 'kind' y aplica la vibracion. */
void Pad_SetupControls(int player, int kind, int preset)
{
    SaveExtra* x = &g_SaveExtra;
    if (player == -1)
    {
        for (int p = 0; p < 2; p++)
        {
            int t = Pad_Kind(p), k = 1;
            if (t == 7)
                k = 0;
            else if (t == 2)
                k = (int8_t)x->padKind[p] == 2 ? 2 : 3;
            if ((int8_t)x->padKind[p] != k)
            {
                x->padKind[p] = (uint8_t)k;
                if (g_Settings.transmission[p] > 5)
                    g_Settings.transmission[p] = 0;
            }
        }
        return;
    }
    uint32_t table;
    if (preset == -1)
    {
        preset = g_Settings.transmission[player & 1];
        table = kind == 2 ? 0x800abc84u : 0x800abb34u;
    }
    else
        table = kind > 1 && kind < 4 ? 0x800abc84u : 0x800abb34u;
    for (int i = 0; i < 14; i++)
        g_PadBinds[player & 1][i] = (uint32_t)Exe_S32(table + preset * 0x38 + i * 4);
    Pad_SetVibration(player, x->vibration[player & 1]);
}

static int8_t  s_padType[2];      /* fe +0x9DE: tipo de cada mando este frame (8001d840, 0 si no vale) */
static int8_t  s_padTypePrev[2];  /* fe +0x9DC */

/* texto de la tabla 800ab978 (punteros a cadenas del ejecutable) */
static const char* PadLabel(int i)
{
    const uint8_t* p = Exe_Ptr((uint32_t)Exe_S32(0x800ab978u + i * 4));
    return p ? (const char*)p : "";
}

/* dibujo de 8 bits en (vx, vy) de VRAM con la paleta de la fila 0xF0 + slot */
static void DrawPadImage(int vx, int vy, int u, int v, int w, int h, int slot, int x, int y)
{
    DR_TPAGE* tp = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
    SPRT* s = (SPRT*)Gfx_AllocPrim(sizeof(SPRT));
    if (!tp || !s)
        return;
    setDrawTPage(tp, 1, 1, getTPage(1, 0, vx, vy));
    Gfx_AddPrimDepth(0, tp);
    setSprt(s);
    setRGB0(s, 0x80, 0x80, 0x80);
    setXY0(s, x, y);
    setUV0(s, u, v);
    setWH(s, w, h);
    s->clut = (unsigned short)getClut(0x200, 0xf0 + slot);
    Gfx_AddPrimDepth(0, s);
}

/* 8007f810 */
void Fe_Screen1000(void)
{
    FrontendState* fe = g_Frontend;
    GameSettings* gs = &g_Settings;
    SaveExtra* x = &g_SaveExtra;
    const uint8_t* atlas = Fe_PadAtlas();
    int joinNow = 0;
    if (fe->firstFrame)
    {
        Fe_SetBackground(6);
        if (atlas)
            Fe_DecodeThumb(2, 2, 2, atlas, 4, 2, 0x380, 0);   /* (el stick del neGcon, para calibrar) */
        Pad_SetupControls(-1, 0, 0);
        fe->numPanels = gs->numPlayers;
        for (int i = 0; i < 2; i++)
        {
            s_padTypePrev[i] = -1;
            x->padKind[i] = 0xff;
            fe->modelOffset[i] = 0;
            fe->namePlayer[i] = 0;   /* estado: 0 normal, 1 vibracion, 2 calibrar, 3 tipo de neGcon */
        }
        fe->modelAnim[0] = fe->modelAnim[1] = -1;
        fe->popup = 0;
        gs->numPlayers = 1;
        if (fe->lastPort == 1)
            joinNow = 1;
    }
    Fe_DrawBackground(0x800);
    fe->marquee = 0;
    char help[2][128];
    int r, g, b;
    Fe_ItemColor(&r, &g, &b, 1);
    for (int i = 0; i < 2; i++)
    {
        int oy = i * 0x67;
        snprintf(help[i], sizeof(help[i]), "%s", HelpText(12));
        int t = Pad_Kind(i);
        s_padType[i] = (int8_t)(t < 0 ? 0 : t);
        int8_t* kind = (int8_t*)&x->padKind[i];
        if (s_padType[i] != s_padTypePrev[i])
        {
            if (i == 1 && gs->numPlayers == 1 && s_padTypePrev[1] == 0)
            {
                gs->numPlayers = 2;   /* se ha conectado el mando 2 */
                Snd_PlayUi(0x48, NULL);
            }
            s_padTypePrev[i] = s_padType[i];
            switch (s_padType[i])
            {
            case 4: *kind = 1; break;
            case 2: *kind = -2; break;   /* se pregunta que neGcon */
            case 7: *kind = 0; break;
            default:
                *kind = -1;
                fe->namePlayer[i] = 0;
                break;
            }
            if (*kind < 0)
            {
                if (*kind == -1 && i == 1)
                    gs->numPlayers = 1;
            }
            else if (atlas)
                Fe_DecodeThumb(2, 2, 2, atlas, *kind, i, 0x300, i * 0x50 + 0x100);
        }
        /* recuadro y pestaña (sube 11 pixeles al entrar) */
        Fe_DrawBox(0x14, 0x14, 0x14, 0x16, 0x24 + oy, 0x127, 0x74 + oy, 0);
        Fe_DrawFrame(0x3c, 0x3c, 0x14, 0x16, 0x23 + oy, 0x127, 0x74 + oy);
        int ty = oy - (fe->modelOffset[i] / 1000 - 0x24);
        Fe_DrawBox(0x14, 0x14, 0x14, 0x16, ty, 0x90, 0x27 + oy, 0);
        Fe_DrawLine(0x28143c3c, 0x17, ty - 1, 0x8f, ty - 1, 1);
        Fe_DrawLine(0x28143c3c, 0x17, ty, 0x18, ty, 1);
        Fe_DrawLine(0x28143c3c, 0x8e, ty, 0x8f, ty, 1);
        Fe_DrawLine(0x280e2a2a, 0x17, 0x23 + oy, 0x8f, 0x23 + oy, 1);
        Fe_DrawLine(0x28143c3c, 0x16, ty, 0x16, 0x27 + oy, 1);
        Fe_DrawLine(0x28143c3c, 0x8f, ty, 0x8f, 0x24 + oy, 1);
        if (*kind == -1 || gs->numPlayers <= i)
        {
            Font_Select(0);
            Fe_DrawTextRgb(0x1c, 0x1c, 0, Fe_GetString(0x9ca), 0x19, 0x18 + oy, 0);
            char msg[64];
            int my;
            if (*kind == -1)
            {
                snprintf(msg, sizeof(msg), "%s %d", Fe_GetString(Pad_Kind(i) == -1 ? 0xfa3 : 0xfa2), i + 1);
                Font_Select(1);
                my = oy + 0x3d;
            }
            else
            {
                Font_Select(1);
                Fe_DrawTextRgb(0x1c, 0x1c, 0, Fe_GetString(0xfa1), 0x9e, oy + 0x38, 1);
                Font_Select(0);
                snprintf(msg, sizeof(msg), "%s", Fe_GetString(s_padType[i] == 2 ? 0x9c5 : 0x9c4));
                my = oy + 0x4c;
            }
            Fe_DrawTextRgb(r / 2 + 0xc, g / 2 + 10, b / 2 + 10, msg, 0x9e, my, 1);
            continue;
        }
        static const uint32_t labels[4] = { 0x800ab9c4u, 0x800aba20u, 0x800aba7cu, 0x800abad8u };
        uint8_t preset = gs->transmission[i];
        if (*kind < 0 || *kind > 3)
        {
            if (fe->namePlayer[i] != 3)
            {
                fe->champReady[i] = 1;
                fe->namePlayer[i] = 3;
            }
        }
        Font_Select(0);
        char title[64];
        snprintf(title, sizeof(title), "%s %d   \x1b%s", Fe_GetString(0x9cb), preset < 3 ? preset + 1 : preset - 2,
                 Fe_GetString(preset < 3 ? 0x9cd : 0x9ce));
        Fe_DrawTextRgb(0x1c, 0x1c, 0, title, 0x19, 0x18 + oy, 0);
        int st = fe->namePlayer[i];
        if (st != 2 && st != 3)
        {
            int vib = Pad_HasVibration(i);
            if (*kind == 2)
                snprintf(help[i] + strlen(help[i]), sizeof(help[i]) - strlen(help[i]), "    %s", HelpText(vib ? 15 : 14));
            else if (vib)
                snprintf(help[i] + strlen(help[i]), sizeof(help[i]) - strlen(help[i]), "    %s", HelpText(13));
            DrawPadImage(0x300, 0x100, 0, i * 0x50, 0x74, 0x50, i, 0x65, 0x24 + oy);
            const uint8_t* tab = Exe_Ptr(labels[*kind] + preset * 0xf);
            if (tab)
            {
                for (int k = 1; k < 8; k++)
                {
                    Fe_DrawTextRgb(0x17, 0x17, 0x17, PadLabel(tab[k]), 0x62, oy + 0x25 + (k - 1) * 0xb, 2);
                    Fe_DrawTextRgb(0x17, 0x17, 0x17, PadLabel(tab[k + 7]), 0xdb, oy + 0x25 + (k - 1) * 0xb, 0);
                }
                Fe_DrawTextRgb(0x17, 0x17, 0x17, PadLabel(tab[0]), 0x9e, oy + 0x25, 1);
            }
        }
        if (fe->namePlayer[i] == 0 && Pad_Kind(0) > 0)
            Fe_DrawHelp(i, help[i], 0x92, 0x18 + oy, 0x92, 0x127, 2);
    }

    Fe_ReadPads();
    if (fe->namePlayer[1] == 0 && (Fe_PadPressed(PADB_CROSS, 1, 1) || joinNow) && gs->numPlayers == 1)
    {
        gs->numPlayers = 2;   /* el jugador 2 entra */
        Snd_PlayUi(0x48, NULL);
    }
    for (int i = 0; i < gs->numPlayers && i < 2; i++)
    {
        int y = 0x2b + i * 0x67;
        int8_t* kind = (int8_t*)&x->padKind[i];
        int st = fe->namePlayer[i];
        if (st == 1)
        {
            /* vibracion si / no (solo si contesta alguien: lastPort) */
            fe->lastPort = -1;
            int res = Fe_MsgBox(0, y, Fe_GetString(0x9c6), (uint8_t*)&fe->champReady[i], i);
            if (res == -1)
                continue;
            if (fe->lastPort >= 0)
            {
                Pad_SetVibration(i, res != 0);
                if (res != 0)
                    Pad_TestVibration(i);
                x->vibration[i] = (uint8_t)(res != 0);
            }
            fe->namePlayer[i] = 0;
        }
        else if (st == 2)
            fe->namePlayer[i] = 0;   /* (calibrar el neGcon: no hay neGcon en PC) */
        else if (st == 3)
        {
            if (s_padType[i] == 2)
            {
                int res = Fe_MsgBox(0, y, Fe_GetString(0x9c7), (uint8_t*)&fe->champReady[i], i);
                if (res != -1)
                {
                    fe->namePlayer[i] = 0;
                    *kind = res == 0 ? 2 : 3;
                    if (atlas)
                        Fe_DecodeThumb(2, 2, 2, atlas, *kind, i, 0x300, i * 0x50 + 0x100);
                }
            }
            else
                fe->namePlayer[i] = 0;
        }
        else
        {
            fe->namePlayer[i] = 0;
            int d = Fe_PadPressed(PADB_LEFT, i, 1) ? -1 : Fe_PadPressed(PADB_RIGHT, i, 1) ? 1 : 0;
            uint8_t* preset = &gs->transmission[i];
            if (d)
            {
                *preset = (uint8_t)(*preset < 3 ? Fe_Clamp(*preset + d, 0, 2) : Fe_Clamp(*preset + d, 3, 5));
                fe->valueDir = 1;
            }
            else if (Fe_PadPressed(PADB_TRIANGLE, i, 1))
                fe->nextScreen = 400;
            else if (Fe_PadPressed(PADB_SQUARE, i, 1))
            {
                *preset = (uint8_t)(*preset + (*preset > 2 ? -3 : 3));   /* automatico / manual */
                fe->valueDir = 1;
            }
            else if (Fe_PadPressed(PADB_L1, i, 1))
            {
                if (Pad_HasVibration(i))
                {
                    fe->champReady[i] = (char)x->vibration[i];
                    fe->namePlayer[i] = 1;
                    Snd_PlayUi(0x48, NULL);
                }
            }
            else if (Fe_PadPressed(PADB_R1, i, 1) && s_padType[i] == 2)
            {
                fe->namePlayer[i] = 2;
                Snd_PlayUi(0x48, NULL);
            }
        }
    }
    Fe_PadRepeatReset();
    for (int i = 0; i < 2; i++)
    {
        if (g_FadeDir != 0)
            continue;
        fe->modelOffset[i] -= g_FrameDelta * fe->modelAnim[i] * 0xa0;
        if (fe->modelOffset[i] < 0)
            fe->modelOffset[i] = 0;
        else if (fe->modelOffset[i] < 0x2af9)
            continue;
        else
            fe->modelOffset[i] = 11000;
        fe->modelAnim[i] = 0;
    }
    if (fe->fadeDone)
        gs->numPlayers = fe->numPanels;
}

/* ---------------------------------------------------------------- pantalla 1900: Hints and Tips */

/* 8008a52c: las tres imagenes CW\HelpScrn\Tips1..3.lsc (fe +0xD = 0xFA..0xFC, para que el fondo no se recargue);
 * X pasa a la siguiente y despues de la ultima vuelve a la 400, triangulo vuelve ya. */
void Fe_Screen1900(void)
{
    FrontendState* fe = g_Frontend;
    if (fe->firstFrame)
    {
        if (fe->bgIndex < 0xfa)
            fe->bgIndex = 0xfa;
        char path[64];
        snprintf(path, sizeof(path), "CW\\HelpScrn\\Tips%d.lsc", fe->bgIndex - 0xf9);
        Lsc_LoadToVram(path, 0);
    }
    Fe_DrawBackground(0x800);
    Fe_DrawHelp(0, HelpText(30), 0xe, 0xd5, 0x23, 0x109, 0);
    Fe_ReadPads();
    if (Fe_PadPressed(PADB_TRIANGLE, -1, 1))
        fe->nextScreen = 400;
    else if (Fe_PadPressed(PADB_CROSS, -1, 1))
    {
        fe->bgIndex++;
        fe->nextScreen = fe->bgIndex == 0xfd ? 400 : 1900;
    }
    Fe_PadRepeatReset();
}
