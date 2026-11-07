# NASCAR Re-Rumble — port nativo de NASCAR Rumble (PsyCross)

Descompilación funcional en C del ejecutable de PS1 (SLUS_010.68) sobre
[PsyCross](https://github.com/OpenDriver2/PsyCross) (capa PsyQ → SDL2 + OpenGL + OpenAL).

**Port no oficial hecho por fans, sin relación con Electronic Arts ni NASCAR.** El repositorio no incluye nada del
juego (ni gráficos, ni sonido, ni el ejecutable original): hace falta la imagen del disco original de PS1
(NTSC-U, SLUS-01068).

## Cómo jugar

1. Descarga el zip de la última versión en [Releases](https://github.com/shicharp/nascar-re-rumble/releases) y
   descomprímelo.
2. Pon la imagen `.bin` de tu disco junto a `nascar.exe`. La primera vez el juego la extrae a la carpeta `disc`.
3. Abre `nascar.exe`. El `LEEME.txt` del zip tiene los detalles.

## Mejoras respecto a la versión de PS1

- **Imagen:** ventana o pantalla completa (Alt derecho + Intro). La imagen se ensancha al
  aspecto de la pantalla (16:9, etc.) mostrando más escenario a los lados, con el marcador anclado a los bordes.
- **Más detalle:** coches, objetos y texturas de la pista siempre con el detalle completo, también de lejos (la PS1
  los simplificaba con la distancia; `NR_PSXLOD=1` vuelve a ese comportamiento).
- **Mandos:** cualquier mando compatible con SDL (Xbox, DualShock, genéricos) o el teclado. Teclas y botones
  configurables para cada jugador en *Game Options > Controller Config*. El stick izquierdo sirve para girar y para
  los menús aunque el mando esté en modo digital.
- **Vibración** con los golpes y el tornado en cualquier mando que vibre (se quita en *Controller Config*).
- **Ritmo estable:** el juego va a 60 Hz exactos en cualquier PC, sin atrasos con la CPU cargada.
- **Si se cierra** de golpe deja un `crash.txt` con el informe del error.
- Siguen los dos jugadores a pantalla partida del original.

## Multijugador en red (nuevo)

El original solo tenía pantalla partida; el port añade partidas en red de **hasta 8 jugadores, uno por PC**.

- **Conexión:** en red local las partidas aparecen solas (*SELECT* en el menú principal → *LAN Game*), o se escribe
  la IP del anfitrión. Por Internet funciona con una VPN para juegos (Radmin VPN, ZeroTier o Tailscale). Usa los
  puertos UDP 37120 y 37121: la primera vez Windows puede pedir permiso en el cortafuegos.
- **Sala:** cada jugador elige piloto y clase de coche, o entra en la pantalla de selección para ver el coche. Con
  cuadrado elige equipo (4 equipos de hasta 2) para las carreras por equipos.
- **Modos:** carrera suelta (normal o por equipos) y campeonato (normal o por equipos, hasta 6 jugadores). En el
  campeonato cada uno conserva en su partida guardada los récords, las copas y lo que desbloquee.
- **Opciones del anfitrión** (solo para los jugadores, no para la IA):
  - *Slipstream*: rebufo, más potencia justo detrás de otro coche.
  - *Catch-up*: ayuda al que va muy por detrás del primer jugador.
  - *Class Engines*: los coches especiales (camiones, leyendas, los de los trucos) llevan el motor normal de su
    clase, para que nadie gane solo por elegirlos.
- **En carrera:** el nombre de cada jugador sobre su coche, en su color. START abre un menú sin parar la carrera
  (para retirarse) y al acabar se pueden mirar los resultados de los demás.
- **Conexiones malas:** cada PC simula la carrera completa y solo viajan las pulsaciones. Si a alguien le va mal la
  conexión, los demás siguen corriendo y su PC se pone al día después. Si alguien se va, la IA lleva su coche.
- Todos tienen que usar el mismo `nascar.exe` (el anfitrión rechaza versiones distintas).

## Controles

Mando compatible con SDL (Xbox, DualShock, etc.) o teclado:

| PlayStation | Teclado | En carrera |
|---|---|---|
| Cruceta | Flechas | Dirección |
| X | C | Acelerar / elegir |
| Cuadrado | X | Frenar |
| Círculo | V | Freno de mano |
| Triángulo | Z | Volver a la pista / atrás |
| L1 / L2 | Mayús izq. / Ctrl izq. | Subir / bajar marcha (manual); en automático, marcador / cámara |
| R1 | Mayús der. | Retrovisor (mantener) |
| R2 | Ctrl der. | Usar poder (mantener) |
| START | Intro | Pausa |
| SELECT | Espacio | Corto: cámara; largo: nivel del marcador |

Para conducir, las teclas y los botones se cambian en **Game Options > Controller Config** (se guardan en
`controls.cfg`). En los menús siguen los de la tabla.

## Compilar

Requisitos: Visual Studio 2022 (MSVC 14.38), CMake 3.31 y Ninja (`pip install --user cmake==3.31.10 ninja`).

```bat
git clone --recursive https://github.com/shicharp/nascar-re-rumble.git
```

Dependencias en `third_party/`:

- PsyCross y ENet 1.3.18: submódulos git (en un clon sin `--recursive`: `git submodule update --init`).
- SDL2 2.32.8: descomprimir `SDL2-devel-2.32.8-VC.zip`
  (https://github.com/libsdl-org/SDL/releases/tag/release-2.32.8) en `third_party/`.
- OpenAL Soft 1.25.2: descomprimir `openal-soft-1.25.2-bin.zip`
  (https://openal-soft.org/openal-binaries/) en `third_party/`.

```bat
build.bat Debug
```

Genera `build\Debug\nascar.exe` (Win32/x86: el código descompilado guarda punteros en 32 bits). Para repartirlo,
`build.bat RelWithDebInfo` (runtime de C estático, sin Visual C++ Redistributable) y junto al exe `nascar.pdb`,
`SDL2.dll`, `OpenAL32.dll` y las licencias de `third_party`. La versión está en `project(... VERSION)` de
`CMakeLists.txt`.

El exe no abre consola: el log va a `NASCAR Rumble.log`, junto al exe; `--console` abre una para verlo mientras se
juega.

## Parches de PsyCross

`third_party/PsyCross` es un submódulo con el PsyCross original; los arreglos propios van en `patches/` y CMake los
aplica al configurar si aún no están:

- `psycross-log-file.patch`: el log se escribe tal cual y se vuelca a disco en cada línea; sin la consola que abría
  PsyCross en Debug.
- `psycross-pad-mapping.patch`: un mapeo de botones por mando y funciones para cambiarlos en el juego.
- `psycross-samplers-8bit.patch`: colores erróneos en el primer sprite de 8 bits detrás de un texto.
- `psycross-vblank-clock.patch`: el reloj de 60 Hz atrasaba, más con la CPU cargada y distinto en cada PC.

## Licencia

El código del port está bajo la licencia MIT (ver `LICENSE`). PsyCross, ENet, SDL2 y OpenAL Soft tienen las suyas
(en `third_party/` y junto al exe en las versiones publicadas). NASCAR Rumble y todo su contenido son de sus
dueños: el port solo lee los datos del disco de cada jugador.
