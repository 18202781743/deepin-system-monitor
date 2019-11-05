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

#include <proc/sysinfo.h>
#include <unistd.h>

#include <DApplication>
#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QPainter>
#include <thread>

#include "constant.h"
#include "process_item.h"
#include "process_tree.h"
#include "status_monitor.h"
#include "utils.h"

using namespace Utils;

StatusMonitor::StatusMonitor(int tabIndex)
{
    // Init size.
    setFixedWidth(Utils::getStatusBarMaxWidth());

    // Init attributes.
    findWindowTitle = new FindWindowTitle();
    processReadKbs = new QMap<int, unsigned long>();
    processRecvBytes = new QMap<int, long>();
    processSentBytes = new QMap<int, long>();
    processWriteKbs = new QMap<int, unsigned long>();
    processCpuPercents = new QMap<int, double>();
    wineApplicationDesktopMaps = new QMap<QString, int>();
    wineServerDesktopMaps = new QMap<int, QString>();

    settings = Settings::instance();
    Q_ASSERT(settings != nullptr);
    settings->init();
    isCompactMode = settings->getOption("compact_mode").toBool();

    if (tabIndex == 0) {
        tabName = DApplication::translate("Process.Show.Mode", "Applications");
        filterType = OnlyGUI;
    } else if (tabIndex == 1) {
        tabName = DApplication::translate("Process.Show.Mode", "My processes");
        filterType = OnlyMe;
    } else {
        tabName = DApplication::translate("Process.Show.Mode", "All processes");
        filterType = AllProcess;
    }

    totalCpuTime = 0;
    workCpuTime = 0;
    prevTotalCpuTime = 0;
    prevWorkCpuTime = 0;
    currentUsername = qgetenv("USER");

    prevTotalRecvBytes = 0;
    prevTotalSentBytes = 0;

    updateSeconds = updateDuration / 1000.0;

    // Init widgets.
    layout = new QVBoxLayout(this);

    initCompactMode();

    connect(this, &StatusMonitor::updateMemoryStatus, this, &StatusMonitor::handleMemoryStatus,
            Qt::QueuedConnection);
    connect(this, &StatusMonitor::updateCpuStatus, this, &StatusMonitor::handleCpuStatus,
            Qt::QueuedConnection);
    connect(this, &StatusMonitor::updateNetworkStatus, this, &StatusMonitor::handleNetworkStatus,
            Qt::QueuedConnection);
    connect(this, &StatusMonitor::updateDiskStatus, this, &StatusMonitor::handleDiskStatus,
            Qt::QueuedConnection);

    // Start timer.
    updateStatusTimer = new QTimer(this);
    connect(updateStatusTimer, SIGNAL(timeout()), this, SLOT(updateStatus()));
    updateStatusTimer->start(updateDuration);
}

StatusMonitor::~StatusMonitor()
{
    delete wineApplicationDesktopMaps;
    delete wineServerDesktopMaps;
    delete findWindowTitle;
    delete processRecvBytes;
    delete processSentBytes;
    delete processReadKbs;
    delete processWriteKbs;
    delete processCpuPercents;
}

void StatusMonitor::switchToAllProcess()
{
    filterType = AllProcess;
    tabName = DApplication::translate("Process.Show.Mode", "All processes");

    updateStatus();
}

void StatusMonitor::switchToOnlyGui()
{
    filterType = OnlyGUI;
    tabName = DApplication::translate("Process.Show.Mode", "Applications");

    updateStatus();
}

void StatusMonitor::switchToOnlyMe()
{
    filterType = OnlyMe;
    tabName = DApplication::translate("Process.Show.Mode", "My processes");

    updateStatus();
}

