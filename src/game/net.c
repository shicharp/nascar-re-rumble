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

#define NET_VERSION   9
#define NET_CHANNELS  2           /* 0 sala y ordenes; 1 pulsaciones y salidas de la carrera (fiables y en orden) */
#define NET_RING      128         /* frames de pulsaciones guardados por humano */
#define NET_SUM_EVERY 30          /* frames entre sumas de control (medio segundo) */
#define NET_SUM_RING  16
#define NET_WAIT_MS   15000       /* sin pulsaciones de una maquina tanto tiempo: el anfitrion la da por salida */
#define NET_RESEND_MS 30          /* esperando, se repite el ultimo paquete de pulsaciones cada tanto */
#define NET_TAIL      32          /* pulsaciones que manda el anfitrion con la salida de una maquina */

enum
{
    MSG_HELLO = 1,    /* cliente -> anfitrion: version, nombre, piloto, cambio */
    MSG_WELCOME,      /* anfitrion -> cliente: su numero de maquina */
    MSG_REJECT,       /* anfitrion -> cliente: motivo (texto) */
    MSG_LOBBY,        /* anfitrion -> todos: la lista de jugadores */
    MSG_START,        /* anfitrion -> los de la carrera: NetRaceSetup */
    MSG_INPUT,        /* pulsaciones de un humano: las de un frame y las anteriores (sin garantia; se reenvian) */
    MSG_LEAVE,        /* maquina que deja la carrera, frame desde el que su humano va sin mando y sus ultimas */
    MSG_SUM,          /* cliente -> anfitrion: sumas de la carrera en un frame */
    MSG_DESYNC,       /* anfitrion -> todos: frame desincronizado */
    MSG_UPDATE,       /* cliente -> anfitrion: su piloto o su nombre han cambiado */
    MSG_STAGE,        /* cliente -> anfitrion: donde esta (NET_P_*) */
    MSG_CUPQUIT,      /* cliente -> anfitrion: deja el campeonato */
    MSG_CUPEND,       /* anfitrion -> todos: el campeonato se acaba antes de la ultima carrera */
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
static uint32_t     s_contig[NET_MAX_MACHINES];     /* anfitrion: ultimo frame recibido sin huecos de cada maquina */
static uint8_t      s_lastPacket[256];              /* el ultimo paquete de pulsaciones propio (para repetirlo) */
static int          s_lastPacketLen;
static NetRaceSetup s_setup;
static NetInput     s_inputs[NET_MAX_HUMANS][NET_RING];
static uint32_t     s_leftFrom[NET_MAX_MACHINES];   /* frame desde el que la maquina ya no manda (0 sigue) */
static uint32_t     s_lastInput[NET_MAX_MACHINES];  /* anfitrion: ultimo frame recibido de cada maquina */
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
    Msg_Bytes(m, p->name, NET_NAME_LEN + 1);
}

static void Rd_Player(Rd* r, NetPlayer* p)
{
    p->driver = (uint8_t)Rd_U8(r);
    p->car = (uint8_t)Rd_U8(r);
    p->transmission = (uint8_t)Rd_U8(r);
    p->inRace = (uint8_t)Rd_U8(r);
    Rd_Bytes(r, p->name, NET_NAME_LEN + 1);
    p->name[NET_NAME_LEN] = 0;
}

/* el jugador de esta maquina, con los datos del front-end */
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

static void StoreInput(int h, uint32_t frame, uint32_t bits, int ax, int ay, int kind)
{
    if (h < 0 || h >= NET_MAX_HUMANS || frame == 0)
        return;
    NetInput* in = &s_inputs[h][frame % NET_RING];
    in->frame = frame;
    in->bits = bits;
    in->ax = (uint8_t)ax;
    in->ay = (uint8_t)ay;
    in->kind = (uint8_t)kind;
}

