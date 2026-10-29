/* Solo en el port: partidas en LAN con ENet (ver net.h). */
#include <enet/enet.h>
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
#include "net.h"

#define NET_VERSION   2
#define NET_CHANNELS  2           /* 0 sala y ordenes; 1 pulsaciones y salidas de la carrera (fiables y en orden) */
#define NET_RING      128         /* frames de pulsaciones guardados por humano */
#define NET_SUM_EVERY 30          /* frames entre sumas de control (medio segundo) */
#define NET_SUM_RING  16
#define NET_WAIT_MS   15000       /* sin pulsaciones de una maquina tanto tiempo: el anfitrion la da por salida */

enum
{
    MSG_HELLO = 1,    /* cliente -> anfitrion: version, nombre, piloto, cambio */
    MSG_WELCOME,      /* anfitrion -> cliente: su numero de maquina */
    MSG_REJECT,       /* anfitrion -> cliente: motivo (texto) */
    MSG_LOBBY,        /* anfitrion -> todos: la lista de jugadores */
    MSG_START,        /* anfitrion -> los de la carrera: NetRaceSetup */
    MSG_INPUT,        /* pulsaciones de un humano para un frame (el anfitrion las reenvia) */
    MSG_LEAVE,        /* maquina que deja la carrera y frame desde el que su humano va sin mando */
    MSG_SUM,          /* cliente -> anfitrion: sumas de la carrera en un frame */
    MSG_DESYNC,       /* anfitrion -> todos: frame desincronizado */
};

static ENetHost* s_enet;
static ENetPeer* s_server;                      /* cliente: la conexion con el anfitrion */
static ENetPeer* s_peers[NET_MAX_MACHINES];     /* anfitrion: la de cada maquina */
static int       s_isHost, s_active, s_machine = -1;
static NetPlayer s_players[NET_MAX_MACHINES];
static char      s_status[64];
static int       s_init;

/* carrera */
typedef struct
{
    uint32_t frame;               /* frame al que corresponde (0 vacio) */
    uint32_t bits;                /* ctrl +8: bits de accion */
    uint8_t  ax, ay, kind;        /* ctrl +0xC / +0xD / +0xE: stick y tipo de mando */
} NetInput;

static int          s_inRace, s_startPending, s_myHuman = -1, s_frame, s_desync;
static int          s_waiting;                      /* Net_RaceFrame espera las pulsaciones del frame s_frame */
static NetRaceSetup s_setup;
static NetInput     s_inputs[NET_MAX_HUMANS][NET_RING];
static uint32_t     s_leftFrom[NET_MAX_MACHINES];   /* frame desde el que la maquina ya no manda (0 sigue) */
static uint32_t     s_lastInput[NET_MAX_MACHINES];  /* anfitrion: ultimo frame recibido de cada maquina */
static GameSettings s_savedSettings;                /* cliente: sus ajustes, que la salida pisa */
static struct
{
    uint32_t frame;
    uint32_t sum[NET_MAX_MACHINES][3];
    uint8_t  have[NET_MAX_MACHINES];
} s_sums[NET_SUM_RING];

/* ---------------------------------------------------------------- mensajes */

typedef struct
{
    uint8_t buf[512];
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
    Msg_U8(m, p->transmission);
    Msg_U8(m, p->inRace);
    Msg_Bytes(m, p->name, NET_NAME_LEN + 1);
}

static void Rd_Player(Rd* r, NetPlayer* p)
{
    p->driver = (uint8_t)Rd_U8(r);
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
    p->transmission = g_Settings.transmission[0] <= 5 ? g_Settings.transmission[0] : 0;
    if (g_Settings.playerNameSet[0] && g_Settings.playerName[0][0])
        snprintf(p->name, sizeof(p->name), "%s", g_Settings.playerName[0]);
    else
        snprintf(p->name, sizeof(p->name), "Player");
}

