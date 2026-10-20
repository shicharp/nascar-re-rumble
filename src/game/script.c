/* Interprete de guiones (80067890): maquina de pila con bytecode. Lo usan las cabeceras SHDR del streaming (condicion
 * para cargar el asset), los guiones de las entidades (+0xD0 golpe, +0xD4 muerte, +0xD8; disparadores 0xE, Csac) y
 * el mensaje 9.
 * Codigos (a = la cima de la pila, b = la siguiente, c = la tercera; por encima de 8 se saca a, de 0x1C b y de 0x37 c):
 *  0x80 | n mete n; 1 ~byte; 2 byte + 0x80; 3 u16 y 4 u32 (en big endian); 5 el argumento; 6 / 7 nada;
 *  8 salto relativo (desde el byte del desplazamiento); 9 !a; 0xA tabla de bytes DAT_800b0dc0[a]; 0xB a >= 0:
 *  aleatorio de la carrera [a & 31] (0..999), a < 0: |Rand_A| % -a; 0xC / 0xD comentario a (encolado / ya);
 *  0xE / 0xF comentario a pegado a la entidad (0xF aunque haya cola); 0x10 GameInfo[a]; 0x11 *GameInfo[0x27 + a]
 *  (punteros a variables del juego, 80067644); 0x12 temporizador[a] / 5; 0x13 contador[a]; 0x14 salto si a == 0;
 *  0x15 - 0x1C incrementan / decrementan (temporizadores de 5 en 5) y vuelven a meter a; 0x1D - 0x20 asignan b;
 *  0x21 b == a, 0x22 !=, 0x23 a < b, 0x24 b >= a, 0x25 b < a, 0x26 a >= b, 0x27 b + a, 0x28 b - a, 0x29 b * a,
 *  0x2A b / a, 0x2B b % a, 0x2C b && a, 0x2D b || a; 0x2E con b == 0 termina devolviendo a; 0x2F b == 0: mensaje
 *  0xC a la entidad, b == -1: bit a de los desbloqueos (GameInfo +0x4C), b > 0: mensaje 0xC a la entidad de id b;
 *  0x30 - 0x37 suman / restan b y vuelven a meter a; 0x38 - 0x3D ordenes a una entidad (c = id, 0 = la propia).
 *  Al llegar a un 0 devuelve la cima. La pila empieza con 1 (y el valor inicial si es >= 0).
 * Estado (Game_Init 80079470, puesto a cero en cada carga 80067680): 32 aleatorios 0..999 (DAT_800af6dc, tambien
 * GameInfo +0x18..+0x24), 100 contadores (DAT_800af75c) y 32 temporizadores en tics * 5 (DAT_800af6c4, empiezan en
 * -10) que 80067834 descuenta cada frame. DAT_800af920 desactiva el interprete al salir de la carrera (80067880). */
#include <stdlib.h>
#include <string.h>
#include "nr.h"
#include "globals.h"
#include "racecar.h"
#include "actor.h"
#include "carphys_int.h"
#include "snd.h"
#include "pause.h"
#include "cnet.h"
#include "script.h"

#define STACK_MAX 64

static int s_rand[32];        /* DAT_800af6dc */
static int s_counters[100];   /* DAT_800af75c */
static int s_timers[32];      /* DAT_800af6c4 */
static int s_disabled;        /* DAT_800af920 */
static int s_unknown1, s_unknown3;   /* DAT_800af750 / DAT_800af754 (sin identificar) */

static int Script_LogOn(void)
{
    static int s_on = -1;
    if (s_on < 0)
        s_on = getenv("NR_SCRIPTLOG") != NULL;
    return s_on;
}

/* GameInfo como tabla de enteros (0xAC bytes) */
static int* GI(int i)
{
    static int dummy;
    if (!g_GameInfo || i < 0 || i >= (int)(sizeof(GameInfo) / 4))
        return &dummy;
    return (int*)g_GameInfo + i;
}