void StatusMonitor::updateStatus()
{
    // Read the list of open processes information.
    PROCTAB *proc =
        openproc(PROC_FILLMEM |   // memory status: read information from /proc/#pid/statm
                 PROC_FILLSTAT |  // cpu status: read information from /proc/#pid/stat
                 PROC_FILLUSR     // user status: resolve user ids to names via /etc/passwd
        );
    static proc_t proc_info;
    memset(&proc_info, 0, sizeof(proc_t));

    StoredProcType processes;
    while (readproc(proc, &proc_info) != NULL) {
        processes[proc_info.tid] = proc_info;
    }
    closeproc(proc);

    // Fill in CPU.
    prevWorkCpuTime = workCpuTime;
    prevTotalCpuTime = totalCpuTime;
    totalCpuTime = getTotalCpuTime(workCpuTime);

    processCpuPercents->clear();
    if (prevProcesses.size() > 0) {
        // we have previous proc info
        for (auto &newItr : processes) {
            for (auto &prevItr : prevProcesses) {
                if (newItr.first == prevItr.first) {
                    // PID matches, calculate the cpu
                    (*processCpuPercents)[newItr.second.tid] = calculateCPUPercentage(
                        &prevItr.second, &newItr.second, prevTotalCpuTime, totalCpuTime);

                    break;
                }
            }
        }
    }

    // Read tray icon process.
    QList<int> trayProcessXids = Utils::getTrayWindows();
    QMap<int, int> trayProcessMap;

    for (u_int32_t xid : trayProcessXids) {
        trayProcessMap[findWindowTitle->getWindowPid(xid)] = xid;
    }

    // Fill gui chlid process information when filterType is OnlyGUI.
    findWindowTitle->updateWindowInfos();
    ProcessTree *processTree = new ProcessTree();
    processTree->scanProcesses(processes);
    QMap<int, ChildPidInfo> childInfoMap;
    if (filterType == OnlyGUI) {
        QList<int> guiPids = findWindowTitle->getWindowPids();

        // Tray pid also need add in gui pids list.
        for (int pid : trayProcessMap.keys()) {
            if (!guiPids.contains(pid)) {
                guiPids << pid;
            }
        }

        for (int guiPid : guiPids) {
            QList<int> childPids;
            childPids = processTree->getAllChildPids(guiPid);

            for (int childPid : childPids) {
                DiskStatus dStatus = {0, 0};
                NetworkStatus nStatus = {0, 0, 0, 0};
                ChildPidInfo childPidInfo;

                childPidInfo.cpu = 0;
                childPidInfo.memory = 0;
                childPidInfo.diskStatus = dStatus;
                childPidInfo.networkStatus = nStatus;

                childInfoMap[childPid] = childPidInfo;
            }
        }
    }

    // Read processes information.
    int guiProcessNumber = 0;
    int systemProcessNumber = 0;
    QList<DSimpleListItem *> items;

    wineApplicationDesktopMaps->clear();
    wineServerDesktopMaps->clear();

    unsigned long diskReadTotalKbs = 0;
    unsigned long diskWriteTotalKbs = 0;

    for (auto &i : processes) {
        int pid = (&i.second)->tid;
        QString cmdline = Utils::getProcessCmdline(pid);
        bool isWineProcess = cmdline.startsWith("c:\\");
        QString name = getProcessName(&i.second, cmdline);
        QString user = (&i.second)->euser;
        double cpu = (*processCpuPercents)[pid];

        std::string desktopFile = getProcessDesktopFile(pid, name, cmdline, trayProcessMap);
        QString title = findWindowTitle->getWindowTitle(pid);
        bool isGui = trayProcessMap.contains(pid) || (title != "");

        // Record wine application and wineserver.real desktop file.
        // We need transfer wineserver.real network traffic to the corresponding wine program.
        if (name == "wineserver.real") {
            // Insert pid<->desktopFile to map to search in all network process list.
            QString gioDesktopFile =
                Utils::getProcessEnvironmentVariable(pid, "GIO_LAUNCHED_DESKTOP_FILE");
            if (gioDesktopFile != "") {
                (*wineServerDesktopMaps)[pid] = gioDesktopFile;
            }
        } else {
            // Insert desktopFile<->pid to map to search in all network process list.
            // If title is empty, it's just a wine program, but not wine GUI window.
            if (isWineProcess && title != "") {
                (*wineApplicationDesktopMaps)[QString::fromStdString(desktopFile)] = pid;
            }
        }

        if (isGui) {
            guiProcessNumber++;
        } else {
            systemProcessNumber++;
        }

        bool appendItem = false;
        if (filterType == OnlyGUI) {
            appendItem = (user == currentUsername && isGui);
        } else if (filterType == OnlyMe) {
            appendItem = (user == currentUsername);
        } else if (filterType == AllProcess) {
            appendItem = true;
        }

        if (appendItem) {
            if (title == "") {
                if (isWineProcess) {
                    // If wine process's window title is blank, it's not GUI window process.
                    // Title use process name instead.
                    title = name;
                } else {
                    title = getDisplayNameFromName(name, desktopFile);
                }
            }

            // Add tray prefix in title if process is tray process.
            if (trayProcessMap.contains(pid)) {
                title = QString("%1: %2")
                            .arg(DApplication::translate("Process.Table", "Tray"))
                            .arg(title);
            }

            QString displayName;
            if (filterType == AllProcess) {
                displayName = QString("[%1] %2").arg(user).arg(title);
            } else {
                displayName = title;
            }

            long memory = getProcessMemory(cmdline, (&i.second)->resident, (&i.second)->share);
            QPixmap icon = getProcessIcon(pid, desktopFile, findWindowTitle, 24);
            ProcessItem *item = new ProcessItem(icon, name, displayName, cpu, memory, pid, user,
                                                (&i.second)->state);
            items << item;
        } else {
            // Fill GUI processes information for continue merge action.
            if (filterType == OnlyGUI) {
                if (childInfoMap.contains(pid)) {
                    long memory =
                        getProcessMemory(cmdline, (&i.second)->resident, (&i.second)->share);

                    childInfoMap[pid].cpu = cpu;
                    childInfoMap[pid].memory = memory;
                }
            }
        }

        // Calculate disk IO kbs.
        DiskStatus diskStatus = getProcessDiskStatus(pid);
        diskReadTotalKbs += diskStatus.readKbs;
        diskWriteTotalKbs += diskStatus.writeKbs;
    }

    // Remove dead process from network status maps.
    for (auto pid : processSentBytes->keys()) {
        bool foundProcess = false;
        for (auto &i : processes) {
            if ((&i.second)->tid == pid) {
                foundProcess = true;
                break;
            }
        }

        if (!foundProcess) {
            processSentBytes->remove(pid);
        }
    }
    for (auto pid : processRecvBytes->keys()) {
        bool foundProcess = false;
        for (auto &i : processes) {
            if ((&i.second)->tid == pid) {
                foundProcess = true;
                break;
            }
        }

        if (!foundProcess) {
            processRecvBytes->remove(pid);
        }
    }

    // Read memory information.
    meminfo();

    // Update memory status.
    if (kb_swap_total > 0.0) {
        updateMemoryStatus((kb_main_total - kb_main_available) * 1024, kb_main_total * 1024,
                           kb_swap_used * 1024, kb_swap_total * 1024);
    } else {
        updateMemoryStatus((kb_main_total - kb_main_available) * 1024, kb_main_total * 1024, 0, 0);
    }

    // Update process's network status.
    NetworkTrafficFilter::Update update;

    QMap<int, NetworkStatus> networkStatusSnapshot;

    while (NetworkTrafficFilter::getRowUpdate(update)) {
        if (update.action != NETHOGS_APP_ACTION_REMOVE) {
            (*processSentBytes)[update.record.pid] = update.record.sent_bytes;
            (*processRecvBytes)[update.record.pid] = update.record.recv_bytes;

            NetworkStatus status = {update.record.sent_bytes, update.record.recv_bytes,
                                    update.record.sent_kbs, update.record.recv_kbs};

            (networkStatusSnapshot)[update.record.pid] = status;
        }
    }

    // Transfer wineserver.real network traffic to the corresponding wine program.
    QMap<int, NetworkStatus>::iterator i;
    for (i = networkStatusSnapshot.begin(); i != networkStatusSnapshot.end(); ++i) {
        if (wineServerDesktopMaps->contains(i.key())) {
            QString wineDesktopFile = (*wineServerDesktopMaps)[i.key()];

            if (wineApplicationDesktopMaps->contains(wineDesktopFile)) {
                // Transfer wineserver.real network traffic to the corresponding wine program.
                int wineApplicationPid = (*wineApplicationDesktopMaps)[wineDesktopFile];
                networkStatusSnapshot[wineApplicationPid] = networkStatusSnapshot[i.key()];

                // Reset wineserver network status to zero.
                NetworkStatus networkStatus = {0, 0, 0, 0};
                networkStatusSnapshot[i.key()] = networkStatus;
            }
        }
    }

    // Update ProcessItem's network status.
    for (DSimpleListItem *item : items) {
        ProcessItem *processItem = static_cast<ProcessItem *>(item);
        if (networkStatusSnapshot.contains(processItem->getPid())) {
            processItem->setNetworkStatus(networkStatusSnapshot.value(processItem->getPid()));
        }

        processItem->setDiskStatus(getProcessDiskStatus(processItem->getPid()));
    }

    for (int childPid : childInfoMap.keys()) {
        // Update network status.
        if (networkStatusSnapshot.contains(childPid)) {
            childInfoMap[childPid].networkStatus = networkStatusSnapshot.value(childPid);
        }

        // Update disk status.
        childInfoMap[childPid].diskStatus = getProcessDiskStatus(childPid);
    }

    // Update cpu status.
    std::vector<CpuStruct> cpuTimes = getCpuTimes();
    if (prevWorkCpuTime != 0 && prevTotalCpuTime != 0) {
        std::vector<double> cpuPercentages = calculateCpuPercentages(cpuTimes, prevCpuTimes);

        updateCpuStatus((workCpuTime - prevWorkCpuTime) * 100.0 / (totalCpuTime - prevTotalCpuTime),
                        cpuPercentages);
    } else {
        std::vector<double> cpuPercentages;

        int numCPU = sysconf(_SC_NPROCESSORS_ONLN);
        for (int i = 0; i < numCPU; i++) {
            cpuPercentages.push_back(0);
        }

        updateCpuStatus(0, cpuPercentages);
    }
    prevCpuTimes = cpuTimes;

    // Merge child process when filterType is OnlyGUI.
    if (filterType == OnlyGUI) {
        for (DSimpleListItem *item : items) {
            ProcessItem *processItem = static_cast<ProcessItem *>(item);
            QList<int> childPids;
            childPids = processTree->getAllChildPids(processItem->getPid());

            for (int childPid : childPids) {
                if (childInfoMap.contains(childPid)) {
                    ChildPidInfo info = childInfoMap[childPid];

                    processItem->mergeItemInfo(info.cpu, info.memory, info.diskStatus,
                                               info.networkStatus);
                } else {
                    qDebug() << QString("IMPOSSIBLE: process %1 not exist in childInfoMap")
                                    .arg(childPid);
                }
            }
        }
    }
    delete processTree;

    // Update process status.
    updateProcessStatus(items);

    // Update network status.
    if (prevTotalRecvBytes == 0) {
        prevTotalRecvBytes = totalRecvBytes;
        prevTotalSentBytes = totalSentBytes;

        Utils::getNetworkBandWidth(totalRecvBytes, totalSentBytes);
        updateNetworkStatus(totalRecvBytes, totalSentBytes, 0, 0);
    } else {
        prevTotalRecvBytes = totalRecvBytes;
        prevTotalSentBytes = totalSentBytes;

        Utils::getNetworkBandWidth(totalRecvBytes, totalSentBytes);
        updateNetworkStatus(totalRecvBytes, totalSentBytes,
                            ((totalRecvBytes - prevTotalRecvBytes) / 1024.0) / updateSeconds,
                            ((totalSentBytes - prevTotalSentBytes) / 1024.0) / updateSeconds);
    }

    // Update disk status.
    updateDiskStatus(diskReadTotalKbs / 1000.0, diskWriteTotalKbs / 1000.0);

    // Update process number.
    updateProcessNumber(tabName, guiProcessNumber, systemProcessNumber);

    // Keep processes we've read for cpu calculations next cycle.
    prevProcesses = processes;
}

