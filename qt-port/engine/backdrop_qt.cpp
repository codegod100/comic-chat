// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "engine/backdrop_qt.h"
#include "engine/art_paths.h"

#include <QDir>

bool LoadBackdropImage(const std::string &backdropDir, const std::string &baseName,
                       ComicImage &out)
{
    const std::string path = joinPath(backdropDir, baseName + ".bmp");
    if (out.loadFile(path)) {
        return true;
    }
    // Try case variants
    QDir dir(QString::fromStdString(backdropDir));
    const QStringList bmps = dir.entryList(QStringList() << "*.bmp" << "*.BMP", QDir::Files);
    for (const QString &f : bmps) {
        if (f.startsWith(QString::fromStdString(baseName), Qt::CaseInsensitive)) {
            return out.loadFile(dir.filePath(f).toStdString());
        }
    }
    // First available
    if (!bmps.isEmpty()) {
        return out.loadFile(dir.filePath(bmps.first()).toStdString());
    }
    return false;
}

void DrawBackdrop(ICanvas *canvas, const ComicImage &img, const RECT &dest)
{
    if (!canvas || img.isNull()) {
        canvas->setBrush(CanvasColor::rgb(255, 255, 255));
        canvas->fillRect(dest);
        return;
    }
    img.draw(canvas, dest.left, dest.top, dest.right - dest.left, dest.bottom - dest.top);
}
