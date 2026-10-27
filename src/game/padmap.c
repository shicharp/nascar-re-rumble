/* Solo en el port: botones del PC de cada boton del mando de la PS1 (ver padmap.h). */
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <SDL.h>
#include "PsyX/PsyX_public.h"
#include "nr.h"
#include "padmap.h"

#define PADMAP_FILE "controls.cfg"

/* en el orden de la pantalla y del fichero */
static const char* const s_names[PADMAP_BUTTONS] = { "up", "down", "left", "right", "cross", "circle", "square",
                                                     "triangle", "l1", "r1", "l2", "r2", "start", "select" };
static const size_t s_kbField[PADMAP_BUTTONS] = {
    offsetof(PsyXKeyboardMapping, kc_dpad_up), offsetof(PsyXKeyboardMapping, kc_dpad_down),
    offsetof(PsyXKeyboardMapping, kc_dpad_left), offsetof(PsyXKeyboardMapping, kc_dpad_right),
    offsetof(PsyXKeyboardMapping, kc_cross), offsetof(PsyXKeyboardMapping, kc_circle),
    offsetof(PsyXKeyboardMapping, kc_square), offsetof(PsyXKeyboardMapping, kc_triangle),
    offsetof(PsyXKeyboardMapping, kc_l1), offsetof(PsyXKeyboardMapping, kc_r1),
    offsetof(PsyXKeyboardMapping, kc_l2), offsetof(PsyXKeyboardMapping, kc_r2),
    offsetof(PsyXKeyboardMapping, kc_start), offsetof(PsyXKeyboardMapping, kc_select) };
static const size_t s_gcField[PADMAP_BUTTONS] = {
    offsetof(PsyXControllerMapping, gc_dpad_up), offsetof(PsyXControllerMapping, gc_dpad_down),
    offsetof(PsyXControllerMapping, gc_dpad_left), offsetof(PsyXControllerMapping, gc_dpad_right),
    offsetof(PsyXControllerMapping, gc_cross), offsetof(PsyXControllerMapping, gc_circle),
    offsetof(PsyXControllerMapping, gc_square), offsetof(PsyXControllerMapping, gc_triangle),
    offsetof(PsyXControllerMapping, gc_l1), offsetof(PsyXControllerMapping, gc_r1),
    offsetof(PsyXControllerMapping, gc_l2), offsetof(PsyXControllerMapping, gc_r2),
    offsetof(PsyXControllerMapping, gc_start), offsetof(PsyXControllerMapping, gc_select) };

static PsyXKeyboardMapping   s_defKb;   /* los de PsyCross: valen en los menus */
static PsyXControllerMapping s_defGc;
static PsyXKeyboardMapping   s_kb;      /* los del jugador: solo conduciendo (PadMap_Apply) */
static PsyXControllerMapping s_gc[2];

static int* Field(int port, int device, int button)
{
    if (button < 0 || button >= PADMAP_BUTTONS)
        return NULL;
    if (device == PADMAP_KEYBOARD)
        return (int*)((char*)&s_kb + s_kbField[button]);
    return (int*)((char*)&s_gc[port == 1] + s_gcField[button]);
}

static int ValidCode(int device, int code)
{
    if (device == PADMAP_KEYBOARD)
        return code > SDL_SCANCODE_UNKNOWN && code < SDL_NUM_SCANCODES;
    if (code & CONTROLLER_MAP_FLAG_AXIS)
        return (code & ~CONTROLLER_MAP_FLAG_AXIS) == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
               (code & ~CONTROLLER_MAP_FLAG_AXIS) == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    return code >= 0 && code < SDL_CONTROLLER_BUTTON_MAX;
}

/* fichero: "teclado.cross=6", "mando1.l2=16388"... (codigos de SDL; los gatillos llevan CONTROLLER_MAP_FLAG_AXIS) */
static void Load(void)
{
    FILE* f = fopen(PADMAP_FILE, "r");
    if (!f)
        return;
    char line[128];
    int n = 0;
    while (fgets(line, sizeof(line), f))
    {
        char dev[16], name[16];
        int code;
        if (line[0] == '#' || sscanf(line, "%15[^.].%15[^=]=%d", dev, name, &code) != 3)
            continue;
        int device = !strcmp(dev, "teclado") ? PADMAP_KEYBOARD : PADMAP_CONTROLLER;
        int port = !strcmp(dev, "mando2");
        if (device == PADMAP_CONTROLLER && port == 0 && strcmp(dev, "mando1"))
            continue;
        for (int b = 0; b < PADMAP_BUTTONS; b++)
            if (!strcmp(name, s_names[b]) && ValidCode(device, code))
            {
                *Field(port, device, b) = code;
                n++;
            }
    }
    fclose(f);
    NR_Log("Controles: %d asignaciones de %s\n", n, PADMAP_FILE);
}

void PadMap_Save(void)
{
    FILE* f = fopen(PADMAP_FILE, "w");
    if (!f)
    {
        NR_Log("Controles: no se puede escribir %s\n", PADMAP_FILE);
        return;
    }
    fprintf(f, "# NASCAR Rumble: teclas y botones de cada boton del mando de la PS1 (codigos de SDL).\n"
               "# Los cambia la pantalla de mandos (Game Options > Controller Config, SELECT).\n");
    static const char* const devs[3] = { "teclado", "mando1", "mando2" };
    for (int d = 0; d < 3; d++)
        for (int b = 0; b < PADMAP_BUTTONS; b++)
            fprintf(f, "%s.%s=%d\n", devs[d], s_names[b],
                    *Field(d - 1, d ? PADMAP_CONTROLLER : PADMAP_KEYBOARD, b));
    fclose(f);
}

