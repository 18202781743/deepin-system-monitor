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
#include <DPalette>
#include <QCollator>
#include <QDebug>
#include <QLocale>

#include "chinese2pinyin.h"
#include "dthememanager.h"
#include "process_item.h"
#include "utils.h"

DWIDGET_USE_NAMESPACE

using namespace Utils;
using namespace Pinyin;

ProcessItem::ProcessItem(QPixmap processIcon, QString processName, QString dName, double processCpu,
                         long processMemory, int processPid, QString processUser, char processState)
{
    changeTheme(DApplicationHelper::LightType);

    iconPixmap = processIcon;
    name = processName;
    displayName = dName;
    cpu = processCpu;
    pid = processPid;
    memory = processMemory;
    user = processUser;
    state = processState;

    iconSize = 24;

    padding = 14;
    textPadding = 5;

    networkStatus.sentBytes = 0;
    networkStatus.recvBytes = 0;
    networkStatus.sentKbs = 0;
    networkStatus.recvKbs = 0;

    diskStatus.readKbs = 0;
    diskStatus.writeKbs = 0;

    auto *dAppHelper = DApplicationHelper::instance();
    connect(dAppHelper, &DApplicationHelper::themeTypeChanged, this, &ProcessItem::changeTheme);
}

bool ProcessItem::sameAs(DSimpleListItem *item)
{
    return pid == ((static_cast<ProcessItem *>(item)))->pid;
}

void ProcessItem::drawBackground(QRect rect, QPainter *painter, int index, bool isSelect, bool)
{
    // Init draw path.
    QPainterPath path;
    path.addRect(QRectF(rect));

    painter->setOpacity(1);

    // Draw selected effect.
    if (isSelect) {
        painter->fillPath(path, QBrush(selectLineColor));
    }
    // Draw background effect.
    else {
        // Use different opacity with item index.
        if (index % 2 == 0) {
            painter->fillPath(path, QBrush(evenLineColor));
        } else {
            painter->fillPath(path, QBrush(oddLineColor));
        }
    }
}

void ProcessItem::drawForeground(QRect rect, QPainter *painter, int column, int, bool isSelect,
                                 bool)
{
    // NOTE: Use 'SmoothPixmapTransform' for draw HiDPI icon, otherwise pixmap's edge is not smooth.
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Init opacity and font size.
    painter->setOpacity(1);

    // Set font color with selected status.
    if (isSelect) {
        painter->setPen(QPen(selectedTextColor));
    } else {
        painter->setPen(QPen(textColor));
    }

    // Draw icon and process name.
    if (column == 0) {
        setFontSize(*painter, 10);
        painter->drawPixmap(QRect(rect.x() + padding, rect.y() + (rect.height() - iconSize) / 2,
                                  iconSize, iconSize),
                            iconPixmap);

        QString name = displayName;

        switch (state) {
            case 'Z':
                painter->setPen(QPen(QColor("#FF0056")));
                name = QString("(%1) %2").arg(tr("No response")).arg(displayName);
                break;
            case 'T':
                painter->setPen(QPen(QColor("#FFA500")));
                name = QString("(%1) %2").arg(tr("Suspend")).arg(displayName);
                break;
        }

        int renderWidth = rect.width() - iconSize - padding * 3;
        QFont font = painter->font();
        QFontMetrics fm(font);
        QString renderName = fm.elidedText(name, Qt::ElideRight, renderWidth);
        painter->drawText(
            QRect(rect.x() + iconSize + padding * 2, rect.y(), renderWidth, rect.height()),
            Qt::AlignLeft | Qt::AlignVCenter, renderName);

        displayNameComplete = fm.width(name) <= renderWidth;
    }
    // Draw CPU.
    else if (column == 1) {
        setFontSize(*painter, 9);
        painter->drawText(QRect(rect.x(), rect.y(), rect.width() - textPadding, rect.height()),
                          Qt::AlignRight | Qt::AlignVCenter,
                          QString("%1%").arg(QString::number(cpu, 'f', 1)));
    }
    // Draw memory.
    else if (column == 2) {
        setFontSize(*painter, 9);
        painter->drawText(QRect(rect.x(), rect.y(), rect.width() - textPadding, rect.height()),
                          Qt::AlignRight | Qt::AlignVCenter, formatByteCount(memory));
    }
    // Draw write.
    else if (column == 3) {
        if (diskStatus.writeKbs > 0) {
            setFontSize(*painter, 9);
            painter->drawText(QRect(rect.x(), rect.y(), rect.width() - textPadding, rect.height()),
                              Qt::AlignRight | Qt::AlignVCenter,
                              QString("%1/s").arg(formatByteCount(diskStatus.writeKbs)));
        }
    }
    // Draw read.
    else if (column == 4) {
        if (diskStatus.readKbs > 0) {
            setFontSize(*painter, 9);

            painter->drawText(QRect(rect.x(), rect.y(), rect.width() - textPadding, rect.height()),
                              Qt::AlignRight | Qt::AlignVCenter,
                              QString("%1/s").arg(formatByteCount(diskStatus.readKbs)));
        }
    }
    // Draw download.
    else if (column == 5) {
        if (networkStatus.recvKbs > 0) {
            setFontSize(*painter, 9);
            painter->drawText(QRect(rect.x(), rect.y(), rect.width() - textPadding, rect.height()),
                              Qt::AlignRight | Qt::AlignVCenter,
                              formatBandwidth(networkStatus.recvKbs));
        }
    }
    // Draw upload.
    else if (column == 6) {
        if (networkStatus.sentKbs > 0) {
            setFontSize(*painter, 9);
            painter->drawText(QRect(rect.x(), rect.y(), rect.width() - textPadding, rect.height()),
                              Qt::AlignRight | Qt::AlignVCenter,
                              formatBandwidth(networkStatus.sentKbs));
        }
    }
    // Draw pid.
    else if (column == 7) {
        setFontSize(*painter, 9);
        painter->drawText(QRect(rect.x(), rect.y(), rect.width() - padding, rect.height()),
                          Qt::AlignRight | Qt::AlignVCenter, QString("%1").arg(pid));
    }
}

