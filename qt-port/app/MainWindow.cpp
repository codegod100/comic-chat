// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "app/MainWindow.h"
#include "app/ComicWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Comic Chat (Qt port)"));
    resize(900, 700);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    auto *split = new QSplitter(Qt::Vertical, central);
    m_comic = new ComicWidget(split);
    m_log = new QListWidget(split);
    m_log->setMaximumHeight(120);
    m_log->addItem(QStringLiteral(
        "Phase 3: each line you type becomes a comic panel (layout + art)."));
    split->addWidget(m_comic);
    split->addWidget(m_log);
    split->setStretchFactor(0, 5);
    split->setStretchFactor(1, 1);

    auto *row = new QHBoxLayout;
    m_say = new QLineEdit(central);
    m_say->setPlaceholderText(QStringLiteral("Say something… then Enter"));
    connect(m_say, &QLineEdit::returnPressed, this, &MainWindow::onSay);

    auto *clearBtn = new QPushButton(QStringLiteral("Clear"), central);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_comic->clearPanels();
        m_log->clear();
        m_log->addItem(QStringLiteral("Panels cleared."));
        statusBar()->showMessage(m_comic->statusLine(), 3000);
    });

    row->addWidget(m_say, 1);
    row->addWidget(clearBtn);

    layout->addWidget(split, 1);
    layout->addLayout(row);
    setCentralWidget(central);

    statusBar()->showMessage(QStringLiteral("Ready — Phase 3 comic layout"));
}

void MainWindow::onSay()
{
    const QString text = m_say->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    m_log->addItem(QStringLiteral("you: %1").arg(text));
    m_log->scrollToBottom();
    m_comic->addChatLine(text);
    m_say->clear();
    statusBar()->showMessage(m_comic->statusLine(), 4000);
}
