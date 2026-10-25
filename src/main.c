/* Port de NASCAR Rumble (PS1) sobre PsyCross: arranque del programa en el PC.
 *
 * Uso: nascar.exe [--disc <carpeta del disco extraido>] [--skip-legal]
 *                 [--shot salida.bmp [--shot-frame N]]   captura el frame N y sale
 *                 [--press frame:boton,...]                pulsaciones simuladas (pruebas)
 *                 [--movie fichero.wve]                    pone ese video y sale
 *                 [--viewer [fichero] [--page N]]          visor de paginas Cpag
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "PsyX/PsyX_public.h"

#include "game/nr.h"
#include "game/file.h"
#include "game/gfx.h"
#include "game/pad.h"
#include "game/game.h"
#include "debug_viewer.h"
#include "crash_log.h"

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
    }

    PsyX_Initialise((char*)"NASCAR Rumble", 960, 720, 0);
    CrashLog_Install();   /* solo en el port: la pila del fallo en la salida */

    if (!disc)
        disc = FindDiscRoot();
    if (!disc)
    {
        NR_Log("No encuentro el disco extraido; usa --disc <carpeta>\n");
        PsyX_Shutdown();
        return 1;
    }
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
        ret = Game_Main();
    }

    PsyX_Shutdown();
    return ret;
}
