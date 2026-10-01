# APath

English | [简体中文](README.zh-CN.md)

[![Build](https://github.com/halfstay/apath/actions/workflows/build.yml/badge.svg)](https://github.com/halfstay/apath/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Windows 7+](https://img.shields.io/badge/Windows-7%2B-0078D6.svg)](#build)
[![C99](https://img.shields.io/badge/C-99-555555.svg)](src/API.md)
[![CMake 3.10+](https://img.shields.io/badge/CMake-3.10%2B-064F8C.svg)](#build)
[![MinGW-w64](https://img.shields.io/badge/Compiler-MinGW--w64-6A5ACD.svg)](#build)

<img src="res/apath.svg" width="80" height="80" alt="APath icon">

APath is a Windows editor for 2D game walkability maps. Paint walkable areas on
a grid, preview a triangulated navigation mesh, and place endpoints to find a
smooth route around obstacles. The navigation core can also be used in other
C99 projects.

- Pixel-based map dimensions, including partial cells along the edges.
- Rectangular fill and erase, undo/redo, zoom and scrolling.
- Triangle-based A* pathfinding with arbitrary-angle funnel smoothing.
- Compact `.amd` files and double-click opening through Windows file association.
- Native Win32 interface in English, with no third-party runtime packages.

## Build

Requires **CMake 3.10+**, **Make** and **MinGW-w64** (GCC and windres).
The application supports **Windows 7 and later**.

```sh
mkdir build
cd build
cmake .. && make
```

The default build type is Release. On Linux, CMake automatically selects an
installed 64-bit MinGW compiler, falling back to 32-bit MinGW. The executable
is `build/apath.exe`.

On Windows, use an MSYS/MinGW shell, or select the MinGW Makefiles generator:

```sh
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
mingw32-make
```

To select 32-bit output explicitly:

```sh
mkdir build32
cd build32
cmake .. -DCMAKE_TOOLCHAIN_FILE=mingw32.cmake
make
```

Use `mingw64.cmake` for 64-bit output.

## Use

Run `apath.exe`, or open a map from the command line:

```text
apath.exe "path to map.amd"
```

### Create and edit a map

File / New asks for cell size, X length and Y height, all in **pixels**, from 1
to 65535. Lengths need not be multiples of the cell size: the rightmost column
and top row are clipped to the requested dimensions. For example, an 805 x 603
pixel map with 20-pixel cells has 41 columns and 31 rows.

Maps can contain up to 16,777,216 cells. The bitmap needs at most 2 MiB;
triangulation and undo history need additional memory.

The origin is at the bottom left, with +X pointing right and +Y pointing up.
Green cells are walkable; white cells are blocked.

| Action | Control |
| --- | --- |
| New / Open / Save / Save As | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S |
| Toggle one cell | Left click |
| Clear one cell | Right click |
| Fill a rectangle / clear a rectangle | Left drag / right drag |
| Cancel a selection | Escape |
| Undo / redo | Ctrl+Z / Ctrl+Y |
| Zoom around the pointer | Mouse wheel |
| Restore the client-filling 1:1 view | Ctrl+H |
| Scroll | Scrollbars; drag near the window edges |
| Switch editing / triangulation | Ctrl+M |
| Set start / end in triangulation | Left click / right click |
| About | About menu / F1 |

Dragging previews a rectangle. Release the matching mouse button to apply it as
one undoable edit. You can shrink or reverse the selection before release;
Escape or losing mouse capture cancels it.

### View and navigation

New and Open resize the window to the map's aspect ratio and start at **1:1**
with the whole map visible and no scrollbars. Maps use their stored pixel size
when it fits the screen; large maps shrink uniformly to the monitor's work area,
and very small maps enlarge to a usable window size. This fitted view is the
1:1 baseline. Complete cells stay square, including while zooming.

Scrollbars appear when you zoom in. Wheel zoom cannot go below 1:1, and Ctrl+H
restores the complete-map view. Manual window resizing or maximizing preserves
the map's proportions; any unused area stays white. Very thin maps may also need
white margins to keep the window usable. Zoom affects only the display; stored
map dimensions and path coordinates stay unchanged.

Ctrl+M builds the navigation mesh from the current map. Grid lines remain visible,
and green lines show the triangles. Dense grids display regularly spaced grid
lines so they remain readable when zoomed out.

In triangulation mode, left click places a **red triangle** for the start; right
click places a **red square** for the end. Both must be in walkable space. A blue
route updates whenever either endpoint changes. Disconnected areas have no route.
Switching modes clears endpoints and routes.

Routes use straight segments at arbitrary angles and are intended for point
agents. They can touch obstacle boundaries and do not include clearance for an
agent's radius. After triangle A* selects a corridor, smoothing uses the full
walkable rectangles along it, so internal triangle edges do not force extra
turns. The result is shortest within that rectangle corridor; it does not
guarantee the globally shortest choice between different routes around obstacles.

### Save and open maps

Maps use the `.amd` extension and the file type name **apath map data**. On startup,
APath registers this extension when no association exists. If it already opens
with the current executable, no prompt appears. An association with another
program requires confirmation. Windows may also require choosing APath in its
Default Apps settings.

Unsaved changes are marked with `*` in the title. New, Open and Close prompt
before discarding them. Saves replace the destination only after the new file
has been written successfully.
