/* Tarjeta de memoria emulada (ver memcard.h).
 * Imagen de 128 KB: 16 bloques de 8 KB = 1024 sectores de 128 bytes. Bloque 0: sector 0 cabecera ('MC' y XOR en
 * +0x7F), sectores 1-15 directorio ({+0 estado: 0x51 primer bloque de un fichero, 0x52 / 0x53 intermedio / ultimo,
 * 0xA0 libre, 0xA1-0xA3 borrado; +4 tamano; +8 siguiente bloque; +0xA nombre de 20; +0x7F XOR}), 16-35 lista de
 * sectores rotos (0xFF), 63 copia de la cabecera. Bloques 1-15: datos de los ficheros (sector = bloque * 64). */
#include <stdio.h>
#include <string.h>
#include "nr.h"
#include "memcard.h"

#define CARD_SIZE   0x20000
#define FRAME       0x80
#define BLOCK_SECTORS 64

static uint8_t s_img[CARD_SIZE];
static char    s_path[64];
static int     s_on;            /* Card_Init hecho */
static int     s_status = -3;   /* +0x900 */
static int     s_result;        /* +0x9A4: 1 si la peticion no se pudo hacer */
static int     s_dirRead;       /* +4 (bloque 0) a 1: el directorio esta leido */

/* peticion en curso (+0x8F8 / +0x998 / +0x99C / +0x910) */
static int      s_op;           /* 0 nada, 2 leer, 3 escribir, 6 escribir y despues la entrada del directorio */
static int      s_sector, s_count, s_dirBlock;
static uint8_t* s_readBuf;
static const uint8_t* s_writeBuf;
static int      s_wait;

static int  s_blockState[16];   /* +4 + bloque * 4: 1 ocupado, 2 libre */
static char s_blockName[16][24];

static uint8_t FrameXor(const uint8_t* f)
{
    uint8_t x = 0;
    for (int i = 0; i < FRAME - 1; i++)
        x ^= f[i];
    return x;
}

static void Card_Flush(void)
{
    FILE* f = fopen(s_path, "wb");
    if (!f)
    {
        NR_Log("[tarjeta] no se puede escribir %s\n", s_path);
        return;
    }
    fwrite(s_img, 1, CARD_SIZE, f);
    fclose(f);
}

/* tarjeta recien formateada (el directorio de 8001f910 mas la cabecera y su copia) */
static void Card_Blank(void)
{
    memset(s_img, 0, sizeof(s_img));
    for (int fr = 0; fr < 0x24; fr++)
    {
        uint8_t* f = s_img + fr * FRAME;
        if (fr == 0)
        {
            f[0] = 'M';
            f[1] = 'C';
        }
        else
        {
            f[0] = 0xa0;
            f[8] = f[9] = 0xff;
        }
        f[FRAME - 1] = FrameXor(f);
    }
    for (int fr = 0x10; fr < 0x24; fr++)   /* 8001f8d4: lista de sectores rotos vacia (sin rehacer el XOR) */
    {
        uint8_t* f = s_img + fr * FRAME;
        f[0] = f[1] = f[2] = f[3] = 0xff;
        f[8] = f[9] = 0xff;
    }
    memcpy(s_img + 63 * FRAME, s_img, FRAME);
}

/* 8001f3c4 + 8001f47c: la imagen del puerto (si no existe, una tarjeta formateada vacia) */
void Card_Init(int port)
{
    snprintf(s_path, sizeof(s_path), "memcard%d.mcd", port + 1);
    FILE* f = fopen(s_path, "rb");
    size_t n = 0;
    if (f)
    {
        n = fread(s_img, 1, CARD_SIZE, f);
        fclose(f);
    }
    if (n != CARD_SIZE || s_img[0] != 'M' || s_img[1] != 'C')
    {
        NR_Log("[tarjeta] %s: nueva (formateada)\n", s_path);
        Card_Blank();
        Card_Flush();
    }
    s_on = 1;
    s_status = -2;      /* +0x900 = -2 hasta la primera respuesta de _card_info */
    s_result = 0;
    s_op = 0;
    s_wait = 0;
    s_dirRead = 0;
    memset(s_blockState, 0, sizeof(s_blockState));
}

void Card_Stop(void)
{
    s_on = 0;
}

/* 8001f578: estado y nombre de cada bloque segun su entrada del directorio */
static void Card_ReadDir(void)
{
    s_blockState[0] = 1;
    strcpy(s_blockName[0], "UNKNOWNA");
    for (int b = 1; b < 16; b++)
    {
        const uint8_t* f = s_img + b * FRAME;
        uint8_t attr = FrameXor(f) == f[FRAME - 1] ? f[0] : 0x53;
        if (attr == 0x51)
        {
            s_blockState[b] = 1;
            memcpy(s_blockName[b], f + 10, 20);
            s_blockName[b][20] = 0;
        }
        else if (attr == 0xa0 || attr == 0xa1 || attr == 0xa3)
        {
            s_blockState[b] = 2;
            s_blockName[b][0] = 0;
        }
        else
        {
            s_blockState[b] = 1;
            strcpy(s_blockName[b], "UNKNOWNC");
        }
    }
    s_dirRead = 1;
}

/* la peticion en curso: sectores y, con 6, la entrada del directorio del fichero */
static void Card_Complete(void)
{
    if (s_op == 2)
        memcpy(s_readBuf, s_img + s_sector * FRAME, (size_t)s_count * FRAME);
    else if (s_op == 3 || s_op == 6)
    {
        memcpy(s_img + s_sector * FRAME, s_writeBuf, (size_t)s_count * FRAME);
        Card_Flush();
    }
    s_op = 0;
    s_dirRead = 0;
}