bool ProcessItem::search(const DSimpleListItem *item, QString searchContent)
{
    const ProcessItem *processItem = static_cast<const ProcessItem *>(item);
    QString content = searchContent.toLower();

    QString fullPinyinString = "";
    QString charPinyinString = "";

    if (QLocale::system().name() == "zh_CN") {
        QString displayName = processItem->getDisplayName();
        QStringList pinyinList = Pinyin::splitChineseToPinyin(displayName);
        fullPinyinString = pinyinList.join("");

        for (QString pinyin : pinyinList) {
            charPinyinString += pinyin[0];
        }
    }

    return processItem->getName().toLower().contains(content) ||
           processItem->getDisplayName().toLower().contains(content) ||
           QString::number(processItem->getPid()).contains(content) ||
           processItem->getUser().toLower().contains(content) ||
           fullPinyinString.contains(content) || charPinyinString.contains(content);
}

bool ProcessItem::sortByCPU(const DSimpleListItem *item1, const DSimpleListItem *item2,
                            bool descendingSort)
{
    // Init.
    double cpu1 = (static_cast<const ProcessItem *>(item1))->getCPU();
    double cpu2 = (static_cast<const ProcessItem *>(item2))->getCPU();
    bool sortOrder;

    // Sort item with memory if cpu is same.
    if (cpu1 == cpu2) {
        long memory1 = static_cast<const ProcessItem *>(item1)->getMemory();
        long memory2 = (static_cast<const ProcessItem *>(item2))->getMemory();

        sortOrder = memory1 > memory2;
    }
    // Otherwise sort by cpu.
    else {
        sortOrder = cpu1 > cpu2;
    }

    return descendingSort ? sortOrder : !sortOrder;
}

bool ProcessItem::sortByDiskRead(const DSimpleListItem *item1, const DSimpleListItem *item2,
                                 bool descendingSort)
{
    // Init.
    DiskStatus status1 = (static_cast<const ProcessItem *>(item1))->getDiskStatus();
    DiskStatus status2 = (static_cast<const ProcessItem *>(item2))->getDiskStatus();
    bool sortOrder = status1.readKbs > status2.readKbs;

    return descendingSort ? sortOrder : !sortOrder;
}

bool ProcessItem::sortByDiskWrite(const DSimpleListItem *item1, const DSimpleListItem *item2,
                                  bool descendingSort)
{
    // Init.
    DiskStatus status1 = (static_cast<const ProcessItem *>(item1))->getDiskStatus();
    DiskStatus status2 = (static_cast<const ProcessItem *>(item2))->getDiskStatus();
    bool sortOrder = status1.writeKbs > status2.writeKbs;

    return descendingSort ? sortOrder : !sortOrder;
}

bool ProcessItem::sortByMemory(const DSimpleListItem *item1, const DSimpleListItem *item2,
                               bool descendingSort)
{
    // Init.
    long memory1 = (static_cast<const ProcessItem *>(item1))->getMemory();
    long memory2 = (static_cast<const ProcessItem *>(item2))->getMemory();
    bool sortOrder;

    // Sort item with cpu if memory is same.
    if (memory1 == memory2) {
        double cpu1 = static_cast<const ProcessItem *>(item1)->getCPU();
        double cpu2 = (static_cast<const ProcessItem *>(item2))->getCPU();

        sortOrder = cpu1 > cpu2;
    }
    // Otherwise sort by memory.
    else {
        sortOrder = memory1 > memory2;
    }

    return descendingSort ? sortOrder : !sortOrder;
}