DiskStatus StatusMonitor::getProcessDiskStatus(int pid)
{
    ProcPidIO pidIO;
    getProcPidIO(pid, pidIO);

    DiskStatus status = {0, 0};

    if (processWriteKbs->contains(pid) && pidIO.wchar > 0) {
        status.writeKbs = (pidIO.wchar - processWriteKbs->value(pid)) / updateSeconds;
    }
    (*processWriteKbs)[pid] = pidIO.wchar;

    if (processReadKbs->contains(pid) && pidIO.rchar > 0) {
        status.readKbs = (pidIO.rchar - processReadKbs->value(pid)) / updateSeconds;
    }
    (*processReadKbs)[pid] = pidIO.rchar;

    return status;
}

void StatusMonitor::showDiskMonitor()
{
    if (!isCompactMode) {
        // Set height to show disk monitor.
        //        diskMonitor->setFixedHeight(190);
    }
}

void StatusMonitor::hideDiskMonitor()
{
    if (!isCompactMode) {
        // Set height to 0 to make disk monitor hide.
        //        diskMonitor->setFixedHeight(0);
    }
}

void StatusMonitor::handleMemoryStatus(long usedMemory, long totalMemory, long usedSwap,
                                       long totalSwap)
{
    if (isCompactMode) {
        compactMemoryMonitor->updateStatus(usedMemory, totalMemory, usedSwap, totalSwap);
    } else {
        memoryMonitor->updateStatus(usedMemory, totalMemory, usedSwap, totalSwap);
    }
}

