/*
 * Qddbconv - Standalone Qt6 Audio Converter
 * Copyright (C) 2026 goberghen & Arena.ai Agent
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStringList>
#include "mainwindow.h"

/* Generate a crisp multi-resolution application icon so KDE titlebars and
 * taskbars always display a dedicated Qddbconv icon instead of a generic X11 stub */
static QIcon createAppIcon()
{
    QIcon icon;
    const QList<int> sizes = {16, 22, 32, 48, 64, 128};

    for (int sz : sizes) {
        QPixmap px(sz, sz);
        px.fill(Qt::transparent);

        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing, true);

        const qreal s = sz;
        /* Rounded dark-teal/indigo badge background */
        QLinearGradient bgGrad(0, 0, s, s);
        bgGrad.setColorAt(0.0, QColor(0x1E, 0x88, 0xE5));
        bgGrad.setColorAt(1.0, QColor(0x00, 0x89, 0x7B));

        p.setPen(Qt::NoPen);
        p.setBrush(bgGrad);
        qreal margin = s * 0.06;
        qreal radius = s * 0.22;
        p.drawRoundedRect(QRectF(margin, margin, s - 2 * margin, s - 2 * margin), radius, radius);

        /* Draw circular conversion arrows around the outer ring */
        QPen ringPen(QColor(255, 255, 255, 230), qMax(1.4, s * 0.075), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(ringPen);
        p.setBrush(Qt::NoBrush);
        QRectF arcRect(s * 0.20, s * 0.20, s * 0.60, s * 0.60);
        p.drawArc(arcRect, 35 * 16, 125 * 16);
        p.drawArc(arcRect, 215 * 16, 125 * 16);

        /* Draw central audio waveform bars */
        QPen wavePen(QColor(0xFF, 0xD5, 0x4F), qMax(1.5, s * 0.085), Qt::SolidLine, Qt::RoundCap);
        p.setPen(wavePen);
        p.drawLine(QPointF(s * 0.36, s * 0.42), QPointF(s * 0.36, s * 0.58));
        p.drawLine(QPointF(s * 0.50, s * 0.31), QPointF(s * 0.50, s * 0.69));
        p.drawLine(QPointF(s * 0.64, s * 0.40), QPointF(s * 0.64, s * 0.60));

        p.end();
        icon.addPixmap(px);
    }
    return icon;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("qddbconv"));
    app.setApplicationVersion(QStringLiteral("1.2.1"));

    QIcon appIcon = createAppIcon();
    app.setWindowIcon(appIcon);

    QStringList args = app.arguments();
    args.removeFirst(); /* Remove program executable path */

    MainWindow win(args);
    win.setWindowIcon(appIcon);
    win.show();

    return app.exec();
}
