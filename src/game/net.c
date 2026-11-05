/* Solo en el port: partidas en LAN con ENet (ver net.h). */
#include <enet/enet.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "PsyX/PsyX_public.h"
#include "nr.h"
#include "globals.h"
#include "frontend.h"
#include "racecar.h"
#include "race.h"
#include "carphys_int.h"
#include "pause.h"
#include "net.h"

#define NET_VERSION   12
#define NET_CHANNELS  2           /* 0 sala y ordenes (fiables y en orden); 1 pulsaciones y frames (sin garantia) */
#define NET_RING      2048        /* frames de pulsaciones guardados por humano (34 s: lo que puede atrasarse un cliente) */
#define NET_SUM_EVERY 30          /* frames entre sumas de control (medio segundo) */
#define NET_SUM_RING  16
#define NET_WAIT_MS   15000       /* sin noticias de una maquina tanto tiempo: el anfitrion la da por salida */
#define NET_RESEND_MS 30          /* sin nada nuevo, se repite el ultimo paquete cada tanto */
#define NET_GRACE_MS  35          /* lo que espera el anfitrion las pulsaciones de un cliente que iba a tiempo */
#define NET_HOLD      30          /* frames seguidos que se repite el ultimo mando de quien no llega; luego, sin mando */
#define NET_FRAME_MAX 32          /* frames decididos por paquete (y que quepan en ~1100 bytes) */
#define NET_AHEAD     64          /* lo mas adelantado que se acepta un frame respecto al ultimo decidido */
#define NET_CATCHUP   2           /* cliente: frames por detras del anfitrion a partir de los que corre sin esperar */

enum
{
    MSG_HELLO = 1,    /* cliente -> anfitrion: version, nombre, piloto, cambio */
    MSG_WELCOME,      /* anfitrion -> cliente: su numero de maquina */
    MSG_REJECT,       /* anfitrion -> cliente: motivo (texto) */
    MSG_LOBBY,        /* anfitrion -> todos: la lista de jugadores */
    MSG_START,        /* anfitrion -> los de la carrera: NetRaceSetup */
    MSG_INPUT,        /* cliente -> anfitrion: ultimo frame decidido que tiene y las pulsaciones de su humano (las de
                         un frame y las anteriores; sin garantia) */
    MSG_LEAVE,        /* cliente -> anfitrion: deja la carrera */
    MSG_SUM,          /* cliente -> anfitrion: sumas de la carrera en un frame */
    MSG_DESYNC,       /* anfitrion -> todos: frame desincronizado */
    MSG_UPDATE,       /* cliente -> anfitrion: su piloto o su nombre han cambiado */
    MSG_STAGE,        /* cliente -> anfitrion: donde esta (NET_P_*) */
    MSG_CUPQUIT,      /* cliente -> anfitrion: deja el campeonato */
    MSG_CUPEND,       /* anfitrion -> todos: el campeonato se acaba antes de la ultima carrera */
    MSG_FRAME,        /* anfitrion -> cliente: frames decididos (pulsaciones de todos y quien ya no manda; sin
                         garantia, desde el ultimo que el cliente dice tener) */
};

/* busqueda: la pregunta y la respuesta (con la version, los jugadores y el nombre del anfitrion) */
static const char s_findAsk[8] = { 'N', 'R', 'U', 'M', 'B', 'L', 'E', '?' };
static const char s_findAnswer[8] = { 'N', 'R', 'U', 'M', 'B', 'L', 'E', '!' };

static ENetHost* s_enet;
static ENetPeer* s_server;                      /* cliente: la conexion con el anfitrion */
static ENetPeer* s_peers[NET_MAX_MACHINES];     /* anfitrion: la de cada maquina */
static int       s_isHost, s_active, s_machine = -1;
static int       s_assists;                     /* NET_ASSIST_* de la sala (las elige el anfitrion) */
static int       s_stage = -1;                  /* cliente: lo ultimo que ha dicho de donde esta (NET_P_*) */
static uint32_t  s_cupMask;                     /* anfitrion: maquinas del campeonato en curso (bit 0 el) */
static int       s_cup;                         /* cliente: esta en un campeonato en curso */
static NetPlayer s_players[NET_MAX_MACHINES];
static char      s_status[64];
static int       s_init;
static ENetSocket s_findHostSock = ENET_SOCKET_NULL;   /* anfitrion: contesta a las busquedas */
static ENetSocket s_findSock = ENET_SOCKET_NULL;       /* cliente: pregunta por difusion */
static NetFound   s_found[NET_FIND_MAX];
static int        s_numFound;
static uint32_t   s_findLastAsk;

/* carrera */
typedef struct
{
    uint32_t frame;               /* frame al que corresponde (0 vacio) */
    uint32_t bits;                /* ctrl +8: bits de accion */
    uint8_t  ax, ay, kind;        /* ctrl +0xC / +0xD / +0xE: stick y tipo de mando */
} NetInput;

static int          s_inRace, s_startPending, s_myHuman = -1, s_frame, s_desync;
static int          s_waiting;                      /* Net_RaceFrame espera las pulsaciones del frame s_frame */
static int          s_delay = NET_INPUT_DELAY;      /* retardo de esta carrera (NetRaceSetup.inputDelay) */
static NetRaceSetup s_setup;
static NetInput     s_inputs[NET_MAX_HUMANS][NET_RING];   /* las decididas (anfitrion: tambien las recibidas) */
static uint32_t     s_leftFrom[NET_MAX_MACHINES];   /* frame desde el que la maquina ya no manda (0 sigue) */
/* frames decididos: hasta s_confirmed todos (el cliente, sin huecos); de cada uno, las maquinas que ya no mandan */
static uint32_t     s_confirmed;
static uint32_t     s_maskFrame[NET_RING];          /* frame decidido del hueco (0 ninguno) */
static uint8_t      s_frameMask[NET_RING];
/* anfitrion */
static int          s_hostRace;                     /* hay carrera en red (aunque el anfitrion ya haya salido) */
static uint32_t     s_clockFrame, s_clockTime;      /* su ultimo frame y cuando (el reloj de la carrera si ha salido) */
static uint32_t     s_ack[NET_MAX_MACHINES];        /* ultimo frame decidido que tiene cada cliente */
static uint32_t     s_sentTo[NET_MAX_MACHINES], s_sentTime[NET_MAX_MACHINES];   /* ultimo paquete FRAME */
static uint32_t     s_lastRecv[NET_MAX_MACHINES];   /* enet_time del ultimo paquete de pulsaciones */
static uint8_t      s_peerRacing[NET_MAX_MACHINES]; /* el cliente sigue en la carrera (aunque ya no mande) */
static uint8_t      s_started[NET_MAX_MACHINES];    /* ya ha mandado algo (hasta entonces se le espera: carga) */
static uint8_t      s_late[NET_MAX_MACHINES];       /* su ultimo frame se decidio sin sus pulsaciones */
static int          s_subRun[NET_MAX_HUMANS];       /* frames seguidos decididos sin las pulsaciones del humano */
static uint32_t     s_subFrom[NET_MAX_HUMANS];
/* cliente */
static NetInput     s_own[NET_RING];                /* las pulsaciones propias, para mandarlas */
static uint32_t     s_ownLast;                      /* el ultimo frame de s_own */
static uint32_t     s_hostSeen;                     /* el frame mas alto del anfitrion que ha llegado */
static int          s_hostLate;                     /* y si entonces sus pulsaciones le llegaban tarde */
static int          s_alone;                        /* sin anfitrion: la carrera sigue solo en esta maquina */
static int          s_catchUp;                      /* va atrasado: el siguiente frame no espera al reloj */
static char         s_humanNames[NET_MAX_HUMANS][NET_NAME_LEN + 1];   /* los de la ultima carrera */
static struct
{
    uint32_t frame;
    uint32_t sum[NET_MAX_MACHINES][3];
    uint8_t  have[NET_MAX_MACHINES];
} s_sums[NET_SUM_RING];

/* ---------------------------------------------------------------- log */

/* Solo para estas partidas: los sucesos de la red van a la salida y a red_m<maquina>.txt (junto al ejecutable),
 * con las sumas de cada punto de control; si una carrera se desincroniza, los ficheros de las dos maquinas dicen en
 * que frame y en que parte (coches, actores y guiones, aleatorios) */
static FILE* s_logFile;

static void Net_LogV(int toStdout, const char* fmt, va_list ap)
{
    if (toStdout)
    {
        va_list ap2;
        va_copy(ap2, ap);
        vprintf(fmt, ap2);
        va_end(ap2);
        fflush(stdout);
    }
    if (!s_logFile && s_machine >= 0)
    {
        char name[32];
        snprintf(name, sizeof(name), "red_m%d.txt", s_machine);
        s_logFile = fopen(name, "w");
    }
    if (s_logFile)
    {
        vfprintf(s_logFile, fmt, ap);
        fflush(s_logFile);
    }
}

static void Net_Log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    Net_LogV(1, fmt, ap);
    va_end(ap);
}

static void Net_LogFile(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    Net_LogV(0, fmt, ap);
    va_end(ap);
}

/* ---------------------------------------------------------------- mensajes */

typedef struct
{
    uint8_t buf[1024];
    int     len;
} Msg;

static void Msg_Begin(Msg* m, int type)
{
    m->len = 0;
    m->buf[m->len++] = (uint8_t)type;
}

