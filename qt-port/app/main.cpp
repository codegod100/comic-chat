// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "app/MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("comic-chat-qt"));
    QApplication::setOrganizationName(QStringLiteral("comic-chat"));

    MainWindow w;
    w.show();
    return app.exec();
}