/* 80067644: GameInfo +0x9C..+0xA8 apuntan a variables del juego */
static int* GameVar(int i)
{
    static int dummy;
    switch (i)
    {
    case 0: return &g_RaceTime;        /* DAT_800af748 */
    case 1: return &s_unknown1;        /* DAT_800af750 */
    case 2: return &g_RaceEndTimer;    /* DAT_800af740 */
    case 3: return &s_unknown3;        /* DAT_800af754 */
    default: return &dummy;
    }
}

static int* Counter(int i)
{
    static int dummy;
    return (unsigned)i < 100 ? &s_counters[i] : &dummy;
}

static int* Timer(int i)
{
    static int dummy;
    return (unsigned)i < 32 ? &s_timers[i] : &dummy;
}

/* 80049acc: mensaje 0xC a la entidad de id 'id' (0 si no existe) */
static int Script_AskEntity(int id, int arg)
{
    for (Actor* a = g_Actors; a; a = a->next)
        if (a->handle > 0 && a->id == (uint16_t)id)
            return a->handler(0xc, a, arg, 0);
    return 0;
}

static void Script_Pending(int op)
{
    static uint8_t s_seen[0x40];
    if (op < 0x40 && !s_seen[op])
    {
        s_seen[op] = 1;
        NR_Log("[guion] codigo 0x%02x sin implementar en el port\n", op);
    }
}

