// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "platform/BrowserLaunch.h"

#include <QDesktopServices>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace {

bool tryStartDetached(const QString &program, const QStringList &args)
{
    if (program.isEmpty()) {
        return false;
    }
    return QProcess::startDetached(program, args);
}

} // namespace

bool openUrlInBrowser(const QString &urlString)
{
    const QUrl url(urlString);
    if (!url.isValid()) {
        return false;
    }

    const QString encoded = url.toString(QUrl::FullyEncoded);

    struct BrowserAttempt {
        const char *program;
        const char *windowFlag;
        const char *profileFlag;
        const char *profilePath;
    };

    static const BrowserAttempt kAttempts[] = {
        // Dedicated profile so login always gets a visible window (existing Chrome
        // sessions often swallow URLs without opening a tab).
        {"google-chrome", "--new-window", "--user-data-dir=/tmp/comic-chat-chrome",
         nullptr},
        {"google-chrome-stable", "--new-window", "--user-data-dir=/tmp/comic-chat-chrome",
         nullptr},
        {"chromium", "--new-window", "--user-data-dir=/tmp/comic-chat-chrome", nullptr},
        {"chromium-browser", "--new-window", "--user-data-dir=/tmp/comic-chat-chrome",
         nullptr},
        {"google-chrome", "--new-window", nullptr, nullptr},
        {"google-chrome-stable", "--new-window", nullptr, nullptr},
        {"chromium", "--new-window", nullptr, nullptr},
        {"chromium-browser", "--new-window", nullptr, nullptr},
        {"firefox", "-new-window", nullptr, nullptr},
        {"xdg-open", nullptr, nullptr, nullptr},
    };

    for (const BrowserAttempt &attempt : kAttempts) {
        const QString path =
            QStandardPaths::findExecutable(QString::fromUtf8(attempt.program));
        if (path.isEmpty()) {
            continue;
        }
        QStringList args;
        if (attempt.windowFlag) {
            args << QString::fromUtf8(attempt.windowFlag);
        }
        if (attempt.profileFlag && attempt.profilePath) {
            args << QString::fromUtf8(attempt.profileFlag)
                 << QString::fromUtf8(attempt.profilePath);
        }
        args << encoded;
        if (tryStartDetached(path, args)) {
            return true;
        }
    }

    return QDesktopServices::openUrl(url);
}