static void Msg_U8(Msg* m, int v)
{
    if (m->len < (int)sizeof(m->buf))
        m->buf[m->len++] = (uint8_t)v;
}

static void Msg_U32(Msg* m, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        Msg_U8(m, (int)(v >> (i * 8) & 0xff));
}

static void Msg_Bytes(Msg* m, const void* p, int n)
{
    for (int i = 0; i < n; i++)
        Msg_U8(m, ((const uint8_t*)p)[i]);
}

typedef struct
{
    const uint8_t* p;
    int            len, pos, bad;
} Rd;

static int Rd_U8(Rd* r)
{
    if (r->pos >= r->len)
    {
        r->bad = 1;
        return 0;
    }
    return r->p[r->pos++];
}

static uint32_t Rd_U32(Rd* r)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        v |= (uint32_t)Rd_U8(r) << (i * 8);
    return v;
}

static void Rd_Bytes(Rd* r, void* out, int n)
{
    for (int i = 0; i < n; i++)
        ((uint8_t*)out)[i] = (uint8_t)Rd_U8(r);
}

static void SendRaw(ENetPeer* p, const uint8_t* data, int len, int channel)
{
    if (!p)
        return;
    ENetPacket* pk = enet_packet_create(data, (size_t)len, ENET_PACKET_FLAG_RELIABLE);
    if (enet_peer_send(p, (enet_uint8)channel, pk) < 0)
        enet_packet_destroy(pk);
}

static void Send(ENetPeer* p, const Msg* m, int channel)
{
    SendRaw(p, m->buf, m->len, channel);
}

/* sin garantia ni orden (las pulsaciones: cada paquete repite las anteriores) */
static void SendQuick(ENetPeer* p, const uint8_t* data, int len)
{
    if (!p)
        return;
    /* pruebas: NR_NETLOSS=n pierde el n % de estos paquetes al mandarlos */
    static int s_loss = -1;
    static uint32_t s_lossSeed = 12345;
    if (s_loss < 0)
        s_loss = getenv("NR_NETLOSS") ? atoi(getenv("NR_NETLOSS")) : 0;
    if (s_loss > 0)
    {
        s_lossSeed = s_lossSeed * 1103515245u + 12345u;
        if ((int)((s_lossSeed >> 16) % 100) < s_loss)
            return;
    }
    ENetPacket* pk = enet_packet_create(data, (size_t)len, ENET_PACKET_FLAG_UNSEQUENCED);
    if (enet_peer_send(p, 1, pk) < 0)
        enet_packet_destroy(pk);
}

/* anfitrion: a todos los clientes salvo 'except' */
static void BroadcastRaw(const uint8_t* data, int len, int channel, int except)
{
    for (int i = 1; i < NET_MAX_MACHINES; i++)
        if (s_peers[i] && i != except)
            SendRaw(s_peers[i], data, len, channel);
}

static void Broadcast(const Msg* m, int channel, int except)
{
    BroadcastRaw(m->buf, m->len, channel, except);
}

static void Msg_Player(Msg* m, const NetPlayer* p)
{
    Msg_U8(m, p->driver);
    Msg_U8(m, p->car);
    Msg_U8(m, p->transmission);
    Msg_U8(m, p->inRace);
    Msg_U8(m, p->team);
    Msg_Bytes(m, p->name, NET_NAME_LEN + 1);
}

static void Rd_Player(Rd* r, NetPlayer* p)
{
    p->driver = (uint8_t)Rd_U8(r);
    p->car = (uint8_t)Rd_U8(r);
    p->transmission = (uint8_t)Rd_U8(r);
    p->inRace = (uint8_t)Rd_U8(r);
    p->team = (uint8_t)Rd_U8(r);
    Rd_Bytes(r, p->name, NET_NAME_LEN + 1);
    p->name[NET_NAME_LEN] = 0;
}

int Net_TeamCount(int team, int except)
{
    int n = 0;
    for (int i = 0; i < NET_MAX_MACHINES; i++)
        if (i != except && s_players[i].used && s_players[i].team == team)
            n++;
    return n;
}

/* anfitrion: el equipo de la maquina id: el que pide si tiene sitio, si no el que tenia, si no el primero libre
 * (con 8 jugadores y 4 equipos de 2 siempre hay sitio) */
static int Host_Team(int id, int want, int cur)
{
    if (want < NET_TEAMS && Net_TeamCount(want, id) < 2)
        return want;
    if (cur < NET_TEAMS && Net_TeamCount(cur, id) < 2)
        return cur;
    for (int t = 0; t < NET_TEAMS; t++)
        if (Net_TeamCount(t, id) < 2)
            return t;
    return 0;
}

static int s_reqTeam = -1;   /* Net_SetTeam: el equipo que se pide en el siguiente LocalPlayer */

/* el jugador de esta maquina, con los datos del front-end (el equipo: el que tiene en la sala o el que pide; 0xFF
 * sin numero de maquina todavia: el anfitrion le da uno) */
static void LocalPlayer(NetPlayer* p)
{
    memset(p, 0, sizeof(*p));
    p->used = 1;
    p->driver = g_Settings.lastDriver[0];
    p->car = g_Settings.lastCar[0] <= 2 ? g_Settings.lastCar[0] : 0;
    p->transmission = g_Settings.transmission[0] <= 5 ? g_Settings.transmission[0] : 0;
    if (g_Settings.playerNameSet[0] && g_Settings.playerName[0][0])
        snprintf(p->name, sizeof(p->name), "%s", g_Settings.playerName[0]);
    else
        snprintf(p->name, sizeof(p->name), "Player");
    p->team = (uint8_t)(s_reqTeam >= 0 ? s_reqTeam
                        : s_machine >= 0 && s_players[s_machine].used ? s_players[s_machine].team : 0xff);
}

static void SetStatus(const char* s)
{
    snprintf(s_status, sizeof(s_status), "%s", s);
    Net_Log("[red] %s\n", s);
}

static void Host_SendLobby(void)
{
    Msg m;
    Msg_Begin(&m, MSG_LOBBY);
    for (int i = 0; i < NET_MAX_MACHINES; i++)
    {
        Msg_U8(&m, s_players[i].used);
        if (s_players[i].used)
            Msg_Player(&m, &s_players[i]);
    }
    Msg_U8(&m, s_assists);
    Broadcast(&m, 0, -1);
    int n = Net_NumPlayers();
    Net_Log("[red] sala: %d jugador%s\n", n, n == 1 ? "" : "es");
}

/* ---------------------------------------------------------------- carrera: pulsaciones y salidas */

static int MachineOfHuman(int h)
{
    return h >= 0 && h < s_setup.numHumans ? s_setup.humanMachine[h] : -1;
}

static void StoreInput(NetInput* ring, uint32_t frame, uint32_t bits, int ax, int ay, int kind)
{
    if (frame == 0)
        return;
    NetInput* in = &ring[frame % NET_RING];
    in->frame = frame;
    in->bits = bits;
    in->ax = (uint8_t)ax;
    in->ay = (uint8_t)ay;
    in->kind = (uint8_t)kind;
}

/* sin mando: lo que lleva el coche de quien se ha ido */
static void StoreNone(NetInput* ring, uint32_t frame)
{
    StoreInput(ring, frame, 0, 0x80, 0x80, 4);
}

/* pulsaciones del humano h de los frames (last - count, last] de 'ring': numero de humano, ultimo frame, cuantas, y
 * cada una */
static void Msg_Inputs(Msg* m, const NetInput* ring, int h, uint32_t last, int count)
{
    if ((uint32_t)count > last)
        count = (int)last;
    Msg_U8(m, h);
    Msg_U32(m, last);
    int pos = m->len;
    Msg_U8(m, 0);
    int n = 0;
    for (uint32_t f = last - (uint32_t)count + 1; f <= last && count > 0; f++)
    {
        const NetInput* in = &ring[f % NET_RING];
        if (in->frame != f)
            continue;
        Msg_U32(m, f);
        Msg_U32(m, in->bits);
        Msg_U8(m, in->ax);
        Msg_U8(m, in->ay);
        Msg_U8(m, in->kind);
        n++;
    }
    m->buf[pos] = (uint8_t)n;
}

/* la maquina ya no manda en el frame g */
static int LeftAt(int machine, uint32_t g)
{
    return machine < 0 || (s_leftFrom[machine] && g >= s_leftFrom[machine]);
}

/* anfitrion: las pulsaciones de un cliente (solo las de su humano y de frames sin decidir; las que llegan tarde
 * ya no cuentan) */
static void Host_RdInputs(Rd* r, int from)
{
    int h = Rd_U8(r);
    Rd_U32(r);
    int n = Rd_U8(r);
    for (int i = 0; i < n && !r->bad; i++)
    {
        uint32_t frame = Rd_U32(r), bits = Rd_U32(r);
        int ax = Rd_U8(r), ay = Rd_U8(r), kind = Rd_U8(r);
        if (!r->bad && h < s_setup.numHumans && MachineOfHuman(h) == from && frame > s_confirmed &&
            frame <= s_confirmed + NET_AHEAD && !LeftAt(from, frame) && s_inputs[h][frame % NET_RING].frame != frame)
            StoreInput(s_inputs[h], frame, bits, ax, ay, kind);
    }
}

