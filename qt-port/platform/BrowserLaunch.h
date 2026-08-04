// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <QString>

// Open a URL in the user's browser. Uses explicit browser binaries on Linux
// (Chrome --new-window, xdg-open, …) because QDesktopServices::openUrl often
// fails silently or only hands off to an existing session without a new window.
bool openUrlInBrowser(const QString &url);
