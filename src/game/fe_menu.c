/* Widgets del front-end y pantalla 100 (menu principal). */
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

/* ---------------------------------------------------------------- primitivas 2D */

static int Fe_BaseDepth(void)
{
    return g_Frontend->screen == 900 ? 2000 : 0;
}

/* 8008e614: rectangulo relleno (opcionalmente semitransparente al 50%) */
static void Fe_DrawBox(int r, int g, int b, int x0, int y0, int x1, int y1, int semi)
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
static void Fe_DrawLine(uint32_t color, int x0, int y0, int x1, int y1, int thick)
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

static uint32_t Rgb(int r, int g, int b)
{
    return 0x28000000u | (uint32_t)b << 16 | (uint32_t)g << 8 | (uint32_t)r;
}

/* 8008e51c: contorno rectangular */
static void Fe_DrawOutline(int r, int g, int b, int x0, int y0, int x1, int y1, int thick)
{
    uint32_t c = Rgb(r, g, b);
    Fe_DrawLine(c, x0, y0, x1, y0, thick);
    Fe_DrawLine(c, x1 + 1 - thick, y0, x1 + 1 - thick, y1 + 1, thick);
    Fe_DrawLine(c, x0, y1 + 1 - thick, x1, y1 + 1 - thick, thick);
    Fe_DrawLine(c, x0, y1, x0, y0, thick);
}

/* 8008e75c: marco biselado con esquinas recortadas (color claro y su sombra a mitad) */
static void Fe_DrawFrame(int r, int g, int b, int x0, int y0, int x1, int y1)
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
static void Fe_ReadPads(void)
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
static int Fe_PadPressed(uint32_t mask, int port, int repeat)
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
static void Fe_PadRepeatReset(void)
{
    for (int i = 0; i < 2; i++)
        if (!g_Frontend->held[i])
            g_Frontend->repeatTime[i] = g_FrameTime - 100;
}

/* 8008aad8 */
static int Fe_Clamp(int v, int lo, int hi)
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
static void Fe_ItemColor(int* r, int* g, int* b, int mode)
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
            it->anim = (short)(it->anim + g_FrameDelta * 0x82);   /* (sonido 0x51 al empezar) */
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

/* 8008d5f4: barra de ayuda inferior; si no cabe se desplaza con los bordes difuminados */
static void Fe_DrawHelp(const char* text, int x, int y, int clipX0, int clipX1)
{
    FrontendState* fe = g_Frontend;
    if (!text || Pad_GetType(0) <= 0 || fe->padDisabled)
        return;
    strncpy(fe->textBuf, text, sizeof(fe->textBuf) - 1);
    Font_Select(0);
    int w, h;
    Font_Measure(fe->textBuf, &w, &h);
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
    DR_TPAGE* tp = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
    if (tp)
    {
        setDrawTPage(tp, 1, 1, getTPage(0, 2, 0, 0));
        Gfx_AddPrimDepth(0, tp);
    }
    for (int side = 0; side < 2; side++)
    {
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

/* ---------------------------------------------------------------- pantalla 100 */

/* Nombres de los ajustes (DAT_800b6fc8 = 0x7A.., DAT_800b6fe8 = 0x84.., DAT_800b6ff8 = 0x8E..) */
static const char* SettingName(int item)
{
    switch (item)
    {
    case 2: return Fe_GetString(0x7a + g_Settings.racingClass);
    case 3: return Fe_GetString(0x84 + g_Settings.powerUps);
    case 4: return Fe_GetString(0x8e + g_Settings.hallType);
    default: return NULL;
    }
}

/* 8007b2ac. Pendiente: mensajes emergentes (FUN_8008cb88, textos 0xAF0/0xAF1), sonidos y el ajuste
 * de espaciado de la 'r' que hace el original mientras pinta las opciones. */
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

    Fe_DrawBackground(20000);
    Fe_DrawHelp(items[fe->cursor].help, 0x21, 0xd5, 0x23, 0x109);
    if (fe->descAnim > 0x1e && items[fe->cursor].values)
    {
        const MenuValue* v = &items[fe->cursor].values[0];
        Fe_DrawDescription((v->w * fe->descAnim) / 1024, (v->h * fe->descAnim) / 1024, v->text);
    }

    for (int i = 0; i < 7; i++)
    {
        int r, g, b;
        Fe_ItemColor(&r, &g, &b, i == fe->cursor);
        Fe_DrawMenuItem(r, g, b, items, i, 7);
        const char* value = SettingName(i);
        if (value && *value)
            Fe_DrawOptionValue(r, g, b, &items[i], value, 1);
    }

    Fe_ReadPads();

    if (fe->popup == 0)
    {
        int act = Fe_MenuNavigate(items, 7);
        if (act == 0)
        {
            int dir = 0;
            if (Fe_PadPressed(PADB_LEFT, -1, 1))
                dir = -1;
            else if (Fe_PadPressed(PADB_RIGHT, -1, 1))
                dir = 1;
            if (dir)
            {
                switch (fe->cursor)
                {
                case 2: g_Settings.racingClass = (uint8_t)Fe_Clamp(g_Settings.racingClass + dir, 0, g_Settings.maxClassUnlocked + 1); break;
                case 3: g_Settings.powerUps = (uint8_t)Fe_Clamp(g_Settings.powerUps + dir, 0, 3); break;
                case 4: g_Settings.hallType = (uint8_t)Fe_Clamp(g_Settings.hallType + dir, 0, 1); break;
                default: dir = 0; break;
                }
                fe->valueDir = (char)dir;
            }
        }
        else if (fe->nextScreen == 200)
        {
            if (fe->cursor == 0)
                g_Settings.numPlayers = 1;
            else if (fe->cursor == 1)
            {
                if (Pad_GetType(0) > 0 && Pad_GetType(1) > 0)
                    g_Settings.numPlayers = 2;
                else
                {
                    fe->popup = 2;          /* "conecta un segundo mando" */
                    fe->nextScreen = -1;
                }
            }
        }
    }
    else
    {
        /* TODO: FUN_8008cb88 (cuadro de mensaje). De momento se cierra con X. */
        if (Fe_PadPressed(PADB_CROSS, -1, 0))
            fe->popup = 0;
    }
    Fe_PadRepeatReset();
}
