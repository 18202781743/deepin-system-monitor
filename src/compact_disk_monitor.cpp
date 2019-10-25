/* -*- Mode: C++; indent-tabs-mode: nil; tab-width: 4 -*-
 * -*- coding: utf-8 -*-
 *
 * Copyright (C) 2011 ~ 2018 Deepin, Inc.
 *               2011 ~ 2018 Wang Yong
 *
 * Author:     Wang Yong <wangyong@deepin.com>
 * Maintainer: Wang Yong <wangyong@deepin.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <DApplicationHelper>
#include <DHiDPIHelper>
#include <DPalette>
#include <QApplication>
#include <QDebug>
#include <QPainter>

#include "compact_disk_monitor.h"
#include "constant.h"
#include "dthememanager.h"
#include "smooth_curve_generator.h"
#include "utils.h"

DWIDGET_USE_NAMESPACE

using namespace Utils;

CompactDiskMonitor::CompactDiskMonitor(QWidget *parent)
    : QWidget(parent)
{
    int statusBarMaxWidth = Utils::getStatusBarMaxWidth();
    setFixedWidth(statusBarMaxWidth);
    setFixedHeight(160);

    pointsNumber = int(statusBarMaxWidth / 5.4);

    readSpeeds = new QList<unsigned long>();
    for (int i = 0; i < pointsNumber; i++) {
        readSpeeds->append(0);
    }

    writeSpeeds = new QList<unsigned long>();
    for (int i = 0; i < pointsNumber; i++) {
        writeSpeeds->append(0);
    }
}

CompactDiskMonitor::~CompactDiskMonitor()
{
    delete readSpeeds;
    delete writeSpeeds;
}

void CompactDiskMonitor::updateStatus(unsigned long tReadKbs, unsigned long tWriteKbs)
{
    totalReadKbs = tReadKbs;
    totalWriteKbs = tWriteKbs;

    // Init read path.
    readSpeeds->append(totalReadKbs);

    if (readSpeeds->size() > pointsNumber) {
        readSpeeds->pop_front();
    }

    QList<QPointF> readPoints;

    double readMaxHeight = 0;
    for (int i = 0; i < readSpeeds->size(); i++) {
        if (readSpeeds->at(i) > readMaxHeight) {
            readMaxHeight = readSpeeds->at(i);
        }
    }

    for (int i = 0; i < readSpeeds->size(); i++) {
        if (readMaxHeight < readRenderMaxHeight) {
            readPoints.append(QPointF(i * 5, readSpeeds->at(i)));
        } else {
            readPoints.append(
                QPointF(i * 5, readSpeeds->at(i) * readRenderMaxHeight / readMaxHeight));
        }
    }

    readPath = SmoothCurveGenerator::generateSmoothCurve(readPoints);

    // Init write path.
    writeSpeeds->append(totalWriteKbs);

    if (writeSpeeds->size() > pointsNumber) {
        writeSpeeds->pop_front();
    }

    QList<QPointF> writePoints;

    double writeMaxHeight = 0;
    for (int i = 0; i < writeSpeeds->size(); i++) {
        if (writeSpeeds->at(i) > writeMaxHeight) {
            writeMaxHeight = writeSpeeds->at(i);
        }
    }

    for (int i = 0; i < writeSpeeds->size(); i++) {
        if (writeMaxHeight < writeRenderMaxHeight) {
            writePoints.append(QPointF(i * 5, writeSpeeds->at(i)));
        } else {
            writePoints.append(
                QPointF(i * 5, writeSpeeds->at(i) * writeRenderMaxHeight / writeMaxHeight));
        }
    }

    writePath = SmoothCurveGenerator::generateSmoothCurve(writePoints);

    repaint();
}

void CompactDiskMonitor::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // init colors
    auto *dAppHelper = DApplicationHelper::instance();
    auto palette = dAppHelper->applicationPalette();
    // TODO: change color when UE ready
    QColor textColor = palette.color(DPalette::Text);
    QColor summaryColor = palette.color(DPalette::Text);
    QColor readColor = "#1094D8";
    QColor writeColor = "#F7B300";

    // Draw background grid.
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen framePen;
    painter.setOpacity(0.1);
    framePen.setColor(QColor(textColor));
    framePen.setWidth(1);
    painter.setPen(framePen);

    int penSize = 1;
    int gridX = rect().x() + penSize;
    int gridY = rect().y() + gridRenderOffsetY + gridPaddingTop;
    int gridWidth = rect().width() - gridPaddingRight - penSize * 2;
    int gridHeight = readRenderMaxHeight + writeRenderMaxHeight + waveformRenderPadding;

    QPainterPath framePath;
    framePath.addRect(QRect(gridX, gridY, gridWidth, gridHeight));
    painter.drawPath(framePath);

    // Draw grid.
    QPen gridPen;
    QVector<qreal> dashes;
    qreal space = 3;
    dashes << 5 << space;
    painter.setOpacity(0.05);
    gridPen.setColor(QColor(textColor));
    gridPen.setWidth(1);
    gridPen.setDashPattern(dashes);
    painter.setPen(gridPen);

    int gridLineX = gridX;
    while (gridLineX < gridX + gridWidth - gridSize) {
        gridLineX += gridSize;
        painter.drawLine(gridLineX, gridY + 1, gridLineX, gridY + gridHeight - 1);
    }
    int gridLineY = gridY;
    while (gridLineY < gridY + gridHeight - gridSize) {
        gridLineY += gridSize;
        painter.drawLine(gridX + 1, gridLineY, gridX + gridWidth - 1, gridLineY);
    }
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw disk summary.
    setFontSize(painter, readRenderSize);
    QFontMetrics fm = painter.fontMetrics();

    QString readTitle = QString("%1 %2").arg(tr("Disk read")).arg(formatBandwidth(totalReadKbs));
    QString writeTitle = QString("%1 %2").arg(tr("Disk write")).arg(formatBandwidth(totalWriteKbs));

    painter.setOpacity(1);
    painter.setPen(QPen(QColor(readColor)));
    painter.setBrush(QBrush(QColor(readColor)));
    painter.drawEllipse(QPointF(rect().x() + pointerRenderPaddingX,
                                rect().y() + readRenderPaddingY + pointerRenderPaddingY),
                        pointerRadius, pointerRadius);

    setFontSize(painter, readRenderSize);
    painter.setPen(QPen(QColor(summaryColor)));
    painter.drawText(QRect(rect().x() + readRenderPaddingX, rect().y() + readRenderPaddingY,
                           fm.width(readTitle), rect().height()),
                     Qt::AlignLeft | Qt::AlignTop, readTitle);

    painter.setPen(QPen(QColor(writeColor)));
    painter.setBrush(QBrush(QColor(writeColor)));
    painter.drawEllipse(QPointF(rect().x() + pointerRenderPaddingX,
                                rect().y() + writeRenderPaddingY + pointerRenderPaddingY),
                        pointerRadius, pointerRadius);

    setFontSize(painter, writeRenderSize);
    painter.setPen(QPen(QColor(summaryColor)));
    painter.drawText(QRect(rect().x() + writeRenderPaddingX, rect().y() + writeRenderPaddingY,
                           fm.width(writeTitle), rect().height()),
                     Qt::AlignLeft | Qt::AlignTop, writeTitle);

    painter.translate((rect().width() - pointsNumber * 5) / 2 - 7,
                      readWaveformsRenderOffsetY + gridPaddingTop);
    painter.scale(1, -1);

    qreal devicePixelRatio = qApp->devicePixelRatio();
    qreal diskCurveWidth = 1.2;
    if (devicePixelRatio > 1) {
        diskCurveWidth = 2;
    }
    painter.setPen(QPen(QColor(readColor), diskCurveWidth));
    painter.setBrush(QBrush());
    painter.drawPath(readPath);

    painter.translate(0, writeWaveformsRenderOffsetY);
    painter.scale(1, -1);

    painter.setPen(QPen(QColor(writeColor), diskCurveWidth));
    painter.setBrush(QBrush());
    painter.drawPath(writePath);
}
