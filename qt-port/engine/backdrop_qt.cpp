// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "engine/backdrop_qt.h"
#include "engine/art_paths.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

std::vector<std::string> ListBackdropNames(const std::string &backdropDir)
{
    std::vector<std::string> names;
    QDir dir(QString::fromStdString(backdropDir));
    if (!dir.exists()) {
        return names;
    }
    const QStringList bmps =
        dir.entryList(QStringList() << QStringLiteral("*.bmp") << QStringLiteral("*.BMP"),
                      QDir::Files, QDir::Name);
    QSet<QString> seen;
    for (const QString &f : bmps) {
        const QString base = QFileInfo(f).completeBaseName();
        if (base.isEmpty()) {
            continue;
        }
        const QString key = base.toLower();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        names.push_back(base.toStdString());
    }
    std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) {
        return QString::fromStdString(a).compare(QString::fromStdString(b),
                                                 Qt::CaseInsensitive) < 0;
    });
    return names;
}

bool LoadBackdropImage(const std::string &backdropDir, const std::string &baseName,
                       ComicImage &out)
{
    if (!baseName.empty()) {
        const std::string path = joinPath(backdropDir, baseName + ".bmp");
        if (out.loadFile(path)) {
            return true;
        }
        // Try case variants
        QDir dir(QString::fromStdString(backdropDir));
        const QStringList bmps =
            dir.entryList(QStringList() << QStringLiteral("*.bmp") << QStringLiteral("*.BMP"),
                          QDir::Files);
        for (const QString &f : bmps) {
            if (QFileInfo(f).completeBaseName().compare(QString::fromStdString(baseName),
                                                        Qt::CaseInsensitive) == 0) {
                return out.loadFile(dir.filePath(f).toStdString());
            }
        }
    }
    // First available
    const auto names = ListBackdropNames(backdropDir);
    if (!names.empty()) {
        return out.loadFile(joinPath(backdropDir, names.front() + ".bmp"));
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
