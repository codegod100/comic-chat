// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <QWidget>
#include <QString>

// Scrollable surface that will host comic page rendering.
// Phase 0: demo paint via ICanvas. Later: engine panel.Draw().
class ComicWidget : public QWidget {
    Q_OBJECT
public:
    explicit ComicWidget(QWidget *parent = nullptr);

    void setDemoMessage(const QString &msg);
    QString demoMessage() const { return m_demoMessage; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_demoMessage;
};