/* la maquina deja de mandar: su humano va sin mando desde 'from' (los frames anteriores ya estan en camino). Si ya
 * estaba marcada desde mas tarde, se adelanta (sin anfitrion no va a llegar nada mas) */
static void MarkLeft(int machine, uint32_t from)
{
    if (!from)
        from = 1;
    if (machine < 0 || machine >= NET_MAX_MACHINES || (s_leftFrom[machine] && s_leftFrom[machine] <= from))
        return;
    s_leftFrom[machine] = from;
    s_players[machine].inRace = NET_P_BUSY;   /* (el cliente dice despues donde esta) */
    if (s_inRace)
        Net_Log("[red] la maquina %d deja la carrera (desde el frame %u)\n", machine, s_leftFrom[machine]);
}

/* anfitrion: la maquina va sin mando desde el primer frame sin decidir (los decididos ya llevan sus pulsaciones).
 * Los clientes se enteran con los frames: cada uno dice que maquinas ya no mandan */
static void Host_MachineLeaves(int machine)
{
    if (s_leftFrom[machine] || !s_hostRace)
        return;
    MarkLeft(machine, s_confirmed + 1);
    Host_SendLobby();
}

static void ApplyInput(int h, uint32_t frame)
{
    RaceCar* e = g_Humans[h];
    if (!e || !e->ctrl)
        return;
    const NetInput* in = &s_inputs[h][frame % NET_RING];
    uint8_t* ctrl = e->ctrl;
    if (in->frame == frame)
    {
        *(uint32_t*)(ctrl + 8) = in->bits;
        ctrl[0xc] = in->ax;
        ctrl[0xd] = in->ay;
        ctrl[0xe] = in->kind;
    }
    else
    {
        /* se ha ido: sin mando */
        *(uint32_t*)(ctrl + 8) = 0;
        ctrl[0xc] = ctrl[0xd] = 0x80;
        ctrl[0xe] = 4;
    }
}

/* ---------------------------------------------------------------- carrera: el anfitrion decide los frames */

/* el humano vuelve a llegar a tiempo */
static void Host_OnTime(int h)
{
    int m = MachineOfHuman(h);
    if (s_subRun[h])
    {
        if (s_subRun[h] >= 60)
            Net_Log("[red] la maquina %d no ha llegado a %d frames (desde el %u)\n", m, s_subRun[h], s_subFrom[h]);
        else
            Net_LogFile("[red] la maquina %d no ha llegado a %d frames (desde el %u)\n", m, s_subRun[h], s_subFrom[h]);
    }
    s_subRun[h] = 0;
    if (m >= 0)
        s_late[m] = 0;
}

/* el frame g queda decidido: los que ya no mandan, sin mando (aunque hubiera llegado algo suyo) */
static void Host_Seal(uint32_t g)
{
    uint8_t mask = 0;
    for (int m = 0; m < NET_MAX_MACHINES; m++)
        if (LeftAt(m, g))
            mask |= (uint8_t)(1u << m);
    for (int h = 0; h < s_setup.numHumans; h++)
        if (LeftAt(MachineOfHuman(h), g))
            StoreNone(s_inputs[h], g);
    s_frameMask[g % NET_RING] = mask;
    s_maskFrame[g % NET_RING] = g;
    s_confirmed = g;
}

/* decide los frames que ya tienen las pulsaciones de todos (pueden ir por delante del suyo; si ya no manda nadie,
 * solo los decide el reloj, Host_ForceTo) */
static void Host_ConfirmReady(void)
{
    for (;;)
    {
        uint32_t g = s_confirmed + 1;
        int active = 0;
        for (int h = 0; h < s_setup.numHumans; h++)
        {
            if (LeftAt(MachineOfHuman(h), g))
                continue;
            if (s_inputs[h][g % NET_RING].frame != g)
                return;
            active++;
        }
        if (!active)
            return;
        for (int h = 0; h < s_setup.numHumans; h++)
            if (!LeftAt(MachineOfHuman(h), g))
                Host_OnTime(h);
        Host_Seal(g);
    }
}

/* decide hasta el frame f aunque falten pulsaciones: al que no llega se le repite su ultimo mando NET_HOLD frames
 * (medio segundo de wifi malo apenas se nota) y despues va sin mando hasta que vuelva */
static void Host_ForceTo(uint32_t f)
{
    for (uint32_t g = s_confirmed + 1; g <= f; g++)
    {
        for (int h = 0; h < s_setup.numHumans; h++)
        {
            int m = MachineOfHuman(h);
            NetInput* in = &s_inputs[h][g % NET_RING];
            if (LeftAt(m, g))
                continue;
            if (in->frame == g)
            {
                Host_OnTime(h);
                continue;
            }
            const NetInput* prev = &s_inputs[h][(g - 1) % NET_RING];
            if (!s_subRun[h])
                s_subFrom[h] = g;
            s_subRun[h]++;
            if (s_subRun[h] <= NET_HOLD && prev->frame == g - 1)
            {
                *in = *prev;
                in->frame = g;
            }
            else
                StoreNone(s_inputs[h], g);
            s_late[m] = 1;
        }
        Host_Seal(g);
    }
}

/* el frame del anfitrion: el suyo o, si ya ha salido de la carrera, el que le tocaria (60 Hz desde el ultimo) */
static uint32_t Host_ClockFrame(void)
{
    if (s_inRace || !s_clockFrame)
        return (uint32_t)s_frame;
    return s_clockFrame + (enet_time_get() - s_clockTime) * 60 / 1000;
}

/* a cada cliente que corre, los frames decididos desde el ultimo que dice tener: cuando hay alguno nuevo que
 * mandarle o, si no, cada NET_RESEND_MS (su respuesta pudo perderse) */
static void Host_SendFrames(void)
{
    int per = 1 + 7 * s_setup.numHumans;
    int maxN = 1100 / per < NET_FRAME_MAX ? 1100 / per : NET_FRAME_MAX;
    enet_uint32 now = enet_time_get();
    uint32_t hostFrame = Host_ClockFrame();
    for (int m = 1; m < NET_MAX_MACHINES; m++)
    {
        if (!s_peers[m] || !s_peerRacing[m] || s_ack[m] >= s_confirmed)
            continue;
        if (s_confirmed - s_ack[m] > NET_RING - 2 * NET_AHEAD)
        {
            /* tan atrasado que ya no se guarda lo que le falta */
            Net_Log("[red] la maquina %d va %u frames por detras: fuera de la carrera\n", m, s_confirmed - s_ack[m]);
            s_peerRacing[m] = 0;
            Host_MachineLeaves(m);
            continue;
        }
        uint32_t from = s_ack[m] + 1, to = s_confirmed;
        if (to - from + 1 > (uint32_t)maxN)
            to = from + (uint32_t)maxN - 1;
        if (to <= s_sentTo[m] && now - s_sentTime[m] < NET_RESEND_MS)
            continue;
        Msg msg;
        Msg_Begin(&msg, MSG_FRAME);
        Msg_U32(&msg, hostFrame);
        Msg_U8(&msg, s_late[m]);
        Msg_U32(&msg, from);
        Msg_U8(&msg, (int)(to - from + 1));
        for (uint32_t g = from; g <= to; g++)
        {
            Msg_U8(&msg, s_frameMask[g % NET_RING]);
            for (int h = 0; h < s_setup.numHumans; h++)
            {
                const NetInput* in = &s_inputs[h][g % NET_RING];
                Msg_U32(&msg, in->bits);
                Msg_U8(&msg, in->ax);
                Msg_U8(&msg, in->ay);
                Msg_U8(&msg, in->kind);
            }
        }
        SendQuick(s_peers[m], msg.buf, msg.len);
        s_sentTo[m] = to;
        s_sentTime[m] = now;
    }
}

/* la maquina que lleva NET_WAIT_MS sin mandar nada deja la carrera (se le cuenta como llegada; si vuelve, mira) */
static void Host_CheckTimeouts(void)
{
    if (!s_clockFrame)
        return;   /* (hasta el primer frame del anfitrion: todos cargando) */
    enet_uint32 now = enet_time_get();
    for (int m = 1; m < NET_MAX_MACHINES; m++)
        if (!s_leftFrom[m] && now - s_lastRecv[m] > NET_WAIT_MS)
        {
            Net_Log("[red] la maquina %d no manda nada desde hace %d s\n", m, NET_WAIT_MS / 1000);
            Host_MachineLeaves(m);
        }
}

/* fuera de Net_RaceFrame (Net_Poll): lo que ya se puede decidir y mandar; si el anfitrion ya ha salido, el reloj
 * de la carrera sigue para los que corren, hasta que todos se van y lo tienen */
static void Host_Pump(void)
{
    if (!s_hostRace)
        return;
    Host_CheckTimeouts();
    Host_ConfirmReady();
    if (!s_inRace && s_clockFrame)
    {
        /* cuando ya no manda nadie (y esta decidido), los que quedan mirando siguen solos: solo falta que tengan
         * hasta ahi */
        int allLeft = 1, acked = 1;
        for (int m = 0; m < NET_MAX_MACHINES; m++)
        {
            if (!s_leftFrom[m] || s_confirmed < s_leftFrom[m])
                allLeft = 0;
            if (s_peers[m] && s_peerRacing[m] && s_ack[m] < s_confirmed)
                acked = 0;
        }
        uint32_t vf = Host_ClockFrame();
        if (!allLeft && vf > s_confirmed + 2)
            Host_ForceTo(vf - 2);   /* (dos frames de margen, como su espera de NET_GRACE_MS) */
        if (allLeft && acked)
        {
            s_hostRace = 0;
            Net_Log("[red] todos han dejado la carrera\n");
            return;
        }
    }
    Host_SendFrames();
}

