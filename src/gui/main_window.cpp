#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

#include <DApplication>
#include <DApplicationHelper>
#include <DHiDPIHelper>
#include <DShadowLine>
#include <DStackedWidget>
#include <DTitlebar>
#include <QDebug>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include "constant.h"
#include "main_window.h"
#include "process/system_monitor.h"
#include "process_page_widget.h"
#include "settings.h"
#include "system_service_page_widget.h"
#include "toolbar.h"
#include "utils.h"

std::atomic<MainWindow *> MainWindow::m_instance {nullptr};
std::mutex MainWindow::m_mutex;

MainWindow::MainWindow(DWidget *parent)
    : DMainWindow(parent)
{
    m_settings = Settings::instance();
    if (m_settings)
        m_settings->init();
}

MainWindow::~MainWindow() {}

void MainWindow::displayShortcutHelpDialog()
{
    QRect rect = window()->geometry();
    QPoint pos(rect.x() + rect.width() / 2, rect.y() + rect.height() / 2);

    QJsonObject shortcutObj;
    QJsonArray jsonGroups;

    QJsonObject sysObj;
    sysObj.insert("groupName", DApplication::translate("Help.Shortcut.System", "System"));
    QJsonArray sysObjArr;

    QJsonObject helpItem;
    helpItem.insert("name", DApplication::translate("Help.Shortcut.System", "Help"));
    helpItem.insert("value", "F1");
    sysObjArr.append(helpItem);

    QJsonObject closeItem;
    closeItem.insert("name", DApplication::translate("Help.Shortcut.System", "Close application"));
    closeItem.insert("value", "Alt+F4");
    sysObjArr.append(closeItem);

    QJsonObject shortcutItem;
    shortcutItem.insert("name",
                        DApplication::translate("Help.Shortcut.System", "Show shortcut preview"));
    shortcutItem.insert("value", "Ctrl+Shift+/");
    sysObjArr.append(shortcutItem);

    QJsonObject jsonItem;
    jsonItem.insert("name",
                    DApplication::translate("Help.Shortcut.System", "Maximize / Restore window"));
    jsonItem.insert("value", "Ctrl+Alt+F");
    sysObjArr.append(jsonItem);

    QJsonObject searchItem;
    searchItem.insert("name", DApplication::translate("Title.Bar.Search", "Search"));
    searchItem.insert("value", "Ctrl+F");
    sysObjArr.append(searchItem);

    sysObj.insert("groupItems", sysObjArr);
    jsonGroups.append(sysObj);

    QJsonObject procObj;
    procObj.insert("groupName", DApplication::translate("Title.Bar.Switch", "Process"));
    QJsonArray procObjArr;

    QJsonObject killAppItem;
    killAppItem.insert("name",
                       DApplication::translate("Title.Bar.Context.Menu", "Force end application"));
    killAppItem.insert("value", "Ctrl+Alt+K");
    procObjArr.append(killAppItem);

    QJsonObject endProcItem;
    endProcItem.insert("name",
                       DApplication::translate("Process.Table.Context.Menu", "End process"));
    endProcItem.insert("value", "Alt+E");
    procObjArr.append(endProcItem);
    QJsonObject pauseProcItem;
    pauseProcItem.insert("name",
                         DApplication::translate("Process.Table.Context.Menu", "Pause process"));
    pauseProcItem.insert("value", "Alt+P");
    procObjArr.append(pauseProcItem);
    QJsonObject resumeProcItem;
    resumeProcItem.insert("name",
                          DApplication::translate("Process.Table.Context.Menu", "Resume process"));
    resumeProcItem.insert("value", "Alt+C");
    procObjArr.append(resumeProcItem);
    QJsonObject propItem;
    propItem.insert("name", DApplication::translate("Process.Table.Context.Menu", "Properties"));
    propItem.insert("value", "Alt+Enter");
    procObjArr.append(propItem);
    QJsonObject killProcItem;
    killProcItem.insert("name",
                        DApplication::translate("Process.Table.Context.Menu", "Kill process"));
    killProcItem.insert("value", "Alt+K");
    procObjArr.append(killProcItem);

    procObj.insert("groupItems", procObjArr);
    jsonGroups.append(procObj);

    QJsonObject svcObj;
    svcObj.insert("groupName", DApplication::translate("Title.Bar.Switch", "Service"));
    QJsonArray svcObjArr;

    QJsonObject startSvcItem;
    startSvcItem.insert("name", DApplication::translate("Service.Table.Context.Menu", "Start"));
    startSvcItem.insert("value", "Alt+S");
    svcObjArr.append(startSvcItem);
    QJsonObject stopSvcItem;
    stopSvcItem.insert("name", DApplication::translate("Service.Table.Context.Menu", "Stop"));
    stopSvcItem.insert("value", "Alt+T");
    svcObjArr.append(stopSvcItem);
    QJsonObject restartSvcItem;
    restartSvcItem.insert("name", DApplication::translate("Service.Table.Context.Menu", "Restart"));
    restartSvcItem.insert("value", "Alt+R");
    svcObjArr.append(restartSvcItem);
    QJsonObject refreshSvcItem;
    refreshSvcItem.insert("name", DApplication::translate("Service.Table.Context.Menu", "Refresh"));
    refreshSvcItem.insert("value", "F5");
    svcObjArr.append(refreshSvcItem);

    svcObj.insert("groupItems", svcObjArr);
    jsonGroups.append(svcObj);

    shortcutObj.insert("shortcut", jsonGroups);

    QJsonDocument doc(shortcutObj);

    QProcess *shortcutViewProcess = new QProcess();
    QStringList shortcutString;
    QString param1 = "-j=" + QString(doc.toJson().data());
    QString param2 = "-p=" + QString::number(pos.x()) + "," + QString::number(pos.y());
    shortcutString << param1 << param2;

    shortcutViewProcess->startDetached("deepin-shortcut-viewer", shortcutString);

    connect(shortcutViewProcess, SIGNAL(finished(int)), shortcutViewProcess, SLOT(deleteLater()));
}

