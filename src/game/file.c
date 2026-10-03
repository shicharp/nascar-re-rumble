#include <stdio.h>
#include <string.h>
#include "nr.h"
#include "mem.h"
#include "file.h"

typedef struct
{
    FILE* fp;
    int   size;
    int   pos;
} FileSlot;

static FileSlot s_slots[FILE_MAX_SLOTS];
static char     s_root[512] = "disc";

void File_SetRoot(const char* dir)
{
    strncpy(s_root, dir, sizeof(s_root) - 1);
}

const char* File_GetRoot(void)
{
    return s_root;
}

/* Equivalente a File_BuildCdPath 8001c58c: prefijo + nombre, separadores normalizados, sin ";1".
 * En Windows el sistema de ficheros no distingue mayusculas, igual que el ISO9660 del disco. */
static void BuildPath(char* out, size_t outSize, const char* prefix, const char* name)
{
    snprintf(out, outSize, "%s/%s%s", s_root, prefix ? prefix : "", name);
    for (char* c = out; *c; c++)
    {
        if (*c == '\\')
            *c = '/';
        if (*c == ';')
        {
            *c = 0;
            break;
        }
    }
}

int File_Open(const char* name, const char* prefix)
{
    int slot;
    for (slot = 0; slot < FILE_MAX_SLOTS; slot++)
        if (!s_slots[slot].fp)
            break;
    if (slot == FILE_MAX_SLOTS)
        return FILE_ERR_NOSLOT;

    char path[1024];
    BuildPath(path, sizeof(path), prefix, name);
    FILE* fp = fopen(path, "rb");
    if (!fp)
    {
        NR_Log("File_Open: no se encuentra '%s'\n", path);
        return FILE_ERR_NOTFOUND;
    }

    fseek(fp, 0, SEEK_END);
    s_slots[slot].size = (int)ftell(fp);
    fseek(fp, 0, SEEK_SET);
    s_slots[slot].fp = fp;
    s_slots[slot].pos = 0;
    return slot;
}

/* 8001bf28: el original prueba una lista de rutas (PTR_PTR_800af104); con el disco extraido basta la raiz */
int File_OpenSearchPaths(const char* name)
{
    return File_Open(name, NULL);
}

static FileSlot* GetSlot(int slot)
{
    if (slot < 0 || slot >= FILE_MAX_SLOTS || !s_slots[slot].fp)
        return NULL;
    return &s_slots[slot];
}

int File_GetSize(int slot)
{
    FileSlot* s = GetSlot(slot);
    return s ? s->size : FILE_ERR_BADSLOT;
}

int File_Seek(int slot, int offset)
{
    FileSlot* s = GetSlot(slot);
    if (!s)
        return FILE_ERR_BADSLOT;
    if (offset > s->size)
        offset = s->size;
    fseek(s->fp, offset, SEEK_SET);
    s->pos = offset;
    return offset;
}

int File_ReadAsync(int slot, void* buf, int size, FileReadCallback cb)
{
    FileSlot* s = GetSlot(slot);
    if (!s)
    {
        if (cb)
            cb(0, FILE_ERR_BADSLOT);
        return FILE_ERR_BADSLOT;
    }
    if (size > s->size - s->pos)
        size = s->size - s->pos;
    int n = (int)fread(buf, 1, (size_t)size, s->fp);
    s->pos += n;
    if (cb)
        cb(n, 0);
    return n;
}

int File_ReadAll(int slot, void* buf, int* outSize)
{
    FileSlot* s = GetSlot(slot);
    if (!s)
        return FILE_ERR_BADSLOT;
    File_Seek(slot, 0);
    int n = File_ReadAsync(slot, buf, s->size, NULL);
    if (outSize)
        *outSize = n;
    File_Close(slot);
    return n;
}

void File_Close(int slot)
{
    FileSlot* s = GetSlot(slot);
    if (!s)
        return;
    fclose(s->fp);
    memset(s, 0, sizeof(*s));
}

int File_LoadAlloc(const char* name, void** outBuf, int* outSize)
{
    *outBuf = NULL;
    if (outSize)
        *outSize = 0;

    int slot = File_OpenSearchPaths(name);
    if (slot < 0)
        return slot;

    void* buf = Pool_Alloc(File_GetSize(slot), 0);
    *outBuf = buf;
    return File_ReadAll(slot, buf, outSize);
}
