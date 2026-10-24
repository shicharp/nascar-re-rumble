/* Contrasena del progreso (ver password.h).
 * Palabra 0: bits 0-1 clase maxima (DAT_800b7135), 2-13 el estado (0..3) de las 6 sedes en esa clase
 * (DAT_800b713a + clase + 3 * sede), 14-16 copas de las sedes especiales ganadas (DAT_800b714c..714e != 0) y 17-19
 * con 2, 20-25 las 6 sedes en la clase anterior con 2, 26-30 suma baja, 31 copa EA ganada en Rookie.
 * Palabra 1: bits 0-4 suma alta, 5 / 6 copa EA ganada en Pro / Elite, 7-15 los extras
 * DAT_800b7150..7158. La suma baja suma por cada bit su indice (puesto) o la paridad (quitado); la alta cuenta
 * los bits puestos. Caracter i = bits 5i..5i+4 con el alfabeto DAT_800abeac (i par) o 800abecc (i impar).
 * Los records de la copa EA (sede 7 de las tablas de copa, primer puesto de DAT_800b7998 / 8418 sin la marca +0x11 / +0xD) los pone tambien
 * la contrasena: con ellos se escribe el nombre del jugador y 9000000 como tiempo. */
#include <string.h>
#include "nr.h"
#include "exe.h"
#include "frontend.h"
#include "savegame.h"
#include "password.h"

static uint8_t s_flags[3];   /* fe +0x9D4..+0x9D6: copa EA ganada en cada clase */
static int8_t  s_cheat = -1; /* fe +0x9D7: contrasena especial (0..10) o -1 */

#define P(a) PROGRESS(a)

static uint32_t Rd32(const uint8_t* p)
{
    return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24);
}

static void Wr32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* nombre de una tabla de records (strcpy en el original; aqui como mucho 9 caracteres) */
static void SetName(uint8_t* dst, const char* name)
{
    size_t n = strlen(name);
    if (n > 9)
        n = 9;
    memcpy(dst, name, n);
    dst[n] = 0;
}

static const char* AlphaChar(int pos, int v)
{
    const uint8_t* a = Exe_Ptr((pos & 1) ? 0x800abeccu : 0x800abeacu);
    static char c[2];
    c[0] = a ? (char)a[v & 0x1f] : '?';
    return c;
}

/* suma de 10 bits (baja: indice si el bit esta, paridad si no; alta: bits puestos) */
static unsigned Pw_Sum(uint32_t w0, uint32_t w1)
{
    unsigned lo = 0, hi = 0;
    for (int i = 0; i < 32; i++)
        lo = (lo + ((w0 >> i & 1) ? (unsigned)i : (unsigned)(i % 2 == 0))) & 0x1f;
    for (int i = 0; i < 16; i++)
        lo = (lo + ((w1 >> i & 1) ? 1u : (unsigned)(i & 1))) & 0x1f;
    for (int i = 0; i < 32; i++)
        if (w0 >> i & 1)
            hi = (hi + 1) & 0x1f;
    for (int i = 0; i < 16; i++)
        if (w1 >> i & 1)
            hi = (hi + 1) & 0x1f;
    return lo | hi << 5;
}

/* records de la copa EA (sede 7) puestos por el jugador en cada clase */
static void Pw_RecordFlags(void)
{
    for (int c = 0; c < 3; c++)
    {
        const uint8_t* r = g_SaveExtra.cupRecs + 7 * 0x150 + c * 0x70;
        const uint8_t* u = g_SaveExtra.cupRecs2 + 7 * 0x120 + c * 0x60;
        if ((Rd32(r) != 0 && r[0x11] == 0) || (Rd32(u) != 0 && u[0xd] == 0))
            s_flags[c] = 1;
        else
            s_flags[c] = (uint8_t)(c == 0 && P(0x800b714f) == 1);
    }
}

