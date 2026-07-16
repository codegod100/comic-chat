# Comic Chat — Qt port (`qt-port/`)

Native **Qt6** re-host of the Microsoft Comic Chat client. The original trees
remain historical / Windows-MFC references; this directory is a clean port that
does **not** use MFC or the Visual Studio toolchain.

## Status

| Phase | Goal | Status |
|-------|------|--------|
| 0 | Scaffold, `ICanvas`, demo window | **done** |
| 1 | Math + spline draw | **done** (vector2d, bbox, pe, traj, arc, spline) |
| 2 | `.avb` + backdrop load | **done** (image, pose, avatario, backdrop) |
| 3 | Panel layout from a fake chat line | planned |
| 4 | Main window shell (local say box) | planned |
| 5 | IRC (`QTcpSocket` / TLS) | planned |

Source engine: primarily [`v1.0-pre-modern/`](../v1.0-pre-modern/).

## Build (Linux)

Needs **CMake ≥ 3.16**, **C++17**, **Qt6 Widgets**.

### Nix (recommended on NixOS / cloudy)

```bash
cd qt-port
nix-shell -p cmake qt6.qtbase qt6.qttools ninja --run '
  cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug
  cmake --build build
'
./build/comic-chat-qt
```

### Distro packages

```bash
# Debian/Ubuntu example
sudo apt install cmake g++ qt6-base-dev

cd qt-port
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/comic-chat-qt
```

Point at art with:

```bash
cmake -B build -DCOMIC_ART_DIR=/path/to/comicart
```

Default is `../v1.0-pre-modern/comicart`.

## Layout

```
qt-port/
  app/           Qt shell (MainWindow, ComicWidget)
  platform/      ICanvas, QtCanvas, portable types
  engine/        (later) MFC-free comic core
  net/           (later) IRC client
```

## Design notes

- Comic layout keeps **TWIPS-like** logical coordinates (`UNITSPERINCH = 1440`).
- Drawing goes through **`ICanvas`**, not `CDC` / GDI.
- Engine files are **copied and adapted** here; the MFC trees are not edited for the port.
