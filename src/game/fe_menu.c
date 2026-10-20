/* Widgets del front-end y pantallas de menu 100-500. */
#include <stdio.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "mem.h"
#include "gfx.h"
#include "pad.h"
#include "font.h"
#include "globals.h"
#include "frontend.h"
#include "fe_internal.h"
#include "snd.h"
#include "snd_int.h"

/* ---------------------------------------------------------------- primitivas 2D */

int Fe_BaseDepth(void)
{
    return g_Frontend->screen == 900 ? 2000 : 0;
}

/* 8008e614: rectangulo relleno (opcionalmente semitransparente al 50%) */
void Fe_DrawBox(int r, int g, int b, int x0, int y0, int x1, int y1, int semi)
{
    int depth = Fe_BaseDepth();
    if (semi)
    {
        DR_TPAGE* tp = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
        if (tp)
        {
            setDrawTPage(tp, 1, 1, 0);
            Gfx_AddPrimDepth(depth, tp);
        }
    }
    TILE* t = (TILE*)Gfx_AllocPrim(sizeof(TILE));
    if (!t)
        return;
    setTile(t);
    setSemiTrans(t, semi);
    setRGB0(t, r, g, b);
    setXY0(t, x0, y0);
    setWH(t, x1 - x0, y1 - y0);
    Gfx_AddPrimDepth(depth, t);
}

/* 80079784: linea horizontal o vertical de grosor 'thick' como POLY_F4. color = 0x28BBGGRR. */
void Fe_DrawLine(uint32_t color, int x0, int y0, int x1, int y1, int thick)
{
    POLY_F4* p = (POLY_F4*)Gfx_AllocPrim(sizeof(POLY_F4));
    if (!p)
        return;
    setPolyF4(p);
    setRGB0(p, color & 0xff, (color >> 8) & 0xff, (color >> 16) & 0xff);
    if (x0 == x1)
        setXY4(p, x0, y0, x1 + thick, y0, x0, y1, x1 + thick, y1);
    else
        setXY4(p, x0, y0, x1, y0, x0, y0 + thick, x1, y0 + thick);
    Gfx_AddPrimDepth(0, p);
}

uint32_t Rgb(int r, int g, int b)
{
    return 0x28000000u | (uint32_t)b << 16 | (uint32_t)g << 8 | (uint32_t)r;
}

/* 8008e51c: contorno rectangular */
void Fe_DrawOutline(int r, int g, int b, int x0, int y0, int x1, int y1, int thick)
{
    uint32_t c = Rgb(r, g, b);
    Fe_DrawLine(c, x0, y0, x1, y0, thick);
    Fe_DrawLine(c, x1 + 1 - thick, y0, x1 + 1 - thick, y1 + 1, thick);
    Fe_DrawLine(c, x0, y1 + 1 - thick, x1, y1 + 1 - thick, thick);
    Fe_DrawLine(c, x0, y1, x0, y0, thick);
}

/* 8008e75c: marco biselado con esquinas recortadas (color claro y su sombra a mitad) */
void Fe_DrawFrame(int r, int g, int b, int x0, int y0, int x1, int y1)
{
    uint32_t c = Rgb(r, g, b);
    uint32_t d = Rgb(r / 2, g / 2, (b * 10) / 15);
    Fe_DrawLine(c, x0 + 3, y0, x1 - 2, y0, 1);
    Fe_DrawLine(c, x0, y0 + 3, x0, y1 - 2, 1);
    Fe_DrawLine(c, x1, y0 + 3, x1, y1 - 2, 1);
    Fe_DrawLine(c, x0 + 3, y1, x1 - 2, y1, 1);
    Fe_DrawLine(c, x0 + 1, y0 + 1, x0 + 4, y0 + 1, 1);
    Fe_DrawLine(c, x1 - 3, y0 + 1, x1, y0 + 1, 1);
    Fe_DrawLine(c, x0 + 1, y1 - 1, x0 + 4, y1 - 1, 1);
    Fe_DrawLine(c, x1 - 3, y1 - 1, x1, y1 - 1, 1);
    Fe_DrawLine(c, x0 + 1, y0 + 2, x0 + 1, y0 + 4, 1);
    Fe_DrawLine(c, x1 - 1, y0 + 2, x1 - 1, y0 + 4, 1);
    Fe_DrawLine(c, x0 + 1, y1 - 3, x0 + 1, y1 - 1, 1);
    Fe_DrawLine(c, x1 - 1, y1 - 3, x1 - 1, y1 - 1, 1);
    Fe_DrawLine(d, x0 + 3, y0 - 1, x1 - 2, y0 - 1, 1);
    Fe_DrawLine(d, x0 - 1, y0 + 3, x0 - 1, y1 - 2, 1);
    Fe_DrawLine(d, x1 + 1, y0 + 3, x1 + 1, y1 - 2, 1);
    Fe_DrawLine(d, x0 + 3, y1 + 1, x1 - 2, y1 + 1, 1);
    Fe_DrawLine(d, x0 + 1, y0, x0 + 3, y0, 1);
    Fe_DrawLine(d, x1 - 2, y0, x1, y0, 1);
    Fe_DrawLine(d, x0 + 1, y1, x0 + 3, y1, 1);
    Fe_DrawLine(d, x1 - 2, y1, x1, y1, 1);
    Fe_DrawLine(d, x0, y0 + 1, x0, y0 + 3, 1);
    Fe_DrawLine(d, x1, y0 + 1, x1, y0 + 3, 1);
    Fe_DrawLine(d, x0, y1 - 2, x0, y1, 1);
    Fe_DrawLine(d, x1, y1 - 2, x1, y1, 1);
}