bool ProcessItem::sortByName(const DSimpleListItem *item1, const DSimpleListItem *item2,
                             bool descendingSort)
{
    // Init.
    QString name1 = (static_cast<const ProcessItem *>(item1))->getDisplayName();
    QString name2 = (static_cast<const ProcessItem *>(item2))->getDisplayName();
    bool sortOrder;

    // Sort item with cpu if name is same.
    if (name1 == name2) {
        double cpu1 = static_cast<const ProcessItem *>(item1)->getCPU();
        double cpu2 = (static_cast<const ProcessItem *>(item2))->getCPU();

        sortOrder = cpu1 > cpu2;
    }
    // Otherwise sort by name.
    else {
        QCollator qco(QLocale::system());
        int result = qco.compare(name1, name2);

        sortOrder = result < 0;
    }

    return descendingSort ? sortOrder : !sortOrder;
}

bool ProcessItem::sortByNetworkDownload(const DSimpleListItem *item1, const DSimpleListItem *item2,
                                        bool descendingSort)
{
    // Init.
    NetworkStatus status1 = (static_cast<const ProcessItem *>(item1))->getNetworkStatus();
    NetworkStatus status2 = (static_cast<const ProcessItem *>(item2))->getNetworkStatus();
    bool sortOrder;

    // Sort item with download bytes if download speed is same.
    if (status1.recvKbs == status2.recvKbs) {
        sortOrder = status1.recvBytes > status2.recvBytes;
    }
    // Otherwise sort by download speed.
    else {
        sortOrder = status1.recvKbs > status2.recvKbs;
    }

    return descendingSort ? sortOrder : !sortOrder;
}

bool ProcessItem::sortByNetworkUpload(const DSimpleListItem *item1, const DSimpleListItem *item2,
                                      bool descendingSort)
{
    // Init.
    NetworkStatus status1 = (static_cast<const ProcessItem *>(item1))->getNetworkStatus();
    NetworkStatus status2 = (static_cast<const ProcessItem *>(item2))->getNetworkStatus();
    bool sortOrder;

    // Sort item with upload bytes if upload speed is same.
    if (status1.sentKbs == status2.sentKbs) {
        sortOrder = status1.sentBytes > status2.sentBytes;
    }
    // Otherwise sort by upload speed.
    else {
        sortOrder = status1.sentKbs > status2.sentKbs;
    }

    return descendingSort ? sortOrder : !sortOrder;
}

bool ProcessItem::sortByPid(const DSimpleListItem *item1, const DSimpleListItem *item2,
                            bool descendingSort)
{
    bool sortOrder = (static_cast<const ProcessItem *>(item1))->getPid() >
                     (static_cast<const ProcessItem *>(item2))->getPid();

    return descendingSort ? sortOrder : !sortOrder;
}

DiskStatus ProcessItem::getDiskStatus() const
{
    return diskStatus;
}

NetworkStatus ProcessItem::getNetworkStatus() const
{
    return networkStatus;
}

QString ProcessItem::getDisplayName() const
{
    return displayName;
}

QString ProcessItem::getName() const
{
    return name;
}

QString ProcessItem::getUser() const
{
    return user;
}

double ProcessItem::getCPU() const
{
    return cpu;
}

int ProcessItem::getPid() const
{
    return pid;
}

long ProcessItem::getMemory() const
{
    return memory;
}

bool ProcessItem::isNameDisplayComplete()
{
    return displayNameComplete;
}

void ProcessItem::mergeItemInfo(double childCpu, long childMemory, DiskStatus childDiskStatus,
                                NetworkStatus childNetworkStatus)
{
    cpu += childCpu;
    memory += childMemory;
    diskStatus.readKbs += childDiskStatus.readKbs;
    diskStatus.writeKbs += childDiskStatus.writeKbs;
    networkStatus.sentBytes += childNetworkStatus.sentBytes;
    networkStatus.recvBytes += childNetworkStatus.recvBytes;
    networkStatus.sentKbs += childNetworkStatus.sentKbs;
    networkStatus.recvKbs += childNetworkStatus.recvKbs;
}

void ProcessItem::setDiskStatus(DiskStatus dStatus)
{
    diskStatus = dStatus;
}

void ProcessItem::setNetworkStatus(NetworkStatus nStatus)
{
    networkStatus = nStatus;
}

void ProcessItem::changeTheme(DApplicationHelper::ColorType themeType)
{
    Q_UNUSED(themeType);
    auto *dAppHelper = DApplicationHelper::instance();
    auto palette = dAppHelper->applicationPalette();
    textColor = palette.color(DPalette::Text);
    evenLineColor = palette.color(DPalette::AlternateBase);
    oddLineColor = palette.color(DPalette::Base);
    selectLineColor = palette.color(DPalette::Highlight);
    selectedTextColor = palette.color(DPalette::HighlightedText);
}
