/* Capa de ficheros.
 * Original (CD-ROM): File_Open 8001bd68, File_OpenSearchPaths 8001bf28, File_BuildCdPath 8001c58c,
 * File_Seek 8001c014, File_ReadAsync 8001c250 (DMA + callback), File_ReadAll 8001c948,
 * File_Close 8001bcec, File_LoadAlloc 8001ca54; 4 slots en DAT_800afc10 (stride 0x18).
 * Aqui va sobre stdio, leyendo el disco extraido (carpeta raiz configurable). Las lecturas son
 * sincronas: el callback de File_ReadAsync se llama antes de volver. */
#ifndef NR_FILE_H
#define NR_FILE_H

#define FILE_MAX_SLOTS   4

/* Codigos de error del original */
#define FILE_ERR_NOINIT   (-2)
#define FILE_ERR_NOTFOUND (-5)
#define FILE_ERR_BADSLOT  (-9)
#define FILE_ERR_NOSLOT   (-10)

typedef void (*FileReadCallback)(int bytesRead, int error);

void File_SetRoot(const char* dir);
const char* File_GetRoot(void);

int  File_Open(const char* name, const char* prefix);
int  File_OpenSearchPaths(const char* name);
int  File_GetSize(int slot);
int  File_Seek(int slot, int offset);
int  File_ReadAsync(int slot, void* buf, int size, FileReadCallback cb);
int  File_ReadAll(int slot, void* buf, int* outSize);
void File_Close(int slot);

/* Abre, reserva con Pool_Alloc y lee el fichero entero. Devuelve el tamano o un error < 0. */
int File_LoadAlloc(const char* name, void** outBuf, int* outSize);

#endif
