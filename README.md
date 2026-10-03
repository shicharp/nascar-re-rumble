# NASCAR Rumble — port nativo (PsyCross)

Descompilación funcional en C del ejecutable de PS1 (SLUS_010.68) sobre
[PsyCross](https://github.com/OpenDriver2/PsyCross) (capa PsyQ → SDL2 + OpenGL + OpenAL).

## Compilar

Requisitos: Visual Studio 2022 (MSVC 14.38), CMake 3.31 y Ninja
(`pip install --user cmake==3.31.10 ninja`). Dependencias en `third_party/`:

- PsyCross: submódulo git (`git submodule update --init`).
- SDL2 2.32.8: descomprimir `SDL2-devel-2.32.8-VC.zip`
  (https://github.com/libsdl-org/SDL/releases/tag/release-2.32.8) en `third_party/`.
- OpenAL Soft 1.25.2: descomprimir `openal-soft-1.25.2-bin.zip`
  (https://openal-soft.org/openal-binaries/) en `third_party/`.

Los binarios no se versionan (ver `.gitignore`).

```bat
build.bat Debug
```

Genera `build\Debug\nascar.exe` (Win32/x86: el código descompilado guarda punteros en 32 bits).

## Ejecutar

Necesita el disco extraído (`D:\Proyecto Nascar\disc`, lo genera `tools/extract_iso.py`); lo busca
subiendo carpetas desde el directorio actual, o se indica con `--disc`.

```
nascar.exe [--disc <carpeta>] [--file <fichero de stream>] [--page N] [--shot salida.bmp]
```

Estado actual (primer hito): carga por streaming un fichero de chunks EA (por defecto `CW\FEnd\Fe.trk`),
despacha los assets y muestra las páginas de textura Cpag desde la VRAM emulada.
Flechas izquierda/derecha cambian de página (arriba/abajo de 10 en 10). `--shot` guarda un frame y sale.

## Estructura

| Fichero | Original (SLUS_010.68) |
|---|---|
| `src/game/mem.c` | Mem_Alloc 8001cb84, Mem_Free 8001cc04, Pool_Alloc 80017154, Pool_Free 8001745c |
| `src/game/file.c` | File_* 8001bd68…8001ca54 (CD-ROM → stdio sobre el disco extraído) |
| `src/game/stream.c` | Stream_* 8002a190…8002baa8 (bloques de 0x6000, SHOC/SHDR/SDAT) |
| `src/game/asset.c` | Asset_Dispatch 80024710 |
| `src/game/cpag.c` | Cpag_OnLoaded 8006710c, Cpag_UploadToSlot 80066d00 |
| `src/game/gfx.c` | Gfx_InitBuffers 8001a0b4, Gfx_Flip 8001a24c |
| `src/main.c` | visor provisional (el `main` real está en 800792a8) |

## Notas de PsyCross

- El código del juego se compila como **C++** (como REDRIVER2): con PGXP las coordenadas de las
  primitivas son `half` y en C habría que escribirlas con `_HF()`.
- Las entradas de la OT son `OT_TAG` (8 bytes en x86), no `u_long`.
- El juego cierra cada frame con `PsyX_EndScene()` después de `DrawOTag`; `FntFlush()` va antes.
- `PadRead` no está implementado: el mando se lee con `PadInitDirect` + buffer.
- La VRAM emulada no recibe lo dibujado en OpenGL (`StoreImage` del framebuffer da negro); las
  capturas leen el buffer frontal con `glReadPixels`.
