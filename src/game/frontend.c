#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "libetc.h"
#include "nr.h"
#include "mem.h"
#include "gfx.h"
#include "globals.h"
#include "frontend.h"

FrontendState* g_Frontend;
short g_FadeDir;
short g_FadeLevel;

static_assert(sizeof(FrontendState) == 0x101c, "FrontendState debe medir 0x101C");
static_assert(offsetof(FrontendState, text) == 0x1e0, "offset de la tabla de textos");

/* 80079684 (parte del front-end; la camara 3D del garaje queda pendiente) */
void Frontend_InitCamera(void)
{
    if (!g_Frontend)
        g_Frontend = (FrontendState*)Pool_Alloc(sizeof(FrontendState), 2);
    memset(g_Frontend, 0, sizeof(FrontendState));
    g_GameInfo->unk5c = 0;
    g_GameInfo->unk0c = 1;
    g_GameInfo->unk08 = 1;
    g_GameInfo->unk58 = 1;
}

void Frontend_Free(void)
{
    Pool_Free(g_Frontend);
    g_Frontend = NULL;
}

/* 80079814. Solo la parte que decide la primera pantalla; el original tambien prepara las tablas de
 * menus, coches y textos (FUN_8007a684, Fe_GetString de cientos de ids) y los modelos del garaje. */
void Frontend_InitScreens(void)
{
    FrontendState* fe = g_Frontend;
    fe->screen = 2000;
    fe->prevScreen = -1;

    if (g_BootState == 0)
    {
        /* primer arranque: opciones por defecto (TODO), siguiente pantalla = menu principal */
        g_BootState = 1;
        fe->nextScreen = 100;
    }
    else
    {
        fe->nextScreen = 100;   /* TODO: volver de una carrera (FUN_80090fb4) */
    }

    fe->firstFrame = 1;
    fe->captureBg = 0;
    fe->unk09 = 0;
    fe->unk08 = 0;
    g_FadeDir = -1;
    g_FadeLevel = 0;
    fe->unk0a = 0;
    fe->idleTimer = 0;
}

/* 80089240 */
static void Fe_Screen2000(void)
{
    FrontendState* fe = g_Frontend;
    if (fe->captureBg)
    {
        /* 8008e480: copia el frame mostrado a (0x140,0)/(0x140,0x100) como fondo */
        RECT16 r;
        int y = Gfx_GetDisplayBufferY();
        setRECT(&r, 0, y, 0xa0, 0xf0);
        MoveImage(&r, 0x140, 0);
        setRECT(&r, 0xa0, y, 0xa0, 0xf0);
        MoveImage(&r, 0x140, 0x100);
        DrawSync(0);
    }
    Fe_DrawBackground(0x800);
}

static void Fe_ScreenPending(void)
{
    static short warned = -1;
    if (warned != g_Frontend->screen)
    {
        NR_Log("Frontend: pantalla %d sin portar\n", g_Frontend->screen);
        warned = g_Frontend->screen;
    }
    Fe_DrawBackground(0x800);
    FntPrint("\n\n\n\n\n\n\n\n\n\n\n\n\n     PANTALLA %d SIN PORTAR\n", g_Frontend->screen);
}

/* 8007a994. Pendiente: Camera_ApplyProjection, memoria (DAT_800b7118), avisos de mando
 * desconectado (FUN_8008ec2c) y sonidos de navegacion (FUN_8003f6fc). */
void Frontend_Update(void)
{
    FrontendState* fe = g_Frontend;

    if (g_FadeDir != 0)
    {
        if (g_FadeLevel < 0x100)
        {
            g_FadeLevel = (short)(g_FadeLevel + g_FrameDelta * 4);
            if (g_FadeLevel >= 0x100)
            {
                g_FadeLevel = 0x100;
                if (g_FadeDir == -1)
                    fe->fadeDone = 1;
            }
        }
        else if (fe->nextScreen == -1)
        {
            g_FadeDir = 0;
        }
        else
        {
            if (fe->nextScreen == 0)
            {
                short s = fe->prevScreen;
                fe->prevScreen = -1;
                fe->screen = s;
            }
            else
            {
                fe->prevScreen = fe->screen;
                fe->screen = fe->nextScreen;
            }
            fe->firstFrame = 1;
            fe->nextScreen = -1;
            fe->unk09 = 0;
            fe->unk08 = 0;
            g_FadeDir = 1;
            g_FadeLevel = 0;
        }
    }

    switch (fe->screen)
    {
    case 2000: Fe_Screen2000(); break;
    /* 100 FUN_8007b2ac, 200 FUN_8007b7fc, 300 FUN_8007f4a0, 400 FUN_8007bf00, 500 FUN_8007c540,
     * 600 FUN_80088edc, 700 FUN_8007cb6c, 800 FUN_8007d32c, 900 FUN_8007ddb4, 1000 FUN_8007f810,
     * 1100 FUN_80080cbc, 1200 FUN_80081a9c, 1300 FUN_800838f8, 1400 FUN_800855a0, 1500 FUN_80085d90,
     * 1600 FUN_8007ea64, 1700 FUN_80087130, 1800 FUN_80087fa8, 1900 FUN_8008a52c */
    default: Fe_ScreenPending(); break;
    }

    if (fe->firstFrame)
        fe->firstFrame = 0;
    if (fe->fadeDone)
    {
        fe->fadeDone = 0;
        fe->idleTimer = 0;
    }
    if (fe->nextScreen != -1 && g_FadeDir == 0)
    {
        /* LAB_8007afc4: empieza el fundido a negro hacia la siguiente pantalla */
        g_FadeDir = -1;
        g_FadeLevel = 0;
    }
    fe->idleTimer += g_FrameDelta;
}