/* ---------------------------------------------------------------- mando */

/* 8008a7f4: botones del frame; X, triangulo y cuadrado cuentan solo al pulsarlos */
void Fe_ReadPads(void)
{
    FrontendState* fe = g_Frontend;
    const uint32_t once = PADB_CROSS | PADB_TRIANGLE | PADB_SQUARE;
    for (int i = 0; i < 2; i++)
    {
        fe->pad[i] = 0;
        if (g_FadeDir == 0 && !fe->padDisabled && Pad_GetType(0) > 0)
        {
            uint32_t b = Pad_GetButtons(i);
            fe->pad[i] = b;
            if (!(b & once))
                fe->latch[i] = 0;
            else if (!fe->latch[i])
                fe->latch[i] = 1;
            else
                fe->pad[i] &= ~once;
        }
        fe->held[i] = 0;
    }
}

/* 8008a994: pulsacion con autorrepeticion cada 100 ticks; port -1 = cualquiera de los dos */
int Fe_PadPressed(uint32_t mask, int port, int repeat)
{
    FrontendState* fe = g_Frontend;
    int first = port, last = port, hit = 0;
    if (port == -1)
    {
        first = 0;
        last = 1;
    }
    for (int p = first; p <= last; p++)
    {
        if (fe->pad[p] & mask)
        {
            fe->held[p] = 1;
            if (g_FrameTime - fe->repeatTime[p] > 99 || !repeat)
            {
                fe->repeatTime[p] = g_FrameTime;
                hit = 1;
                if (mask == PADB_CROSS)
                    fe->lastPort = (char)p;
            }
            fe->idleTimer = 0;
        }
    }
    return hit;
}

/* 8008aa90: sin tecla mantenida, la siguiente pulsacion dispara al momento */
void Fe_PadRepeatReset(void)
{
    for (int i = 0; i < 2; i++)
        if (!g_Frontend->held[i])
            g_Frontend->repeatTime[i] = g_FrameTime - 100;
}

/* 8008aad8 */
int Fe_Clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* 8008a66c: arriba/abajo mueven el cursor; X y triangulo eligen pantalla. Devuelve !=0 si hubo accion. */
static int Fe_MenuNavigate(MenuItem* items, int count)
{
    FrontendState* fe = g_Frontend;
    int port = g_Settings.numPlayers == 2 ? -1 : 0;

    if (Fe_PadPressed(PADB_UP, port, 1))
        fe->cursor = (uint8_t)(fe->cursor == 0 ? count - 1 : fe->cursor - 1);
    else if (Fe_PadPressed(PADB_DOWN, port, 1))
        fe->cursor = (uint8_t)(fe->cursor == count - 1 ? 0 : fe->cursor + 1);
    else if (Fe_PadPressed(PADB_CROSS, port, 1))
    {
        if (items[fe->cursor].onSelect < 0)
            return -1;
        fe->nextScreen = items[fe->cursor].onSelect;
        return 1;
    }
    else if (Fe_PadPressed(PADB_TRIANGLE, port, 1))
    {
        if (items[fe->cursor].onBack < 0)
            return 0;
        fe->nextScreen = items[fe->cursor].onBack;
        return 1;
    }
    else
        return 0;

    fe->descAnim = (short)0xf000;   /* el cursor se movio: reinicia el zoom de la descripcion */
    fe->descAnimOn = 1;
    return 1;
}

/* ---------------------------------------------------------------- opciones de menu */

/* 8007a780: texto de un valor; un prefijo "[-NN]" ajusta el ancho de la caja de descripcion */
static void Fe_SetItemValue(MenuItem* item, int index, const char* s)
{
    Font_Select(0);
    int adj = 0;
    if (s[0] == '[' && s[4] == ']')
    {
        adj = (s[2] - '0') * 10 + (s[3] - '0');
        if (s[1] == '-')
            adj = -adj;
    }
    if (adj)
        s += 5;
    MenuValue* v = &item->values[index];
    v->text = s;
    v->w = 100;
    int h = Font_DrawWrapped(0, 0, 0x5e, 0x140, s, 0x210) + 6;
    v->h = (short)h;
    if ((int)strlen(s) > 9)
    {
        int cur = 100, next = 100;
        while (h < (v->w * 2) / 3)
        {
            v->w = (short)cur;
            next = cur - 5;
            v->h = (short)h;
            h = Font_DrawWrapped(0, 0, cur - 0xb, 0x140, s, 0x210) + 6;
            cur = next;
        }
        if (adj)
        {
            v->w = (short)(next + adj);
            v->h = (short)(Font_DrawWrapped(0, 0, next + adj - 6, 0x140, s, 0x210) + 6);
        }
    }
}

/* 8007a684 */
void Fe_InitMenuItem(MenuItem* item, const MenuDef* def, int x, int y)
{
    item->text = Fe_GetString(def->text);
    item->help = Fe_GetString(3000 + def->help);    /* DAT_800b6e78[help] */
    item->values = NULL;
    if (def->numValues)
    {
        item->values = (MenuValue*)Pool_Alloc(def->numValues * (int)sizeof(MenuValue), 2);
        for (int i = def->numValues - 1; i >= 0; i--)
            Fe_SetItemValue(item, i, Fe_GetString(def->text + i + 1));
    }
    item->x = (short)x;
    item->y = (short)y;
    item->onSelect = def->onSelect;
    item->onBack = def->onBack;
    item->type = (char)def->type;
    item->width = def->width;
    item->active = 0;
}

