#include "libetc.h"
#include "globals.h"

int       g_GameState;
int       g_IsFrontend;
GameInfo* g_GameInfo;
int       g_FrameDelta = 1;
int       g_FrameCount;
int       g_FrontendFrames;
char      g_LevelPath[128];
char      g_SelectedTrack[16];
int       g_BootState;

static_assert(sizeof(GameInfo) == 0xac, "GameInfo debe medir 0xAC como en el original");

int Timer_GetVBlank(void)
{
    return VSync(-1);   /* PsyCross: contador de vblanks en tiempo real (60 Hz NTSC) */
}
