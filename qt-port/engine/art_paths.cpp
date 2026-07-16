// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "engine/art_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#ifndef COMIC_ART_DIR
#define COMIC_ART_DIR ""
#endif

std::string joinPath(const std::string &a, const std::string &b)
{
    if (a.empty()) {
        return b;
    }
    if (a.back() == '/' || a.back() == '\\') {
        return a + b;
    }
    return a + "/" + b;
}

static bool looksLikeArtRoot(const QString &root)
{
    QDir d(root);
    if (!d.exists()) {
        return false;
    }
    // Accept either historical casing
    return d.exists("avatars") || d.exists("Avatars") || d.exists("backdrop") ||
           d.exists("Backdrop");
}

static QString pickSub(const QString &root, const char *a, const char *b)
{
    QDir d(root);
    if (d.exists(a)) {
        return d.filePath(a);
    }
    if (d.exists(b)) {
        return d.filePath(b);
    }
    return d.filePath(a);
}

ArtPaths resolveArtPaths()
{
    ArtPaths p;
    QStringList candidates;

    const QString compiled = QString::fromUtf8(COMIC_ART_DIR);
    if (!compiled.isEmpty()) {
        candidates << compiled;
    }

    // Relative to process cwd
    candidates << QDir::current().absoluteFilePath("../v1.0-pre-modern/comicart");
    candidates << QDir::current().absoluteFilePath("v1.0-pre-modern/comicart");
    candidates << QDir::current().absoluteFilePath("comicart");

    // Relative to executable (build/comic-chat-qt)
    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << QDir(appDir).absoluteFilePath("../../v1.0-pre-modern/comicart");
    candidates << QDir(appDir).absoluteFilePath("../../../v1.0-pre-modern/comicart");
    candidates << QDir(appDir).absoluteFilePath("../comicart");

    for (const QString &c : candidates) {
        const QString abs = QFileInfo(c).absoluteFilePath();
        if (looksLikeArtRoot(abs)) {
            p.root = abs.toStdString();
            p.avatars = pickSub(abs, "avatars", "Avatars").toStdString();
            p.backdrop = pickSub(abs, "backdrop", "Backdrop").toStdString();
            return p;
        }
    }

    // Fallback empty — callers handle missing art.
    p.root = compiled.isEmpty() ? std::string() : compiled.toStdString();
    p.avatars = joinPath(p.root, "avatars");
    p.backdrop = joinPath(p.root, "backdrop");
    return p;
}
