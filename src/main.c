/* Port de NASCAR Rumble (PS1) sobre PsyCross: arranque del programa en el PC.
 *
 * Uso: nascar.exe [--disc <carpeta del disco extraido>] [--skip-legal]
 *                 [--shot salida.bmp [--shot-frame N]]   captura el frame N y sale
 *                 [--press frame:boton,...]                pulsaciones simuladas (pruebas)
 *                 [--movie fichero.wve]                    pone ese video y sale
 *                 [--viewer [fichero] [--page N]]          visor de paginas Cpag
 *                 [--display N]                            abre la ventana en el monitor N de SDL
 *                 [--fixed-step]                           carrera a paso fijo de 5 tics (red por entradas)
 *                 [--net-host | --net-join ip]             partida en LAN (anfitrion o cliente; ver net.h)
 *                 [--extract-disc imagen.bin]              saca la imagen del disco a la carpeta 'disc' y sale
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <io.h>

#include "PsyX/PsyX_public.h"
#define SDL_MAIN_HANDLED   /* main propio (PsyCross ya inicializa SDL) */
#include "SDL.h"

#include "game/nr.h"
#include "game/file.h"
#include "game/gfx.h"
#include "game/pad.h"
#include "game/padmap.h"
#include "game/net.h"
#include "game/game.h"
#include "debug_viewer.h"
#include "crash_log.h"
#include "disc_extract.h"

/* Solo en el port: lleva la ventana al centro del monitor 'display' (indices de SDL; se apuntan en el log). PsyCross
 * la crea donde quiere el sistema; es la ventana del contexto de OpenGL actual. */
static void MoveToDisplay(int display)
{
    int n = SDL_GetNumVideoDisplays();
    for (int i = 0; i < n; i++)
    {
        SDL_Rect r;
        if (SDL_GetDisplayBounds(i, &r) == 0)
            NR_Log("Monitor %d: %s (%d, %d) %d x %d\n", i, SDL_GetDisplayName(i), r.x, r.y, r.w, r.h);
    }
    SDL_Window* w = SDL_GL_GetCurrentWindow();
    if (!w || display < 0 || display >= n)
        return;
    SDL_SetWindowPosition(w, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
}

static int DirExists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IFDIR);
}

/* Busca la carpeta del disco extraido subiendo desde el directorio de trabajo */
static const char* FindDiscRoot(void)
{
    static const char* candidates[] = { "disc", "../disc", "../../disc", "../../../disc", "../../../../disc" };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        char probe[256];
        snprintf(probe, sizeof(probe), "%s/CW", candidates[i]);
        if (DirExists(probe))
            return candidates[i];
    }
    return NULL;
}

int main(int argc, char** argv)
{
    const char* disc = NULL;
    const char* viewerFile = NULL;
    const char* shot = NULL;
    int shotFrame = 120;
    int page = 0;
    int display = -1;
    int netHost = 0;
    const char* netJoin = NULL;
    const char* extract = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc) disc = argv[++i];
        else if (!strcmp(argv[i], "--viewer"))
            viewerFile = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "CW\\FEnd\\Fe.trk";
        else if (!strcmp(argv[i], "--page") && i + 1 < argc) page = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--shot-frame") && i + 1 < argc) shotFrame = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--skip-legal")) g_Options.skipLegal = 1;
        else if (!strcmp(argv[i], "--movie") && i + 1 < argc) g_Options.movie = argv[++i];
        else if (!strcmp(argv[i], "--press") && i + 1 < argc) Pad_SetScript(argv[++i]);
        else if (!strcmp(argv[i], "--display") && i + 1 < argc) display = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fixed-step")) g_Options.fixedStep = 1;
        else if (!strcmp(argv[i], "--net-host")) netHost = 1;
        else if (!strcmp(argv[i], "--net-join") && i + 1 < argc) netJoin = argv[++i];
        else if (!strcmp(argv[i], "--extract-disc") && i + 1 < argc) extract = argv[++i];
    }

    /* solo en el port: --extract-disc imagen.bin la saca a "disc" y sale; sin la carpeta del disco y con una imagen
     * .bin al lado, la saca la primera vez */
    if (extract)
        return Disc_Extract(extract, "disc") ? 0 : 1;
    if (!disc)
        disc = FindDiscRoot();
    if (!disc)
    {
        struct _finddata_t fd;
        intptr_t h = _findfirst("*.bin", &fd);
        if (h != -1)
        {
            _findclose(h);
            NR_Log("Extrayendo %s a la carpeta disc (solo la primera vez, tarda un poco)...\n", fd.name);
            if (Disc_Extract(fd.name, "disc"))
                disc = FindDiscRoot();
        }
    }
    if (!disc)
    {
        NR_Log("No encuentro el disco extraido; usa --disc <carpeta>\n");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NASCAR Rumble",
                                 "No encuentro los datos del disco.\n\n"
                                 "Pon la imagen de tu disco de NASCAR Rumble (USA) en formato .bin (MODE2/2352) junto a "
                                 "nascar.exe y vuelve a abrir el juego: la primera vez la extrae a la carpeta 'disc'.\n\n"
                                 "Si ya la tienes extraida, deja esa carpeta junto a nascar.exe con el nombre 'disc'.",
                                 NULL);
        return 1;
    }

    PsyX_Initialise((char*)"NASCAR Rumble", 960, 720, 0);
    CrashLog_Install();   /* solo en el port: la pila del fallo en la salida */
    PadMap_Init();        /* solo en el port: las teclas y botones de controls.cfg */
    if (display >= 0)
        MoveToDisplay(display);
    File_SetRoot(disc);
    NR_Log("Disco: %s\n", disc);

    Gfx_Init();
    if (shot)
        Gfx_SetAutoShot(shot, shotFrame);

    int ret;
    if (viewerFile)
    {
        Pad_Init();
        ret = DebugViewer_Run(viewerFile, page);
    }
    else
    {
        Net_Init();
        if (netHost)
            Net_Host(NET_DEFAULT_PORT);
        else if (netJoin)
            Net_Join(netJoin, NET_DEFAULT_PORT);
        ret = Game_Main();
        Net_Shutdown();
    }

    PsyX_Shutdown();
    return ret;
}