/* ---------------------------------------------------------------- carrera: sumas de control */

static void Sums_Store(uint32_t frame, int machine, const uint32_t* sum)
{
    int slot = (int)(frame / NET_SUM_EVERY) % NET_SUM_RING;
    if (s_sums[slot].frame != frame)
    {
        memset(&s_sums[slot], 0, sizeof(s_sums[slot]));
        s_sums[slot].frame = frame;
    }
    memcpy(s_sums[slot].sum[machine], sum, 3 * sizeof(uint32_t));
    s_sums[slot].have[machine] = 1;
    if (!s_isHost || !s_sums[slot].have[0] || s_desync)
        return;
    for (int m = 1; m < NET_MAX_MACHINES; m++)
        if (s_sums[slot].have[m] && memcmp(s_sums[slot].sum[m], s_sums[slot].sum[0], 3 * sizeof(uint32_t)))
        {
            s_desync = (int)frame;
            Net_Log("[red] DESINCRONIZACION en el frame %u: maquina %d (coches %08x/%08x actores %08x/%08x aleatorios "
                   "%08x/%08x)\n", frame, m, s_sums[slot].sum[0][0], s_sums[slot].sum[m][0], s_sums[slot].sum[0][1],
                   s_sums[slot].sum[m][1], s_sums[slot].sum[0][2], s_sums[slot].sum[m][2]);
            Msg msg;
            Msg_Begin(&msg, MSG_DESYNC);
            Msg_U32(&msg, frame);
            Broadcast(&msg, 0, -1);
            break;
        }
}

/* ---------------------------------------------------------------- anfitrion */

/* huella del ejecutable (FNV-1a de nascar.exe): en lockstep todos tienen que llevar el mismo (otra compilacion puede
 * calcular distinto aunque el protocolo sea el mismo) */
static uint32_t Net_BuildHash(void)
{
    static uint32_t s_hash;
    if (s_hash)
        return s_hash;
    char path[MAX_PATH];
    uint32_t h = 2166136261u;
    if (GetModuleFileNameA(NULL, path, sizeof(path)))
    {
        FILE* f = fopen(path, "rb");
        if (f)
        {
            static uint8_t buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
                for (size_t i = 0; i < n; i++)
                    h = (h ^ buf[i]) * 16777619u;
            fclose(f);
        }
    }
    s_hash = h ? h : 1;
    return s_hash;
}

static void Host_Hello(ENetPeer* peer, Rd* r)
{
    int ver = Rd_U8(r);
    uint32_t build = Rd_U32(r);
    NetPlayer p;
    memset(&p, 0, sizeof(p));
    Rd_Player(r, &p);
    p.used = 1;
    p.inRace = 0;
    const char* why = NULL;
    int id = -1;
    if (r->bad || ver != NET_VERSION || build != Net_BuildHash())
        why = "Different game version (use the same nascar.exe)";
    else
    {
        for (int i = 1; i < NET_MAX_MACHINES && id < 0; i++)
            if (!s_players[i].used)
                id = i;
        if (id < 0)
            why = "The game is full";
    }
    Msg m;
    if (why)
    {
        Msg_Begin(&m, MSG_REJECT);
        Msg_Bytes(&m, why, (int)strlen(why) + 1);
        Send(peer, &m, 0);
        enet_peer_disconnect_later(peer, 0);
        Net_Log("[red] rechazado: %s\n", why);
        return;
    }
    p.team = (uint8_t)Host_Team(id, p.team, 0xff);
    s_players[id] = p;
    s_peers[id] = peer;
    peer->data = (void*)(intptr_t)id;
    Msg_Begin(&m, MSG_WELCOME);
    Msg_U8(&m, id);
    Send(peer, &m, 0);
    Net_Log("[red] entra %s (maquina %d)\n", p.name, id);
    Host_SendLobby();
}

static void Host_Drop(ENetPeer* peer)
{
    int id = (int)(intptr_t)peer->data;
    if (id <= 0 || id >= NET_MAX_MACHINES || s_peers[id] != peer)
        return;
    Net_Log("[red] sale %s (maquina %d)\n", s_players[id].name, id);
    if (s_hostRace)
        Host_MachineLeaves(id);
    s_peerRacing[id] = 0;
    s_cupMask &= ~(1u << id);
    s_peers[id] = NULL;
    memset(&s_players[id], 0, sizeof(s_players[id]));
    Host_SendLobby();
}

static void Host_Receive(ENetPeer* peer, const uint8_t* data, int len, int channel)
{
    Rd r = { data, len, 0, 0 };
    int type = Rd_U8(&r);
    int from = (int)(intptr_t)peer->data;
    switch (type)
    {
    case MSG_HELLO:
        Host_Hello(peer, &r);
        break;
    case MSG_UPDATE:
        if (from > 0 && from < NET_MAX_MACHINES && s_players[from].used)
        {
            NetPlayer p = s_players[from];
            Rd_Player(&r, &p);
            if (!r.bad)
            {
                p.inRace = s_players[from].inRace;
                p.team = (uint8_t)Host_Team(from, p.team, s_players[from].team);
                s_players[from] = p;
                Host_SendLobby();
            }
        }
        break;
    case MSG_INPUT:
    {
        uint32_t ack = Rd_U32(&r);
        if (r.bad || !s_hostRace || from <= 0 || from >= NET_MAX_MACHINES)
            break;
        if (ack > s_ack[from] && ack <= s_confirmed)
            s_ack[from] = ack;
        s_lastRecv[from] = enet_time_get();
        s_started[from] = 1;
        Host_RdInputs(&r, from);
        (void)channel;
        break;
    }
    case MSG_LEAVE:
        if (from > 0 && from < NET_MAX_MACHINES)
        {
            Host_MachineLeaves(from);
            s_peerRacing[from] = 0;
        }
        break;
    case MSG_STAGE:
    {
        int st = Rd_U8(&r);
        /* (mientras corre manda la carrera: la deja con MSG_LEAVE, que llega antes) */
        if (!r.bad && from > 0 && from < NET_MAX_MACHINES && s_players[from].used &&
            s_players[from].inRace != NET_P_RACING && st != NET_P_RACING && st <= NET_P_CHOOSING)
        {
            s_players[from].inRace = (uint8_t)st;
            Host_SendLobby();
        }
        break;
    }
    case MSG_CUPQUIT:
        if (from > 0 && from < NET_MAX_MACHINES && (s_cupMask & 1u << from))
        {
            s_cupMask &= ~(1u << from);
            Net_Log("[red] %s deja el campeonato\n", s_players[from].name);
        }
        break;
    case MSG_SUM:
    {
        uint32_t frame = Rd_U32(&r), sum[3];
        for (int i = 0; i < 3; i++)
            sum[i] = Rd_U32(&r);
        if (!r.bad && from > 0 && from < NET_MAX_MACHINES && s_inRace)
            Sums_Store(frame, from, sum);
        break;
    }
    default:
        break;
    }
}

/* ---------------------------------------------------------------- cliente */

static void Race_Reset(void)
{
    memset(s_inputs, 0, sizeof(s_inputs));
    memset(s_leftFrom, 0, sizeof(s_leftFrom));
    memset(s_maskFrame, 0, sizeof(s_maskFrame));
    memset(s_frameMask, 0, sizeof(s_frameMask));
    memset(s_own, 0, sizeof(s_own));
    memset(s_sentTo, 0, sizeof(s_sentTo));
    memset(s_sentTime, 0, sizeof(s_sentTime));
    memset(s_lastRecv, 0, sizeof(s_lastRecv));
    memset(s_peerRacing, 0, sizeof(s_peerRacing));
    memset(s_started, 0, sizeof(s_started));
    memset(s_late, 0, sizeof(s_late));
    memset(s_subRun, 0, sizeof(s_subRun));
    s_ownLast = 0;
    s_hostSeen = 0;
    s_hostLate = 0;
    s_alone = s_catchUp = 0;
    s_clockFrame = s_clockTime = 0;
    s_hostRace = s_isHost;
    s_delay = s_setup.inputDelay >= NET_INPUT_DELAY && s_setup.inputDelay <= NET_MAX_DELAY ? s_setup.inputDelay
                                                                                             : NET_INPUT_DELAY;
    memset(s_sums, 0, sizeof(s_sums));
    s_frame = 0;
    s_desync = 0;
    s_myHuman = -1;
    memset(s_humanNames, 0, sizeof(s_humanNames));
    /* las maquinas que no corren no mandan nada */
    for (int m = 0; m < NET_MAX_MACHINES; m++)
    {
        int racing = 0;
        for (int h = 0; h < s_setup.numHumans; h++)
            racing |= s_setup.humanMachine[h] == m;
        if (!racing)
            s_leftFrom[m] = 1;
        else if (s_isHost && m > 0)
            s_peerRacing[m] = 1;
    }
    for (int h = 0; h < s_setup.numHumans; h++)
    {
        if (s_setup.humanMachine[h] == s_machine)
            s_myHuman = h;
        int m = s_setup.humanMachine[h];
        snprintf(s_humanNames[h], sizeof(s_humanNames[h]), "%s",
                 m < NET_MAX_MACHINES && s_players[m].used ? s_players[m].name : "Player");
    }
    /* los primeros frames no tienen mando (nadie los ha leido todavia): decididos en todas las maquinas */
    s_confirmed = 0;
    for (uint32_t f = 1; f <= (uint32_t)s_delay; f++)
    {
        for (int h = 0; h < s_setup.numHumans; h++)
            StoreNone(s_inputs[h], f);
        uint8_t mask = 0;
        for (int m = 0; m < NET_MAX_MACHINES; m++)
            if (s_leftFrom[m])
                mask |= (uint8_t)(1u << m);
        s_frameMask[f] = mask;
        s_maskFrame[f] = f;
        s_confirmed = f;
    }
    for (int m = 0; m < NET_MAX_MACHINES; m++)
        s_ack[m] = s_confirmed;
}