/* 8008abf8 */
void Fe_ResetMenu(MenuItem* items, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (items[i].active)
            g_Frontend->cursor = g_Frontend->prevCursor = (uint8_t)i;
        items[i].anim = items[i].type == 2 ? 10000 : 0;
    }
}

/* 8008ac5c: color de la opcion (seleccionada = pulso) */
void Fe_ItemColor(int* r, int* g, int* b, int mode)
{
    unsigned f = g_MenuFrame;
    if (mode == 1)
    {
        int v = (f % 56 < 28) ? (int)(f % 28) : 27 - (int)(f % 28);
        *r = *g = *b = v;
    }
    else if (mode == 2)
    {
        int v = (int)((f >> 1) % 21);
        if ((f >> 1) % 42 < 21)
        {
            *r = v + 8; *g = v; *b = v;
        }
        else
        {
            *r = 0x1c - v; *g = 0x14 - v; *b = 0x14 - v;
        }
    }
    else
    {
        *r = 0x15; *g = 0x15; *b = 0;
    }
}

/* 8008b338 */
static void Fe_DrawMenuItem(int r, int g, int b, MenuItem* items, int index, int count)
{
    FrontendState* fe = g_Frontend;
    int lit = !(r == -1 && g == -1 && b == -1);
    int pad = fe->screen == 300 ? 0xe : (fe->screen == 600 ? 0xb : 0);
    MenuItem* it = &items[index];
    int y = it->y, x = it->x - fe->slideX;
    int unselected = (r == 0x15 && g == 0x15 && b == 0);

    Fe_DrawBox(0, 0, 0, x - (pad + 3), y - 3, x + 0x69, y + 0xe, 0);
    it->active = !unselected;
    Font_Select(1);
    if (unselected || !lit)
    {
        Font_DrawString(it->text, x - (pad - 1), y - 5, 0x8c63);
        Font_DrawString(it->text, x - pad, y - 6, lit ? 0xfbde : 0xad6b);
        if (it->type == 1 && it->anim > 0)
        {
            it->anim = (short)(it->anim - g_FrameDelta * 0x82);
            if (it->anim < 0)
                it->anim = 0;
        }
    }
    else
    {
        Fe_DrawBox(0x4a, 0, 0, x - (pad + 3), y - 2, x + 0x69, y + 0xd, 0);
        Font_DrawString(it->text, x - (pad - 1), y - 5, 0);
        Font_DrawString(it->text, x - pad, y - 6,
                        (((r / 3 + 0x15) * 0x400 | (g / 3 + 0x15) * 0x20) & 0xffe3) | 0x8003);
        if (it->type == 1 && it->anim < 10000)
        {
            if (it->anim == 0)
                Snd_PlayUi(0x51, NULL);
            it->anim = (short)(it->anim + g_FrameDelta * 0x82);
            if (it->anim > 10000)
                it->anim = 10000;
        }
    }
    if (index == count - 1)
        Fe_DrawFrame(r + 0xa0, 0xa0, 0xb4, items[0].x - (pad + 4), items[0].y - 3, it->x + 0x69, it->y + 0xd);
}

/* 8008d064: caja con el valor de un ajuste a la derecha de la opcion */
static void Fe_DrawOptionValue(int r, int g, int b, MenuItem* it, const char* value, int arrows)
{
    FrontendState* fe = g_Frontend;
    int y = it->y, x = it->x + 9;
    int open = (it->width * it->anim) / 10000;
    int sx = fe->slideX;
    unsigned textColor;
    int add;
    if (r == 0x15 && g == 0x15 && b == 0)
    {
        r = g = b = 0x80;
        textColor = 8;
        add = 0x28;
    }
    else
    {
        r <<= 3; g <<= 3; b <<= 3;
        textColor = 9;
        add = 0;
    }
    Fe_DrawLine(Rgb(r + add, g + add, b + add), x - (sx - 0x61), y + 4, x - (sx - 0x6a), y + 4, 1);
    Fe_DrawLine(Rgb(r, g, b), x - (sx - 0x61), y + 5, x - (sx - 0x6a), y + 5, 2);
    Fe_DrawLine(Rgb(r / 2, g / 2, b / 2), x - (sx - 0x61), y + 7, x - (sx - 0x6a), y + 7, 1);
    if (open == 0)
        Fe_DrawLine(0x28464646, x - (sx - 0x68), y - 1, x - (sx - 0x68), y + 0xb, 1);
    else
        Fe_DrawBox(0x3c, 0x3c, 0x3c, x - (sx - 0x69), y - 2, x + open + 0x69 - sx, y + 0xb, 1);
    Fe_DrawOutline(r, g, b, x - (sx - 0x69), y - 2, x + open + 0x69 - sx, y + 0xb, 1);

    int lx, ly0, ly1;
    uint32_t lc;
    if (it->type == 1)
    {
        int e = x - (sx - 0x6a) + open;
        Fe_DrawLine(0x28e4e4e4, e, y - 2, e, y + 0xc, 1);
        Fe_DrawLine(0x28aaaaaa, e + 1, y - 2, e + 1, y + 0xc, 2);
        Fe_DrawLine(0x28787878, e + 3, y - 1, e + 3, y + 0xb, 2);
        lc = 0x288c8c96;
        lx = e + 5;
        ly0 = y;
        ly1 = y + 10;
    }
    else
    {
        uint32_t c = Rgb(r, g, b);
        Fe_DrawLine(c, x - (sx - 0x66), y + 3, x - (sx - 0x66), y + 8, 1);
        Fe_DrawLine(c, x - (sx - 0x67), y + 1, x - (sx - 0x67), y + 10, 1);
        lc = Rgb(r / 2 + 8, g / 2 + 8, b / 2 + 8);
        lx = x - (sx - 0x68);
        ly0 = y;
        ly1 = y + 0xb;
    }
    Fe_DrawLine(lc, lx, ly0, lx, ly1, 1);

    Font_Select(0);
    if (arrows)
        Font_DrawString("$", x - (sx - 0x4a), y - 1, textColor);   /* icono izquierda/derecha */
    Font_Select(1);

    /* recorta el texto al ancho abierto de la caja */
    char buf[64] = "";
    int n = (int)strlen(value), i = 0, w, h;
    for (; i < n && i < (int)sizeof(buf) - 1; i++)
    {
        buf[i] = value[i];
        buf[i + 1] = 0;
        Font_Measure(buf, &w, &h);
        if (!(x + w + 0x6e < x - (sx - 0x6a) + open))
        {
            buf[i] = 0;
            break;
        }
    }
    Font_DrawString(buf, x - (sx - 0x6e), y - 7, textColor);
}