void PadMap_Init(void)
{
    s_defKb = s_kb = g_cfg_keyboardMapping;
    s_defGc = s_gc[0] = s_gc[1] = g_cfg_controllerMapping;
    Load();
}

void PadMap_Apply(int driving)
{
    g_cfg_keyboardMapping = driving ? s_kb : s_defKb;
    g_cfg_controllerMapping = driving ? s_gc[0] : s_defGc;
    g_cfg_controllerMapping2 = driving ? s_gc[1] : s_defGc;
}

int PadMap_HasKeyboard(int port)
{
    return PsyX_Pad_GetKeyboardPorts() >> port & 1;
}

int PadMap_HasController(int port)
{
    return PsyX_Pad_GetGameController(port) != NULL;
}

int PadMap_Get(int port, int device, int button)
{
    int* p = Field(port, device, button);
    return p ? *p : -1;
}

void PadMap_Set(int port, int device, int button, int code)
{
    int* p = Field(port, device, button);
    if (!p || !ValidCode(device, code))
        return;
    for (int b = 0; b < PADMAP_BUTTONS; b++)
    {
        int* q = Field(port, device, b);
        if (b != button && *q == code)
            *q = *p;   /* la tenia otro boton: se queda con la de este, para que ninguno se quede sin nada */
    }
    *p = code;
}

void PadMap_Reset(int port)
{
    if (PadMap_HasKeyboard(port))
        s_kb = s_defKb;
    s_gc[port == 1] = s_defGc;
}

const char* PadMap_ButtonName(int button)
{
    return button >= 0 && button < PADMAP_BUTTONS ? s_names[button] : "";
}

/* con los nombres del mando de Xbox, que es el que SDL toma de modelo */
const char* PadMap_CodeName(int device, int code)
{
    static char buf[32];
    if (device == PADMAP_KEYBOARD)
    {
        const char* n = SDL_GetScancodeName((SDL_Scancode)code);
        snprintf(buf, sizeof(buf), "%s", n && *n ? n : "?");
        return buf;
    }
    if (code & CONTROLLER_MAP_FLAG_AXIS)
        return (code & ~CONTROLLER_MAP_FLAG_AXIS) == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? "LT" : "RT";
    static const char* const buttons[SDL_CONTROLLER_BUTTON_MAX] = {
        "A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3", "LB", "RB", "D-Pad Up", "D-Pad Down",
        "D-Pad Left", "D-Pad Right", "Misc", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", "Touchpad" };
    return code >= 0 && code < SDL_CONTROLLER_BUTTON_MAX && buttons[code] ? buttons[code] : "?";
}

/* ---------------------------------------------------------------- captura */

static int     s_capPort, s_capDevice;
static uint8_t s_capPrev[SDL_NUM_SCANCODES];   /* teclas o botones que ya estaban pulsados (hay que soltarlos antes) */

/* teclas que no se pueden asignar: Esc cancela, F1-F12 y Alt derecho son de PsyCross (Alt der. + Intro: pantalla
 * completa; F4 cambia los puertos del teclado...) */
static int KeyReserved(int sc)
{
    return sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_RALT || (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12);
}

/* estado de cada tecla, o de cada boton del mando (y los gatillos pasada la mitad, detras de los botones) */
static int CaptureState(uint8_t* out)
{
    if (s_capDevice == PADMAP_KEYBOARD)
    {
        int n = 0;
        const uint8_t* k = SDL_GetKeyboardState(&n);
        n = n < SDL_NUM_SCANCODES ? n : SDL_NUM_SCANCODES;
        memcpy(out, k, n);
        return n;
    }
    SDL_GameController* gc = (SDL_GameController*)PsyX_Pad_GetGameController(s_capPort);
    memset(out, 0, SDL_CONTROLLER_BUTTON_MAX + 2);
    if (gc)
    {
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++)
            out[b] = SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b);
        out[SDL_CONTROLLER_BUTTON_MAX] = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16384;
        out[SDL_CONTROLLER_BUTTON_MAX + 1] = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384;
    }
    return SDL_CONTROLLER_BUTTON_MAX + 2;
}

void PadMap_CaptureBegin(int port, int device)
{
    s_capPort = port;
    s_capDevice = device;
    CaptureState(s_capPrev);
}

int PadMap_CapturePoll(void)
{
    if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_ESCAPE])
        return -2;
    uint8_t now[SDL_NUM_SCANCODES];
    int n = CaptureState(now);
    int found = -1;
    for (int i = 0; i < n && found < 0; i++)
    {
        if (!now[i] || s_capPrev[i])
            continue;
        if (s_capDevice == PADMAP_KEYBOARD)
            found = KeyReserved(i) ? -1 : i;
        else if (i == SDL_CONTROLLER_BUTTON_GUIDE)
            continue;   /* (en Windows abre la barra de juegos) */
        else if (i >= SDL_CONTROLLER_BUTTON_MAX)
            found = (i == SDL_CONTROLLER_BUTTON_MAX ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT) |
                    CONTROLLER_MAP_FLAG_AXIS;
        else
            found = i;
    }
    memcpy(s_capPrev, now, n);   /* lo que se suelta se puede volver a pulsar */
    return found;
}
