/* Solo en el port: pantalla 2100, partidas en red local (ver net.h). Se llega con SELECT en el menu principal y se
 * vuelve a ella al acabar una carrera en red.
 *  - Sin partida: crear (anfitrion) o unirse.
 *  - Unirse: las partidas que contestan a la busqueda por difusion, o una direccion escrita con el teclado.
 *  - Sala: los jugadores; cada uno elige su piloto con izquierda / derecha. El anfitrion pasa con X a elegir la
 *    carrera (pantalla 200, solo carrera suelta) y Fe_StartRace mete a los demas en la parrilla. */
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "PsyX/PsyX_public.h"
#include "nr.h"
#include "pad.h"
#include "font.h"
#include "globals.h"
#include "frontend.h"
#include "fe_internal.h"
#include "net.h"

enum { LAN_MENU, LAN_FIND, LAN_ADDR };

static int  s_mode;           /* sin partida: LAN_MENU / LAN_FIND / LAN_ADDR */
static int  s_sel;
static char s_addr[48];       /* direccion escrita */
static int  s_typing;         /* el teclado escribe en s_addr (g_cfg_gameOnTextInput) */

static void Lan_OnText(const char* text)
{
    size_t n = strlen(s_addr);
    if (!text)
    {
        if (n)
            s_addr[n - 1] = 0;   /* retroceso */
        return;
    }
    for (; *text && n + 1 < sizeof(s_addr); text++)
        if ((*text >= '0' && *text <= '9') || (*text >= 'a' && *text <= 'z') || (*text >= 'A' && *text <= 'Z') ||
            *text == '.' || *text == '-')
        {
            s_addr[n++] = *text;
            s_addr[n] = 0;
        }
}

static void Lan_Typing(int on)
{
    s_typing = on;
    g_cfg_gameOnTextInput = on ? Lan_OnText : NULL;
}

static void Lan_Text(int r, int g, int b, const char* s, int x, int y, int align)
{
    Fe_DrawTextRgb(r, g, b, s, x, y, align);
}

static void Lan_Box(const char* title)
{
    Fe_DrawBox(0x14, 0x14, 0x14, 0x16, 0x14, 0x127, 0xcf, 0);
    Fe_DrawFrame(0x3c, 0x3c, 0x14, 0x16, 0x13, 0x127, 0xcf);
    Font_Select(0);
    Lan_Text(0x1c, 0x1c, 0, title, 0x1c, 0x18, 0);
    Fe_DrawLine(0x28143c3c, 0x1a, 0x26, 0x123, 0x26, 1);
}

/* fila elegible: fondo rojo oscuro y texto amarillo si es la del cursor */
static void Lan_Row(const char* s, int y, int sel)
{
    if (sel)
    {
        Fe_DrawBox(0x50, 0x0a, 0x0a, 0x1a, y - 1, 0x123, y + 9, 0);
        Lan_Text(0x1f, 0x1c, 0, s, 0x22, y, 0);
    }
    else
        Lan_Text(0x17, 0x17, 0x17, s, 0x22, y, 0);
}

static void Lan_Help(const char* s)
{
    Fe_DrawHelp(0, s, 0x1c, 0xd5, 0x1c, 0x127, 0);
}

static int Lan_Up(void)
{
    return Fe_PadPressed(PADB_UP, -1, 1);
}

static int Lan_Down(void)
{
    return Fe_PadPressed(PADB_DOWN, -1, 1);
}