/* 8008d5f4: barra de ayuda inferior; si no cabe se desplaza con los bordes difuminados.
 * port: mando cuyo tipo se comprueba (1 = el segundo); edges: 0 ambos degradados, 1 solo el izquierdo,
 * 2 solo el derecho, 3 ninguno. Pendiente: sustitucion de iconos para el mando neGcon (tipo 2). */
void Fe_DrawHelp(int port, const char* text, int x, int y, int clipX0, int clipX1, int edges)
{
    FrontendState* fe = g_Frontend;
    if (!text || Pad_GetType(port == 1) <= 0 || fe->padDisabled || Pad_GetType(0) <= 0)
        return;
    strncpy(fe->textBuf, text, sizeof(fe->textBuf) - 1);
    Font_Select(0);
    int w, h;
    Font_Measure(fe->textBuf, &w, &h);
    if (clipX1 != clipX0 && fe->marquee)
        return;
    if (clipX1 - clipX0 >= w || clipX1 - clipX0 <= 0)
    {
        Font_DrawString(fe->textBuf, x, y, 9);
        return;
    }
    w += 6;
    int sx = clipX0 - (g_FrameTime * 10 / 0x4c) % w;
    Font_DrawClipped(fe->textBuf, sx, y, clipX0, clipX1, 9);
    Font_DrawClipped(fe->textBuf, sx + w, y, clipX0, clipX1, 9);

    /* degradados que oscurecen los extremos (mezcla 2 = resta) */
    if (edges < 3)
    {
        DR_TPAGE* tp = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
        if (tp)
        {
            setDrawTPage(tp, 1, 1, getTPage(0, 2, 0, 0));
            Gfx_AddPrimDepth(0, tp);
        }
    }
    for (int side = 0; side < 2; side++)
    {
        if ((side == 0 && edges >= 2) || (side == 1 && edges != 0 && edges != 2))
            continue;
        POLY_G4* p = (POLY_G4*)Gfx_AllocPrim(sizeof(POLY_G4));
        if (!p)
            return;
        setPolyG4(p);
        setSemiTrans(p, 1);
        int xa = side ? clipX1 - 0xf : clipX0 - 2, xb = side ? clipX1 + 2 : clipX0 + 0xf;
        int ca = side ? 0 : 0xff, cb = side ? 0xff : 0;
        setRGB0(p, ca, ca, ca); setRGB1(p, cb, cb, cb);
        setRGB2(p, ca, ca, ca); setRGB3(p, cb, cb, cb);
        setXY4(p, xa, y, xb, y, xa, y + 0xb, xb, y + 0xb);
        Gfx_AddPrimDepth(0, p);
    }
}

/* 8008b6b4: caja de descripcion arriba a la derecha */
static void Fe_DrawDescription(int w, int h, const char* text)
{
    if (!w || !h || !text)
        return;
    Fe_DrawBox(0x78, 0x78, 0x78, 0x128 - w, 0x1a, 0x12e, h + 0x19, 1);
    Fe_DrawFrame(0xa0, 0xa0, 0xaa, 0x127 - w, 0x19, 0x12d, h + 0x19);
    Font_Select(0);
    Font_DrawWrapped(0x12d - w, 0x1b, 0x127, h + 0x15, text, 0xe702);
}

/* ---------------------------------------------------------------- cuadro de mensaje */

/* 8008cb88: cuadro de mensaje. Sin 'yesNo' es de tipo OK; con 'yesNo' es Si/No (izquierda/derecha
 * cambian *yesNo). Devuelve -1 mientras sigue abierto, 1 si se acepta y 0 si se cancela. */