/* pulsaciones del humano h de los frames (last - count, last]: numero de humano, ultimo frame, cuantas, y cada una */
static void Msg_Inputs(Msg* m, int type, int h, uint32_t last, int count)
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
        const NetInput* in = &s_inputs[h][f % NET_RING];
        if (in->frame != f)
            continue;   /* (no deberia faltar ninguna) */
        Msg_U32(m, f);
        Msg_U32(m, in->bits);
        Msg_U8(m, in->ax);
        Msg_U8(m, in->ay);
        Msg_U8(m, in->kind);
        n++;
    }
    m->buf[pos] = (uint8_t)n;
    (void)type;
}

/* anfitrion: el ultimo frame recibido sin huecos de la maquina (lo que pueden tener los demas) */
static void Host_AdvanceContig(int machine, int h)
{
    while (s_inputs[h][(s_contig[machine] + 1) % NET_RING].frame == s_contig[machine] + 1)
        s_contig[machine]++;
}

static void Rd_Inputs(Rd* r)
{
    int h = Rd_U8(r);
    Rd_U32(r);
    int n = Rd_U8(r);
    for (int i = 0; i < n && !r->bad; i++)
    {
        uint32_t frame = Rd_U32(r), bits = Rd_U32(r);
        int ax = Rd_U8(r), ay = Rd_U8(r), kind = Rd_U8(r);
        if (!r->bad && s_inRace && s_inputs[h & 7][frame % NET_RING].frame != frame)
            StoreInput(h, frame, bits, ax, ay, kind);
    }
    int m = MachineOfHuman(h);
    if (s_isHost && m > 0 && s_inRace)
    {
        Host_AdvanceContig(m, h);
        s_lastInput[m] = s_contig[m];
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

/* anfitrion: decide el frame desde el que la maquina va sin mando y lo dice a los demas (por el canal de las
 * pulsaciones, detras de las suyas) */
static void Host_MachineLeaves(int machine)
{
    if (s_leftFrom[machine] || !s_setup.numHumans)
        return;
    /* desde el primer frame que le falta al anfitrion (los demas solo tienen lo que el ha reenviado); con la salida
     * van sus ultimas pulsaciones, por si a alguno se le perdio alguna */
    uint32_t from = (machine == 0 ? (uint32_t)s_frame + (uint32_t)s_delay : s_contig[machine]) + 1;
    int h = -1;
    for (int k = 0; k < s_setup.numHumans; k++)
        if (s_setup.humanMachine[k] == machine)
            h = k;
    MarkLeft(machine, from);
    Msg m;
    Msg_Begin(&m, MSG_LEAVE);
    Msg_U8(&m, machine);
    Msg_U32(&m, from);
    Msg_U8(&m, h >= 0);
    if (h >= 0)
        Msg_Inputs(&m, MSG_LEAVE, h, from - 1, NET_TAIL);
    Broadcast(&m, 0, machine);
    Host_SendLobby();
}

static int HaveInput(int h, uint32_t frame)
{
    int m = MachineOfHuman(h);
    if (m < 0 || (s_leftFrom[m] && frame >= s_leftFrom[m]))
        return 1;
    return s_inputs[h][frame % NET_RING].frame == frame;
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
    if (s_inRace && s_players[id].inRace == NET_P_RACING)
        Host_MachineLeaves(id);
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
                s_players[from] = p;
                Host_SendLobby();
            }
        }
        break;
    case MSG_INPUT:
        Rd_Inputs(&r);
        if (!r.bad)
            for (int i = 1; i < NET_MAX_MACHINES; i++)   /* a los demas clientes, tambien sin garantia */
                if (s_peers[i] && i != from)
                    SendQuick(s_peers[i], data, len);
        (void)channel;
        break;
    case MSG_LEAVE:
        if (from > 0 && from < NET_MAX_MACHINES)
            Host_MachineLeaves(from);
        break;
    case MSG_STAGE:
    {
        int st = Rd_U8(&r);
        /* (mientras corre manda la carrera: la deja con MSG_LEAVE, que llega antes) */
        if (!r.bad && from > 0 && from < NET_MAX_MACHINES && s_players[from].used &&
            s_players[from].inRace != NET_P_RACING && st != NET_P_RACING && st <= NET_P_BUSY)
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
    memset(s_lastInput, 0, sizeof(s_lastInput));
    memset(s_contig, 0, sizeof(s_contig));
    s_lastPacketLen = 0;
    s_delay = s_setup.inputDelay >= NET_INPUT_DELAY && s_setup.inputDelay <= NET_MAX_DELAY ? s_setup.inputDelay
                                                                                             : NET_INPUT_DELAY;
    memset(s_sums, 0, sizeof(s_sums));
    s_frame = 0;
    s_desync = 0;
    s_myHuman = -1;
    memset(s_humanNames, 0, sizeof(s_humanNames));
    for (int h = 0; h < s_setup.numHumans; h++)
    {
        if (s_setup.humanMachine[h] == s_machine)
            s_myHuman = h;
        int m = s_setup.humanMachine[h];
        snprintf(s_humanNames[h], sizeof(s_humanNames[h]), "%s",
                 m < NET_MAX_MACHINES && s_players[m].used ? s_players[m].name : "Player");
        /* los primeros frames no tienen mando: nadie los ha leido todavia */
        for (uint32_t f = 1; f <= (uint32_t)s_delay; f++)
            StoreInput(h, f, 0, 0x80, 0x80, 4);
    }
    for (int m = 0; m < NET_MAX_MACHINES; m++)
        s_contig[m] = (uint32_t)s_delay;
    /* las maquinas que no corren no mandan nada */
    for (int m = 0; m < NET_MAX_MACHINES; m++)
    {
        int racing = 0;
        for (int h = 0; h < s_setup.numHumans; h++)
            racing |= s_setup.humanMachine[h] == m;
        if (!racing)
            s_leftFrom[m] = 1;
    }
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
    case MSG_INPUT:
        Rd_Inputs(&r);
        break;
    case MSG_LEAVE:
    {
        int m = Rd_U8(&r);
        uint32_t from = Rd_U32(&r);
        if (Rd_U8(&r))
            Rd_Inputs(&r);   /* sus ultimas pulsaciones */
        if (!r.bad)
            MarkLeft(m, from);
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

static void Net_HandleEvent(ENetEvent* ev)
{
    switch (ev->type)
    {
    case ENET_EVENT_TYPE_CONNECT:
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
        {
            SetStatus(s_machine < 0 ? "Cannot connect" : "Disconnected from the host");
            /* sin anfitrion no llega nada mas: en la carrera, los demas van sin mando desde el frame que falta */
            if (s_inRace)
                for (int m = 0; m < NET_MAX_MACHINES; m++)
                    if (m != s_machine)
                        MarkLeft(m, (uint32_t)s_frame + (s_waiting ? 0 : 1));
            int racing = s_inRace;
            NetRaceSetup setup = s_setup;
            int myHuman = s_myHuman, frame = s_frame;
            enet_host_destroy(s_enet);
            s_enet = NULL;
            Net_Reset();
            if (racing)
            {
                /* la carrera sigue en esta maquina hasta que salga */
                s_inRace = 1;
                s_setup = setup;
                s_myHuman = myHuman;
                s_frame = frame;
            }
        }
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
    /* retardo: el camino mas lento entre dos maquinas pasa por el anfitrion (ida del ping de un cliente mas la del
     * otro), mas 2 frames de margen; entre NET_INPUT_DELAY y NET_MAX_DELAY */
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
    int oneWay = (int)(r1 + r2) / 2;
    int delay = (oneWay * 60 + 999) / 1000 + 2;
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
}

int Net_TakeStart(void)
{
    int v = s_startPending;
    s_startPending = 0;
    return v;
}

/* el ultimo paquete de pulsaciones propio, a todos (anfitrion) o al anfitrion (cliente) */
static void Net_SendOwnInputs(void)
{
    if (!s_lastPacketLen)
        return;
    if (s_isHost)
    {
        for (int i = 1; i < NET_MAX_MACHINES; i++)
            if (s_peers[i])
                SendQuick(s_peers[i], s_lastPacket, s_lastPacketLen);
    }
    else
        SendQuick(s_server, s_lastPacket, s_lastPacketLen);
}

void Net_RaceFrame(void)
{
    if (!s_inRace)
        return;
    uint32_t f = (uint32_t)++s_frame;
    /* lo que se acaba de leer del mando local vale para dentro de s_delay frames */
    RaceCar* me = s_myHuman >= 0 ? g_Humans[s_myHuman] : NULL;
    if (me && me->ctrl && !s_leftFrom[s_machine])
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
        uint32_t when = f + (uint32_t)s_delay;
        StoreInput(s_myHuman, when, in.bits, in.ax, in.ay, in.kind);
        Msg m;
        Msg_Begin(&m, MSG_INPUT);
        Msg_Inputs(&m, MSG_INPUT, s_myHuman, when, NET_REDUNDANT);
        memcpy(s_lastPacket, m.buf, (size_t)m.len);
        s_lastPacketLen = m.len;
        Net_SendOwnInputs();
    }
    /* espera las de todos para este frame */
    s_waiting = 1;
    enet_uint32 start = enet_time_get(), waitStart = start, lastSend = start;
    int missing = -1;
    for (;;)
    {
        int all = 1;
        for (int h = 0; h < s_setup.numHumans; h++)
            if (!HaveInput(h, f))
            {
                all = 0;
                missing = MachineOfHuman(h);
            }
        if (all)
            break;
        Net_Service(1);
        PsyX_UpdateInput();   /* (la ventana sigue respondiendo) */
        /* si el que falta tambien espera (por las nuestras), que no se queden los dos esperando un paquete perdido */
        if (enet_time_get() - lastSend >= NET_RESEND_MS)
        {
            lastSend = enet_time_get();
            Net_SendOwnInputs();
        }
        if (enet_time_get() - start > NET_WAIT_MS)
        {
            for (int h = 0; h < s_setup.numHumans; h++)
                if (!HaveInput(h, f))
                {
                    int m = MachineOfHuman(h);
                    Net_Log("[red] la maquina %d no manda nada desde hace %d s\n", m, NET_WAIT_MS / 1000);
                    if (s_isHost)
                        Host_MachineLeaves(m);
                    else if (m == 0 || !s_enet)
                        MarkLeft(m, f);   /* sin anfitrion */
                }
            start = enet_time_get();
        }
    }
    s_waiting = 0;
    enet_uint32 waited = enet_time_get() - waitStart;
    if (waited >= 100)
    {
        /* (las esperas largas: quien las causa) */
        if (waited >= 1000)
            Net_Log("[red] espera de %u ms en el frame %u por la maquina %d\n", waited, f, missing);
        else
            Net_LogFile("[red] espera de %u ms en el frame %u por la maquina %d\n", waited, f, missing);
    }
    for (int h = 0; h < s_setup.numHumans; h++)
        ApplyInput(h, f);
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
    Net_Log("[red] fin de la carrera en red (frame %d%s)\n", s_frame, s_desync ? ", desincronizada" : "");
}

int Net_DesyncFrame(void)
{
    return s_desync;
}

/* ---------------------------------------------------------------- donde esta cada uno y campeonato */

void Net_SetStage(int stage)
{
    if (s_isHost || !s_server || s_machine < 0 || s_inRace || stage == s_stage)
        return;
    s_stage = stage;
    Msg m;
    Msg_Begin(&m, MSG_STAGE);
    Msg_U8(&m, stage);
    Send(s_server, &m, 0);
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