void StatusMonitor::handleCpuStatus(double cpuPercent, std::vector<double> cpuPercents)
{
    if (isCompactMode) {
        compactCpuMonitor->updateStatus(cpuPercent, cpuPercents);
    } else {
        cpuMonitor->updateStatus(cpuPercent, cpuPercents);
    }
}

void StatusMonitor::handleNetworkStatus(long totalRecvBytes, long totalSentBytes,
                                        float totalRecvKbs, float totalSentKbs)
{
    if (isCompactMode) {
        compactNetworkMonitor->updateStatus(totalRecvBytes, totalSentBytes, totalRecvKbs,
                                            totalSentKbs);
    } else {
        networkMonitor->updateStatus(totalRecvBytes, totalSentBytes, totalRecvKbs, totalSentKbs);
    }
}

void StatusMonitor::handleDiskStatus(float totalReadKbs, float totalWriteKbs)
{
    if (isCompactMode) {
        compactDiskMonitor->updateStatus(totalReadKbs, totalWriteKbs);
    } else {
        //        diskMonitor->updateStatus(totalReadKbs, totalWriteKbs);
    }
}

void StatusMonitor::initCompactMode()
{
    if (isCompactMode) {
        compactCpuMonitor = new CompactCpuMonitor();
        compactMemoryMonitor = new CompactMemoryMonitor();
        compactNetworkMonitor = new CompactNetworkMonitor();
        compactDiskMonitor = new CompactDiskMonitor();

        layout->addWidget(compactCpuMonitor, 0, Qt::AlignHCenter);
        layout->addWidget(compactMemoryMonitor, 0, Qt::AlignHCenter);
        layout->addWidget(compactNetworkMonitor, 0, Qt::AlignHCenter);
        layout->addWidget(compactDiskMonitor, 0, Qt::AlignHCenter);
    } else {
        cpuMonitor = new CpuMonitor();
        memoryMonitor = new MemoryMonitor();
        networkMonitor = new NetworkMonitor();

        layout->addWidget(cpuMonitor, 0, Qt::AlignHCenter);
        layout->addWidget(memoryMonitor, 0, Qt::AlignHCenter);
        layout->addWidget(networkMonitor, 0, Qt::AlignHCenter);
    }
}