int Fe_MsgBox(int x, int y, const char* text, uint8_t* yesNo, int port)
{
    FrontendState* fe = g_Frontend;
    fe->marquee = 0;
    if (!x) x = 0x5f;
    if (!y) y = 0x51;
    Fe_DrawBox(0x1e, 0x1e, 0x1e, x, y, x + 0x82, y + 0x40, 0);
    Fe_DrawBox(0, 0, 0, x, y + 0x35, x + 0x82, y + 0x40, 0);
    Fe_DrawHelp(port == 1, Fe_GetString(yesNo ? 3028 : 3029), x + 6, y + 0x35, x + 5, x + 0x7e, 0);   /* DAT_800b6ee8/ec */
    Fe_DrawLine(0x28323232, x, y + 0x35, x + 0x82, y + 0x35, 1);
    Fe_DrawFrame(0x6e, 0x6e, 0x6e, x, y, x + 0x82, y + 0x40);
    Font_Select(0);
    int r, g, b;
    Fe_ItemColor(&r, &g, &b, 1);
    unsigned hilite = (unsigned)b | 0xe300;
    Font_DrawWrapped(x + 0xd, y + 2, x + 0x70, y + 0x33, text, 9);
    Font_Select(1);

    const char* last;
    int lastX;
    if (!yesNo)
    {
        Fe_DrawBox(0, 0, 0, x + 0x34, y + 0x22, x + 0x50, y + 0x2e, 0);
        Fe_DrawFrame(0x30, 0x30, 0x3a, x + 0x33, y + 0x22, x + 0x51, y + 0x2e);
        last = Fe_GetString(0xfa7);                                         /* Ok */
        lastX = x + 0x3b;
    }
    else
    {
        if (*yesNo == 0)
        {
            Fe_DrawBox(0, 0, 0, x + 0x47, y + 0x22, x + 0x61, y + 0x2e, 0);
            Fe_DrawFrame(0x30, 0x30, 0x3a, x + 0x46, y + 0x22, x + 0x62, y + 0x2e);
        }
        else
        {
            Fe_DrawBox(0, 0, 0, x + 0x1f, y + 0x22, x + 0x3b, y + 0x2e, 0);
            Fe_DrawFrame(0x30, 0x30, 0x3a, x + 0x1e, y + 0x22, x + 0x3c, y + 0x2e);
        }
        Font_DrawString(Fe_GetString(0xfa5), x + 0x24, y + 0x1d, *yesNo ? hilite : 0xe31a);   /* Yes */
        last = Fe_GetString(0xfa6);                                                         /* No */
        lastX = x + 0x4d;
        if (*yesNo)
            hilite = 0xe31a;
    }
    Font_DrawString(last, lastX, y + 0x1d, hilite);

    int result = -1;
    if (Fe_PadPressed(PADB_CROSS, port, 1))
        result = (!yesNo || *yesNo) ? 1 : 0;
    else if (Fe_PadPressed(PADB_TRIANGLE, port, 1))
        result = 0;
    if (result != -1)
        Snd_PlayUi(result ? 0x48 : 0x5c, NULL);
    else if (yesNo)
    {
        if ((Fe_PadPressed(PADB_LEFT, port, 1) && !*yesNo) || (Fe_PadPressed(PADB_RIGHT, port, 1) && *yesNo))
        {
            *yesNo = !*yesNo;
            fe->valueDir = 1;
        }
    }
    fe->idleTimer = 0;
    fe->marquee = result == -1;
    return result;
}

/* ---------------------------------------------------------------- menus */

/* Lo que repiten todas las pantallas de menu: descripcion (con zoom), ayuda, opciones y sus valores.
 * valueOf(i) devuelve el texto del valor de la opcion i (o NULL); descIndex elige la descripcion. */
static int s_narrowR;   /* solo el menu principal: la r de la fuente 1 un pixel mas estrecha en las opciones */

static void Fe_DrawMenuScreen(MenuItem* items, int count, const char* (*valueOf)(int), int descIndex)
{
    FrontendState* fe = g_Frontend;
    Fe_DrawBackground(20000);
    Fe_DrawHelp(0, items[fe->cursor].help, 0x21, 0xd5, 0x23, 0x109, 0);
    if (fe->descAnim > 0x1e && items[fe->cursor].values)
    {
        const MenuValue* v = &items[fe->cursor].values[descIndex];
        Fe_DrawDescription((v->w * fe->descAnim) / 1024, (v->h * fe->descAnim) / 1024, v->text);
    }
    if (s_narrowR)
    {
        Font_Select(1);
        Font_AdjustAdvance('r', -1);
    }
    for (int i = 0; i < count; i++)
    {
        int r, g, b;
        Fe_ItemColor(&r, &g, &b, i == fe->cursor);
        Fe_DrawMenuItem(r, g, b, items, i, count);
        const char* value = valueOf ? valueOf(i) : NULL;
        if (value && *value)
            Fe_DrawOptionValue(r, g, b, &items[i], value, 1);
    }
    if (s_narrowR)
    {
        Font_Select(1);
        Font_AdjustAdvance('r', 1);
    }
}

static int Fe_LeftRight(int port, int repeat)
{
    if (Fe_PadPressed(PADB_LEFT, port, repeat))
        return -1;
    if (Fe_PadPressed(PADB_RIGHT, port, repeat))
        return 1;
    return 0;
}

static int PlayersPort(void)
{
    return g_Settings.numPlayers == 1 ? 0 : -1;
}

/* ---------------------------------------------------------------- pantalla 100: menu principal */

/* DAT_800b6fc8 = 0x7A.., DAT_800b6fe8 = 0x84.., DAT_800b6ff8 = 0x8E.. */
static const char* MainMenuValue(int item)
{
    switch (item)
    {
    case 2: return Fe_GetString(0x7a + g_Settings.racingClass);
    case 3: return Fe_GetString(0x84 + g_Settings.powerUps);
    case 4: return Fe_GetString(0x8e + g_Settings.hallType);
    default: return NULL;
    }
}