/* 8001f560 (lo que dejan los eventos de la BIOS): -4 / -5 mientras dura la peticion, despues 0 */
int Card_Status(void)
{
    if (!s_on)
        return -3;
    if (s_op)
    {
        if (s_wait-- > 0)
            return s_status;
        Card_Complete();
        s_status = 0;
    }
    else if (s_status == -2)
        s_status = 0;   /* _card_info: tarjeta presente y directorio leido */
    return s_status;
}

int Card_Result(void)
{
    return s_result;
}

/* 8001f848: hay tarjeta (o acaba de cambiar) */
static int Card_Present(void)
{
    int st = Card_Status();
    return st == -2 || st == -1 || st >= 0;
}

/* 8001fba4 */
int Card_BlockState(int block)
{
    if ((unsigned)block >= 16)
        return -1;
    if (!s_dirRead)
        Card_ReadDir();
    return s_blockState[block];
}

const char* Card_BlockName(int block)
{
    if ((unsigned)block >= 16)
        return "";
    if (!s_dirRead)
        Card_ReadDir();
    return s_blockName[block];
}

/* 8001fa3c */
int Card_FindFile(const char* name)
{
    if (!Card_Present())
        return 0;
    Card_ReadDir();
    for (int b = 0; b < 16; b++)
        if (s_blockState[b] == 1 && strcmp(name, s_blockName[b]) == 0)
            return b;
    return 0;
}

/* 8001fc14: primer bloque de un hueco de bloques libres seguidos para 'bytes' (0 si no cabe) */
int Card_AllocBlocks(int bytes)
{
    Card_ReadDir();
    int need = (bytes + 0x1fff) >> 13;
    for (int start = 0; start + need <= 16; start++)
    {
        int ok = 1;
        for (int k = 0; k < need && ok; k++)
            ok = Card_BlockState(start + k) == 2;
        if (ok)
            return start;
    }
    return 0;
}

/* 8001fce0: entrada del directorio de un fichero de un bloque (0x51, 0x2000 bytes, sin siguiente) */
static void Card_WriteDirEntry(const char* name, int block)
{
    uint8_t* f = s_img + block * FRAME;
    memset(f, 0, FRAME);
    f[0] = 0x51;
    f[5] = 0x20;
    f[8] = f[9] = 0xff;
    size_t n = strlen(name);
    memcpy(f + 10, name, n > 20 ? 20 : n);
    f[FRAME - 1] = FrameXor(f);
}

/* 8001df64 / 8001e08c: empieza una peticion (si hay otra, falla: +0x9A4 = 1) */
static void Card_Request(int op, int sector, int count, uint8_t* rbuf, const uint8_t* wbuf)
{
    if (s_op || sector < 0 || count <= 0 || (sector + count) * FRAME > CARD_SIZE)
    {
        s_result = 1;
        return;
    }
    s_op = op;
    s_sector = sector;
    s_count = count;
    s_readBuf = rbuf;
    s_writeBuf = wbuf;
    s_result = 0;
    s_status = op == 2 ? -4 : -5;
    s_wait = 1;
}

/* 8001fae8 */
int Card_ReadFile(const char* name, void* buf, int bytes)
{
    if (bytes == 0)
        return 0;
    int b = Card_FindFile(name);
    if (!b)
        return 0;
    int n = (bytes + 0x7f) >> 7;
    Card_Request(2, b * BLOCK_SECTORS, n, (uint8_t*)buf, NULL);
    return s_result ? 0 : n;
}

/* 8001fd88: si no existe, busca sitio, apunta su entrada y la escribe con los datos (peticion 6) */
int Card_WriteFile(const char* name, const void* buf, int bytes)
{
    int n = (bytes + 0x7f) >> 7;
    int b = Card_FindFile(name);
    if (!b)
    {
        b = Card_AllocBlocks(bytes);
        if (!b)
            return 0;
        Card_WriteDirEntry(name, b);
        s_dirBlock = b;
        Card_Request(6, b * BLOCK_SECTORS, n, NULL, (const uint8_t*)buf);
    }
    else
        Card_Request(3, b * BLOCK_SECTORS, n, NULL, (const uint8_t*)buf);
    return s_result == 0 ? 0x2000 : 0;
}

/* 8001f910: formatea: escribe los sectores 1-0x23 de un directorio vacio (la cabecera se queda como estaba).
 * Devuelve 1 si hay otra peticion en curso. */
int Card_Format(void)
{
    static uint8_t dir[0x24 * FRAME];
    if (s_op)
        return 1;
    memset(dir, 0, sizeof(dir));
    for (int fr = 0; fr < 0x24; fr++)
    {
        uint8_t* f = dir + fr * FRAME;
        if (fr == 0)
        {
            f[0] = 'M';
            f[1] = 'C';
        }
        else
        {
            f[0] = 0xa0;
            f[8] = f[9] = 0xff;
        }
        f[FRAME - 1] = FrameXor(f);
    }
    for (int fr = 0x10; fr < 0x24; fr++)
    {
        uint8_t* f = dir + fr * FRAME;
        f[0] = f[1] = f[2] = f[3] = 0xff;
        f[8] = f[9] = 0xff;
    }
    Card_Request(3, 1, 0x23, NULL, dir + FRAME);
    return 0;
}