void MainWindow::initUI()
{
    m_loading = true;
    Q_EMIT loadingStatusChanged(m_loading);

    // Init window size.
    int width = Constant::WINDOW_MIN_WIDTH;
    int height = Constant::WINDOW_MIN_HEIGHT;

    if (m_settings->getOption(kSettingKeyWindowWidth).isValid()) {
        width = m_settings->getOption(kSettingKeyWindowWidth).toInt();
    }

    if (m_settings->getOption(kSettingKeyWindowHeight).isValid()) {
        height = m_settings->getOption(kSettingKeyWindowHeight).toInt();
    }

    resize(width, height);

    titlebar()->setIcon(QIcon::fromTheme("deepin-system-monitor"));
    m_toolbar = new Toolbar(this, this);
    titlebar()->setCustomWidget(m_toolbar);
    titlebar()->setMenuDisabled(true);

    DMenu *menu = new DMenu();
    titlebar()->setMenu(menu);

    // kill process
    m_killAction = new QAction(
        DApplication::translate("Title.Bar.Context.Menu", "Force end application"), menu);
    m_killAction->setShortcut(QKeySequence(Qt::CTRL + Qt::ALT + Qt::Key_K));
    connect(m_killAction, &QAction::triggered, this, [=]() { Q_EMIT killProcessPerformed(); });

    // display mode
    m_modeMenu = new DMenu(DApplication::translate("Title.Bar.Context.Menu", "View"), menu);
    QActionGroup *modeGroup = new QActionGroup(m_modeMenu);
    modeGroup->setExclusive(true);
    auto *expandModeAction =
        new QAction(DApplication::translate("Title.Bar.Context.Menu", "Expand"), modeGroup);
    expandModeAction->setCheckable(true);
    auto *compactModeAction =
        new QAction(DApplication::translate("Title.Bar.Context.Menu", "Compact"), modeGroup);
    compactModeAction->setCheckable(true);
    m_modeMenu->addAction(expandModeAction);
    m_modeMenu->addAction(compactModeAction);

    QVariant vmode = m_settings->getOption(kSettingKeyDisplayMode);
    if (vmode.isValid()) {
        bool ok = false;
        int mode = vmode.toInt(&ok);
        if (ok) {
            if (mode == kDisplayModeExpand) {
                expandModeAction->setChecked(true);
            } else if (mode == kDisplayModeCompact) {
                compactModeAction->setChecked(true);
            }
        } else {
            expandModeAction->setChecked(true);
            m_settings->setOption(kSettingKeyDisplayMode, kDisplayModeExpand);
            Q_EMIT displayModeChanged(kDisplayModeExpand);
        }
    }

    connect(expandModeAction, &QAction::triggered, this, [=]() {
        m_settings->setOption(kSettingKeyDisplayMode, kDisplayModeExpand);
        Q_EMIT displayModeChanged(kDisplayModeExpand);
    });
    connect(compactModeAction, &QAction::triggered, this, [=]() {
        m_settings->setOption(kSettingKeyDisplayMode, kDisplayModeCompact);
        Q_EMIT displayModeChanged(kDisplayModeCompact);
    });

    menu->addAction(m_killAction);
    menu->addMenu(m_modeMenu);

    DApplicationHelper *dAppHelper = DApplicationHelper::instance();
    connect(dAppHelper, &DApplicationHelper::themeTypeChanged, this,
            [=]() { m_settings->setOption(kSettingKeyThemeType, DApplicationHelper::LightType); });

    m_pages = new DStackedWidget(this);
    setCentralWidget(m_pages);
    setContentsMargins(0, 0, 0, 0);

    m_tbShadow = new DShadowLine(m_pages);
    m_tbShadow->setFixedWidth(m_pages->width());
    m_tbShadow->setFixedHeight(10);
    m_tbShadow->move(0, 0);
    m_tbShadow->show();

    m_spinner = new DSpinner(m_pages);
    auto pa = DApplicationHelper::instance()->applicationPalette();
    QBrush hlBrush = pa.color(DPalette::Active, DPalette::Highlight);
    pa.setColor(DPalette::Highlight, hlBrush.color());
    m_spinner->setPalette(pa);
    m_spinner->move(m_pages->rect().center() - m_spinner->rect().center());
    m_spinner->start();
    m_spinner->show();

    auto *mon = SystemMonitor::instance();
    connect(mon, &SystemMonitor::initialSysInfoLoaded, this, [=]() {
        if (m_loading) {
            m_spinner->hide();
            m_spinner->stop();

            m_procPage = new ProcessPageWidget(m_pages);
            m_svcPage = new SystemServicePageWidget(m_pages);
            m_pages->setContentsMargins(0, 0, 0, 0);
            m_pages->addWidget(m_procPage);
            m_pages->addWidget(m_svcPage);

            m_tbShadow->raise();

            m_loading = false;
            Q_EMIT loadingStatusChanged(m_loading);
        }
    });

    installEventFilter(this);
}