static void Client_Receive(const uint8_t* data, int len)
{
    Rd r = { data, len, 0, 0 };
    int type = Rd_U8(&r);
    switch (type)
    {
    case MSG_WELCOME:
        s_machine = Rd_U8(&r);
        Net_Log("[red] conectado: maquina %d\n", s_machine);
        SetStatus("Connected");
        break;
    case MSG_REJECT:
    {
        char why[64];
        int n = 0;
        while (n < (int)sizeof(why) - 1 && r.pos < r.len && (why[n] = (char)Rd_U8(&r)) != 0)
            n++;
        why[n] = 0;
        SetStatus(why);
        break;
    }
    case MSG_LOBBY:
        for (int i = 0; i < NET_MAX_MACHINES; i++)
        {
            NetPlayer p;
            memset(&p, 0, sizeof(p));
            p.used = (uint8_t)Rd_U8(&r);
            if (p.used)
                Rd_Player(&r, &p);
            if (!r.bad)
                s_players[i] = p;
        }
        {
            int a = Rd_U8(&r);
            if (!r.bad)
                s_assists = a;
        }
        Net_Log("[red] sala: %d jugadores\n", Net_NumPlayers());
        break;
    case MSG_START:
        if (s_inRace)
            break;
        Rd_Bytes(&r, &s_setup, (int)sizeof(s_setup));
        if (r.bad || s_setup.numHumans > NET_MAX_HUMANS)
            break;
        s_inRace = 1;
        s_stage = NET_P_RACING;
        s_cup = s_setup.champType != 0;
        Race_Reset();
        s_startPending = 1;
        Net_Log("[red] salida: %d humanos, soy el %d, semilla %u\n", s_setup.numHumans, s_myHuman, s_setup.seed);
        break;
    case MSG_FRAME:
    {
        /* frames decididos por el anfitrion (pueden llegar repetidos o desordenados) */
        uint32_t hostFrame = Rd_U32(&r);
        int late = Rd_U8(&r);
        uint32_t first = Rd_U32(&r);
        int n = Rd_U8(&r);
        if (r.bad || !s_inRace || s_alone)
            break;
        if (hostFrame >= s_hostSeen)
        {
            s_hostSeen = hostFrame;
            s_hostLate = late;
        }
        for (int i = 0; i < n; i++)
        {
            uint32_t g = first + (uint32_t)i;
            int mask = Rd_U8(&r);
            NetInput in[NET_MAX_HUMANS];
            for (int h = 0; h < s_setup.numHumans; h++)
            {
                in[h].bits = Rd_U32(&r);
                in[h].ax = (uint8_t)Rd_U8(&r);
                in[h].ay = (uint8_t)Rd_U8(&r);
                in[h].kind = (uint8_t)Rd_U8(&r);
            }
            if (r.bad)
                break;
            if (g <= s_confirmed || g > s_confirmed + NET_AHEAD || s_maskFrame[g % NET_RING] == g)
                continue;
            for (int h = 0; h < s_setup.numHumans; h++)
                StoreInput(s_inputs[h], g, in[h].bits, in[h].ax, in[h].ay, in[h].kind);
            s_frameMask[g % NET_RING] = (uint8_t)mask;
            s_maskFrame[g % NET_RING] = g;
        }
        while (s_maskFrame[(s_confirmed + 1) % NET_RING] == s_confirmed + 1)
            s_confirmed++;
        break;
    }
    case MSG_CUPEND:
        if (s_cup)
            Net_Log("[red] el anfitrion acaba el campeonato\n");
        s_cup = 0;
        break;
    case MSG_DESYNC:
    {
        uint32_t frame = Rd_U32(&r);
        if (!s_desync)
        {
            s_desync = (int)frame;
            Net_Log("[red] DESINCRONIZACION en el frame %u (aviso del anfitrion)\n", frame);
        }
        break;
    }
    default:
        break;
    }
}

/* ---------------------------------------------------------------- API */

void Net_Init(void)
{
    if (!s_init && enet_initialize() == 0)
    {
        s_init = 1;
        atexit(Net_Shutdown);   /* (tambien al salir con exit: los demas se enteran en el acto, no por tiempo) */
    }
}

void Net_Shutdown(void)
{
    Net_FindStop();
    Net_Leave();
    if (s_init)
        enet_deinitialize();
    s_init = 0;
    if (s_logFile)
        fclose(s_logFile);
    s_logFile = NULL;
}

static void Net_Reset(void)
{
    if (s_findHostSock != ENET_SOCKET_NULL)
        enet_socket_destroy(s_findHostSock);
    s_findHostSock = ENET_SOCKET_NULL;
    s_server = NULL;
    memset(s_peers, 0, sizeof(s_peers));
    memset(s_players, 0, sizeof(s_players));
    s_isHost = s_active = 0;
    s_machine = -1;
    s_inRace = s_startPending = 0;
    s_stage = -1;
    s_cupMask = 0;
    s_cup = 0;
}

int Net_Host(int port)
{
    Net_Init();
    Net_Leave();
    ENetAddress a;
    a.host = ENET_HOST_ANY;
    a.port = (enet_uint16)(port > 0 ? port : NET_DEFAULT_PORT);
    s_enet = enet_host_create(&a, NET_MAX_MACHINES - 1, NET_CHANNELS, 0, 0);
    if (!s_enet)
    {
        SetStatus("Cannot open the port");
        return 0;
    }
    s_isHost = s_active = 1;
    s_machine = 0;
    LocalPlayer(&s_players[0]);
    s_players[0].team = 0;
    if (getenv("NR_ASSISTS"))   /* pruebas: las ayudas sin pasar por la sala */
        s_assists = atoi(getenv("NR_ASSISTS")) & (NET_ASSIST_DRAFT | NET_ASSIST_CATCHUP);
    s_findHostSock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s_findHostSock != ENET_SOCKET_NULL)
    {
        ENetAddress fa;
        fa.host = ENET_HOST_ANY;
        fa.port = NET_FIND_PORT;
        enet_socket_set_option(s_findHostSock, ENET_SOCKOPT_NONBLOCK, 1);
        enet_socket_set_option(s_findHostSock, ENET_SOCKOPT_REUSEADDR, 1);
        if (enet_socket_bind(s_findHostSock, &fa) < 0)
        {
            enet_socket_destroy(s_findHostSock);
            s_findHostSock = ENET_SOCKET_NULL;
        }
    }
    char st[64];
    snprintf(st, sizeof(st), "Hosting on port %d", a.port);
    SetStatus(st);
    return 1;
}

int Net_Join(const char* host, int port)
{
    Net_Init();
    Net_Leave();
    ENetAddress a;
    if (enet_address_set_host(&a, host) != 0)
    {
        SetStatus("Unknown address");
        return 0;
    }
    a.port = (enet_uint16)(port > 0 ? port : NET_DEFAULT_PORT);
    s_enet = enet_host_create(NULL, 1, NET_CHANNELS, 0, 0);
    if (!s_enet)
    {
        SetStatus("Cannot open the network");
        return 0;
    }
    s_server = enet_host_connect(s_enet, &a, NET_CHANNELS, NET_VERSION);
    if (!s_server)
    {
        enet_host_destroy(s_enet);
        s_enet = NULL;
        SetStatus("Cannot connect");
        return 0;
    }
    s_active = 1;
    SetStatus("Connecting...");
    return 1;
}

void Net_Leave(void)
{
    if (!s_enet)
    {
        Net_Reset();
        return;
    }
    if (s_isHost)
    {
        for (int i = 1; i < NET_MAX_MACHINES; i++)
            if (s_peers[i])
                enet_peer_disconnect(s_peers[i], 0);
    }
    else if (s_server)
        enet_peer_disconnect(s_server, 0);
    enet_host_flush(s_enet);
    enet_host_destroy(s_enet);
    s_enet = NULL;
    Net_Reset();
}

/* cliente: se ha perdido el anfitrion (desconexion, o nada en NET_WAIT_MS). En la carrera no llega nada mas: sigue
 * solo en esta maquina hasta que salga, los demas sin mando desde el primer frame sin decidir */
static void Client_HostGone(const char* status)
{
    SetStatus(status);
    int racing = s_inRace;
    if (racing)
        for (int m = 0; m < NET_MAX_MACHINES; m++)
            if (m != s_machine)
                MarkLeft(m, s_confirmed + 1);
    NetRaceSetup setup = s_setup;
    int myHuman = s_myHuman, frame = s_frame, machine = s_machine;
    if (s_enet)
        enet_host_destroy(s_enet);
    s_enet = NULL;
    Net_Reset();
    if (racing)
    {
        s_inRace = 1;
        s_alone = 1;
        s_setup = setup;
        s_myHuman = myHuman;
        s_frame = frame;
        s_machine = machine;   /* (para seguir con su mando) */
    }
}

