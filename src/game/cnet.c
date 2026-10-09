#include <stdlib.h>
#include "nr.h"
#include "model.h"
#include "track.h"
#include "vecmath.h"
#include "cnet.h"

uint8_t* g_Nets[CNET_MAX];
int      g_NumNets;

#define NODE(net, i) ((net) + 4 + (i) * 12)

void Cnet_Reset(void)
{
    g_NumNets = 0;
}

int Cnet_Count(const uint8_t* net)
{
    return net ? (int16_t)NR_Read16(net + 2) : 0;
}

void Cnet_NodePos(const uint8_t* net, int node, int* out)
{
    const uint8_t* n = NODE(net, node);
    out[0] = (int16_t)NR_Read16(n + 6) << 8;
    out[1] = (int16_t)NR_Read16(n + 8) << 8;
    out[2] = (int16_t)NR_Read16(n + 10) << 8;
}

static void Write16(uint8_t* p, int v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* 8004c368. Pendiente: las banderas de superficie de la red principal (FUN_80065ee8, bits 0-1). */
int Cnet_OnLoaded(NrAsset* a)
{
    Asset_Register(a);
    uint8_t* net = a->data + 0xc;
    if (g_NumNets < CNET_MAX)
        g_Nets[g_NumNets++] = net;
    int n = Cnet_Count(net);
    for (int i = 0; i < n; i++)
    {
        uint8_t* p = NODE(net, i) + 4;
        int pos[3] = { (int16_t)NR_Read16(p + 4) << 8, (int16_t)NR_Read16(p + 6) << 4,
                       (int16_t)NR_Read16(p + 2) << 8 };
        TrackPos tp = { 0xff, 0, -1 };
        int h0 = Track_GroundHeight(&tp, pos, 0);
        TrackPos tp2 = { 0xff, 0, -1 };
        int h2 = Track_GroundHeight(&tp2, pos, 2);
        int y = (h0 + 0x7f) >> 8;
        if (h2 != h0 && abs(h2 - pos[1]) < abs(h0 - pos[1]))
            y = (h2 + 0x7f) >> 8;
        Write16(p + 4, y);
        Write16(p + 2, pos[0] >> 8);
        Write16(p + 6, pos[2] >> 8);
    }
    return 1;
}

/* 8002cc50: nodo mas cercano en el plano (distancia aproximada octogonal) */
int Cnet_FindNearest(const uint8_t* net, const int* pos, int skipFlagged)
{
    if (!net)
        return 0;
    int best = -1, bestD = 0;
    int n = Cnet_Count(net);
    for (int i = 0; i < n; i++)
    {
        const uint8_t* p = NODE(net, i);
        if (skipFlagged && (NR_Read16(p + 4) & 0x1000))
            continue;
        int d[2] = { pos[0] - ((int16_t)NR_Read16(p + 6) << 8), pos[2] - ((int16_t)NR_Read16(p + 10) << 8) };
        int dist = Vec_ApproxLen2(d);
        if (best < 0 || dist < bestD)
        {
            best = i;
            bestD = dist;
        }
    }
    return best;
}

/* 8004bd74 */
int Cnet_Next(const uint8_t* net, int node)
{
    if (!net || node < 0 || node >= Cnet_Count(net))
        return -1;
    uint32_t l = NR_Read32(NODE(net, node));
    int links[3] = { (int)(l >> 22), (int)((l >> 12) & 0x3ff), (int)((l >> 2) & 0x3ff) };
    for (int k = 0; k < 3; k++)
        if (links[k] >= 0)
            return links[k];
    return -1;
}

/* 8004becc: nodo de la red mas cercano en 3D */
int Cnet_FindNearest3D(const uint8_t* net, const int* pos)
{
    int best = -1, bestD = 0;
    for (int i = 0; i < Cnet_Count(net); i++)
    {
        int n[3];
        Cnet_NodePos(net, i, n);
        int d[3] = { pos[0] - n[0], pos[1] - n[1], pos[2] - n[2] };
        int l = Vec_ApproxLen3(d);
        if (best < 0 || l < bestD)
        {
            best = i;
            bestD = l;
        }
    }
    return best;
}
