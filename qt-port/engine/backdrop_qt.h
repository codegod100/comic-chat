// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "engine/image.h"

#include <string>
#include <vector>

// Base names of .bmp files in backdropDir (e.g. "room8bs", "field", "pastoral").
std::vector<std::string> ListBackdropNames(const std::string &backdropDir);

// Load a backdrop BMP by base name (e.g. "room8bs") from the backdrop dir.
bool LoadBackdropImage(const std::string &backdropDir, const std::string &baseName,
                       ComicImage &out);

// Stretch-draw full image into dest rect.
void DrawBackdrop(ICanvas *canvas, const ComicImage &img, const RECT &dest);