/* 8007b2ac */
void Fe_Screen100(void)
{
    FrontendState* fe = g_Frontend;
    MenuItem* items = fe->mainMenu;

    if (fe->firstFrame)
    {
        Fe_SetBackground(0);
        Fe_ResetMenu(items, 7);
        fe->popup = 0;
        fe->slideX = 300;
        g_Settings.numPlayers = 2;
    }
    s_narrowR = 1;
    Fe_DrawMenuScreen(items, 7, MainMenuValue, 0);
    s_narrowR = 0;
    Fe_ReadPads();

    if (fe->popup == 1 || fe->popup == 2)
    {
        if (Fe_MsgBox(0, 100, Fe_GetString(fe->popup == 1 ? 0xaf0 : 0xaf1), NULL, -1) != -1)
            fe->popup = 0;
    }
    else if (Fe_MenuNavigate(items, 7) == 0)
    {
        int dir = Fe_LeftRight(-1, 1);
        switch (dir ? fe->cursor : -1)
        {
        case 2: g_Settings.racingClass = (uint8_t)Fe_Clamp(g_Settings.racingClass + dir, 0, g_Settings.maxClassUnlocked + 1); break;
        case 3: g_Settings.powerUps = (uint8_t)Fe_Clamp(g_Settings.powerUps + dir, 0, 3); break;
        case 4: g_Settings.hallType = (uint8_t)Fe_Clamp(g_Settings.hallType + dir, 0, 1); break;
        default: dir = 0; break;
        }
        if (dir)
            fe->valueDir = (char)dir;
    }
    else if (fe->nextScreen == 200)
    {
        if (fe->cursor == 0)
        {
            if (fe->lastPort == 0)
                g_Settings.numPlayers = 1;
            else
            {
                Snd_PlayUi(0x4c, NULL);   /* (no esta en el banco del front-end: no suena, como en el original) */
                fe->popup = 1;      /* 1 jugador tiene que usar el puerto 1 */
                fe->nextScreen = -1;
            }
        }
        else if (fe->cursor == 1)
        {
            if (Pad_GetType(0) > 0 && Pad_GetType(1) > 0)
                g_Settings.numPlayers = 2;
            else
            {
                Snd_PlayUi(0x4c, NULL);
                fe->popup = 2;      /* hacen falta mandos en los dos puertos */
                fe->nextScreen = -1;
            }
        }
    }
    Fe_PadRepeatReset();
}

/* ---------------------------------------------------------------- pantalla 200: tipo de carrera */

/* DAT_800b7008/7018 (1 jugador) y DAT_800b7028/7038 (2 jugadores) */
static const char* RaceMenuValue(int item)
{
    int p = g_Settings.numPlayers;
    if (item == 0)
        return Fe_GetString((p == 1 ? 0xcb : 0x130) + g_Settings.singleRace[p]);
    if (item == 1)
        return Fe_GetString((p == 1 ? 0xd6 : 0x13a) + g_Settings.championship[p]);
    return NULL;
}

/* 8007b7fc */
void Fe_Screen200(void)
{
    FrontendState* fe = g_Frontend;
    int p = g_Settings.numPlayers;
    int port = PlayersPort();
    MenuItem* items = p == 1 ? fe->raceMenu1P : fe->raceMenu2P;

    if (fe->firstFrame)
    {
        Fe_SetBackground(0);
        Fe_ResetMenu(items, 5);
        fe->popup = 0;
    }
    int desc = fe->cursor == 0 ? g_Settings.singleRace[p] : (fe->cursor == 1 ? g_Settings.championship[p] : 0);
    Fe_DrawMenuScreen(items, 5, RaceMenuValue, desc);
    Fe_ReadPads();

    if (fe->popup == 1)
    {
        if (Fe_MsgBox(0, 0, Fe_GetString(0x898), NULL, port) != -1)   /* sin campeonato en Kid Play */
            fe->popup = 0;
    }
    else if (Fe_MenuNavigate(items, 5) == 0)
    {
        int dir = Fe_LeftRight(port, 1);
        if (dir && fe->cursor == 0)
            g_Settings.singleRace[p] = (uint8_t)Fe_Clamp(g_Settings.singleRace[p] + dir, 0, p == 1 ? 1 : 2);
        else if (dir && fe->cursor == 1)
            g_Settings.championship[p] = (uint8_t)Fe_Clamp(g_Settings.championship[p] + dir, 0, 2);
        else
            dir = 0;
        fe->valueDir = (char)dir;
        if (dir)
        {
            fe->descAnim = (short)0xf000;
            fe->descAnimOn = 1;
        }
    }
    else if (fe->nextScreen == 1200)
    {
        fe->teamMode = 0;
        fe->pickOpponents = 0;
        if (g_Settings.racingClass == 0 && fe->cursor == 1)
        {
            fe->popup = 1;
            fe->nextScreen = -1;
        }
        else
        {
            /* DAT_800b7115: 1 jugador 0/1 carrera, 2..4 campeonato, 5 contrarreloj, 6 showdown;
             * 2 jugadores 7..9 carrera, 10..12 campeonato, 13 contrarreloj, 14 showdown.
             * fe+0x9F2..0x9F4: equipos, elegir rivales y cuantos (de las opciones de carrera). */
            const int8_t* o1 = g_Settings.raceOpts1P;   /* vehiculos, equipos, vueltas, elegir rivales */
            const int8_t* o2 = g_Settings.raceOpts2P;   /* vehiculos, equipos, equipos cyber, vueltas, elegir */
            uint8_t mode = 0;
            switch (fe->cursor)
            {
            case 0:
                if (p == 1)
                {
                    mode = g_Settings.singleRace[1];
                    if (mode == 0)
                        fe->numOpponents = (uint8_t)(o1[0] - 1);
                    else
                    {
                        fe->numOpponents = (uint8_t)(o1[1] * 2 - 2);
                        fe->teamMode = 1;
                    }
                    fe->pickOpponents = o1[3];
                }
                else
                {
                    mode = (uint8_t)(g_Settings.singleRace[2] + 7);
                    if (mode == 7)
                        fe->numOpponents = (uint8_t)(o2[0] - 2);
                    else if (mode == 8)
                        fe->numOpponents = (uint8_t)(o2[1] * 2 - 2);
                    else
                    {
                        fe->numOpponents = (uint8_t)(o2[2] * 2 - 4);
                        fe->teamMode = 1;
                    }
                    fe->pickOpponents = o2[4];
                }
                break;
            case 1:
                mode = (uint8_t)(g_Settings.championship[p] + (p == 1 ? 2 : 10));
                if (mode == (p == 1 ? 3 : 12))
                    fe->teamMode = 1;
                break;
            case 2:
                mode = p == 1 ? 5 : 13;
                break;
            case 3:
                mode = p == 1 ? 6 : 14;
                fe->numOpponents = p == 1 ? 1 : 0;
                if (p == 1)
                    fe->pickOpponents = 1;
                break;
            }
            g_Settings.gameMode = mode;
            g_Settings.raceMenuCursor = fe->cursor;
        }
    }
    Fe_PadRepeatReset();
}