/* 80067890 */
int Script_Run(const uint8_t* code, int arg, int init, void* ent)
{
    if (!code || code[0] == 0)
        return 1;
    if (s_disabled)
        return 0;
    Actor* self = (Actor*)ent;
    int st[STACK_MAX];
    int sp = 0;
    st[sp++] = 1;
    if (init >= 0)
        st[sp++] = init;
    int a = 0, b = 0, c = 0;
    const uint8_t* pc = code;
    do
    {
        int op = *pc++;
        if (op & 0x80)
        {
            if (sp < STACK_MAX)
                st[sp++] = op & 0x7f;
            continue;
        }
        if (op > 8)
        {
            a = sp > 0 ? st[--sp] : 0;
            if (op > 0x1c)
            {
                b = sp > 0 ? st[--sp] : 0;
                if (op > 0x37)
                    c = sp > 0 ? st[--sp] : 0;
            }
        }
        int push = 1, v = a;
        switch (op)
        {
        case 1: v = ~(int)*pc++; break;
        case 2: v = *pc++ + 0x80; break;
        case 3: v = pc[0] << 8 | pc[1]; pc += 2; break;
        case 4: v = (int)((uint32_t)pc[0] << 24 | (uint32_t)pc[1] << 16 | (uint32_t)pc[2] << 8 | pc[3]); pc += 4; break;
        case 5: v = arg; break;
        case 8: pc += *pc; push = 0; break;
        case 9: v = a == 0; break;
        case 0xa: v = 0; Script_Pending(op); break;   /* DAT_800b0dc0: tabla de la carrera sin identificar */
        case 0xb:
            if (a >= 0)
                v = s_rand[a & 31];
            else
            {
                int r = Rand_A();
                v = (int)((unsigned)abs(r) % (unsigned)-a);
            }
            break;
        case 0xc: Speech_Queue(a, NULL, 0); push = 0; break;   /* 8002b0f4 */
        case 0xd: Speech_Queue(a, NULL, 0); push = 0; break;   /* 8002b1d0 (en el original, delante de la cola) */
        case 0xe:
        case 0xf: v = Speech_Queue(a, self, op == 0xe); break;   /* 8002b2d8 (frase que sigue a la entidad) */
        case 0x10: v = *GI(a); break;
        case 0x11: v = *GameVar(a); break;
        case 0x12: v = *Timer(a) / 5; break;
        case 0x13: v = *Counter(a); break;
        case 0x14:
            if (a == 0)
                pc += *pc;
            else
                pc++;
            push = 0;
            break;
        case 0x15: (*GI(a))++; break;
        case 0x16: (*GI(0x27 + a))++; break;   /* (el original suma al puntero, no a la variable) */
        case 0x17: *Timer(a) += 5; break;
        case 0x18: (*Counter(a))++; break;
        case 0x19: (*GI(a))--; break;
        case 0x1a: (*GI(0x27 + a))--; break;
        case 0x1b: *Timer(a) -= 5; break;
        case 0x1c: (*Counter(a))--; break;
        case 0x1d: *GI(a) = b; push = 0; break;
        case 0x1e: *GameVar(a) = b; push = 0; break;
        case 0x1f: *Timer(a) = b * 5; push = 0; break;
        case 0x20: *Counter(a) = b; push = 0; break;
        case 0x21: v = b == a; break;
        case 0x22: v = b != a; break;
        case 0x23: v = a < b; break;
        case 0x24: v = b >= a; break;
        case 0x25: v = b < a; break;
        case 0x26: v = a >= b; break;
        case 0x27: v = a + b; break;
        case 0x28: v = b - a; break;
        case 0x29: v = a * b; break;
        case 0x2a: v = a ? b / a : 0; break;
        case 0x2b: v = a ? b % a : 0; break;
        case 0x2c: v = b != 0 && a != 0; break;
        case 0x2d: v = b != 0 || a != 0; break;
        case 0x2e:
            if (b == 0)
                return a;
            push = 0;
            break;
        case 0x2f:
            if (b == 0)
                v = self ? self->handler(0xc, self, a, 0) : 0;
            else if (b == -1)
                v = g_GameInfo ? (int)((uint32_t)g_GameInfo->unlockMask & (1u << (a & 31))) : 0;
            else if (b > 0)
                v = Script_AskEntity(b, a);
            else
                push = 0;
            break;
        case 0x30: *GI(a) += b; break;
        case 0x31: *GI(0x27 + a) += b; break;
        case 0x32: *Timer(a) += b * 5; break;
        case 0x33: *Counter(a) += b; break;
        case 0x34: *GI(a) -= b; break;
        case 0x35: *GI(0x27 + a) -= b; break;
        case 0x36: *Timer(a) -= b * 5; break;
        case 0x37: *Counter(a) -= b; break;
        case 0x38:
            /* orden b (argumento a) a la propia entidad (c == 0, 80049788) o a las de id c (800498e4) */
            if (c == 0)
            {
                if (self)
                    Actor_PushCmd(self, b, a);
            }
            else
                Actors_CmdById(c, b, a);
            push = 0;
            break;
        case 0x39: Actors_CmdByGroup(c, b, a); push = 0; break;   /* 800499b4 */
        case 0x3a: Actors_CmdByTeam(c, b, a); push = 0; break;    /* 80049a40 */
        case 0x3b: Cnet_SetNodeOff(c, a, b); push = 0; break;     /* 8004a168: nodo a de la red c */
        case 0x3c: case 0x3d:
            /* generadores Csac (8006094c / 800609c0) */
            Script_Pending(op);
            push = 0;
            break;
        default:
            push = 0;   /* 6, 7 y codigos desconocidos */
            break;
        }
        if (push && sp < STACK_MAX)
            st[sp++] = v;
    } while (*pc != 0);
    int r = sp > 0 ? st[sp - 1] : 0;
    if (Script_LogOn())
        NR_Log("[guion] %02x %02x %02x %02x... -> %d\n", code[0], code[1], code[2], code[3], r);
    return r;
}

/* 80067680 (Race_Load / Frontend_Load) */
void Script_ResetRace(void)
{
    if (g_GameInfo)
        for (int k = 0; k < 4; k++)
            *GI(6 + k) = (int)((unsigned)Rand_A() % 1000);   /* GameInfo +0x18..+0x24 */
    for (int k = 0; k < 32; k++)
        s_rand[k] = (int)((unsigned)Rand_A() % 1000);
    memset(s_counters, 0, sizeof(s_counters));
    for (int k = 0; k < 32; k++)
        s_timers[k] = -10;
    s_disabled = 0;
}

/* 80067834 (Race_Frame1P / 2P) */
void Script_Tick(int dt)
{
    for (int k = 0; k < 32; k++)
        if (s_timers[k] > 0)
        {
            int v = s_timers[k] - dt;
            s_timers[k] = v < 5 ? 0 : v;
        }
}

/* 80067880 (al salir de la carrera) */
void Script_Disable(void)
{
    s_disabled = 1;
}