void StatusMonitor::enableCompactMode()
{
    if (!isCompactMode) {
        layout->removeWidget(cpuMonitor);
        layout->removeWidget(memoryMonitor);
        layout->removeWidget(networkMonitor);

        cpuMonitor->deleteLater();
        memoryMonitor->deleteLater();
        networkMonitor->deleteLater();

        compactCpuMonitor = new CompactCpuMonitor();
        compactMemoryMonitor = new CompactMemoryMonitor();
        compactNetworkMonitor = new CompactNetworkMonitor();
        compactDiskMonitor = new CompactDiskMonitor();

        layout->addWidget(compactCpuMonitor, 0, Qt::AlignHCenter);
        layout->addWidget(compactMemoryMonitor, 0, Qt::AlignHCenter);
        layout->addWidget(compactNetworkMonitor, 0, Qt::AlignHCenter);
        layout->addWidget(compactDiskMonitor, 0, Qt::AlignHCenter);
    }

    isCompactMode = true;
}

void StatusMonitor::disableCompactMode()
{
    if (isCompactMode) {
        layout->removeWidget(compactCpuMonitor);
        layout->removeWidget(compactMemoryMonitor);
        layout->removeWidget(compactNetworkMonitor);
        layout->removeWidget(compactDiskMonitor);

        compactCpuMonitor->deleteLater();
        compactMemoryMonitor->deleteLater();
        compactNetworkMonitor->deleteLater();
        compactDiskMonitor->deleteLater();

        cpuMonitor = new CpuMonitor();
        memoryMonitor = new MemoryMonitor();
        networkMonitor = new NetworkMonitor();
        //        diskMonitor = new DiskMonitor();

        layout->addWidget(cpuMonitor, 0, Qt::AlignHCenter);
        layout->addWidget(memoryMonitor, 0, Qt::AlignHCenter);
        layout->addWidget(networkMonitor, 0, Qt::AlignHCenter);
        //        layout->addWidget(diskMonitor, 0, Qt::AlignHCenter);
    }

    isCompactMode = false;
}
