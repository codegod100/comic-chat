// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "app/MainWindow.h"
#include "app/ComicWidget.h"

#include <QLineEdit>
#include <QListWidget>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Comic Chat (Qt port)"));
    resize(900, 640);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    auto *split = new QSplitter(Qt::Vertical, central);
    m_comic = new ComicWidget(split);
    m_log = new QListWidget(split);
    m_log->setMaximumHeight(140);
    m_log->addItem(QStringLiteral(
        "Phase 0: scaffold only — comic engine not ported yet."));
    m_log->addItem(QStringLiteral(
        "Type below and press Enter to update the demo balloon text."));
    split->addWidget(m_comic);
    split->addWidget(m_log);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 1);

    m_say = new QLineEdit(central);
    m_say->setPlaceholderText(QStringLiteral("Say something… (local demo)"));
    connect(m_say, &QLineEdit::returnPressed, this, &MainWindow::onSay);

    layout->addWidget(split, 1);
    layout->addWidget(m_say);
    setCentralWidget(central);

    statusBar()->showMessage(QStringLiteral("Ready — Phase 0 scaffold"));
}

void MainWindow::onSay()
{
    const QString text = m_say->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    m_log->addItem(QStringLiteral("you: %1").arg(text));
    m_log->scrollToBottom();
    m_comic->setDemoMessage(text);
    m_say->clear();
    statusBar()->showMessage(QStringLiteral("Demo balloon updated (no IRC yet)"), 3000);
}