static void Net_HandleEvent(ENetEvent* ev)
{
    switch (ev->type)
    {
    case ENET_EVENT_TYPE_CONNECT:
        /* un PC que se queda unos segundos colgado no se desconecta: en la carrera, el anfitrion lo da por salido a
         * los NET_WAIT_MS sin noticias y, si vuelve antes, se pone al dia */
        enet_peer_timeout(ev->peer, 0, NET_WAIT_MS, NET_WAIT_MS + 5000);
        if (!s_isHost)
        {
            Msg m;
            NetPlayer me;
            LocalPlayer(&me);
            Msg_Begin(&m, MSG_HELLO);
            Msg_U8(&m, NET_VERSION);
            Msg_U32(&m, Net_BuildHash());
            Msg_Player(&m, &me);
            Send(s_server, &m, 0);
        }
        else
            ev->peer->data = (void*)(intptr_t)-1;   /* sin numero hasta el HELLO */
        break;
    case ENET_EVENT_TYPE_RECEIVE:
        if (s_isHost)
            Host_Receive(ev->peer, ev->packet->data, (int)ev->packet->dataLength, ev->channelID);
        else
            Client_Receive(ev->packet->data, (int)ev->packet->dataLength);
        enet_packet_destroy(ev->packet);
        break;
    case ENET_EVENT_TYPE_DISCONNECT:
        if (s_isHost)
            Host_Drop(ev->peer);
        else
            Client_HostGone(s_machine < 0 ? "Cannot connect" : "Disconnected from the host");
        break;
    default:
        break;
    }
}

static void Net_Service(int timeoutMs)
{
    ENetEvent ev;
    while (s_enet && enet_host_service(s_enet, &ev, (enet_uint32)timeoutMs) > 0)
    {
        Net_HandleEvent(&ev);
        timeoutMs = 0;
    }
}

/* anfitrion: contesta a las preguntas de la busqueda */
static void Find_Answer(void)
{
    if (s_findHostSock == ENET_SOCKET_NULL)
        return;
    uint8_t data[64];
    for (;;)
    {
        ENetAddress from;
        ENetBuffer b;
        b.data = data;
        b.dataLength = sizeof(data);
        int n = enet_socket_receive(s_findHostSock, &from, &b, 1);
        if (n <= 0)
            break;
        if (n < 9 || memcmp(data, s_findAsk, 8) || data[8] != NET_VERSION)
            continue;
        uint8_t out[8 + 2 + NET_NAME_LEN + 1];
        memcpy(out, s_findAnswer, 8);
        out[8] = NET_VERSION;
        out[9] = (uint8_t)Net_NumPlayers();
        memcpy(out + 10, s_players[0].name, NET_NAME_LEN + 1);
        ENetBuffer ob;
        ob.data = out;
        ob.dataLength = sizeof(out);
        enet_socket_send(s_findHostSock, &from, &ob, 1);
    }
}

/* cliente: pregunta cada segundo (difusion y, para probar en el mismo PC, 127.0.0.1) y apunta las respuestas */
static void Find_Poll(void)
{
    if (s_findSock == ENET_SOCKET_NULL)
        return;
    uint32_t now = enet_time_get();
    if (now - s_findLastAsk > 1000)
    {
        s_findLastAsk = now;
        uint8_t ask[9];
        memcpy(ask, s_findAsk, 8);
        ask[8] = NET_VERSION;
        ENetBuffer b;
        b.data = ask;
        b.dataLength = sizeof(ask);
        ENetAddress to;
        to.port = NET_FIND_PORT;
        to.host = ENET_HOST_BROADCAST;
        enet_socket_send(s_findSock, &to, &b, 1);
        enet_address_set_host_ip(&to, "127.0.0.1");
        enet_socket_send(s_findSock, &to, &b, 1);
        /* las que no contestan en 3 s se quitan */
        for (int i = 0; i < s_numFound;)
            if (now - s_found[i].seen > 3000)
                s_found[i] = s_found[--s_numFound];
            else
                i++;
    }
    uint8_t data[64];
    for (;;)
    {
        ENetAddress from;
        ENetBuffer b;
        b.data = data;
        b.dataLength = sizeof(data);
        int n = enet_socket_receive(s_findSock, &from, &b, 1);
        if (n <= 0)
            break;
        if (n < 10 + NET_NAME_LEN + 1 || memcmp(data, s_findAnswer, 8) || data[8] != NET_VERSION)
            continue;
        char ip[32];
        if (enet_address_get_host_ip(&from, ip, sizeof(ip)) < 0)
            continue;
        /* el mismo anfitrion contesta por la difusion y por 127.0.0.1: si ya esta como local, no se repite */
        int k = 0;
        while (k < s_numFound && strcmp(s_found[k].addr, ip) &&
               !(strcmp(ip, "127.0.0.1") && !strcmp(s_found[k].addr, "127.0.0.1") &&
                 !memcmp(s_found[k].name, data + 10, NET_NAME_LEN)))
            k++;
        if (k == s_numFound)
        {
            if (s_numFound >= NET_FIND_MAX)
                continue;
            s_numFound++;
            snprintf(s_found[k].addr, sizeof(s_found[k].addr), "%s", ip);
        }
        memcpy(s_found[k].name, data + 10, NET_NAME_LEN + 1);
        s_found[k].name[NET_NAME_LEN] = 0;
        s_found[k].players = data[9];
        s_found[k].seen = now;
    }
}

void Net_Poll(void)
{
    Net_Service(0);
    if (s_isHost)
        Host_Pump();
    Find_Answer();
    Find_Poll();
}

void Net_FindStart(void)
{
    Net_Init();
    if (s_findSock != ENET_SOCKET_NULL)
        return;
    s_numFound = 0;
    s_findLastAsk = 0;
    s_findSock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s_findSock == ENET_SOCKET_NULL)
        return;
    enet_socket_set_option(s_findSock, ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(s_findSock, ENET_SOCKOPT_BROADCAST, 1);
    ENetAddress a;
    a.host = ENET_HOST_ANY;
    a.port = 0;
    enet_socket_bind(s_findSock, &a);
}

void Net_FindStop(void)
{
    if (s_findSock != ENET_SOCKET_NULL)
        enet_socket_destroy(s_findSock);
    s_findSock = ENET_SOCKET_NULL;
    s_numFound = 0;
}

int Net_FindCount(void)
{
    return s_numFound;
}

const NetFound* Net_Found(int i)
{
    return i >= 0 && i < s_numFound ? &s_found[i] : NULL;
}

void Net_UpdateLocalPlayer(void)
{
    if (!s_active || s_machine < 0)
        return;
    NetPlayer me;
    LocalPlayer(&me);
    if (s_isHost)
    {
        me.inRace = s_players[0].inRace;
        me.team = (uint8_t)Host_Team(0, me.team, s_players[0].team);
        s_players[0] = me;
        Host_SendLobby();
    }
    else if (s_server)
    {
        Msg m;
        Msg_Begin(&m, MSG_UPDATE);
        Msg_Player(&m, &me);
        Send(s_server, &m, 0);
    }
}

void Net_SetTeam(int team)
{
    s_reqTeam = team;
    Net_UpdateLocalPlayer();
    s_reqTeam = -1;
}

int Net_Active(void)
{
    return s_active;
}

int Net_IsHost(void)
{
    return s_isHost;
}

int Net_Connected(void)
{
    return s_active && s_machine >= 0;
}

int Net_MachineId(void)
{
    return s_machine;
}

const NetPlayer* Net_Player(int machine)
{
    return machine >= 0 && machine < NET_MAX_MACHINES && s_players[machine].used ? &s_players[machine] : NULL;
}

int Net_NumPlayers(void)
{
    int n = 0;
    for (int i = 0; i < NET_MAX_MACHINES; i++)
        n += s_players[i].used;
    return n;
}

const char* Net_Status(void)
{
    return s_status;
}

/* ---------------------------------------------------------------- API de la carrera */

int Net_Assists(void)
{
    return s_inRace ? s_setup.assists : 0;
}

int Net_LobbyAssists(void)
{
    return s_assists;
}

void Net_SetAssists(int assists)
{
    if (!s_isHost)
        return;
    s_assists = assists;
    Host_SendLobby();
}

int Net_InRace(void)
{
    return s_inRace;
}

int Net_HumanView(int human)
{
    if (human < 0)
        return -1;
    if (!s_inRace)
        return human < 2 ? human : -1;
    return human == s_myHuman ? 0 : -1;
}

const char* Net_HumanName(int human)
{
    if (!s_inRace)
        return NULL;
    int m = MachineOfHuman(human);
    if (m >= 0 && s_players[m].used)
        return s_players[m].name;
    return human >= 0 && human < NET_MAX_HUMANS && s_humanNames[human][0] ? s_humanNames[human] : "Player";
}

int Net_HumanTransmission(int human)
{
    if (s_inRace && human >= 0 && human < s_setup.numHumans)
        return s_setup.transmission[human];
    return g_Settings.transmission[human & 1];
}