/* ---------------------------------------------------------------- pantalla 300: opciones de carrera */

/* 800abeed: minimo y maximo de cada opcion (indice jugadores * 10 + opcion * 2 + 5) y 800abef8: hasta que
 * opcion se muestran numeros y que opcion es Si/No */
static const int8_t s_raceOptLimits[3][5][2] = {
    { { 0, 0 } },
    { { 1, 6 }, { 1, 3 }, { 1, 8 }, { 0, 1 }, { 0, 0 } },
    { { 2, 6 }, { 1, 3 }, { 2, 3 }, { 1, 8 }, { 0, 1 } },
};
static const uint8_t s_raceOptNumbers[3][2] = { { 0, 0 }, { 2, 3 }, { 3, 4 } };

static const char* RaceOptValue(int item)
{
    static char buf[16];
    int p = g_Settings.numPlayers;
    const int8_t* opts = p == 1 ? g_Settings.raceOpts1P : g_Settings.raceOpts2P;
    if (item <= s_raceOptNumbers[p][0])
        snprintf(buf, sizeof(buf), "%i", opts[item]);
    else if (item == s_raceOptNumbers[p][1])
        snprintf(buf, sizeof(buf), "%s", Fe_GetString(opts[item] ? 0xfa5 : 0xfa6));
    return buf;
}

/* 8007f4a0 */
void Fe_Screen300(void)
{
    FrontendState* fe = g_Frontend;
    int p = g_Settings.numPlayers;
    int port = PlayersPort();
    int count = p == 1 ? 4 : 5;
    MenuItem* items = p == 1 ? fe->raceOpts1P : fe->raceOpts2P;
    int8_t* opts = p == 1 ? g_Settings.raceOpts1P : g_Settings.raceOpts2P;

    if (fe->firstFrame)
    {
        Fe_SetBackground(0);
        Fe_ResetMenu(items, count);
    }
    Fe_DrawMenuScreen(items, count, RaceOptValue, 0);
    Fe_ReadPads();
    if (Fe_MenuNavigate(items, count) == 0)
    {
        int dir = Fe_LeftRight(port, 1);
        if (dir)
        {
            int c = fe->cursor;
            opts[c] = (int8_t)Fe_Clamp(opts[c] + dir, s_raceOptLimits[p][c][0], s_raceOptLimits[p][c][1]);
            fe->valueDir = 1;
        }
    }
    Fe_PadRepeatReset();
}

/* ---------------------------------------------------------------- pantalla 400: opciones de juego */

/* DAT_800af958 = 0x264.. (jugadores), DAT_800af960 = 0x26E.. (memoria), DAT_800af968 = 0x278.. (records) */
static const char* GameOptValue(int item)
{
    switch (item)
    {
    case 1: return Fe_GetString(0x264 + g_Settings.setNamePlayers);
    case 2: return Fe_GetString(0x26e + g_Settings.loadSaveMode);
    case 3: return Fe_GetString(0x278 + g_Settings.clearRecords);
    default: return NULL;
    }
}

/* 8007bf00. popup: -1 nada, 0/1 confirmar borrado (pistas/copas), 10/11 borrado hecho.
 * Pendiente: el borrado real de records (FUN_800931d8). */
