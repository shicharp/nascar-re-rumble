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
int       g_RaceExit = 3;
int       g_BootState;
short     g_Venue;
char      g_TrackName[32];
char      g_SplitScreen;
uint8_t   g_PlayerTeam[2];

static_assert(sizeof(GameInfo) == 0xac, "GameInfo debe medir 0xAC como en el original");

int g_Ticks;
int g_GameTime;
int g_FrameTime;
static int s_lastVBlank = -1;

int Timer_GetVBlank(void)
{
    return VSync(-1);   /* PsyCross: contador de vblanks en tiempo real (60 Hz NTSC) */
}

void Timer_Update(void)
{
    int now = Timer_GetVBlank();
    if (s_lastVBlank < 0)
        s_lastVBlank = now;
    int dv = now - s_lastVBlank;
    s_lastVBlank = now;
    g_Ticks += dv * 5;
    if (g_GameState == 4 || g_GameState == 13)
        g_GameTime += dv * 5;
}
