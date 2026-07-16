// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <QMainWindow>

class ComicWidget;
class QLineEdit;
class QListWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onSay();

private:
    ComicWidget *m_comic = nullptr;
    QListWidget *m_log = nullptr;
    QLineEdit *m_say = nullptr;
};