/* la sala: los jugadores, el piloto de cada uno y quien corre */
static void Lan_Lobby(void)
{
    FrontendState* fe = g_Frontend;
    int host = Net_IsHost(), me = Net_MachineId();
    char line[96];
    Lan_Box(host ? "LAN Game - Hosting" : "LAN Game");
    Lan_Text(0x12, 0x12, 0x12, "Player", 0x22, 0x2a, 0);
    Lan_Text(0x12, 0x12, 0x12, "Driver", 0x90, 0x2a, 0);
    int row = 0;
    for (int m = 0; m < NET_MAX_MACHINES; m++)
    {
        const NetPlayer* p = Net_Player(m);
        if (!p)
            continue;
        int y = 0x38 + row * 0xc;
        int v = m == me ? 0x1f : 0x17;
        snprintf(line, sizeof(line), "%d. %s", row + 1, p->name);
        if (m == me)
            Fe_DrawBox(0x28, 0x0a, 0x0a, 0x1a, y - 1, 0x123, y + 9, 0);
        Lan_Text(v, m == me ? 0x1c : v, m == me ? 0 : v, line, 0x22, y, 0);
        Lan_Text(v, v, v, p->driver < NUM_DRIVERS ? g_Drivers[p->driver].name : "?", 0x90, y, 0);
        const char* tag = m == 0 ? "Host" : p->inRace ? "Racing" : "";
        Lan_Text(0x12, 0x12, 0x12, tag, 0x120, y, 2);
        row++;
    }
    if (Net_DesyncFrame())
        Lan_Text(0x1f, 0x08, 0x08, "The last race went out of sync", 0x9e, 0xb6, 1);
    Lan_Text(0x14, 0x14, 0x14, host ? "Up to 8 players. Press X to choose the race." :
                                      "Waiting for the host to start the race...", 0x9e, 0xc2, 1);
    Lan_Help(host ? "\x1b*\x13 Choose Race   \x1b$\x13 Driver   \x1b~\x13 Leave" :
                    "\x1b$\x13 Driver   \x1b~\x13 Leave");

    int d = Fe_PadPressed(PADB_LEFT, -1, 1) ? -1 : Fe_PadPressed(PADB_RIGHT, -1, 1) ? 1 : 0;
    if (d)
    {
        /* los pilotos normales (0..0x20); el anfitrion comprueba que este libre y disponible */
        g_Settings.lastDriver[0] = (uint8_t)((g_Settings.lastDriver[0] + 0x21 + d) % 0x21);
        Net_UpdateLocalPlayer();
        Snd_PlayUi(0x47, NULL);
    }
    else if (host && Fe_PadPressed(PADB_CROSS, -1, 1))
    {
        g_Settings.numPlayers = 1;
        fe->nextScreen = 200;
        Snd_PlayUi(0x48, NULL);
    }
    else if (Fe_PadPressed(PADB_TRIANGLE, -1, 1))
    {
        Net_Leave();
        s_mode = LAN_MENU;
        s_sel = 0;
    }
}