void MainWindow::initConnections()
{
    connect(m_toolbar, &Toolbar::procTabButtonClicked, this, [=]() {
        m_pages->setCurrentIndex(m_pages->indexOf(m_procPage));
#ifdef ROLLBACK_BUG_4299
        m_modeMenu->setEnabled(true);
        m_killAction->setEnabled(true);
#endif
        m_tbShadow->raise();
        m_tbShadow->show();
    });
    connect(m_toolbar, &Toolbar::serviceTabButtonClicked, this, [=]() {
        m_pages->setCurrentIndex(m_pages->indexOf(m_svcPage));
#ifdef ROLLBACK_BUG_4299
        m_modeMenu->setEnabled(false);
        m_killAction->setEnabled(false);
#endif
        m_tbShadow->raise();
        m_tbShadow->show();
    });
    connect(m_pages, &DStackedWidget::currentChanged, this,
            [=]() { m_toolbar->clearSearchText(); });

    connect(this, &MainWindow::loadingStatusChanged, [=](bool loading) {
        if (loading) {
            titlebar()->setMenuDisabled(true);
        } else {
            titlebar()->setMenuDisabled(false);
        }
    });
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    DMainWindow::resizeEvent(event);

    if (m_tbShadow)
        m_tbShadow->setFixedWidth(event->size().width());
    if (m_spinner)
        m_spinner->move(m_pages->rect().center() - m_spinner->rect().center());

    m_settings->setOption(kSettingKeyWindowWidth, event->size().width());
    m_settings->setOption(kSettingKeyWindowHeight, event->size().height());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_settings->setOption(kSettingKeyWindowWidth, width());
    m_settings->setOption(kSettingKeyWindowHeight, height());
    m_settings->flush();

    DMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *kev = dynamic_cast<QKeyEvent *>(event);
        if (kev->matches(QKeySequence::Quit)) {
            DApplication::quit();
            return true;
        } else if (kev->matches(QKeySequence::Find)) {
            toolbar()->focusInput();
            return true;
        } else if ((kev->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) &&
                   kev->key() == Qt::Key_F) {
            if (windowState() & Qt::WindowMaximized) {
                showNormal();
            } else {
                showMaximized();
            }
            return true;
        } else if ((kev->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) &&
                   kev->key() == Qt::Key_K) {
            Q_EMIT killProcessPerformed();
            return true;
        } else if ((kev->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) &&
                   kev->key() == Qt::Key_Question) {
            displayShortcutHelpDialog();
            return true;
        } else {
            return false;
        }
    } else {
        return DMainWindow::eventFilter(obj, event);
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    DMainWindow::showEvent(event);

    auto getCpuModel = [=]() -> char * {
        char *buf = nullptr;
        size_t len = 0;
        ssize_t nread = 0;
        FILE *fp = fopen("/proc/cpuinfo", "r");
        if (!fp)
            return nullptr;

        char vid[] = "vendor_id";
        while ((nread = getline(&buf, &len, fp)) != -1) {
            if (strncasecmp(buf, vid, strlen(vid)) == 0) {
                return buf;
            }
        }
        return nullptr;
    };

    if (m_loading) {
        auto *mon = SystemMonitor::instance();
        char *buf = getCpuModel();
        QString cpu(buf);
        if (cpu.contains("AMD") || cpu.contains("Intel")) {
            mon->updateStatus();
        } else {
            m_timer = new QTimer(this);
            m_timer->setSingleShot(true);
            connect(m_timer, &QTimer::timeout, this, [=]() { mon->updateStatus(); });
            m_timer->start(100);
        }
        free(buf);
    }
}