/* 8008f344 */
void Pw_Encode(char* out)
{
    int cls = g_Settings.maxClassUnlocked;
    P(0x800b7135) = (uint8_t)cls;
    uint32_t w0 = (uint32_t)(cls & 3) | (uint32_t)(P(0x800b713a + cls) & 3) << 2 |
                  (uint32_t)(P(0x800b713d + cls) & 3) << 4 | (uint32_t)(P(0x800b7140 + cls) & 3) << 6 |
                  (uint32_t)(P(0x800b7143 + cls) & 3) << 8 | (uint32_t)(P(0x800b7146 + cls) & 3) << 10 |
                  (uint32_t)(P(0x800b7149 + cls) & 3) << 12;
    unsigned cups = 0;
    for (int k = 0; k < 3; k++)
        if (P(0x800b714c + k) != 0)
            cups |= 1u << k;
    for (int k = 3; k < 6; k++)
        if (P(0x800b714c + k - 3) >= 2)
            cups |= 1u << k;
    w0 |= cups << 14;
    if (cls != 0)
        for (int k = 0; k < 6; k++)
            if (P(0x800b7139 + cls + 3 * k) == 2)
                w0 |= 1u << (20 + k);
    unsigned extra = 0;
    for (int k = 1; k < 10; k++)
        if (P(0x800b714f + k) == 1)
            extra |= 1u << k;
    Pw_RecordFlags();
    if (s_flags[2])
        extra |= 1;
    w0 = (w0 & 0x3ffffff) | (s_flags[0] ? 0x80000000u : 0);
    uint32_t w1 = (s_flags[1] ? 0x20u : 0) | extra << 6;
    unsigned cs = Pw_Sum(w0, w1);
    w0 = (w0 & 0x3ffffff) | ((cs & 0x1f) | (s_flags[0] ? 0x20u : 0)) << 26;
    w1 = extra << 6 | cs >> 5 | (s_flags[1] ? 0x20u : 0);
    for (int ch = 0; ch < 10; ch++)
    {
        int v = 0;
        for (int b = 0; b < 5; b++)
        {
            int bit = ch * 5 + b;
            uint32_t w = bit < 32 ? w0 : w1;
            if (w >> (bit & 31) & 1)
                v |= 1 << b;
        }
        out[ch] = AlphaChar(ch, v)[0];
    }
    out[10] = 0;
}

/* 8008fb60: las especiales (textos 0x839..0x843) o 10 caracteres de los alfabetos con la suma bien */
int Pw_Decode(const char* text, uint32_t* w)
{
    s_cheat = -1;
    for (int i = 0; i < 11; i++)
        if (strcmp(Fe_GetString(0x839 + i), text) == 0)
        {
            s_cheat = (int8_t)i;
            return 1;
        }
    int val[10];
    for (int ch = 0; ch < 10; ch++)
    {
        const uint8_t* a = Exe_Ptr((ch & 1) ? 0x800abeccu : 0x800abeacu);
        int v = 0;
        while (a && v < 0x20 && (uint8_t)text[ch] != a[v])
            v++;
        if (!a || v == 0x20)
            return 0;
        val[ch] = v;
    }
    w[0] = w[1] = 0;
    for (int bit = 0; bit < 50; bit++)
        if (val[bit / 5] >> (bit % 5) & 1)
            w[bit >> 5] |= 1u << (bit & 31);
    s_flags[0] = (uint8_t)(w[0] >> 31);
    s_flags[1] = (uint8_t)(w[1] >> 5 & 1);
    s_flags[2] = (uint8_t)(w[1] >> 6 & 1);
    unsigned stored = (w[0] >> 26 & 0x1f) | (w[1] & 0x1f) << 5;
    w[0] = (w[0] & 0x3ffffff) | (s_flags[0] ? 0x80000000u : 0);
    w[1] = (w[1] & ~0x3fu) | (s_flags[1] ? 0x20u : 0);
    return Pw_Sum(w[0], w[1]) == stored;
}