int Net_RaceSeed(void)
{
    return (int)s_setup.seed;
}

const NetRaceSetup* Net_Setup(void)
{
    return &s_setup;
}

int Net_HostRacers(int* machines)
{
    int n = 0;
    if (!s_isHost)
        return 0;
    /* los de la sala o, en un campeonato empezado, los suyos que esperan en la clasificacion */
    int max = (g_Settings.champType ? NET_CUP_MAX : NET_MAX_HUMANS) - 1;
    for (int m = 1; m < NET_MAX_MACHINES && n < max; m++)
        if (s_players[m].used && s_peers[m] &&
            (s_cupMask ? (s_cupMask & 1u << m) && s_players[m].inRace == NET_P_STANDINGS
                       : s_players[m].inRace == NET_P_LOBBY))
            machines[n++] = m;
    return n;
}

void Net_HostStartRace(void)
{
    NetRaceSetup* s = &s_setup;
    GameSettings* gs = &g_Settings;
    s->seed = (uint32_t)g_FrontendFrames;
    s->unlockMask = (uint32_t)g_GameInfo->unlockMask;
    s->venue = gs->champType ? gs->curVenue : gs->venue;
    s->track = gs->champType ? gs->curTrack : gs->track;
    s->champType = gs->champType;
    s->cupRaces = gs->cupRaces;
    s->raceIndex = gs->raceIndex;
    s->cupSelVenue = gs->venue;
    s->cupSelTrack = gs->track;
    memcpy(s->cupVenue, gs->cupVenue, sizeof(s->cupVenue));
    memcpy(s->cupTrack, gs->cupTrack, sizeof(s->cupTrack));
    static_assert(sizeof(s->records) == sizeof(g_CarRecords), "NetRaceSetup.records = g_CarRecords");
    memcpy(s->records, g_CarRecords, sizeof(s->records));
    s->laps = gs->laps;
    s->racingClass = gs->racingClass;
    s->powerUps = (uint8_t)g_GameInfo->powerUps;
    s->teamRace = gs->teamRace;
    s->gameMode = gs->gameMode;
    s->gridCount = g_RaceGrid.count;
    s->assists = (uint8_t)s_assists;
    static_assert(sizeof(s->gameInfo) == sizeof(GameInfo), "NetRaceSetup.gameInfo = GameInfo");
    memcpy(s->gameInfo, g_GameInfo, sizeof(s->gameInfo));
    memcpy(s->grid, g_RaceGrid.car, sizeof(s->grid));
    /* GridCar.player es el numero de humano: el 0 es el anfitrion; los demas, sus maquinas (Fe_StartRace las ha
     * puesto en ese orden con Net_HostRacers) */
    int machines[NET_MAX_HUMANS], n = Net_HostRacers(machines);
    s->numHumans = (uint8_t)(n + 1);
    s_cupMask = gs->champType ? 1 : 0;
    for (int i = 0; i < n && gs->champType; i++)
        s_cupMask |= 1u << machines[i];
    memset(s->humanMachine, 0xff, sizeof(s->humanMachine));
    s->humanMachine[0] = 0;
    s->transmission[0] = s_players[0].transmission = gs->transmission[0] <= 5 ? gs->transmission[0] : 0;
    for (int i = 0; i < n; i++)
    {
        s->humanMachine[i + 1] = (uint8_t)machines[i];
        s->transmission[i + 1] = s_players[machines[i]].transmission;
    }
    /* retardo: el cliente va por detras del anfitrion lo que tarda en llegarle un frame decidido, y su pulsacion
     * tarda otro tanto en volver: el ping del que mas tiene, mas 2 frames de margen; entre NET_INPUT_DELAY y
     * NET_MAX_DELAY */
    uint32_t r1 = 0, r2 = 0;
    for (int i = 0; i < n; i++)
    {
        uint32_t rtt = s_peers[machines[i]] ? s_peers[machines[i]]->roundTripTime : 0;
        if (rtt > r1)
        {
            r2 = r1;
            r1 = rtt;
        }
        else if (rtt > r2)
            r2 = rtt;
    }
    int delay = ((int)r1 * 60 + 999) / 1000 + 2;
    s->inputDelay = (uint8_t)(delay < NET_INPUT_DELAY ? NET_INPUT_DELAY : delay > NET_MAX_DELAY ? NET_MAX_DELAY : delay);
    Net_Log("[red] ping de los clientes: %u y %u ms -> retardo de %d frames\n", r1, r2, s->inputDelay);
    Msg m;
    static_assert(sizeof(*s) < sizeof(m.buf), "NetRaceSetup en un mensaje");
    Msg_Begin(&m, MSG_START);
    Msg_Bytes(&m, s, (int)sizeof(*s));
    for (int i = 0; i < n; i++)
    {
        Send(s_peers[machines[i]], &m, 0);
        s_players[machines[i]].inRace = 1;
    }
    s_players[0].inRace = 1;
    s_inRace = 1;
    Race_Reset();
    Host_SendLobby();
    Net_Log("[red] salida: %d humanos, semilla %u\n", s->numHumans, s->seed);
    char grid[96];
    int len = 0;
    for (int i = 0; i < g_RaceGrid.count && len < (int)sizeof(grid) - 12; i++)   /* hueco: humano (h) o IA, equipo */
        len += snprintf(grid + len, sizeof(grid) - len, " %s%d/e%d", g_RaceGrid.car[i].player == 0xff ? "ia" : "h",
                        g_RaceGrid.car[i].player == 0xff ? i : g_RaceGrid.car[i].player, g_RaceGrid.car[i].team);
    Net_Log("[red] parrilla:%s\n", grid);
}

int Net_TakeStart(void)
{
    int v = s_startPending;
    s_startPending = 0;
    return v;
}

/* cliente: al anfitrion, el ultimo frame decidido que tiene y sus ultimas pulsaciones (aunque ya no mande: el
 * anfitrion sigue mandandole frames mientras mira) */
static void Client_SendInputs(void)
{
    if (!s_server)
        return;
    Msg m;
    Msg_Begin(&m, MSG_INPUT);
    Msg_U32(&m, s_confirmed);
    Msg_Inputs(&m, s_own, s_myHuman >= 0 ? s_myHuman : 0, s_ownLast, s_ownLast ? NET_REDUNDANT : 0);
    SendQuick(s_server, m.buf, m.len);
}

/* cliente sin anfitrion: el frame lo decide el solo (su mando; los demas ya no mandan) */
static void Client_AloneFrame(uint32_t f)
{
    for (int h = 0; h < s_setup.numHumans; h++)
    {
        const NetInput* own = &s_own[f % NET_RING];
        if (h == s_myHuman && own->frame == f && !LeftAt(s_machine, f))
            s_inputs[h][f % NET_RING] = *own;
        else
            StoreNone(s_inputs[h], f);
    }
    uint8_t mask = 0;
    for (int m = 0; m < NET_MAX_MACHINES; m++)
        if (LeftAt(m, f))
            mask |= (uint8_t)(1u << m);
    s_frameMask[f % NET_RING] = mask;
    s_maskFrame[f % NET_RING] = f;
    if (f > s_confirmed)
        s_confirmed = f;
}

/* cliente: el frame f ya se puede simular (decidido o, si ya no manda nadie, sin mando para todos) */
static int Client_HaveFrame(uint32_t f)
{
    if (s_maskFrame[f % NET_RING] == f)
        return 1;
    for (int m = 0; m < NET_MAX_MACHINES; m++)
        if (!LeftAt(m, f))
            return 0;
    return 1;
}

/* pruebas: NR_NETSTALL=frame:ms[,frame:ms...] para esta maquina en esos frames de la carrera (un PC que se cuelga) */
static void Net_DebugStall(uint32_t f)
{
    const char* p = getenv("NR_NETSTALL");
    while (p && *p)
    {
        unsigned frame = 0, ms = 0;
        if (sscanf(p, "%u:%u", &frame, &ms) == 2 && frame == f)
        {
            Net_Log("[red] prueba: parado %u ms en el frame %u\n", ms, f);
            Sleep(ms);
        }
        p = strchr(p, ',');
        if (p)
            p++;
    }
}