void Fe_Screen400(void)
{
    FrontendState* fe = g_Frontend;
    MenuItem* items = fe->gameOptions;

    if (fe->firstFrame)
    {
        Fe_SetBackground(0);
        Fe_ResetMenu(items, 6);
        g_Settings.numPlayers = 2;
        fe->popup = -1;
        fe->portPopup = 0;
    }
    Fe_DrawMenuScreen(items, 6, GameOptValue, 0);
    Fe_ReadPads();

    if (fe->portPopup)
    {
        if (Fe_MsgBox(0, 100, Fe_GetString(fe->portPopup == 1 ? 0xaf0 : 0xaf1), NULL, -1) != -1)
            fe->portPopup = 0;
    }
    else if (fe->popup == 0 || fe->popup == 1)
    {
        int r = Fe_MsgBox(0, 100, Fe_GetString(fe->popup == 0 ? 0x8fc : 0x8fd), &g_Settings.yesNo, -1);
        if (r == 1)
        {
            fe->marquee = 1;
            fe->popup = (short)(fe->popup + 10);
        }
        else if (r == 0)
            fe->popup = -1;
    }
    else if (fe->popup >= 10)
    {
        if (Fe_MsgBox(0, 100, Fe_GetString(fe->popup == 10 ? 0x8fe : 0x8ff), NULL, -1) != -1)
            fe->popup = -1;
    }
    else
    {
        int act = Fe_MenuNavigate(items, 6);
        if (act == 0)
        {
            int dir = Fe_LeftRight(-1, 1);
            switch (dir ? fe->cursor : -1)
            {
            case 1: g_Settings.setNamePlayers = (uint8_t)Fe_Clamp(g_Settings.setNamePlayers + dir, 0, 1); break;
            case 2: g_Settings.loadSaveMode = (uint8_t)Fe_Clamp(g_Settings.loadSaveMode + dir, 0, 1); break;
            case 3: g_Settings.clearRecords = (uint8_t)Fe_Clamp(g_Settings.clearRecords + dir, 0, 1); break;
            default: dir = 0; break;
            }
            fe->valueDir = (char)dir;
        }
        else if (act == -1)
        {
            if (fe->cursor == 3)
            {
                g_Settings.yesNo = 0;
                fe->popup = g_Settings.clearRecords;
                Snd_PlayUi(0x48, NULL);
            }
            else if (fe->cursor == 2)
                fe->nextScreen = g_Settings.loadSaveMode == 0 ? 800 : 700;
        }
        else if (fe->nextScreen == 1100)
        {
            if (g_Settings.setNamePlayers == 0 && fe->lastPort != 0)
            {
                fe->portPopup = 1;
                fe->nextScreen = -1;
                Snd_PlayUi(0x4c, NULL);   /* (0x4C no esta en el banco del front-end: no suena, como en el original) */
            }
            else if (g_Settings.setNamePlayers == 1 && (Pad_GetType(0) < 1 || Pad_GetType(1) < 1))
            {
                fe->portPopup = 2;
                fe->nextScreen = -1;
                Snd_PlayUi(0x4c, NULL);
            }
            else
                g_Settings.numPlayers = (uint8_t)(g_Settings.setNamePlayers + 1);
        }
    }
    Fe_PadRepeatReset();
}

/* ---------------------------------------------------------------- pantalla 500: audio */

/* DAT_800b7048 = 0x2BE.. (Mono/Stereo/Dolby) */
static const char* AudioValue(int item)
{
    return item == 4 ? Fe_GetString(0x2be + g_Settings.audioMode) : NULL;
}

/* fe +0x948: voz del sonido de prueba del menu de audio */
static SndHandle s_testVoice;

/* 8007c540. Las barras de volumen usan la imagen Gmdc 5 decodificada en (0x300, 0x100). Al mover los volumenes de
 * efectos, motor o comentarista suena un sonido de prueba (0x4A, 0x5A, 0x5B: 8003f4b0 + 8003f7b8) que sigue
 * sonando con el volumen de la barra hasta cambiar de opcion o salir (8003f48c). */
void Fe_Screen500(void)
{
    FrontendState* fe = g_Frontend;
    MenuItem* items = fe->audioMenu;
    int stop = 0;

    if (fe->firstFrame)
    {
        Fe_SetBackground(0);
        Fe_ResetMenu(items, 5);
        Fe_DecodeGmdc(5, 0x300, 0x100);
    }
    Fe_DrawMenuScreen(items, 5, AudioValue, 0);
    for (int i = 0; i < 4; i++)
    {
        const MenuItem* it = &items[i];
        Fe_DrawVramRect(0x300, 0x100, 0x4e, 0xd, it->x + 0x71, it->y - 1, 0, 0);
        int knob = it->x + (g_Settings.volume[i] * 0x32) / 0x7f;
        Fe_DrawOutline(0, 0, 0, knob + 0x7b, it->y, knob + 0x82, it->y + 9, 2);
    }
    Fe_ReadPads();
    if (Fe_MenuNavigate(items, 5) == 0)
    {
        int dir = Fe_LeftRight(-1, fe->cursor > 3);   /* los volumenes cambian sin espera */
        if (dir && fe->cursor == 4)
        {
            g_Settings.audioMode = (uint8_t)Fe_Clamp(g_Settings.audioMode + dir, 0, 2);
            fe->valueDir = 1;
            Snd_ApplySettings(g_Settings.volume, g_Settings.audioMode);   /* 8003d43c */
        }
        else if (dir)
        {
            int vol = Fe_Clamp(g_Settings.volume[fe->cursor] + dir, 0, 0x7f);
            g_Settings.volume[fe->cursor] = (uint8_t)vol;
            Snd_ApplySettings(g_Settings.volume, g_Settings.audioMode);   /* 8003d498 / 8003d680 / 8003d538 / 8003d628 */
            static const int ids[4] = { 0x4a, 0x5a, 0, 0x5b };          /* efectos, motor, musica (sin), comentarista */
            int id = ids[fe->cursor & 3];
            if (id)
            {
                if (!Handle_Valid(&s_testVoice))
                {
                    int def = Def_FromId(id);
                    SndVoice* v = def != 0xff ? Voice_Alloc(vol, 10, 0, def) : NULL;
                    if (v)
                    {
                        Handle_Set(&s_testVoice, v);
                        if (vol > 0)
                            Def_Play(def, v, 0x40, vol, -1, -1, 0);
                    }
                }
                else
                    Voice_Set(s_testVoice.v, s_testVoice.v->pan, vol, -1, 0);
            }
        }
    }
    else if (fe->cursor != fe->prevCursor)
        stop = 1;
    Fe_PadRepeatReset();
    if (fe->fadeDone)
        stop = 1;
    if (stop)
        Handle_Free(&s_testVoice);   /* 8003f48c */
}
