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
| 3 | Panel layout from chat lines | **done** (left→right strip, side scroll) |
| 4 | Main window shell (local say box) | **done** |
| 5 | IRC (`QTcpSocket` / TLS) | **done** (`net/IrcClient`) |

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

### IRC

Use the top bar: host, port, nick, channel, **TLS** checkbox, Connect.

- Example: `irc.freeq.at` / `6697` / TLS on / `#comicchat`
- Offline: leave disconnected and type in the say box (local panels only)
- Incoming `PRIVMSG` becomes a new comic panel with the speaker nick


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
