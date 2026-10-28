/* Solo en el port: partidas en LAN con ENet (ver net.h). */
#include <enet/enet.h>
#include <stdio.h>
#include <string.h>
#include "nr.h"
#include "globals.h"
#include "frontend.h"
#include "net.h"

#define NET_VERSION   1
#define NET_CHANNELS  2           /* 0 sala y ordenes, 1 pulsaciones (los dos fiables y en orden) */

enum
{
    MSG_HELLO = 1,    /* cliente -> anfitrion: version, nombre, piloto, cambio */
    MSG_WELCOME,      /* anfitrion -> cliente: su numero de maquina */
    MSG_REJECT,       /* anfitrion -> cliente: motivo (texto) */
    MSG_LOBBY,        /* anfitrion -> todos: la lista de jugadores */
};

static ENetHost* s_enet;
static ENetPeer* s_server;                      /* cliente: la conexion con el anfitrion */
static ENetPeer* s_peers[NET_MAX_MACHINES];     /* anfitrion: la de cada maquina */
static int       s_isHost, s_active, s_machine = -1;
static NetPlayer s_players[NET_MAX_MACHINES];
static char      s_status[64];
static int       s_init;

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

static void Send(ENetPeer* p, const Msg* m, int channel)
{
    if (!p)
        return;
    ENetPacket* pk = enet_packet_create(m->buf, (size_t)m->len, ENET_PACKET_FLAG_RELIABLE);
    if (enet_peer_send(p, (enet_uint8)channel, pk) < 0)
        enet_packet_destroy(pk);
}

/* anfitrion: a todos los clientes salvo 'except' */
static void Broadcast(const Msg* m, int channel, int except)
{
    for (int i = 1; i < NET_MAX_MACHINES; i++)
        if (s_peers[i] && i != except)
            Send(s_peers[i], m, channel);
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
    s_peers[id] = NULL;
    memset(&s_players[id], 0, sizeof(s_players[id]));
    Host_SendLobby();
}

static void Host_Receive(ENetPeer* peer, const uint8_t* data, int len)
{
    Rd r = { data, len, 0, 0 };
    int type = Rd_U8(&r);
    switch (type)
    {
    case MSG_HELLO:
        Host_Hello(peer, &r);
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------- cliente */

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
    default:
        break;
    }
}

/* ---------------------------------------------------------------- API */

void Net_Init(void)
{
    if (!s_init && enet_initialize() == 0)
        s_init = 1;
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

void Net_Poll(void)
{
    if (!s_enet)
        return;
    ENetEvent ev;
    while (s_enet && enet_host_service(s_enet, &ev, 0) > 0)
    {
        switch (ev.type)
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
                ev.peer->data = (void*)(intptr_t)-1;   /* sin numero hasta el HELLO */
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            if (s_isHost)
                Host_Receive(ev.peer, ev.packet->data, (int)ev.packet->dataLength);
            else
                Client_Receive(ev.packet->data, (int)ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            if (s_isHost)
                Host_Drop(ev.peer);
            else
            {
                SetStatus(s_machine < 0 ? "Cannot connect" : "Disconnected from the host");
                enet_host_destroy(s_enet);
                s_enet = NULL;
                Net_Reset();
            }
            break;
        default:
            break;
        }
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