/* 80090008: progreso, copas y records de la copa EA a partir de la contrasena; las especiales dan extras */
void Pw_Apply(const uint32_t* w)
{
    if (s_cheat >= 0)
    {
        if (s_cheat < 10)
            P(0x800b714f + s_cheat) = 1;
        if (s_cheat == 10)
            for (int i = 0; i < 10; i++)
                P(0x800b714f + i) = 1;
        return;
    }
    Save_ClearRecords(1);
    memset(&P(0x800b7135), 0, 0x24);
    int cls = (int)(w[0] & 3);
    if (cls > 2)
        cls = 2;
    P(0x800b7135) = (uint8_t)cls;
    g_Settings.maxClassUnlocked = (uint8_t)cls;
    P(0x800b713a + cls) = (uint8_t)(w[0] >> 2 & 3);
    P(0x800b713d + cls) = (uint8_t)(w[0] >> 4 & 3);
    P(0x800b7140 + cls) = (uint8_t)((uint8_t)w[0] >> 6);
    P(0x800b7143 + cls) = (uint8_t)(w[0] >> 8 & 3);
    P(0x800b7146 + cls) = (uint8_t)(w[0] >> 10 & 3);
    P(0x800b7149 + cls) = (uint8_t)(w[0] >> 12 & 3);
    for (int c = 0; c < cls; c++)
        for (int v = 0; v < 6; v++)
            P(0x800b713a + c + 3 * v) = 1;
    for (int c = 0; c < cls; c++)
        for (int v = 0; v < 6; v++)
            if ((w[0] >> 20 & 0x3f) >> v & 1 || (cls == 2 && c == 0))
                P(0x800b713a + c + 3 * v) = 2;
    for (int k = 0; k < 3; k++)
        if ((w[0] >> 14 & 0x3f) >> k & 1)
            P(0x800b714c + k) = 1;
    for (int k = 3; k < 6; k++)
        if ((w[0] >> 14 & 0x3f) >> k & 1)
            P(0x800b7149 + k) = 2;
    int venues = 0;
    for (int v = 0; v < 6; v++)
        if (P(0x800b713a + 3 * v) || P(0x800b713b + 3 * v) || P(0x800b713c + 3 * v))
            venues++;
    P(0x800b7136) = (uint8_t)(venues / 2 < 3 ? venues / 2 : 2);
    for (int i = 0; i < (venues + 1) / 2; i++)
        P(0x800b7137 + i) = 1;
    for (int k = 1; k < 10; k++)
        P(0x800b714f + k) = (uint8_t)((w[1] >> 6 & 0x3ff) >> k & 1);
    if (s_flags[0] || s_flags[1] || s_flags[2])
        P(0x800b714f) = 1;
    const GameSettings* g = &g_Settings;
    const char* name1 = g->playerNameSet[0] ? g->playerName[0] : Fe_GetString(4000);
    const char* name2 = g->playerNameSet[1] ? g->playerName[1] : Fe_GetString(0xfa1);
    for (int v = 0; v < 8; v++)
        for (int c = 0; c < 3; c++)
        {
            uint8_t* r = g_SaveExtra.cupRecs + v * 0x150 + c * 0x70;
            uint8_t* u = g_SaveExtra.cupRecs2 + v * 0x120 + c * 0x60;
            int on;
            if (v < 7)
            {
                if (P(0x800b713a + 3 * v + c) == 2)
                {
                    SetName(r + 0x12, name1);
                    Wr32(r + 4, 9000000);
                    r[0x1b] = 0;
                }
                on = P(0x800b713a + 3 * v + c);
            }
            else
                on = s_flags[c];
            if (on)
            {
                SetName(r + 8, name1);
                Wr32(u, 9000000);
                Wr32(r, 9000000);
                u[0xd] = 0;
                r[0x11] = 0;
                SetName(u + 4, name1);
                SetName(u + 0xe, name2);
            }
        }
    int all = 0;
    for (int v = 0; v < 6; v++)
        if (P(0x800b713a + 3 * v) && P(0x800b713b + 3 * v) && P(0x800b713c + 3 * v))
            all++;
    g_SaveExtra.b711c = (uint8_t)(all == 6);
}