void Fe_ScreenLan(void)
{
    FrontendState* fe = g_Frontend;
    if (fe->firstFrame)
    {
        Fe_SetBackground(6);
        s_sel = 0;
        g_Settings.numPlayers = 1;   /* un jugador por maquina (el menu principal deja 2: aviso del mando 2) */
        if (s_mode == LAN_FIND)
            Net_FindStart();
    }
    Fe_DrawBackground(0x800);
    fe->marquee = 0;
    Fe_ReadPads();
    char line[96];

    if (Net_IsHost() || Net_Connected())
    {
        Net_FindStop();
        Lan_Typing(0);
        Lan_Lobby();
    }
    else if (Net_Active())
    {
        Lan_Box("LAN Game");
        Lan_Text(0x17, 0x17, 0x17, Net_Status(), 0x9e, 0x60, 1);
        Lan_Help("\x1b~\x13 Cancel");
        if (Fe_PadPressed(PADB_TRIANGLE, -1, 1))
            Net_Leave();
    }
    else if (s_mode == LAN_MENU)
    {
        Lan_Box("LAN Game");
        Lan_Row("Host a Game", 0x40, s_sel == 0);
        Lan_Row("Join a Game", 0x50, s_sel == 1);
        Lan_Text(0x14, 0x14, 0x14, "Race against up to 7 more PCs on your local network.", 0x9e, 0x78, 1);
        Lan_Text(0x14, 0x14, 0x14, "Your name is the one in Game Options > Set Name.", 0x9e, 0x86, 1);
        if (Net_Status()[0])
            Lan_Text(0x1c, 0x10, 0x10, Net_Status(), 0x9e, 0xa4, 1);
        Lan_Help("\x1b*\x13 Select   \x1b^\x13 Change Selection   \x1b~\x13 Previous Menu");
        if (Lan_Up() || Lan_Down())
        {
            s_sel ^= 1;
            Snd_PlayUi(0x46, NULL);
        }
        else if (Fe_PadPressed(PADB_CROSS, -1, 1))
        {
            Snd_PlayUi(0x48, NULL);
            if (s_sel == 0)
                Net_Host(NET_DEFAULT_PORT);
            else
            {
                s_mode = LAN_FIND;
                s_sel = 0;
                Net_FindStart();
            }
        }
        else if (Fe_PadPressed(PADB_TRIANGLE, -1, 1))
            fe->nextScreen = 100;
    }
    else if (s_mode == LAN_FIND)
    {
        Lan_Box("Join a Game");
        int n = Net_FindCount();
        if (s_sel > n)
            s_sel = n;
        for (int i = 0; i < n; i++)
        {
            const NetFound* f = Net_Found(i);
            snprintf(line, sizeof(line), "%s's game  (%d/8)  %s", f->name, f->players, f->addr);
            Lan_Row(line, 0x30 + i * 0xc, s_sel == i);
        }
        if (n == 0)
            Lan_Text(0x12, 0x12, 0x12, "Searching the local network...", 0x22, 0x30, 0);
        Lan_Row("Enter an address", 0x30 + (n ? n : 1) * 0xc + 4, s_sel == n);
        if (Net_Status()[0] && strcmp(Net_Status(), "Connecting..."))
            Lan_Text(0x1c, 0x10, 0x10, Net_Status(), 0x9e, 0xc2, 1);   /* (por que fallo la ultima vez) */
        Lan_Help("\x1b*\x13 Join   \x1b^\x13 Change Selection   \x1b~\x13 Previous Menu");
        if (Lan_Up() && s_sel > 0)
        {
            s_sel--;
            Snd_PlayUi(0x46, NULL);
        }
        else if (Lan_Down() && s_sel < n)
        {
            s_sel++;
            Snd_PlayUi(0x46, NULL);
        }
        else if (Fe_PadPressed(PADB_CROSS, -1, 1))
        {
            Snd_PlayUi(0x48, NULL);
            if (s_sel < n)
            {
                char addr[32];
                snprintf(addr, sizeof(addr), "%s", Net_Found(s_sel)->addr);
                Net_FindStop();
                Net_Join(addr, NET_DEFAULT_PORT);
                s_mode = LAN_FIND;   /* (si falla, vuelve a la lista) */
            }
            else
            {
                s_mode = LAN_ADDR;
                Lan_Typing(1);
            }
        }
        else if (Fe_PadPressed(PADB_TRIANGLE, -1, 1))
        {
            Net_FindStop();
            s_mode = LAN_MENU;
            s_sel = 1;
        }
    }
    else
    {
        /* direccion con el teclado: Intro (START) conecta, Esc cancela (las letras de los botones del teclado
         * escriben, asi que aqui no valen X ni triangulo) */
        Lan_Box("Join a Game");
        Lan_Text(0x17, 0x17, 0x17, "Type the address of the host PC:", 0x22, 0x40, 0);
        Fe_DrawBox(0x08, 0x08, 0x08, 0x22, 0x50, 0x118, 0x5e, 0);
        snprintf(line, sizeof(line), "%s%s", s_addr, (g_FrameTime / 150) & 1 ? "_" : "");
        Lan_Text(0x1f, 0x1c, 0, line, 0x26, 0x52, 0);
        Lan_Help("Enter: Connect   Esc: Cancel");
        const uint8_t* keys = SDL_GetKeyboardState(NULL);
        if (Fe_PadPressed(PADB_START, -1, 0) && s_addr[0])
        {
            Lan_Typing(0);
            Snd_PlayUi(0x48, NULL);
            Net_FindStop();
            Net_Join(s_addr, NET_DEFAULT_PORT);
            s_mode = LAN_FIND;
        }
        else if (keys[SDL_SCANCODE_ESCAPE])
        {
            Lan_Typing(0);
            s_mode = LAN_FIND;
            Net_FindStart();
        }
    }
    if (fe->nextScreen != -1 && fe->nextScreen != 2100)
    {
        Net_FindStop();
        Lan_Typing(0);
    }
    Fe_PadRepeatReset();
}