static void SetStatus(const char* s)
{
    snprintf(s_status, sizeof(s_status), "%s", s);
    NR_Log("[red] %s\n", s);
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
    Broadcast(&m, 0, -1);
    int n = Net_NumPlayers();
    NR_Log("[red] sala: %d jugador%s\n", n, n == 1 ? "" : "es");
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

static void Msg_Input(Msg* m, int h, uint32_t frame, const NetInput* in)
{
    Msg_Begin(m, MSG_INPUT);
    Msg_U8(m, h);
    Msg_U32(m, frame);
    Msg_U32(m, in->bits);
    Msg_U8(m, in->ax);
    Msg_U8(m, in->ay);
    Msg_U8(m, in->kind);
}

static void Rd_Input(Rd* r)
{
    int h = Rd_U8(r);
    uint32_t frame = Rd_U32(r), bits = Rd_U32(r);
    int ax = Rd_U8(r), ay = Rd_U8(r), kind = Rd_U8(r);
    if (!r->bad && s_inRace)
        StoreInput(h, frame, bits, ax, ay, kind);
    int m = MachineOfHuman(h);
    if (s_isHost && m > 0 && frame > s_lastInput[m])
        s_lastInput[m] = frame;
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
    s_players[machine].inRace = 0;
    if (s_inRace)
        NR_Log("[red] la maquina %d deja la carrera (desde el frame %u)\n", machine, s_leftFrom[machine]);
}

/* anfitrion: decide el frame desde el que la maquina va sin mando y lo dice a los demas (por el canal de las
 * pulsaciones, detras de las suyas) */
static void Host_MachineLeaves(int machine)
{
    if (s_leftFrom[machine] || !s_setup.numHumans)
        return;
    uint32_t from = (machine == 0 ? (uint32_t)s_frame + NET_INPUT_DELAY : s_lastInput[machine]) + 1;
    MarkLeft(machine, from);
    Msg m;
    Msg_Begin(&m, MSG_LEAVE);
    Msg_U8(&m, machine);
    Msg_U32(&m, from);
    Broadcast(&m, 1, machine);
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
            NR_Log("[red] DESINCRONIZACION en el frame %u: maquina %d (coches %08x/%08x actores %08x/%08x aleatorios "
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

static void Host_Hello(ENetPeer* peer, Rd* r)
{
    int ver = Rd_U8(r);
    NetPlayer p;
    memset(&p, 0, sizeof(p));
    Rd_Player(r, &p);
    p.used = 1;
    p.inRace = 0;
    const char* why = NULL;
    int id = -1;
    if (r->bad || ver != NET_VERSION)
        why = "Different game version";
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
        NR_Log("[red] rechazado: %s\n", why);
        return;
    }
    s_players[id] = p;
    s_peers[id] = peer;
    peer->data = (void*)(intptr_t)id;
    Msg_Begin(&m, MSG_WELCOME);
    Msg_U8(&m, id);
    Send(peer, &m, 0);
    NR_Log("[red] entra %s (maquina %d)\n", p.name, id);
    Host_SendLobby();
}

static void Host_Drop(ENetPeer* peer)
{
    int id = (int)(intptr_t)peer->data;
    if (id <= 0 || id >= NET_MAX_MACHINES || s_peers[id] != peer)
        return;
    NR_Log("[red] sale %s (maquina %d)\n", s_players[id].name, id);
    if (s_inRace && s_players[id].inRace)
        Host_MachineLeaves(id);
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
    case MSG_INPUT:
        Rd_Input(&r);
        if (!r.bad)
            BroadcastRaw(data, len, channel, from);   /* a los demas clientes */
        break;
    case MSG_LEAVE:
        if (from > 0 && from < NET_MAX_MACHINES)
            Host_MachineLeaves(from);
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
    memset(s_sums, 0, sizeof(s_sums));
    s_frame = 0;
    s_desync = 0;
    s_myHuman = -1;
    for (int h = 0; h < s_setup.numHumans; h++)
    {
        if (s_setup.humanMachine[h] == s_machine)
            s_myHuman = h;
        /* los primeros frames no tienen mando: nadie los ha leido todavia */
        for (uint32_t f = 1; f <= NET_INPUT_DELAY; f++)
            StoreInput(h, f, 0, 0x80, 0x80, 4);
    }
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
        NR_Log("[red] conectado: maquina %d\n", s_machine);
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
        NR_Log("[red] sala: %d jugadores\n", Net_NumPlayers());
        break;
    case MSG_START:
        if (s_inRace)
            break;
        Rd_Bytes(&r, &s_setup, (int)sizeof(s_setup));
        if (r.bad || s_setup.numHumans > NET_MAX_HUMANS)
            break;
        s_savedSettings = g_Settings;
        s_inRace = 1;
        Race_Reset();
        s_startPending = 1;
        NR_Log("[red] salida: %d humanos, soy el %d, semilla %u\n", s_setup.numHumans, s_myHuman, s_setup.seed);
        break;
    case MSG_INPUT:
        Rd_Input(&r);
        break;
    case MSG_LEAVE:
    {
        int m = Rd_U8(&r);
        uint32_t from = Rd_U32(&r);
        if (!r.bad)
            MarkLeft(m, from);
        break;
    }
    case MSG_DESYNC:
    {
        uint32_t frame = Rd_U32(&r);
        if (!s_desync)
        {
            s_desync = (int)frame;
            NR_Log("[red] DESINCRONIZACION en el frame %u (aviso del anfitrion)\n", frame);
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
    Net_Leave();
    if (s_init)
        enet_deinitialize();
    s_init = 0;
}

static void Net_Reset(void)
{
    s_server = NULL;
    memset(s_peers, 0, sizeof(s_peers));
    memset(s_players, 0, sizeof(s_players));
    s_isHost = s_active = 0;
    s_machine = -1;
    s_inRace = s_startPending = 0;
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

void Net_Poll(void)
{
    Net_Service(0);
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
    int m = s_inRace ? MachineOfHuman(human) : -1;
    return m >= 0 && s_players[m].used ? s_players[m].name : (s_inRace ? "Player" : NULL);
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
    for (int m = 1; m < NET_MAX_MACHINES && n < NET_MAX_HUMANS - 1; m++)
        if (s_players[m].used && s_peers[m] && !s_players[m].inRace)
            machines[n++] = m;
    return n;
}

void Net_HostStartRace(void)
{
    NetRaceSetup* s = &s_setup;
    GameSettings* gs = &g_Settings;
    s->seed = (uint32_t)g_FrontendFrames;
    s->unlockMask = (uint32_t)g_GameInfo->unlockMask;
    s->venue = gs->venue;
    s->track = gs->track;
    s->laps = gs->laps;
    s->racingClass = gs->racingClass;
    s->powerUps = (uint8_t)g_GameInfo->powerUps;
    s->teamRace = gs->teamRace;
    s->gameMode = gs->gameMode;
    s->gridCount = g_RaceGrid.count;
    memcpy(s->grid, g_RaceGrid.car, sizeof(s->grid));
    /* GridCar.player es el numero de humano: el 0 es el anfitrion; los demas, sus maquinas (Fe_StartRace las ha
     * puesto en ese orden con Net_HostRacers) */
    int machines[NET_MAX_HUMANS], n = Net_HostRacers(machines);
    s->numHumans = (uint8_t)(n + 1);
    memset(s->humanMachine, 0xff, sizeof(s->humanMachine));
    s->humanMachine[0] = 0;
    s->transmission[0] = s_players[0].transmission = gs->transmission[0] <= 5 ? gs->transmission[0] : 0;
    for (int i = 0; i < n; i++)
    {
        s->humanMachine[i + 1] = (uint8_t)machines[i];
        s->transmission[i + 1] = s_players[machines[i]].transmission;
    }
    Msg m;
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
    NR_Log("[red] salida: %d humanos, semilla %u\n", s->numHumans, s->seed);
}

int Net_TakeStart(void)
{
    int v = s_startPending;
    s_startPending = 0;
    return v;
}

void Net_RaceFrame(void)
{
    if (!s_inRace)
        return;
    uint32_t f = (uint32_t)++s_frame;
    /* lo que se acaba de leer del mando local vale para dentro de NET_INPUT_DELAY frames */
    RaceCar* me = s_myHuman >= 0 ? g_Humans[s_myHuman] : NULL;
    if (me && me->ctrl && !s_leftFrom[s_machine])
    {
        NetInput in;
        in.bits = *(uint32_t*)(me->ctrl + 8);
        in.ax = me->ctrl[0xc];
        in.ay = me->ctrl[0xd];
        in.kind = me->ctrl[0xe];
        uint32_t when = f + NET_INPUT_DELAY;
        StoreInput(s_myHuman, when, in.bits, in.ax, in.ay, in.kind);
        Msg m;
        Msg_Input(&m, s_myHuman, when, &in);
        if (s_isHost)
            Broadcast(&m, 1, -1);
        else
            Send(s_server, &m, 1);
    }
    /* espera las de todos para este frame */
    s_waiting = 1;
    enet_uint32 start = enet_time_get();
    for (;;)
    {
        int all = 1;
        for (int h = 0; h < s_setup.numHumans; h++)
            if (!HaveInput(h, f))
                all = 0;
        if (all)
            break;
        Net_Service(1);
        PsyX_UpdateInput();   /* (la ventana sigue respondiendo) */
        if (enet_time_get() - start > NET_WAIT_MS)
        {
            for (int h = 0; h < s_setup.numHumans; h++)
                if (!HaveInput(h, f))
                {
                    int m = MachineOfHuman(h);
                    NR_Log("[red] la maquina %d no manda nada desde hace %d s\n", m, NET_WAIT_MS / 1000);
                    if (s_isHost)
                        Host_MachineLeaves(m);
                    else if (m == 0 || !s_enet)
                        MarkLeft(m, f);   /* sin anfitrion */
                }
            start = enet_time_get();
        }
    }
    s_waiting = 0;
    for (int h = 0; h < s_setup.numHumans; h++)
        ApplyInput(h, f);
}

void Net_RaceStep(void)
{
    if (!s_inRace || s_frame % NET_SUM_EVERY != 0)
        return;
    uint32_t sum[3];
    Race_StateChecksum(sum);
    if (s_isHost)
        Sums_Store((uint32_t)s_frame, 0, sum);
    else if (s_server)
    {
        Msg m;
        Msg_Begin(&m, MSG_SUM);
        Msg_U32(&m, (uint32_t)s_frame);
        for (int i = 0; i < 3; i++)
            Msg_U32(&m, sum[i]);
        Send(s_server, &m, 1);
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
        Send(s_server, &m, 1);
        g_Settings = s_savedSettings;   /* sus ajustes, que la salida habia cambiado */
    }
    s_players[s_machine >= 0 ? s_machine : 0].inRace = 0;
    s_inRace = 0;
    NR_Log("[red] fin de la carrera en red (frame %d%s)\n", s_frame, s_desync ? ", desincronizada" : "");
}

int Net_DesyncFrame(void)
{
    return s_desync;
}
