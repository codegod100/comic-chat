# AGENTS.md

## Cursor Cloud specific instructions

This repository is primarily a **historical archive** of Microsoft Comic Chat source snapshots
(1996–1998). Almost everything under `v1.0-pre/`, `v1.0/`, `v2.1b/`, `v2.5-beta-1/`, the
`*-modern/` folders, and `artifacts/` is **Win32 / MFC** code that only builds on Windows with
Visual C++ / NMAKE (see the root `README.md`). None of that is buildable or runnable on the Linux
cloud VM.

The **only** component that builds and runs on Linux is the experimental **Qt6 port** in
[`qt-port/`](qt-port/) (`comic-chat-qt`). That is the service the cloud environment targets.

### Building & running the Qt port

Standard commands live in [`qt-port/README.md`](qt-port/README.md). The important non-obvious
caveat on this VM:

- The default `c++`/`cc` compiler alternative is **clang++**, and it fails to link with
  `cannot find -lstdc++` (the `libstdc++.so` dev symlink only lives in gcc's private lib dir).
  You **must** point CMake at g++ explicitly, e.g.:

  ```bash
  cmake -G Ninja -B qt-port/build -S qt-port -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++
  cmake --build qt-port/build
  ```

- Run the GUI on the VM's display: `DISPLAY=:1 ./qt-port/build/comic-chat-qt`.
  For headless smoke checks use `-platform offscreen`.
  Qt wants `XDG_RUNTIME_DIR` set (e.g. `export XDG_RUNTIME_DIR=/tmp/runtime-ubuntu` and
  `mkdir -p` it), otherwise it only prints a harmless warning.

- Art assets default to `../v1.0-pre-modern/comicart` (bundled in the repo). Override with
  `-DCOMIC_ART_DIR=/path/to/comicart` at configure time.

### Offline vs IRC

The app works fully **offline**: type in the "Say something…" box and each message renders as a
generated comic panel — this exercises the core engine and needs no network. IRC connectivity
(top bar: host/port/nick/channel/TLS → Connect) reaches out to external IRC servers
(e.g. `irc.freeq.at:6697`) which may be unreachable from the sandbox; offline paneling is the
reliable way to demonstrate the app.

### Lint / tests

There is no configured linter or automated test suite for `qt-port/`; verification is done by
building and running the app. The `.github/workflows/build-modern.yml` workflow only builds the
Windows `*-modern` clients and does not run on Linux.