void Net_RaceFrame(void)
{
    if (!s_inRace)
        return;
    uint32_t f = (uint32_t)++s_frame;
    Net_DebugStall(f);
    /* lo que se acaba de leer del mando local vale para dentro de s_delay frames */
    RaceCar* me = s_myHuman >= 0 ? g_Humans[s_myHuman] : NULL;
    uint32_t when = f + (uint32_t)s_delay;
    if (me && me->ctrl && s_machine >= 0 && !LeftAt(s_machine, when))
    {
        NetInput in;
        in.bits = *(uint32_t*)(me->ctrl + 8);
        in.ax = me->ctrl[0xc];
        in.ay = me->ctrl[0xd];
        in.kind = me->ctrl[0xe];
        if (Pause_MenuOpen())
        {
            /* con el menu abierto, los botones son del menu: el coche va sin mando */
            in.bits = 0;
            in.ax = in.ay = 0x80;
            in.kind = 4;
        }
        if (s_isHost)
        {
            if (when > s_confirmed)
                StoreInput(s_inputs[s_myHuman], when, in.bits, in.ax, in.ay, in.kind);
        }
        else
        {
            StoreInput(s_own, when, in.bits, in.ax, in.ay, in.kind);
            s_ownLast = when;
        }
    }
    enet_uint32 start = enet_time_get(), lastSend = start;
    s_waiting = 1;
    if (s_isHost)
    {
        /* decide el frame: espera un poco (NET_GRACE_MS) al que iba a tiempo y, mientras cargan, al que aun no ha
         * mandado nada; al que ya llegaba tarde no se le espera */
        s_clockFrame = f;
        s_clockTime = start;
        if (f == 1)
            for (int m = 1; m < NET_MAX_MACHINES; m++)
                s_lastRecv[m] = start;   /* (el plazo de NET_WAIT_MS cuenta desde aqui) */
        int missing = -1;
        for (;;)
        {
            Host_ConfirmReady();
            if (s_confirmed >= f)
                break;
            int wait = 0;
            enet_uint32 now = enet_time_get();
            for (int h = 0; h < s_setup.numHumans; h++)
            {
                int m = MachineOfHuman(h);
                for (uint32_t g = s_confirmed + 1; g <= f; g++)
                    if (!LeftAt(m, g) && s_inputs[h][g % NET_RING].frame != g)
                    {
                        missing = m;
                        if (m > 0 && (!s_started[m] || (!s_late[m] && now - start < NET_GRACE_MS)))
                            wait = 1;
                        break;
                    }
            }
            if (!wait)
            {
                Host_ForceTo(f);
                break;
            }
            Host_CheckTimeouts();
            Host_SendFrames();
            Net_Service(1);
            PsyX_UpdateInput();   /* (la ventana sigue respondiendo) */
        }
        Host_SendFrames();
        enet_uint32 waited = enet_time_get() - start;
        if (waited >= 100)   /* (solo al empezar: esperando a que carguen) */
            Net_Log("[red] espera de %u ms en el frame %u por la maquina %d\n", waited, f, missing);
    }
    else
    {
        /* solo frames decididos por el anfitrion: se le espera a el (a nadie mas) */
        Client_SendInputs();
        while (!Client_HaveFrame(f))
        {
            if (s_alone)
            {
                Client_AloneFrame(f);
                break;
            }
            Net_Service(1);
            PsyX_UpdateInput();
            if (enet_time_get() - lastSend >= NET_RESEND_MS)
            {
                /* (por si se ha perdido el ultimo: que no se queden los dos esperando) */
                lastSend = enet_time_get();
                Client_SendInputs();
            }
            if (!s_alone && enet_time_get() - start > NET_WAIT_MS)
            {
                Net_Log("[red] el anfitrion no manda nada desde hace %d s\n", NET_WAIT_MS / 1000);
                Client_HostGone("Disconnected from the host");
            }
        }
        enet_uint32 waited = enet_time_get() - start;
        if (waited >= 1000)
            Net_Log("[red] espera de %u ms en el frame %u por el anfitrion\n", waited, f);
        else if (waited >= 100)
            Net_LogFile("[red] espera de %u ms en el frame %u por el anfitrion\n", waited, f);
        /* quien ya no manda desde este frame (lo decide el anfitrion: el primer frame que lo dice) */
        if (s_maskFrame[f % NET_RING] == f)
            for (int m = 0; m < NET_MAX_MACHINES; m++)
                if ((s_frameMask[f % NET_RING] & (1u << m)) && !s_leftFrom[m])
                    MarkLeft(m, f);
        /* atrasado respecto al anfitrion (se colgo, cargo mas tarde o su reloj va mas lento): corre sin esperar al
         * reloj mientras tenga frames decididos. Si el anfitrion dice que sus pulsaciones le llegan tarde, hasta el
         * ultimo decidido (con solo NET_CATCHUP de margen se quedaria en el limite, llegando tarde siempre) */
        s_catchUp = !s_alone && (s_hostLate || s_hostSeen > f + NET_CATCHUP) && Client_HaveFrame(f + 1);
    }
    s_waiting = 0;
    for (int h = 0; h < s_setup.numHumans; h++)
        ApplyInput(h, f);
}

int Net_CatchingUp(void)
{
    return s_inRace && s_catchUp;
}

void Net_RaceStep(void)
{
    if (!s_inRace || s_frame % NET_SUM_EVERY != 0)
        return;
    uint32_t sum[3];
    Race_StateChecksum(sum);
    Net_LogFile("[red] sumas frame %d: coches %08x actores y guiones %08x aleatorios %08x\n", s_frame, sum[0], sum[1],
                sum[2]);
    if (s_isHost)
        Sums_Store((uint32_t)s_frame, 0, sum);
    else if (s_server)
    {
        Msg m;
        Msg_Begin(&m, MSG_SUM);
        Msg_U32(&m, (uint32_t)s_frame);
        for (int i = 0; i < 3; i++)
            Msg_U32(&m, sum[i]);
        Send(s_server, &m, 0);
    }
}

void Net_RaceEnd(void)
{
    if (!s_inRace)
        return;
    if (s_isHost)
        Host_MachineLeaves(0);
    else if (s_server)
    {
        Msg m;
        Msg_Begin(&m, MSG_LEAVE);
        Msg_U8(&m, s_machine);
        Msg_U32(&m, 0);
        Send(s_server, &m, 0);
    }
    s_players[s_machine >= 0 ? s_machine : 0].inRace = NET_P_BUSY;
    s_stage = NET_P_BUSY;   /* (como lo apunta el anfitrion al recibir la salida) */
    s_inRace = 0;
    s_catchUp = 0;
    Net_Log("[red] fin de la carrera en red (frame %d%s)\n", s_frame, s_desync ? ", desincronizada" : "");
    if (s_alone)
        s_machine = -1;   /* (la seguia solo, ya sin partida) */
    s_alone = 0;
}

int Net_DesyncFrame(void)
{
    return s_desync;
}

/* ---------------------------------------------------------------- donde esta cada uno y campeonato */

void Net_SetStage(int stage)
{
    if (s_isHost)
    {
        /* el del anfitrion solo lo miran los clientes (si puede elegir coche); en la sala o fuera */
        stage = stage == NET_P_LOBBY ? NET_P_LOBBY : NET_P_BUSY;
        if (s_active && !s_inRace && s_players[0].inRace != stage)
        {
            s_players[0].inRace = (uint8_t)stage;
            Host_SendLobby();
        }
        return;
    }
    if (!s_server || s_machine < 0 || s_inRace || stage == s_stage)
        return;
    s_stage = stage;
    Msg m;
    Msg_Begin(&m, MSG_STAGE);
    Msg_U8(&m, stage);
    Send(s_server, &m, 0);
}

int Net_HostInLobby(void)
{
    return s_active && !s_isHost && s_machine >= 0 && s_players[0].used && s_players[0].inRace == NET_P_LOBBY;
}

const char* Net_ChoosingName(void)
{
    for (int m = 1; m < NET_MAX_MACHINES; m++)
        if (s_isHost && s_players[m].used && s_peers[m] && s_players[m].inRace == NET_P_CHOOSING)
            return s_players[m].name;
    return NULL;
}

int Net_CupActive(void)
{
    return s_isHost ? s_cupMask != 0 : s_cup && s_active;
}

int Net_CupAllReady(void)
{
    for (int m = 1; m < NET_MAX_MACHINES; m++)
        if ((s_cupMask & 1u << m) && s_peers[m] && s_players[m].used && s_players[m].inRace != NET_P_STANDINGS)
            return 0;
    return 1;
}

int Net_HostCupHuman(int human)
{
    int m = MachineOfHuman(human);
    if (m == 0)
        return 0;
    int machines[NET_MAX_HUMANS], n = Net_HostRacers(machines);
    for (int i = 0; i < n; i++)
        if (machines[i] == m)
            return i + 1;   /* (Net_HostStartRace los numera en este orden) */
    return -1;
}

void Net_LeaveCup(void)
{
    Msg m;
    if (s_isHost && s_cupMask)
    {
        Msg_Begin(&m, MSG_CUPEND);
        Broadcast(&m, 0, -1);
        Net_Log("[red] el anfitrion acaba el campeonato\n");
    }
    else if (!s_isHost && s_cup && s_server)
    {
        Msg_Begin(&m, MSG_CUPQUIT);
        Send(s_server, &m, 0);
        Net_Log("[red] deja el campeonato\n");
    }
    s_cupMask = 0;
    s_cup = 0;
}

void Net_CupFinished(void)
{
    s_cupMask = 0;
    s_cup = 0;
}

int Net_LocalHuman(void)
{
    return s_myHuman;
}

const char* Net_ViewHumanName(int player)
{
    if (player < 0 || player >= NET_MAX_HUMANS)
        return NULL;
    /* el cliente intercambia en la parrilla su humano con el 0 (el del anfitrion) para ser el jugador 1 */
    int h = player;
    if (!s_isHost && s_myHuman > 0)
        h = player == 0 ? s_myHuman : player == s_myHuman ? 0 : player;
    return h < s_setup.numHumans && s_humanNames[h][0] ? s_humanNames[h] : NULL;
}

int Net_HumanGone(int human)
{
    /* (frame >= el de la salida: todas las maquinas lo saben antes de simularlo, porque sin el no tienen sus
     * pulsaciones) */
    int m = s_inRace ? MachineOfHuman(human) : -1;
    return m >= 0 && s_leftFrom[m] && (uint32_t)s_frame >= s_leftFrom[m];
}