/* Fundido de Frontend_MainLoop. El original redibuja el framebuffer como textura modulada por
 * (nivel / 2); aqui se aproxima con un TILE de mezcla sustractiva (B - F). */
void Frontend_DrawFade(void)
{
    if (g_FadeDir == 0)
        return;
    int level = g_FadeDir < 0 ? 0x100 - g_FadeLevel : g_FadeLevel;
    int bright = level / 2;                     /* 0x80 = imagen normal, 0 = negro */
    int sub = (0x80 - bright) * 2;
    if (sub <= 0)
        return;
    if (sub > 255)
        sub = 255;

    TILE* t = (TILE*)Gfx_AllocPrim(sizeof(TILE));
    DR_TPAGE* tp = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
    if (!t || !tp)
        return;
    setTile(t);
    setSemiTrans(t, 1);
    setRGB0(t, sub, sub, sub);
    setXY0(t, 0, 0);
    setWH(t, GFX_SCREEN_W, GFX_SCREEN_H);
    setDrawTPage(tp, 1, 0, getTPage(0, 2, 0, 0));   /* abr 2 = resta */
    Gfx_AddPrim(0, t);
    Gfx_AddPrim(0, tp);   /* addPrim inserta delante: el DR_TPAGE se procesa antes que el TILE */
}

/* 80090984 */
const char* Fe_GetString(int id)
{
    const FrontendState* fe = g_Frontend;
    if (!fe || !fe->text)
        return "";
    int i = 0;
    while (i < fe->textSize)
    {
        int n = 0;
        while (i < fe->textSize && fe->text[i] != ' ')
            n = n * 10 + (fe->text[i++] - '0');
        if (n == id)
            return fe->text + i + 1;
        while (i < fe->textSize && fe->text[i] != 0)
            i++;
        i++;
    }
    return "";
}

/* 8008c86c: rectangulo de VRAM de 16 bits como POLY_FT4 (tpage en modo 15 bits) */
void Fe_DrawVramRect(int vramX, int vramY, int w, int h, int x, int y, int depth, int semi)
{
    if (w <= 0 || h <= 0)
        return;
    POLY_FT4* p = (POLY_FT4*)Gfx_AllocPrim(sizeof(POLY_FT4));
    if (!p)
        return;
    setPolyFT4(p);
    setSemiTrans(p, semi);
    setRGB0(p, 0x80, 0x80, 0x80);
    int u = vramX & 0x3f, v = vramY & 0xff;
    setXY4(p, x, y, x + w, y, x, y + h, x + w, y + h);
    setUV4(p, u, v, u + w, v, u, v + h, u + w, v + h);
    p->tpage = getTPage(2, 0, vramX & ~0x3f, vramY & ~0xff);
    p->clut = 0;
    Gfx_AddPrimDepth(depth, p);
}

/* 8008b79c: las dos mitades de 160x240 de (0x140,0) y (0x140,0x100) */
void Fe_DrawBackground(int depth)
{
    Fe_DrawVramRect(0x140, 0, 0xa0, 0xf0, 0, 0, depth, 0);
    Fe_DrawVramRect(0x140, 0x100, 0xa0, 0xf0, 0xa0, 0, depth, 0);
}

/* 80092e08: Crle id 0 = imagen cruda para VRAM (0x240,0) (+ resto en (0x2C0,0));
 * otros ids = tabla que se queda en memoria */
int Fe_OnCrle(NrAsset* a)
{
    if (a->id == 0)
    {
        int rows = (a->size + 0xff) >> 8;
        int rows2 = 0;
        if (rows > 0x200)
        {
            rows2 = (a->size - 0x1ffc1) >> 6;
            rows = 0x200;
        }
        RECT16 r;
        setRECT(&r, 0x240, 0, 0x80, rows);
        LoadImage(&r, (u_long*)a->data);
        DrawSync(0);
        if (rows2 > 0)
        {
            setRECT(&r, 0x2c0, 0, 0x40, rows2);
            LoadImage(&r, (u_long*)(a->data + 0x20000));
            DrawSync(0);
        }
        if (g_Frontend)
        {
            g_Frontend->unk1e4 = 0;
            g_Frontend->crleRows = (short)rows;
            g_Frontend->crleRows2 = (short)rows2;
        }
        return 0;   /* el original hace Pool_Free del asset */
    }
    return 1;       /* TODO: tabla en DAT_800af950 + 0x9d8 */
}

/* 80092dec */
int Fe_OnTxtR(NrAsset* a)
{
    if (!g_Frontend)
        return 0;
    g_Frontend->text = (const char*)a->data;
    g_Frontend->textSize = (unsigned short)a->size;
    return 1;
}
