// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <string>

// Resolves ComicArt directories. Prefers COMIC_ART_DIR compile def, then
// ../v1.0-pre-modern/comicart relative to cwd / executable heuristics.
struct ArtPaths {
    std::string root;      // .../comicart
    std::string avatars;   // .../comicart/avatars or Avatars
    std::string backdrop;  // .../comicart/backdrop or Backdrop
};

ArtPaths resolveArtPaths();
std::string joinPath(const std::string &a, const std::string &b);
