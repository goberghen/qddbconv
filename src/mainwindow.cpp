/*
 * Qddbconv - Standalone Qt6 Audio Converter
 * Copyright (C) 2026 goberghen & Arena.ai Agent
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QProxyStyle>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QThreadPool>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

/* Proxy style that ensures top-bar QToolButtons render with the exact same native
 * pushbutton frame (PE_PanelButtonCommand), height, and padding as standard QPushButtons
 * across Windows Native (windowsvista), Fusion (Breeze Light/Dark), and KDE Breeze. */
class CleanUiProxyStyle : public QProxyStyle {
public:
    explicit CleanUiProxyStyle(QStyle *baseStyle) : QProxyStyle(baseStyle) {}

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget) const override
    {
        if (element == PE_PanelButtonTool && widget && widget->property("qddbToolbarBtn").toBool()) {
            QProxyStyle::drawPrimitive(PE_PanelButtonCommand, option, painter, widget);
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    QSize sizeFromContents(ContentsType type, const QStyleOption *option,
                           const QSize &contentsSize, const QWidget *widget) const override
    {
        QSize sz = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
        if (type == CT_ToolButton && widget && widget->property("qddbToolbarBtn").toBool()) {
            sz.setWidth(sz.width() + 14);
            sz.setHeight(qMax(sz.height() + 4, 28));
        } else if (type == CT_PushButton) {
            sz.setHeight(qMax(sz.height(), 28));
        } else if (type == CT_ComboBox || type == CT_LineEdit || type == CT_SpinBox) {
            sz.setHeight(qMax(sz.height(), 26));
        }
        return sz;
    }
};

/* Render crisp KDE Breeze-style 16x16 / 24x24 / 32x32 vector icons via QPainter
 * when running on platforms without a system XDG icon theme (e.g. Windows / Wine). */
static QIcon makeBreezeFallbackIcon(const QString &themeName, const QColor &fgColor)
{
    QIcon sysIcon = QIcon::fromTheme(themeName);
    if (!sysIcon.isNull())
        return sysIcon;

    const QColor blue(0x3d, 0xae, 0xe9);
    const QColor green(0x27, 0xae, 0x60);
    const QColor red(0xda, 0x44, 0x53);

    QIcon icon;
    for (int sz : {16, 24, 32}) {
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal s = sz / 16.0;
        p.scale(s, s);

        if (themeName == QLatin1String("document-open")) {
            p.setPen(QPen(fgColor, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath doc;
            doc.moveTo(3, 2); doc.lineTo(9.5, 2); doc.lineTo(13, 5.5);
            doc.lineTo(13, 14); doc.lineTo(3, 14); doc.closeSubpath();
            p.drawPath(doc);
            p.drawLine(QPointF(9.2, 2), QPointF(9.2, 5.5));
            p.drawLine(QPointF(9.2, 5.5), QPointF(13, 5.5));
            p.setPen(QPen(green, 1.6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(8, 7.2), QPointF(8, 11.8));
            p.drawLine(QPointF(5.7, 9.5), QPointF(10.3, 9.5));
        } else if (themeName == QLatin1String("folder-open") || themeName == QLatin1String("folder")) {
            QPainterPath fld;
            fld.moveTo(1.5, 3.5); fld.lineTo(6.0, 3.5); fld.lineTo(7.5, 5.2);
            fld.lineTo(14.5, 5.2); fld.lineTo(14.5, 13.0); fld.lineTo(1.5, 13.0);
            fld.closeSubpath();
            p.setBrush(QColor(0x3d, 0xae, 0xe9, 65));
            p.setPen(QPen(fgColor, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(fld);
            p.drawLine(QPointF(1.5, 6.5), QPointF(14.5, 6.5));
        } else if (themeName == QLatin1String("media-optical-audio")) {
            p.setPen(QPen(fgColor, 1.3));
            p.setBrush(QColor(0x3d, 0xae, 0xe9, 35));
            p.drawEllipse(QPointF(8, 8), 6.0, 6.0);
            p.setPen(QPen(blue, 1.4));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(8, 8), 1.9, 1.9);
        } else if (themeName == QLatin1String("media-playback-start")) {
            QPainterPath tri;
            tri.moveTo(4.0, 2.5); tri.lineTo(13.5, 8.0); tri.lineTo(4.0, 13.5);
            tri.closeSubpath();
            p.setPen(QPen(green, 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(green);
            p.drawPath(tri);
        } else if (themeName == QLatin1String("process-stop") || themeName == QLatin1String("application-exit")) {
            p.setPen(QPen(red, 1.4));
            p.setBrush(QColor(0xda, 0x44, 0x53, 35));
            p.drawEllipse(QPointF(8, 8), 6.0, 6.0);
            p.setPen(QPen(red, 1.6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(5.5, 5.5), QPointF(10.5, 10.5));
            p.drawLine(QPointF(10.5, 5.5), QPointF(5.5, 10.5));
        } else if (themeName == QLatin1String("document-properties")) {
            p.setPen(QPen(fgColor, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawRoundedRect(QRectF(2.5, 2.0, 11.0, 12.0), 1.5, 1.5);
            p.setPen(QPen(blue, 1.5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(5.0, 5.5), QPointF(11.0, 5.5));
            p.drawLine(QPointF(5.0, 8.0), QPointF(11.0, 8.0));
            p.drawLine(QPointF(5.0, 10.5), QPointF(9.0, 10.5));
        } else if (themeName == QLatin1String("view-refresh")) {
            p.setPen(QPen(fgColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawArc(QRectF(2.5, 2.5, 11.0, 11.0), 45 * 16, 285 * 16);
            p.drawLine(QPointF(12.2, 2.6), QPointF(12.8, 5.6));
            p.drawLine(QPointF(9.8, 5.2), QPointF(12.8, 5.6));
        } else if (themeName == QLatin1String("edit-select-all")) {
            p.setPen(QPen(fgColor, 1.2, Qt::DashLine));
            p.drawRect(QRectF(2.5, 2.5, 11.0, 11.0));
            p.setPen(QPen(blue, 1.2));
            p.setBrush(QColor(0x3d, 0xae, 0xe9, 100));
            p.drawRect(QRectF(5.0, 5.0, 6.0, 6.0));
        } else if (themeName == QLatin1String("list-remove")) {
            p.setPen(Qt::NoPen);
            p.setBrush(red);
            p.drawRoundedRect(QRectF(3.0, 6.8, 10.0, 2.4), 1.0, 1.0);
        } else if (themeName == QLatin1String("edit-clear-list")) {
            p.setPen(QPen(fgColor, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(QPointF(3.0, 4.2), QPointF(13.0, 4.2));
            p.drawLine(QPointF(6.0, 2.5), QPointF(10.0, 2.5));
            p.drawRoundedRect(QRectF(4.3, 4.2, 7.4, 9.3), 1.0, 1.0);
            p.setPen(QPen(red, 1.3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(6.8, 6.5), QPointF(6.8, 11.2));
            p.drawLine(QPointF(9.2, 6.5), QPointF(9.2, 11.2));
        } else if (themeName == QLatin1String("application-menu") || themeName == QLatin1String("show-menu")) {
            p.setPen(QPen(fgColor, 1.7, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(2.5, 4.0), QPointF(13.5, 4.0));
            p.drawLine(QPointF(2.5, 8.0), QPointF(13.5, 8.0));
            p.drawLine(QPointF(2.5, 12.0), QPointF(13.5, 12.0));
        } else if (themeName == QLatin1String("configure") || themeName == QLatin1String("preferences-system")) {
            p.setPen(QPen(fgColor, 1.3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(2.0, 4.5), QPointF(14.0, 4.5));
            p.drawLine(QPointF(2.0, 8.0), QPointF(14.0, 8.0));
            p.drawLine(QPointF(2.0, 11.5), QPointF(14.0, 11.5));
            p.setBrush(blue);
            p.setPen(QPen(fgColor, 1.1));
            p.drawEllipse(QPointF(5.5, 4.5), 1.8, 1.8);
            p.drawEllipse(QPointF(10.5, 8.0), 1.8, 1.8);
            p.drawEllipse(QPointF(6.5, 11.5), 1.8, 1.8);
        } else {
            p.setPen(QPen(blue, 1.4));
            p.drawEllipse(QPointF(8, 8), 6.0, 6.0);
            p.setPen(QPen(fgColor, 1.6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(8.0, 7.2), QPointF(8.0, 11.2));
            p.drawPoint(QPointF(8.0, 5.0));
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

MainWindow::MainWindow(const QStringList &initialPaths, QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Qddbconv audio converter v%1").arg(QCoreApplication::applicationVersion()));
    resize(1180, 680);
    setAcceptDrops(true);

    connect(this, &MainWindow::trackStatusChanged,
            this, &MainWindow::onTrackStatusUpdated,
            Qt::QueuedConnection);

    m_presets = PresetManager::loadAllPresets();
    setupActionsAndMenus();
    setupUi();
    loadSettings();
    updateControlTooltips();

    if (!initialPaths.isEmpty())
        addInputPaths(initialPaths);
}

MainWindow::~MainWindow()
{
    m_cancelled.store(true);
    QThreadPool::globalInstance()->waitForDone(3000);
}

void MainWindow::setupActionsAndMenus()
{
    QStyle *st = style();

    /* File actions */
    m_actAddFiles = new QAction(QIcon::fromTheme(QStringLiteral("document-open"), st->standardIcon(QStyle::SP_DialogOpenButton)),
                                tr("Add Files..."), this);
    m_actAddFiles->setShortcut(QKeySequence::Open); /* Ctrl+O */
    m_actAddFiles->setToolTip(tr("Add one or more audio files to the conversion queue (Ctrl+O)"));
    connect(m_actAddFiles, &QAction::triggered, this, &MainWindow::onAddFiles);

    m_actAddFolder = new QAction(QIcon::fromTheme(QStringLiteral("folder-open"), st->standardIcon(QStyle::SP_DirOpenIcon)),
                                 tr("Add Folder..."), this);
    m_actAddFolder->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    m_actAddFolder->setToolTip(tr("Recursively scan and add all audio files from a folder (Ctrl+Shift+O)"));
    connect(m_actAddFolder, &QAction::triggered, this, &MainWindow::onAddFolder);

    m_actAddCue = new QAction(QIcon::fromTheme(QStringLiteral("media-optical-audio"), st->standardIcon(QStyle::SP_DriveCDIcon)),
                              tr("Add CUE..."), this);
    m_actAddCue->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_U));
    m_actAddCue->setToolTip(tr("Add a .cue sheet and split its album image into individual tracks (Ctrl+U)"));
    connect(m_actAddCue, &QAction::triggered, this, &MainWindow::onAddCue);

    m_actStartConvert = new QAction(QIcon::fromTheme(QStringLiteral("media-playback-start"), st->standardIcon(QStyle::SP_MediaPlay)),
                                    tr("Start Conversion"), this);
    m_actStartConvert->setShortcuts({QKeySequence(Qt::Key_F5), QKeySequence(Qt::CTRL | Qt::Key_R)});
    m_actStartConvert->setToolTip(tr("Start converting all tracks in the queue (F5 / Ctrl+R)"));
    connect(m_actStartConvert, &QAction::triggered, this, &MainWindow::onStartConversion);

    m_actCancelConvert = new QAction(QIcon::fromTheme(QStringLiteral("process-stop"), st->standardIcon(QStyle::SP_BrowserStop)),
                                     tr("Cancel Conversion"), this);
    m_actCancelConvert->setShortcut(QKeySequence(Qt::Key_Escape));
    m_actCancelConvert->setEnabled(false);
    connect(m_actCancelConvert, &QAction::triggered, this, &MainWindow::onCancelConversion);

    m_actQuit = new QAction(QIcon::fromTheme(QStringLiteral("application-exit"), st->standardIcon(QStyle::SP_DialogCloseButton)),
                            tr("Quit"), this);
    m_actQuit->setShortcut(QKeySequence::Quit); /* Ctrl+Q */
    m_actQuit->setToolTip(tr("Quit application (Ctrl+Q)"));
    connect(m_actQuit, &QAction::triggered, this, &QWidget::close);

    /* Edit & Track Metadata actions (inspired by deadbeef-qt5 MetadataDialog & PlayList) */
    m_actEditMetadata = new QAction(QIcon::fromTheme(QStringLiteral("document-properties"), st->standardIcon(QStyle::SP_FileDialogDetailedView)),
                                    tr("Track Metadata / Properties..."), this);
    m_actEditMetadata->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_I),
                                     QKeySequence(Qt::ALT | Qt::Key_Return)});
    m_actEditMetadata->setToolTip(tr("View or edit metadata tags and audio properties of selected track(s) (Ctrl+I / Double-click)"));
    connect(m_actEditMetadata, &QAction::triggered, this, &MainWindow::onEditTrackMetadata);

    m_actReloadMetadata = new QAction(QIcon::fromTheme(QStringLiteral("view-refresh"), st->standardIcon(QStyle::SP_BrowserReload)),
                                      tr("Reload Metadata"), this);
    m_actReloadMetadata->setShortcut(QKeySequence::Refresh); /* F5 or standard refresh */
    m_actReloadMetadata->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    m_actReloadMetadata->setToolTip(tr("Re-read tags and audio properties from source files on disk (Ctrl+Shift+R)"));
    connect(m_actReloadMetadata, &QAction::triggered, this, &MainWindow::onReloadMetadata);

    m_actOpenSourceFolder = new QAction(QIcon::fromTheme(QStringLiteral("folder"), st->standardIcon(QStyle::SP_DirIcon)),
                                        tr("View Track in Folder"), this);
    m_actOpenSourceFolder->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    m_actOpenSourceFolder->setToolTip(tr("Open the directory containing the selected track in file manager (Ctrl+Shift+F)"));
    connect(m_actOpenSourceFolder, &QAction::triggered, this, &MainWindow::onOpenSourceFolder);

    m_actSelectAll = new QAction(QIcon::fromTheme(QStringLiteral("edit-select-all")),
                                 tr("Select All"), this);
    m_actSelectAll->setShortcut(QKeySequence::SelectAll); /* Ctrl+A */
    connect(m_actSelectAll, &QAction::triggered, this, [this]() {
        if (m_table)
            m_table->selectAll();
    });

    m_actRemoveSelected = new QAction(QIcon::fromTheme(QStringLiteral("list-remove"), st->standardIcon(QStyle::SP_TrashIcon)),
                                      tr("Remove Selected"), this);
    m_actRemoveSelected->setShortcut(QKeySequence::Delete); /* Del */
    m_actRemoveSelected->setToolTip(tr("Remove selected tracks from the queue (Del)"));
    connect(m_actRemoveSelected, &QAction::triggered, this, &MainWindow::onRemoveSelected);

    m_actClearList = new QAction(QIcon::fromTheme(QStringLiteral("edit-clear-list"), st->standardIcon(QStyle::SP_DialogResetButton)),
                                 tr("Clear List"), this);
    m_actClearList->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_L),
                                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Delete)});
    m_actClearList->setToolTip(tr("Remove all tracks from the queue (Ctrl+L)"));
    connect(m_actClearList, &QAction::triggered, this, &MainWindow::onClearList);

    /* View actions (Menubar toggle + KDE HIG Toolbar Text Position submenu) */
    m_actShowMenuBar = new QAction(QIcon::fromTheme(QStringLiteral("show-menu")),
                                   tr("Show Menubar"), this);
    m_actShowMenuBar->setCheckable(true);
    m_actShowMenuBar->setChecked(true);
    m_actShowMenuBar->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    m_actShowMenuBar->setToolTip(tr("Toggle menubar visibility and hamburger menu button (Ctrl+M)"));
    connect(m_actShowMenuBar, &QAction::toggled, this, &MainWindow::onToggleMenuBar);

    m_toolbarStyleGroup = new QActionGroup(this);
    m_toolbarStyleGroup->setExclusive(true);

    auto addToolbarStyleAct = [this](const QString &title, Qt::ToolButtonStyle styleVal) {
        QAction *a = new QAction(title, this);
        a->setCheckable(true);
        a->setData(static_cast<int>(styleVal));
        if (styleVal == m_toolbarStyle)
            a->setChecked(true);
        m_toolbarStyleGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, styleVal]() {
            onChangeToolButtonStyle(styleVal);
        });
        return a;
    };

    QAction *actIconsOnly = addToolbarStyleAct(tr("Icons Only"), Qt::ToolButtonIconOnly);
    QAction *actTextOnly = addToolbarStyleAct(tr("Text Only"), Qt::ToolButtonTextOnly);
    QAction *actTextBeside = addToolbarStyleAct(tr("Text Alongside Icons"), Qt::ToolButtonTextBesideIcon);
    QAction *actTextUnder = addToolbarStyleAct(tr("Text Under Icons"), Qt::ToolButtonTextUnderIcon);

    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);

    auto addThemeAct = [this](const QString &title, const QString &themeId) {
        QAction *a = new QAction(title, this);
        a->setCheckable(true);
        a->setData(themeId);
        if (themeId == m_uiTheme)
            a->setChecked(true);
        m_themeGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, themeId]() {
            onChangeUiTheme(themeId);
        });
        return a;
    };

    QAction *actThemeBreezeLight = addThemeAct(tr("Breeze Light"), QStringLiteral("breeze-light"));
    QAction *actThemeBreezeDark = addThemeAct(tr("Breeze Dark"), QStringLiteral("breeze-dark"));
#ifdef Q_OS_WIN
    QAction *actThemeWinNative = addThemeAct(tr("Windows Classic / Native"), QStringLiteral("windowsvista"));
#endif
    QAction *actThemeSystem = addThemeAct(tr("System Default"), QStringLiteral("system"));

    /* Settings actions */
    m_actPresets = new QAction(QIcon::fromTheme(QStringLiteral("configure"), st->standardIcon(QStyle::SP_FileDialogContentsView)),
                               tr("Encoder Presets..."), this);
    m_actPresets->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    m_actPresets->setToolTip(tr("Open Encoder Presets manager (Ctrl+P)"));
    connect(m_actPresets, &QAction::triggered, this, &MainWindow::onManagePresets);

    m_actPreferences = new QAction(QIcon::fromTheme(QStringLiteral("preferences-system"), st->standardIcon(QStyle::SP_ComputerIcon)),
                                   tr("Configure qddbconv..."), this);
    m_actPreferences->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Comma));
    m_actPreferences->setToolTip(tr("Configure external tool paths and general preferences (Ctrl+Shift+,)"));
    connect(m_actPreferences, &QAction::triggered, this, &MainWindow::onOpenPreferences);

    /* Help actions */
    m_actAbout = new QAction(QIcon::fromTheme(QStringLiteral("help-about"), st->standardIcon(QStyle::SP_MessageBoxInformation)),
                             tr("About qddbconv..."), this);
    m_actAbout->setShortcut(QKeySequence::HelpContents); /* F1 */
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::onAbout);

    /* Register all actions on MainWindow itself so shortcuts work even when menubar is hidden */
    addActions({
        m_actAddFiles, m_actAddFolder, m_actAddCue,
        m_actStartConvert, m_actCancelConvert, m_actQuit,
        m_actEditMetadata, m_actReloadMetadata, m_actOpenSourceFolder,
        m_actSelectAll, m_actRemoveSelected, m_actClearList,
        m_actShowMenuBar, m_actPresets, m_actPreferences, m_actAbout
    });

    /* Build standard Menubar */
    QMenuBar *mb = menuBar();

    QMenu *fileMenu = mb->addMenu(tr("&File"));
    fileMenu->addAction(m_actAddFiles);
    fileMenu->addAction(m_actAddFolder);
    fileMenu->addAction(m_actAddCue);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actStartConvert);
    fileMenu->addAction(m_actCancelConvert);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actQuit);

    QMenu *editMenu = mb->addMenu(tr("&Edit"));
    editMenu->addAction(m_actEditMetadata);
    editMenu->addAction(m_actReloadMetadata);
    editMenu->addAction(m_actOpenSourceFolder);
    editMenu->addSeparator();
    editMenu->addAction(m_actSelectAll);
    editMenu->addAction(m_actRemoveSelected);
    editMenu->addAction(m_actClearList);

    QMenu *viewMenu = mb->addMenu(tr("&View"));
    viewMenu->addAction(m_actShowMenuBar);
    QMenu *toolbarTextMenu = viewMenu->addMenu(tr("Toolbar Text Position"));
    toolbarTextMenu->addAction(actIconsOnly);
    toolbarTextMenu->addAction(actTextOnly);
    toolbarTextMenu->addAction(actTextBeside);
    toolbarTextMenu->addAction(actTextUnder);

    QMenu *themeMenu = viewMenu->addMenu(tr("UI Theme"));
    themeMenu->addAction(actThemeBreezeLight);
    themeMenu->addAction(actThemeBreezeDark);
#ifdef Q_OS_WIN
    themeMenu->addAction(actThemeWinNative);
#endif
    themeMenu->addAction(actThemeSystem);

    QMenu *settingsMenu = mb->addMenu(tr("&Settings"));
    settingsMenu->addAction(m_actPresets);
    settingsMenu->addAction(m_actPreferences);

    QMenu *helpMenu = mb->addMenu(tr("&Help"));
    helpMenu->addAction(m_actAbout);

    /* Build popup menu for the Dolphin-style Hamburger button */
    m_hamburgerMenu = new QMenu(this);
    m_hamburgerMenu->addAction(m_actAddFiles);
    m_hamburgerMenu->addAction(m_actAddFolder);
    m_hamburgerMenu->addAction(m_actAddCue);
    m_hamburgerMenu->addSeparator();
    m_hamburgerMenu->addMenu(editMenu);
    m_hamburgerMenu->addMenu(viewMenu);
    m_hamburgerMenu->addMenu(settingsMenu);
    m_hamburgerMenu->addMenu(helpMenu);
    m_hamburgerMenu->addSeparator();
    m_hamburgerMenu->addAction(m_actShowMenuBar);
    m_hamburgerMenu->addSeparator();
    m_hamburgerMenu->addAction(m_actQuit);
}

void MainWindow::setupUi()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *rootLayout = new QVBoxLayout(central);

    /* Top toolbar using QToolButtons so KDE HIG Toolbar Text Position modes work seamlessly */
    QHBoxLayout *topBar = new QHBoxLayout();
    topBar->setSpacing(6);
    auto makeToolBtn = [this](QAction *act) {
        QToolButton *btn = new QToolButton(this);
        btn->setProperty("qddbToolbarBtn", true);
        btn->setDefaultAction(act);
        btn->setIconSize(QSize(16, 16));
        btn->setToolButtonStyle(m_toolbarStyle);
        btn->setAutoRaise(false);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_toolbarButtons.append(btn);
        return btn;
    };

    QToolButton *addFilesBtn = makeToolBtn(m_actAddFiles);
    QToolButton *addFolderBtn = makeToolBtn(m_actAddFolder);
    QToolButton *addCueBtn = makeToolBtn(m_actAddCue);
    QToolButton *editMetaBtn = makeToolBtn(m_actEditMetadata);
    QToolButton *removeBtn = makeToolBtn(m_actRemoveSelected);
    QToolButton *clearBtn = makeToolBtn(m_actClearList);

    /* Dolphin-style Hamburger menu button (shown when Menubar is hidden via Ctrl+M) */
    m_hamburgerBtn = new QToolButton(this);
    m_hamburgerBtn->setProperty("qddbToolbarBtn", true);
    m_hamburgerBtn->setIconSize(QSize(16, 16));
    m_hamburgerBtn->setIcon(makeBreezeFallbackIcon(QStringLiteral("application-menu"), palette().color(QPalette::ButtonText)));
    m_hamburgerBtn->setText(tr("Menu"));
    m_hamburgerBtn->setToolButtonStyle(m_toolbarStyle);
    m_hamburgerBtn->setPopupMode(QToolButton::InstantPopup);
    m_hamburgerBtn->setMenu(m_hamburgerMenu);
    m_hamburgerBtn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_hamburgerBtn->setToolTip(tr("Open Application Menu (press Ctrl+M to restore Menubar)"));
    m_hamburgerBtn->setVisible(false);
    m_toolbarButtons.append(m_hamburgerBtn);

    /* Top-right Quit button */
    QToolButton *topQuitBtn = makeToolBtn(m_actQuit);

    topBar->addWidget(addFilesBtn);
    topBar->addWidget(addFolderBtn);
    topBar->addWidget(addCueBtn);
    topBar->addWidget(editMetaBtn);
    topBar->addWidget(removeBtn);
    topBar->addWidget(clearBtn);
    topBar->addStretch();
    topBar->addWidget(m_hamburgerBtn);
    topBar->addWidget(topQuitBtn);
    rootLayout->addLayout(topBar);

    /* Main horizontal splitter: Left = Converter Settings, Right = Live Output Preview */
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

    /* Left Pane: Converter Settings */
    QWidget *leftPane = new QWidget(splitter);
    leftPane->setMinimumWidth(410);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 6, 0);

    /* 1. Output Destination Group */
    QGroupBox *destGroup = new QGroupBox(tr("Output Destination"), leftPane);
    QVBoxLayout *destLayout = new QVBoxLayout(destGroup);

    destLayout->addWidget(new QLabel(tr("Output folder:"), destGroup));
    QHBoxLayout *folderRow = new QHBoxLayout();
    m_outFolderEdit = new QLineEdit(destGroup);
    m_browseFolderBtn = new QPushButton(tr("Browse..."), destGroup);
    m_browseFolderBtn->setToolTip(tr("Choose destination directory for converted audio files"));
    folderRow->addWidget(m_outFolderEdit, 1);
    folderRow->addWidget(m_browseFolderBtn);
    destLayout->addLayout(folderRow);

    m_writeToSourceCheck = new QCheckBox(tr("Write to source track folder"), destGroup);
    m_writeToSourceCheck->setToolTip(tr("Save each converted file in the same directory as its original source file"));

    m_preserveStructureCheck = new QCheckBox(tr("Preserve folder structure"), destGroup);
    m_preserveStructureCheck->setToolTip(tr("Recreate the original subfolder hierarchy inside the output folder"));

    destLayout->addWidget(m_writeToSourceCheck);
    destLayout->addWidget(m_preserveStructureCheck);

    destLayout->addWidget(new QLabel(tr("Output file name (Title Formatting):"), destGroup));
    QHBoxLayout *patternRow = new QHBoxLayout();
    m_outFilePatternEdit = new QLineEdit(destGroup);
    m_outFilePatternEdit->setText(QStringLiteral("[%tracknumber%. ][%artist% - ]%title%"));

    /* Qmmp-style tag formatter popup menu button */
    QToolButton *tagMenuBtn = new QToolButton(destGroup);
    tagMenuBtn->setText(QStringLiteral("%"));
    tagMenuBtn->setToolTip(tr("Insert metadata tag placeholder into output file name pattern"));
    tagMenuBtn->setPopupMode(QToolButton::InstantPopup);
    QMenu *tagMenu = new QMenu(tagMenuBtn);
    const QList<QPair<QString, QString>> tagPlaceholders = {
        {tr("Artist (%artist%)"), QStringLiteral("%artist%")},
        {tr("Title (%title%)"), QStringLiteral("%title%")},
        {tr("Album (%album%)"), QStringLiteral("%album%")},
        {tr("Album Artist (%albumartist%)"), QStringLiteral("%albumartist%")},
        {tr("Track Number (%tracknumber%)"), QStringLiteral("%tracknumber%")},
        {tr("Total Tracks (%totaltracks%)"), QStringLiteral("%totaltracks%")},
        {tr("Disc Number (%disc%)"), QStringLiteral("%disc%")},
        {tr("Year (%year%)"), QStringLiteral("%year%")},
        {tr("Genre (%genre%)"), QStringLiteral("%genre%")},
        {tr("Comment (%comment%)"), QStringLiteral("%comment%")},
        {tr("Original File Name (%filename%)"), QStringLiteral("%filename%")},
        {tr("Conditional Block ([...])"), QStringLiteral("[%tracknumber%. ]")},
        {tr("Subfolder Pattern (%artist%/%album%/%tracknumber%. %title%)"),
         QStringLiteral("%artist%/[%year% - ]%album%/[%tracknumber%. ]%title%")}
    };
    for (const auto &pair : tagPlaceholders) {
        tagMenu->addAction(pair.first, this, [this, val = pair.second]() {
            if (val.contains(QLatin1Char('/')))
                m_outFilePatternEdit->setText(val);
            else
                m_outFilePatternEdit->insert(val);
        });
    }
    tagMenuBtn->setMenu(tagMenu);

    patternRow->addWidget(m_outFilePatternEdit, 1);
    patternRow->addWidget(tagMenuBtn);
    destLayout->addLayout(patternRow);

    leftLayout->addWidget(destGroup);

    /* 2. Encoder & DSP Settings Group */
    QGroupBox *encGroup = new QGroupBox(tr("Encoder && DSP"), leftPane);
    QFormLayout *encForm = new QFormLayout(encGroup);
    encForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_encoderCombo = new QComboBox(encGroup);
    m_encoderCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QLabel *encLabel = new QLabel(tr("Encoder:"), encGroup);
    encLabel->setToolTip(
        tr("Output audio format and encoder preset (Ctrl+P in Settings -> Encoder Presets... to manage).\n"
           "Automatic fallback chain when a preset's primary binary or library is missing:\n"
           "CLI binary -> FFmpeg library encoder -> FFmpeg built-in encoder\n"
           "(hover over the selected preset or dropdown items to see how each preset will run)."));
    encForm->addRow(encLabel, m_encoderCombo);

    m_sampleFormatCombo = new QComboBox(encGroup);
    m_sampleFormatCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_sampleFormatCombo->addItem(tr("Keep source format"), 0);
    m_sampleFormatCombo->addItem(tr("8-bit unsigned int"), 8);
    m_sampleFormatCombo->addItem(tr("16-bit signed int"), 16);
    m_sampleFormatCombo->addItem(tr("24-bit signed int"), 24);
    m_sampleFormatCombo->addItem(tr("32-bit signed int"), 32);
    m_sampleFormatCombo->addItem(tr("32-bit IEEE float"), 33);
    m_sampleFormatCombo->setToolTip(tr("Select target PCM bit depth passed to the encoder (or keep original bit depth)"));
    encForm->addRow(tr("Output sample format:"), m_sampleFormatCombo);

    m_sampleRateCombo = new QComboBox(encGroup);
    m_sampleRateCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_sampleRateCombo->setEditable(true);
    m_sampleRateCombo->setInsertPolicy(QComboBox::NoInsert);
    m_sampleRateCombo->addItem(tr("Keep source sample rate"), 0);
    const QList<int> sampleRates = {
        8000, 11025, 12000, 16000, 18900, 22050, 24000, 32000, 37800,
        44100, 48000, 64000, 88200, 96000, 176400, 192000, 352800, 384000
    };
    for (int sr : sampleRates)
        m_sampleRateCombo->addItem(QStringLiteral("%1 Hz").arg(sr), sr);
    m_sampleRateCombo->setToolTip(
        tr("Choose a target sample rate (8000 Hz .. 384000 Hz) or type any custom rate in Hz (0 = keep source)"));
    encForm->addRow(tr("Resample (DSP):"), m_sampleRateCombo);

    m_channelsCombo = new QComboBox(encGroup);
    m_channelsCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_channelsCombo->addItem(tr("Keep source channels"), 0);
    m_channelsCombo->addItem(tr("Stereo (2.0)"), 2);
    m_channelsCombo->addItem(tr("Mono (1.0)"), 1);
    m_channelsCombo->setToolTip(tr("Keep original channel count or downmix/upmix to Stereo or Mono"));
    encForm->addRow(tr("Channels:"), m_channelsCombo);

    leftLayout->addWidget(encGroup);

    /* 3. More Options Group */
    QGroupBox *optGroup = new QGroupBox(tr("More Options"), leftPane);
    QVBoxLayout *optLayout = new QVBoxLayout(optGroup);

    m_embedCoverCheck = new QCheckBox(tr("Embed cover art (from source tags or album folder)"), optGroup);
    m_embedCoverCheck->setChecked(true);
    m_embedCoverCheck->setToolTip(
        tr("Automatically embed cover art from source file tags or cover.jpg/folder.jpg/front.png in the track directory\n"
           "(cover art downscaling can be configured in Settings -> Configure qddbconv...)"));
    optLayout->addWidget(m_embedCoverCheck);

    m_bypassSameFormatCheck = new QCheckBox(tr("Copy file without conversion if format is not changing"), optGroup);
    m_bypassSameFormatCheck->setToolTip(
        tr("Copy file without conversion if the source extension matches the target preset extension\n"
           "(re-tagging after copy can be enabled in Settings -> Configure qddbconv...)"));
    optLayout->addWidget(m_bypassSameFormatCheck);

    QFormLayout *optForm = new QFormLayout();
    optForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_overwriteCombo = new QComboBox(optGroup);
    m_overwriteCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_overwriteCombo->addItem(tr("Prompt"), 0);
    m_overwriteCombo->addItem(tr("Skip existing file"), 1);
    m_overwriteCombo->addItem(tr("Overwrite existing file"), 2);
    m_overwriteCombo->setToolTip(tr("Action to take when the destination file already exists on disk"));
    optForm->addRow(tr("When file exists:"), m_overwriteCombo);

    m_threadsSpin = new QSpinBox(optGroup);
    m_threadsSpin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_threadsSpin->setRange(1, 64);
    m_threadsSpin->setValue(qMax(1, QThread::idealThreadCount()));
    m_threadsSpin->setToolTip(tr("Number of tracks to convert simultaneously across CPU cores"));
    optForm->addRow(tr("Parallel threads:"), m_threadsSpin);
    optLayout->addLayout(optForm);

    leftLayout->addWidget(optGroup);
    leftLayout->addStretch();

    /* Right Pane: Track List & Live Output Preview */
    QGroupBox *previewGroup = new QGroupBox(
        tr("Tracks && Output Preview (Drag && Drop files/folders/.cue here; Double-click row to edit tags)"),
        splitter);
    QVBoxLayout *previewLayout = new QVBoxLayout(previewGroup);

    m_table = new QTableWidget(0, 4, previewGroup);
    m_table->setHorizontalHeaderLabels({
        tr("Source File"),
        tr("Track / Artist"),
        tr("Output File Preview"),
        tr("Status")
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->setColumnWidth(0, 200);
    m_table->setColumnWidth(1, 190);
    previewLayout->addWidget(m_table);

    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        onEditTrackMetadata();
    });

    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu ctx(this);
        ctx.addAction(m_actEditMetadata);
        ctx.addAction(m_actReloadMetadata);
        ctx.addAction(m_actOpenSourceFolder);
        ctx.addSeparator();
        ctx.addAction(m_actAddFiles);
        ctx.addAction(m_actAddFolder);
        ctx.addAction(m_actAddCue);
        ctx.addSeparator();
        ctx.addAction(m_actSelectAll);
        ctx.addAction(m_actRemoveSelected);
        ctx.addAction(m_actClearList);
        ctx.exec(m_table->viewport()->mapToGlobal(pos));
    });

    splitter->addWidget(leftPane);
    splitter->addWidget(previewGroup);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    rootLayout->addWidget(splitter, 1);

    /* Bottom Progress & Action Bar */
    QHBoxLayout *bottomBar = new QHBoxLayout();
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setToolTip(tr("Overall conversion progress"));

    m_statusLabel = new QLabel(tr("Ready"), this);
    m_convertBtn = new QPushButton(m_actStartConvert->icon(), tr("Convert"), this);
    m_convertBtn->setDefault(true);
    m_convertBtn->setToolTip(m_actStartConvert->toolTip());

    m_cancelBtn = new QPushButton(m_actCancelConvert->icon(), tr("Cancel"), this);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setToolTip(tr("Abort active conversion tasks (Esc)"));

    bottomBar->addWidget(m_progressBar, 1);
    bottomBar->addWidget(m_statusLabel);
    bottomBar->addWidget(m_convertBtn);
    bottomBar->addWidget(m_cancelBtn);
    rootLayout->addLayout(bottomBar);

    populatePresetCombo();

    /* Wire signals for live preview and tooltip updates */
    connect(m_browseFolderBtn, &QPushButton::clicked, this, &MainWindow::onBrowseOutputFolder);
    connect(m_outFolderEdit, &QLineEdit::textChanged, this, [this]() {
        updateControlTooltips();
        updateOutputPreview();
    });
    connect(m_outFilePatternEdit, &QLineEdit::textChanged, this, [this]() {
        updateControlTooltips();
        updateOutputPreview();
    });
    connect(m_encoderCombo, &QComboBox::currentIndexChanged, this, [this]() {
        updateControlTooltips();
        updateOutputPreview();
    });
    connect(m_writeToSourceCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_outFolderEdit->setEnabled(!checked);
        m_browseFolderBtn->setEnabled(!checked);
        m_preserveStructureCheck->setEnabled(!checked);
        updateOutputPreview();
    });
    connect(m_preserveStructureCheck, &QCheckBox::toggled, this, &MainWindow::updateOutputPreview);

    connect(m_convertBtn, &QPushButton::clicked, this, &MainWindow::onStartConversion);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancelConversion);
}

void MainWindow::onToggleMenuBar(bool visible)
{
    menuBar()->setVisible(visible);
    if (m_hamburgerBtn)
        m_hamburgerBtn->setVisible(!visible);
}

void MainWindow::onChangeToolButtonStyle(Qt::ToolButtonStyle style)
{
    m_toolbarStyle = style;
    for (QToolButton *btn : m_toolbarButtons) {
        /* If an action has no icon in the current system theme and IconsOnly was selected,
         * fall back to showing text on that button so it never becomes blank */
        if (style == Qt::ToolButtonIconOnly && btn->icon().isNull())
            btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        else
            btn->setToolButtonStyle(style);
    }
    if (m_toolbarStyleGroup) {
        for (QAction *a : m_toolbarStyleGroup->actions()) {
            if (a->data().toInt() == static_cast<int>(style)) {
                a->setChecked(true);
                break;
            }
        }
    }
}

void MainWindow::refreshActionIcons()
{
    const QColor fg = qApp->palette().color(QPalette::ButtonText);
    if (m_actAddFiles)        m_actAddFiles->setIcon(makeBreezeFallbackIcon(QStringLiteral("document-open"), fg));
    if (m_actAddFolder)       m_actAddFolder->setIcon(makeBreezeFallbackIcon(QStringLiteral("folder-open"), fg));
    if (m_actAddCue)          m_actAddCue->setIcon(makeBreezeFallbackIcon(QStringLiteral("media-optical-audio"), fg));
    if (m_actStartConvert)    m_actStartConvert->setIcon(makeBreezeFallbackIcon(QStringLiteral("media-playback-start"), fg));
    if (m_actCancelConvert)   m_actCancelConvert->setIcon(makeBreezeFallbackIcon(QStringLiteral("process-stop"), fg));
    if (m_actQuit)            m_actQuit->setIcon(makeBreezeFallbackIcon(QStringLiteral("application-exit"), fg));
    if (m_actEditMetadata)    m_actEditMetadata->setIcon(makeBreezeFallbackIcon(QStringLiteral("document-properties"), fg));
    if (m_actReloadMetadata)  m_actReloadMetadata->setIcon(makeBreezeFallbackIcon(QStringLiteral("view-refresh"), fg));
    if (m_actOpenSourceFolder)m_actOpenSourceFolder->setIcon(makeBreezeFallbackIcon(QStringLiteral("folder"), fg));
    if (m_actSelectAll)       m_actSelectAll->setIcon(makeBreezeFallbackIcon(QStringLiteral("edit-select-all"), fg));
    if (m_actRemoveSelected)  m_actRemoveSelected->setIcon(makeBreezeFallbackIcon(QStringLiteral("list-remove"), fg));
    if (m_actClearList)       m_actClearList->setIcon(makeBreezeFallbackIcon(QStringLiteral("edit-clear-list"), fg));
    if (m_actShowMenuBar)     m_actShowMenuBar->setIcon(makeBreezeFallbackIcon(QStringLiteral("show-menu"), fg));
    if (m_actPresets)         m_actPresets->setIcon(makeBreezeFallbackIcon(QStringLiteral("configure"), fg));
    if (m_actPreferences)     m_actPreferences->setIcon(makeBreezeFallbackIcon(QStringLiteral("preferences-system"), fg));
    if (m_actAbout)           m_actAbout->setIcon(makeBreezeFallbackIcon(QStringLiteral("help-about"), fg));
    if (m_hamburgerBtn)       m_hamburgerBtn->setIcon(makeBreezeFallbackIcon(QStringLiteral("application-menu"), fg));
    if (m_convertBtn && m_actStartConvert) m_convertBtn->setIcon(m_actStartConvert->icon());
    if (m_cancelBtn && m_actCancelConvert) m_cancelBtn->setIcon(m_actCancelConvert->icon());
}

void MainWindow::applyUiTheme(const QString &themeId)
{
    static QString s_initialStyleName;
    static QPalette s_initialPalette;
    static bool s_savedInitial = false;
    if (!s_savedInitial && qApp && qApp->style()) {
        s_initialStyleName = qApp->style()->objectName();
        s_initialPalette = qApp->palette();
        s_savedInitial = true;
    }

    m_uiTheme = themeId;

    if (themeId == QLatin1String("breeze-light")) {
        QStyle *base = QStyleFactory::create(QStringLiteral("Breeze"));
        if (!base)
            base = QStyleFactory::create(QStringLiteral("Fusion"));
        if (base)
            qApp->setStyle(new CleanUiProxyStyle(base));

        QPalette pal;
        pal.setColor(QPalette::Window,          QColor(0xef, 0xf0, 0xf1));
        pal.setColor(QPalette::WindowText,      QColor(0x23, 0x26, 0x29));
        pal.setColor(QPalette::Base,            QColor(0xfc, 0xfc, 0xfc));
        pal.setColor(QPalette::AlternateBase,   QColor(0xf4, 0xf5, 0xf6));
        pal.setColor(QPalette::ToolTipBase,     QColor(0x23, 0x26, 0x29));
        pal.setColor(QPalette::ToolTipText,     QColor(0xef, 0xf0, 0xf1));
        pal.setColor(QPalette::Text,            QColor(0x23, 0x26, 0x29));
        pal.setColor(QPalette::Button,          QColor(0xf6, 0xf7, 0xf8));
        pal.setColor(QPalette::ButtonText,      QColor(0x23, 0x26, 0x29));
        pal.setColor(QPalette::BrightText,      QColor(0xff, 0xff, 0xff));
        pal.setColor(QPalette::Light,           QColor(0xff, 0xff, 0xff));
        pal.setColor(QPalette::Midlight,        QColor(0xf7, 0xf8, 0xf9));
        pal.setColor(QPalette::Mid,             QColor(0xd2, 0xd4, 0xd6));
        pal.setColor(QPalette::Dark,            QColor(0xb8, 0xbb, 0xbe));
        pal.setColor(QPalette::Shadow,          QColor(0x88, 0x8b, 0x8e));
        pal.setColor(QPalette::Link,            QColor(0x29, 0x80, 0xb9));
        pal.setColor(QPalette::Highlight,       QColor(0x3d, 0xae, 0xe9));
        pal.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        pal.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x95, 0x99, 0x9c));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x95, 0x99, 0x9c));
        pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x95, 0x99, 0x9c));
        qApp->setPalette(pal);
        qApp->setStyleSheet(QString());
    } else if (themeId == QLatin1String("breeze-dark")) {
        QStyle *base = QStyleFactory::create(QStringLiteral("Fusion"));
        if (base)
            qApp->setStyle(new CleanUiProxyStyle(base));

        QPalette pal;
        pal.setColor(QPalette::Window,          QColor(0x2a, 0x2e, 0x32));
        pal.setColor(QPalette::WindowText,      QColor(0xef, 0xf0, 0xf1));
        pal.setColor(QPalette::Base,            QColor(0x1b, 0x1e, 0x20));
        pal.setColor(QPalette::AlternateBase,   QColor(0x23, 0x26, 0x29));
        pal.setColor(QPalette::ToolTipBase,     QColor(0x31, 0x36, 0x3b));
        pal.setColor(QPalette::ToolTipText,     QColor(0xef, 0xf0, 0xf1));
        pal.setColor(QPalette::Text,            QColor(0xef, 0xf0, 0xf1));
        pal.setColor(QPalette::Button,          QColor(0x31, 0x36, 0x3b));
        pal.setColor(QPalette::ButtonText,      QColor(0xef, 0xf0, 0xf1));
        pal.setColor(QPalette::BrightText,      QColor(0xff, 0xff, 0xff));
        pal.setColor(QPalette::Light,           QColor(0x45, 0x4c, 0x52));
        pal.setColor(QPalette::Midlight,        QColor(0x3a, 0x40, 0x45));
        pal.setColor(QPalette::Mid,             QColor(0x25, 0x29, 0x2c));
        pal.setColor(QPalette::Dark,            QColor(0x1d, 0x20, 0x23));
        pal.setColor(QPalette::Shadow,          QColor(0x12, 0x14, 0x16));
        pal.setColor(QPalette::Link,            QColor(0x3d, 0xae, 0xe9));
        pal.setColor(QPalette::Highlight,       QColor(0x3d, 0xae, 0xe9));
        pal.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        pal.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x6e, 0x73, 0x78));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6e, 0x73, 0x78));
        pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x6e, 0x73, 0x78));
        qApp->setPalette(pal);
        qApp->setStyleSheet(QString());
    } else if (themeId == QLatin1String("windowsvista")) {
        QStyle *base = QStyleFactory::create(QStringLiteral("windowsvista"));
        if (!base)
            base = QStyleFactory::create(QStringLiteral("Fusion"));
        if (base)
            qApp->setStyle(new CleanUiProxyStyle(base));
        qApp->setPalette(qApp->style()->standardPalette());
        qApp->setStyleSheet(QString());
    } else {
        QStyle *base = !s_initialStyleName.isEmpty()
            ? QStyleFactory::create(s_initialStyleName)
            : nullptr;
        if (!base)
            base = QStyleFactory::create(QStringLiteral("Fusion"));
        if (base)
            qApp->setStyle(new CleanUiProxyStyle(base));
        qApp->setPalette(s_initialPalette);
        qApp->setStyleSheet(QString());
    }

    refreshActionIcons();
}

void MainWindow::onChangeUiTheme(const QString &themeId)
{
    applyUiTheme(themeId);
    if (m_themeGroup) {
        for (QAction *a : m_themeGroup->actions()) {
            if (a->data().toString() == themeId) {
                a->setChecked(true);
                break;
            }
        }
    }
}

void MainWindow::refreshTableRow(int row)
{
    if (row < 0 || row >= m_tracks.size())
        return;

    const TrackItem &item = m_tracks[row];
    QString srcDisplay = QFileInfo(item.sourcePath).fileName();
    if (!item.cuePath.isEmpty())
        srcDisplay = QStringLiteral("%1 [Track %2]").arg(QFileInfo(item.cuePath).fileName(), item.trackNumber);

    QString brNote = item.bitrateKbps > 0
        ? QStringLiteral(", %1 kbps").arg(item.bitrateKbps)
        : QString();

    QString srcTooltip = item.cuePath.isEmpty()
        ? tr("Source file: %1\nCodec: %2 (%3 Hz, %4-bit, %5 ch%6)\nDuration: %7 s")
              .arg(item.sourcePath,
                   item.codecName.isEmpty() ? QStringLiteral("audio") : item.codecName)
              .arg(item.sampleRate)
              .arg(item.bitsPerSample)
              .arg(item.channels)
              .arg(brNote)
              .arg(item.durationSec, 0, 'f', 1)
        : tr("CUE sheet: %1\nAudio image: %2\nTrack %3 (%4s .. %5s)")
              .arg(item.cuePath, item.sourcePath, item.trackNumber)
              .arg(item.startSec, 0, 'f', 2)
              .arg(item.endSec, 0, 'f', 2);

    QString metaDisplay = item.artist.isEmpty()
        ? item.title
        : QStringLiteral("%1 - %2").arg(item.artist, item.title);

    QString coverState;
    if (item.coverArtPath == QLatin1String("__none__"))
        coverState = tr("Disabled");
    else if (!item.coverArtPath.isEmpty())
        coverState = QFileInfo(item.coverArtPath).fileName();
    else if (item.hasEmbeddedArt)
        coverState = tr("Embedded in source");
    else
        coverState = tr("None");

    QString metaTooltip = tr("Title: %1\nArtist: %2\nAlbum: %3\nAlbum Artist: %4\nTrack: %5/%6\nDisc: %7\nYear: %8\nGenre: %9\nComment: %10\nCover Art: %11")
                              .arg(item.title, item.artist, item.album, item.albumArtist,
                                   item.trackNumber, item.totalTracks, item.discNumber,
                                   item.year, item.genre, item.comment, coverState);

    if (QTableWidgetItem *c0 = m_table->item(row, 0)) {
        c0->setText(srcDisplay);
        c0->setToolTip(srcTooltip);
    }
    if (QTableWidgetItem *c1 = m_table->item(row, 1)) {
        c1->setText(metaDisplay);
        c1->setToolTip(metaTooltip);
    }
    if (QTableWidgetItem *c3 = m_table->item(row, 3)) {
        c3->setText(item.statusText);
        c3->setToolTip(item.statusText);
    }
}

void MainWindow::onEditTrackMetadata()
{
    QList<int> rows;
    for (const QModelIndex &idx : m_table->selectionModel()->selectedRows())
        rows.append(idx.row());
    std::sort(rows.begin(), rows.end());

    if (rows.isEmpty()) {
        if (m_table->currentRow() >= 0)
            rows.append(m_table->currentRow());
        else
            return;
    }

    const bool multi = (rows.size() > 1);
    const TrackItem &first = m_tracks[rows.first()];

    QDialog dlg(this);
    dlg.setWindowTitle(multi
        ? tr("Edit Metadata (%1 tracks selected)").arg(rows.size())
        : tr("Track Metadata && Properties — %1").arg(QFileInfo(first.sourcePath).fileName()));
    dlg.resize(680, 480);

    QVBoxLayout *mainLayout = new QVBoxLayout(&dlg);
    QTabWidget *tabs = new QTabWidget(&dlg);

    /* Tab 1: Editable Metadata + Cover Art Preview */
    QWidget *metaTab = new QWidget(&dlg);
    QHBoxLayout *metaTabLayout = new QHBoxLayout(metaTab);

    QFormLayout *metaForm = new QFormLayout();

    QLineEdit *titleEdit = new QLineEdit(first.title, metaTab);
    if (multi) {
        titleEdit->setEnabled(false);
        titleEdit->setPlaceholderText(tr("(Multiple tracks selected — titles preserved individually)"));
    }

    QLineEdit *artistEdit = new QLineEdit(first.artist, metaTab);
    QLineEdit *albumEdit = new QLineEdit(first.album, metaTab);
    QLineEdit *albumArtistEdit = new QLineEdit(first.albumArtist, metaTab);

    QHBoxLayout *trkRow = new QHBoxLayout();
    QLineEdit *trackNumEdit = new QLineEdit(first.trackNumber, metaTab);
    if (multi)
        trackNumEdit->setEnabled(false);
    QLineEdit *totalTracksEdit = new QLineEdit(first.totalTracks, metaTab);
    QLineEdit *discEdit = new QLineEdit(first.discNumber, metaTab);
    trkRow->addWidget(trackNumEdit, 1);
    trkRow->addWidget(new QLabel(tr("/ Total:"), metaTab));
    trkRow->addWidget(totalTracksEdit, 1);
    trkRow->addWidget(new QLabel(tr("Disc:"), metaTab));
    trkRow->addWidget(discEdit, 1);

    QHBoxLayout *yrGenreRow = new QHBoxLayout();
    QLineEdit *yearEdit = new QLineEdit(first.year, metaTab);
    QLineEdit *genreEdit = new QLineEdit(first.genre, metaTab);
    yrGenreRow->addWidget(yearEdit, 1);
    yrGenreRow->addWidget(new QLabel(tr("Genre:"), metaTab));
    yrGenreRow->addWidget(genreEdit, 2);

    QLineEdit *commentEdit = new QLineEdit(first.comment, metaTab);

    metaForm->addRow(tr("Title:"), titleEdit);
    metaForm->addRow(tr("Artist:"), artistEdit);
    metaForm->addRow(tr("Album:"), albumEdit);
    metaForm->addRow(tr("Album Artist:"), albumArtistEdit);
    metaForm->addRow(tr("Track #:"), trkRow);
    metaForm->addRow(tr("Year:"), yrGenreRow);
    metaForm->addRow(tr("Comment:"), commentEdit);

    /* Direct write-to-source button (explicitly disabled for CUE tracks to protect album images) */
    QPushButton *writeSourceBtn = new QPushButton(
        tr("Write Tags Directly to Source File on Disk"), metaTab);
    if (!first.cuePath.isEmpty()) {
        writeSourceBtn->setEnabled(false);
        writeSourceBtn->setToolTip(
            tr("Disabled for CUE subtracks so the shared album audio image is never overwritten.\n"
               "Click OK below to apply edited tags to the converted output files."));
    } else if (multi) {
        writeSourceBtn->setEnabled(false);
        writeSourceBtn->setToolTip(
            tr("Select a single non-CUE audio file to write tags directly to the source file on disk.\n"
               "Click OK below to apply batch-edited tags to the conversion queue."));
    } else {
        writeSourceBtn->setToolTip(
            tr("Immediately update the metadata tags inside the original source file (%1)").arg(first.sourcePath));
    }
    metaForm->addRow(QString(), writeSourceBtn);
    metaTabLayout->addLayout(metaForm, 1);

    /* Right side of Metadata tab: Cover Art Preview & Chooser */
    QGroupBox *coverBox = new QGroupBox(tr("Cover Art"), metaTab);
    QVBoxLayout *coverLayout = new QVBoxLayout(coverBox);
    QLabel *coverPreviewLabel = new QLabel(coverBox);
    coverPreviewLabel->setFixedSize(150, 150);
    coverPreviewLabel->setAlignment(Qt::AlignCenter);
    coverPreviewLabel->setFrameShape(QFrame::StyledPanel);

    QLabel *coverInfoLabel = new QLabel(coverBox);
    coverInfoLabel->setAlignment(Qt::AlignCenter);
    coverInfoLabel->setWordWrap(true);

    QString chosenCoverPath = first.coverArtPath;
    bool coverModified = false;

    auto updateCoverPreview = [&]() {
        TrackItem tempItem = first;
        tempItem.coverArtPath = chosenCoverPath;
        QString desc;
        QImage img = TrackProbe::loadTrackCoverImage(tempItem, &desc);
        if (!img.isNull()) {
            coverPreviewLabel->setPixmap(
                QPixmap::fromImage(img.scaled(146, 146, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        } else {
            coverPreviewLabel->setPixmap(QPixmap());
            coverPreviewLabel->setText(tr("No Cover"));
        }
        coverInfoLabel->setText(desc);
        coverPreviewLabel->setToolTip(desc);
    };
    updateCoverPreview();

    QPushButton *chooseCoverBtn = new QPushButton(tr("Choose Image..."), coverBox);
    chooseCoverBtn->setToolTip(tr("Select a custom cover image (.jpg, .png, .webp) for the selected track(s)"));
    QPushButton *clearCoverBtn = new QPushButton(tr("Clear Cover"), coverBox);
    clearCoverBtn->setToolTip(tr("Do not embed cover art for the selected track(s)"));

    connect(chooseCoverBtn, &QPushButton::clicked, &dlg, [&]() {
        QString startDir = QFileInfo(first.sourcePath).absolutePath();
        QString file = QFileDialog::getOpenFileName(
            &dlg,
            tr("Select Cover Image"),
            startDir,
            tr("Images (*.jpg *.jpeg *.png *.webp *.bmp *.gif *.tif *.tiff);;All Files (*)"));
        if (!file.isEmpty()) {
            chosenCoverPath = file;
            coverModified = true;
            updateCoverPreview();
        }
    });

    connect(clearCoverBtn, &QPushButton::clicked, &dlg, [&]() {
        chosenCoverPath = QStringLiteral("__none__");
        coverModified = true;
        updateCoverPreview();
    });

    coverLayout->addWidget(coverPreviewLabel, 0, Qt::AlignHCenter);
    coverLayout->addWidget(coverInfoLabel);
    coverLayout->addWidget(chooseCoverBtn);
    coverLayout->addWidget(clearCoverBtn);
    coverLayout->addStretch();
    metaTabLayout->addWidget(coverBox);

    connect(writeSourceBtn, &QPushButton::clicked, &dlg, [&]() {
        if (!first.cuePath.isEmpty() || multi)
            return;

        if (QMessageBox::question(
                &dlg,
                tr("Write Tags to Source File"),
                tr("Overwrite metadata tags directly inside the source file?\n%1").arg(first.sourcePath)) != QMessageBox::Yes) {
            return;
        }

        TrackItem updated = first;
        updated.title = titleEdit->text().trimmed();
        updated.artist = artistEdit->text().trimmed();
        updated.album = albumEdit->text().trimmed();
        updated.albumArtist = albumArtistEdit->text().trimmed();
        updated.trackNumber = trackNumEdit->text().trimmed();
        updated.totalTracks = totalTracksEdit->text().trimmed();
        updated.discNumber = discEdit->text().trimmed();
        updated.year = yearEdit->text().trimmed();
        updated.genre = genreEdit->text().trimmed();
        updated.comment = commentEdit->text().trimmed();
        if (coverModified)
            updated.coverArtPath = chosenCoverPath;

        EncoderPreset tagPreset;
        tagPreset.tag_id3v2 = true;
        tagPreset.tag_flac = true;
        tagPreset.tag_oggvorbis = true;
        tagPreset.tag_mp4 = true;
        tagPreset.tag_apev2 = true;

        QString err;
        if (AudioConverter::writeMetadataTags(
                updated,
                updated.sourcePath,
                tagPreset,
                m_embedCoverCheck->isChecked(),
                m_resizeCoverArt,
                m_maxCoverSizePx,
                err)) {
            QMessageBox::information(&dlg, tr("Tags Written"),
                tr("Metadata tags were written to:\n%1").arg(updated.sourcePath));
        } else {
            QMessageBox::warning(&dlg, tr("Tag Error"),
                tr("Failed to write tags to source file:\n%1").arg(err));
        }
    });

    tabs->addTab(metaTab, tr("&Metadata"));

    /* Tab 2: Technical Properties (inspired by deadbeef-qt5 MetadataDialog props table) */
    QWidget *propsTab = new QWidget(&dlg);
    QVBoxLayout *propsLayout = new QVBoxLayout(propsTab);
    QTableWidget *propsTable = new QTableWidget(0, 2, propsTab);
    propsTable->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
    propsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    propsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    propsTable->verticalHeader()->hide();
    propsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    propsTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto addProp = [&](const QString &k, const QString &v) {
        int r = propsTable->rowCount();
        propsTable->insertRow(r);
        QTableWidgetItem *ki = new QTableWidgetItem(k);
        QTableWidgetItem *vi = new QTableWidgetItem(v);
        vi->setToolTip(v);
        propsTable->setItem(r, 0, ki);
        propsTable->setItem(r, 1, vi);
    };

    addProp(tr("Location"), first.sourcePath);
    if (!first.cuePath.isEmpty()) {
        addProp(tr("CUE Sheet"), first.cuePath);
        addProp(tr("Subtrack Range"), QStringLiteral("%1 s .. %2 s").arg(first.startSec, 0, 'f', 2).arg(first.endSec, 0, 'f', 2));
    }
    if (first.fileSizeBytes > 0) {
        addProp(tr("File Size"),
                QStringLiteral("%1 (%2 bytes)")
                    .arg(QLocale::system().formattedDataSize(first.fileSizeBytes),
                         QLocale::system().toString(first.fileSizeBytes)));
    }
    int mins = static_cast<int>(first.durationSec) / 60;
    int secs = static_cast<int>(first.durationSec) % 60;
    addProp(tr("Duration"), QStringLiteral("%1:%2 (%3 s)")
                                .arg(mins)
                                .arg(secs, 2, 10, QLatin1Char('0'))
                                .arg(first.durationSec, 0, 'f', 2));
    addProp(tr("Codec"), first.codecName.isEmpty() ? QStringLiteral("Unknown") : first.codecName);
    addProp(tr("Bitrate"), first.bitrateKbps > 0 ? QStringLiteral("%1 kbps").arg(first.bitrateKbps) : tr("Unknown"));
    addProp(tr("Sample Rate"), QStringLiteral("%1 Hz").arg(first.sampleRate));
    addProp(tr("Bits Per Sample"), QStringLiteral("%1-bit").arg(first.bitsPerSample));
    addProp(tr("Channels"), QString::number(first.channels));
    if (QTableWidgetItem *outCell = m_table->item(rows.first(), 2))
        addProp(tr("Output Path Preview"), outCell->text());

    propsLayout->addWidget(propsTable, 1);

    QPushButton *copyPropsBtn = new QPushButton(tr("Copy Properties to Clipboard"), propsTab);
    connect(copyPropsBtn, &QPushButton::clicked, &dlg, [propsTable]() {
        QStringList lines;
        for (int r = 0; r < propsTable->rowCount(); ++r) {
            lines.append(QStringLiteral("%1: %2")
                             .arg(propsTable->item(r, 0)->text(),
                                  propsTable->item(r, 1)->text()));
        }
        QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    });
    propsLayout->addWidget(copyPropsBtn);

    tabs->addTab(propsTab, tr("&Properties"));
    mainLayout->addWidget(tabs, 1);

    QDialogButtonBox *box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    mainLayout->addWidget(box);

    if (dlg.exec() == QDialog::Accepted) {
        for (int r : rows) {
            TrackItem &t = m_tracks[r];
            if (!multi) {
                t.title = titleEdit->text().trimmed();
                t.trackNumber = trackNumEdit->text().trimmed();
            }
            t.artist = artistEdit->text().trimmed();
            t.album = albumEdit->text().trimmed();
            t.albumArtist = albumArtistEdit->text().trimmed();
            t.totalTracks = totalTracksEdit->text().trimmed();
            t.discNumber = discEdit->text().trimmed();
            t.year = yearEdit->text().trimmed();
            t.genre = genreEdit->text().trimmed();
            t.comment = commentEdit->text().trimmed();
            if (coverModified)
                t.coverArtPath = chosenCoverPath;
            refreshTableRow(r);
        }
        updateOutputPreview();
    }
}

void MainWindow::onReloadMetadata()
{
    QList<int> rows;
    for (const QModelIndex &idx : m_table->selectionModel()->selectedRows())
        rows.append(idx.row());
    if (rows.isEmpty()) {
        for (int r = 0; r < m_tracks.size(); ++r)
            rows.append(r);
    }

    for (int r : rows) {
        if (r >= 0 && r < m_tracks.size() && m_tracks[r].cuePath.isEmpty()) {
            m_tracks[r] = TrackProbe::probeFile(m_tracks[r].sourcePath);
            refreshTableRow(r);
        }
    }
    updateOutputPreview();
}

void MainWindow::onOpenSourceFolder()
{
    int row = m_table->currentRow();
    if (row < 0 || row >= m_tracks.size())
        return;

    QString dirPath = QFileInfo(m_tracks[row].sourcePath).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
}

void MainWindow::onOpenPreferences()
{
    QSettings s(QStringLiteral("qddbconv"), QStringLiteral("qddbconv"));

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Configure qddbconv"));
    dlg.setMinimumWidth(580);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);

    /* 1. External Tools & Directories Group */
    QGroupBox *pathsGroup = new QGroupBox(tr("External Tools && Directories"), &dlg);
    QFormLayout *pathsForm = new QFormLayout(pathsGroup);
    pathsForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    QLineEdit *ffmpegEdit = new QLineEdit(
        s.value(QStringLiteral("ffmpegPath"), QStringLiteral("ffmpeg")).toString(), pathsGroup);
    ffmpegEdit->setToolTip(tr("Path or command name for ffmpeg executable"));

    QLineEdit *ffprobeEdit = new QLineEdit(
        s.value(QStringLiteral("ffprobePath"), QStringLiteral("ffprobe")).toString(), pathsGroup);
    ffprobeEdit->setToolTip(tr("Path or command name for ffprobe executable"));

    QHBoxLayout *tempRow = new QHBoxLayout();
    QLineEdit *tempDirEdit = new QLineEdit(
        s.value(QStringLiteral("tempDir"), QDir::tempPath()).toString(), pathsGroup);
    tempDirEdit->setToolTip(tr("Directory used for temporary decoded WAV files (e.g. /tmp)"));
    QPushButton *browseTempBtn = new QPushButton(tr("Browse..."), pathsGroup);
    connect(browseTempBtn, &QPushButton::clicked, &dlg, [&dlg, tempDirEdit]() {
        QString dir = QFileDialog::getExistingDirectory(
            &dlg, tr("Select Temporary WAV Directory"), tempDirEdit->text());
        if (!dir.isEmpty())
            tempDirEdit->setText(dir);
    });
    tempRow->addWidget(tempDirEdit, 1);
    tempRow->addWidget(browseTempBtn);

    QHBoxLayout *presetsDirRow = new QHBoxLayout();
    QLineEdit *presetsDirEdit = new QLineEdit(PresetManager::userPresetDir(), pathsGroup);
    presetsDirEdit->setPlaceholderText(PresetManager::defaultUserPresetDir());
#ifdef Q_OS_WIN
    presetsDirEdit->setToolTip(
        tr("Directory where custom encoder presets (*.txt) are stored.\n"
           "Default: %LOCALAPPDATA%\\qddbconv\\presets (or <app_folder>\\presets in portable mode)."));
#else
    presetsDirEdit->setToolTip(
        tr("Directory where custom encoder presets (*.txt) are stored.\n"
           "Default: $XDG_DATA_HOME/qddbconv/presets (~/.local/share/qddbconv/presets)."));
#endif
    QPushButton *browsePresetsBtn = new QPushButton(tr("Browse..."), pathsGroup);
    connect(browsePresetsBtn, &QPushButton::clicked, &dlg, [&dlg, presetsDirEdit]() {
        QString startDir = presetsDirEdit->text().trimmed();
        if (startDir.isEmpty())
            startDir = PresetManager::defaultUserPresetDir();
        QString dir = QFileDialog::getExistingDirectory(
            &dlg, tr("Select User Presets Directory"), startDir);
        if (!dir.isEmpty())
            presetsDirEdit->setText(dir);
    });
    QPushButton *defaultPresetsBtn = new QPushButton(tr("Default"), pathsGroup);
    defaultPresetsBtn->setToolTip(tr("Reset to default user presets directory (%1)")
                                      .arg(PresetManager::defaultUserPresetDir()));
    connect(defaultPresetsBtn, &QPushButton::clicked, &dlg, [presetsDirEdit]() {
        presetsDirEdit->setText(PresetManager::defaultUserPresetDir());
    });
    presetsDirRow->addWidget(presetsDirEdit, 1);
    presetsDirRow->addWidget(browsePresetsBtn);
    presetsDirRow->addWidget(defaultPresetsBtn);
#ifdef Q_OS_WIN
    QPushButton *portablePresetsBtn = new QPushButton(tr("Portable (./presets)"), pathsGroup);
    portablePresetsBtn->setToolTip(
        tr("Store custom presets in the 'presets' subfolder inside the qddbconv application folder"));
    connect(portablePresetsBtn, &QPushButton::clicked, &dlg, [presetsDirEdit]() {
        presetsDirEdit->setText(
            QDir::cleanPath(QCoreApplication::applicationDirPath() + QLatin1String("/presets")));
    });
    presetsDirRow->addWidget(portablePresetsBtn);
#endif

    pathsForm->addRow(tr("FFmpeg binary:"), ffmpegEdit);
    pathsForm->addRow(tr("FFprobe binary:"), ffprobeEdit);
    pathsForm->addRow(tr("Temporary WAV directory:"), tempRow);
    pathsForm->addRow(tr("User presets directory:"), presetsDirRow);
    layout->addWidget(pathsGroup);

    /* 2. Cover Art & File Copy Group */
    QGroupBox *coverCopyGroup = new QGroupBox(tr("Cover Art && File Copy"), &dlg);
    QVBoxLayout *coverCopyLayout = new QVBoxLayout(coverCopyGroup);

    QHBoxLayout *resizeCoverRow = new QHBoxLayout();
    QCheckBox *resizeCoverCheck = new QCheckBox(tr("Resize embedded cover art if larger than:"), coverCopyGroup);
    resizeCoverCheck->setChecked(m_resizeCoverArt);
    resizeCoverCheck->setToolTip(tr("Downscale large high-resolution cover scans before embedding into audio files"));
    QSpinBox *maxCoverSpin = new QSpinBox(coverCopyGroup);
    maxCoverSpin->setRange(100, 4096);
    maxCoverSpin->setSingleStep(100);
    maxCoverSpin->setValue(m_maxCoverSizePx);
    maxCoverSpin->setSuffix(tr(" px"));
    maxCoverSpin->setEnabled(m_resizeCoverArt);
    maxCoverSpin->setToolTip(tr("Maximum width or height in pixels for embedded cover art"));
    connect(resizeCoverCheck, &QCheckBox::toggled, maxCoverSpin, &QWidget::setEnabled);
    resizeCoverRow->addWidget(resizeCoverCheck);
    resizeCoverRow->addWidget(maxCoverSpin);
    resizeCoverRow->addStretch();
    coverCopyLayout->addLayout(resizeCoverRow);

    QCheckBox *retagAfterCopyCheck = new QCheckBox(
        tr("Re-tag copied files when bypassing conversion (same format)"), coverCopyGroup);
    retagAfterCopyCheck->setChecked(m_retagAfterCopy);
    retagAfterCopyCheck->setToolTip(
        tr("Rewrite metadata tags and cover art on copied files when 'Copy file without conversion' is active"));
    coverCopyLayout->addWidget(retagAfterCopyCheck);
    layout->addWidget(coverCopyGroup);

    /* 3. Queue & Interface Behavior Group */
    QGroupBox *queueGroup = new QGroupBox(tr("Queue && Interface"), &dlg);
    QVBoxLayout *queueLayout = new QVBoxLayout(queueGroup);
    QCheckBox *confirmClearCheck = new QCheckBox(
        tr("Ask for confirmation before clearing track queue"), queueGroup);
    confirmClearCheck->setChecked(s.value(QStringLiteral("confirmClearList"), false).toBool());
    queueLayout->addWidget(confirmClearCheck);
    layout->addWidget(queueGroup);

    QDialogButtonBox *box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(box);

    if (dlg.exec() == QDialog::Accepted) {
        s.setValue(QStringLiteral("ffmpegPath"), ffmpegEdit->text().trimmed());
        s.setValue(QStringLiteral("ffprobePath"), ffprobeEdit->text().trimmed());
        s.setValue(QStringLiteral("tempDir"), tempDirEdit->text().trimmed());
        QString newPresetsDir = QDir::cleanPath(presetsDirEdit->text().trimmed());
        if (newPresetsDir.isEmpty() || newPresetsDir == PresetManager::defaultUserPresetDir())
            s.remove(QStringLiteral("presetsDir"));
        else
            s.setValue(QStringLiteral("presetsDir"), newPresetsDir);
        m_resizeCoverArt = resizeCoverCheck->isChecked();
        m_maxCoverSizePx = maxCoverSpin->value();
        m_retagAfterCopy = retagAfterCopyCheck->isChecked();
        s.setValue(QStringLiteral("resizeCoverArt"), m_resizeCoverArt);
        s.setValue(QStringLiteral("maxCoverSizePx"), m_maxCoverSizePx);
        s.setValue(QStringLiteral("retagAfterCopy"), m_retagAfterCopy);
        s.setValue(QStringLiteral("confirmClearList"), confirmClearCheck->isChecked());
        m_presets = PresetManager::loadAllPresets();
        populatePresetCombo();
    }
}

void MainWindow::onAbout()
{
    QMessageBox::about(
        this,
        tr("About Qddbconv"),
        tr("<h3>Qddbconv audio converter v%1</h3>"
           "<p><b>Copyright &copy; 2026 goberghen &amp; Arena.ai Agent</b></p>"
           "<p>Licensed under the <b>GNU General Public License v2.0 (GPL-2.0)</b>.</p>"
           "<p>Standalone Qt6 multi-threaded audio converter inspired by the UX and "
           "preset architecture of DeaDBeeF, deadbeef-qt5, and Qmmp.</p>"
           "<p><b>Keyboard Shortcuts:</b><br>"
           "• <b>Ctrl+O</b> — Add Files<br>"
           "• <b>Ctrl+Shift+O</b> — Add Folder<br>"
           "• <b>Ctrl+U</b> — Add CUE Sheet<br>"
           "• <b>Ctrl+I / Double-Click</b> — Edit Track Metadata &amp; Properties<br>"
           "• <b>Ctrl+Shift+R</b> — Reload Metadata from Disk<br>"
           "• <b>Ctrl+Shift+F</b> — View Track in Folder<br>"
           "• <b>Del</b> — Remove Selected<br>"
           "• <b>Ctrl+L</b> — Clear List<br>"
           "• <b>Ctrl+P</b> — Encoder Presets<br>"
           "• <b>Ctrl+M</b> — Toggle Menubar / Hamburger Menu<br>"
           "• <b>F5 / Ctrl+R</b> — Start Conversion<br>"
           "• <b>Ctrl+Q</b> — Quit</p>")
            .arg(QCoreApplication::applicationVersion()));
}

void MainWindow::updateControlTooltips()
{
    m_outFolderEdit->setToolTip(tr("Destination directory:\n%1").arg(m_outFolderEdit->text()));
    m_outFilePatternEdit->setToolTip(
        tr("Current pattern: %1\n\n"
           "Supported tags:\n"
           "  %title%, %artist%, %album%, %albumartist%\n"
           "  %tracknumber%, %totaltracks%, %disc%, %year%, %genre%, %comment%, %filename%\n"
           "  [...] conditional block (included only if inner tag is non-empty)\n"
           "  / creates subdirectories (e.g. %artist%/%album%/%tracknumber%. %title%)")
            .arg(m_outFilePatternEdit->text()));

    int idx = m_encoderCombo->currentIndex();
    if (idx >= 0 && idx < m_presets.size()) {
        const EncoderPreset &p = m_presets[idx];
        m_encoderCombo->setToolTip(
            tr("Preset: %1\nExtension: .%2\nCommand: %3\n%4")
                .arg(p.title,
                     p.ext,
                     p.encoder.isEmpty() ? tr("(Internal RIFF WAVE writer)") : p.encoder,
                     AudioConverter::describePresetExecution(p)));
    }
}

void MainWindow::populatePresetCombo()
{
    QString currentTitle = m_encoderCombo->currentText();
    m_encoderCombo->blockSignals(true);
    m_encoderCombo->clear();
    for (int i = 0; i < m_presets.size(); ++i) {
        const EncoderPreset &p = m_presets[i];
        QString label = p.readonly ? QStringLiteral("[Built-in] %1").arg(p.title) : p.title;
        m_encoderCombo->addItem(label);
        m_encoderCombo->setItemData(
            i,
            tr("Preset: %1\nExtension: .%2\nCommand: %3\n%4")
                .arg(p.title,
                     p.ext,
                     p.encoder.isEmpty() ? tr("(Internal RIFF WAVE writer)") : p.encoder,
                     AudioConverter::describePresetExecution(p)),
            Qt::ToolTipRole);
    }
    int idx = m_encoderCombo->findText(currentTitle);
    if (idx >= 0)
        m_encoderCombo->setCurrentIndex(idx);
    else if (m_encoderCombo->count() > 0)
        m_encoderCombo->setCurrentIndex(0);
    m_encoderCombo->blockSignals(false);
    updateControlTooltips();
}

void MainWindow::loadSettings()
{
    QSettings s(QStringLiteral("qddbconv"), QStringLiteral("qddbconv"));
    QString defaultMusic = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (defaultMusic.isEmpty())
        defaultMusic = QDir::homePath();

    m_outFolderEdit->setText(s.value(QStringLiteral("outputFolder"), defaultMusic).toString());
    m_outFilePatternEdit->setText(
        s.value(QStringLiteral("outputFilePattern"), QStringLiteral("[%tracknumber%. ][%artist% - ]%title%")).toString());
    m_writeToSourceCheck->setChecked(s.value(QStringLiteral("writeToSourceFolder"), false).toBool());
    m_preserveStructureCheck->setChecked(s.value(QStringLiteral("preserveFolderStructure"), false).toBool());
    m_embedCoverCheck->setChecked(s.value(QStringLiteral("embedCoverArt"), true).toBool());
    m_resizeCoverArt = s.value(QStringLiteral("resizeCoverArt"), false).toBool();
    m_maxCoverSizePx = qBound(100, s.value(QStringLiteral("maxCoverSizePx"), 1000).toInt(), 4096);
    m_bypassSameFormatCheck->setChecked(s.value(QStringLiteral("bypassSameFormat"), false).toBool());
    m_retagAfterCopy = s.value(QStringLiteral("retagAfterCopy"), false).toBool();

    int presetIdx = s.value(QStringLiteral("encoderPresetIndex"), 2).toInt();
    if (presetIdx >= 0 && presetIdx < m_encoderCombo->count())
        m_encoderCombo->setCurrentIndex(presetIdx);

    m_sampleFormatCombo->setCurrentIndex(
        qBound(0, s.value(QStringLiteral("sampleFormatIndex"), 0).toInt(), m_sampleFormatCombo->count() - 1));
    m_sampleRateCombo->setCurrentIndex(
        qBound(0, s.value(QStringLiteral("sampleRateIndex"), 0).toInt(), m_sampleRateCombo->count() - 1));
    m_channelsCombo->setCurrentIndex(
        qBound(0, s.value(QStringLiteral("channelsIndex"), 0).toInt(), m_channelsCombo->count() - 1));
    m_overwriteCombo->setCurrentIndex(
        qBound(0, s.value(QStringLiteral("overwriteAction"), 2).toInt(), 2));

    bool showMenu = s.value(QStringLiteral("showMenuBar"), true).toBool();
    m_actShowMenuBar->setChecked(showMenu);
    onToggleMenuBar(showMenu);

    int tbStyle = s.value(QStringLiteral("toolButtonStyle"), static_cast<int>(Qt::ToolButtonTextBesideIcon)).toInt();
    onChangeToolButtonStyle(static_cast<Qt::ToolButtonStyle>(tbStyle));

#ifdef Q_OS_WIN
    QString defaultTheme = QStringLiteral("breeze-light");
#else
    QString defaultTheme = QStringLiteral("system");
#endif
    QString themeId = s.value(QStringLiteral("uiTheme"), defaultTheme).toString();
    onChangeUiTheme(themeId);
}

void MainWindow::saveSettings()
{
    QSettings s(QStringLiteral("qddbconv"), QStringLiteral("qddbconv"));
    s.setValue(QStringLiteral("outputFolder"), m_outFolderEdit->text());
    s.setValue(QStringLiteral("outputFilePattern"), m_outFilePatternEdit->text());
    s.setValue(QStringLiteral("writeToSourceFolder"), m_writeToSourceCheck->isChecked());
    s.setValue(QStringLiteral("preserveFolderStructure"), m_preserveStructureCheck->isChecked());
    s.setValue(QStringLiteral("embedCoverArt"), m_embedCoverCheck->isChecked());
    s.setValue(QStringLiteral("resizeCoverArt"), m_resizeCoverArt);
    s.setValue(QStringLiteral("maxCoverSizePx"), m_maxCoverSizePx);
    s.setValue(QStringLiteral("bypassSameFormat"), m_bypassSameFormatCheck->isChecked());
    s.setValue(QStringLiteral("retagAfterCopy"), m_retagAfterCopy);
    s.setValue(QStringLiteral("encoderPresetIndex"), m_encoderCombo->currentIndex());
    s.setValue(QStringLiteral("sampleFormatIndex"), m_sampleFormatCombo->currentIndex());
    s.setValue(QStringLiteral("sampleRateIndex"), m_sampleRateCombo->currentIndex());
    s.setValue(QStringLiteral("channelsIndex"), m_channelsCombo->currentIndex());
    s.setValue(QStringLiteral("overwriteAction"), m_overwriteCombo->currentIndex());
    s.setValue(QStringLiteral("showMenuBar"), m_actShowMenuBar->isChecked());
    s.setValue(QStringLiteral("toolButtonStyle"), static_cast<int>(m_toolbarStyle));
    s.setValue(QStringLiteral("uiTheme"), m_uiTheme);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }
    if (!paths.isEmpty())
        addInputPaths(paths);
}

void MainWindow::addInputPaths(const QStringList &paths)
{
    m_statusLabel->setText(tr("Probing files..."));
    QApplication::processEvents();

    QList<TrackItem> added = TrackProbe::expandInputs(paths);
    for (const TrackItem &item : added) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_tracks.append(item);

        m_table->setItem(row, 0, new QTableWidgetItem());
        m_table->setItem(row, 1, new QTableWidgetItem());
        m_table->setItem(row, 2, new QTableWidgetItem());
        m_table->setItem(row, 3, new QTableWidgetItem());
        refreshTableRow(row);
    }

    updateOutputPreview();
    m_statusLabel->setText(tr("%1 track(s) in queue").arg(m_tracks.size()));
}

void MainWindow::onAddFiles()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Select Audio Files"),
        QDir::homePath(),
        tr("Audio Files (*.flac *.mp3 *.mp2 *.ogg *.oga *.opus *.spx *.m4a *.m4b *.aac *.mp4 *.wav *.w64 *.wv *.ape *.mpc *.tta *.tak *.shn *.aiff *.aif *.au *.snd *.caf *.voc *.wma *.ac3 *.eac3 *.dts *.thd *.mlp *.amr *.gsm *.mmf *.mid *.midi *.kar *.rmi *.ra *.dsf *.dff *.mka *.webm *.mod *.xm *.it *.s3m *.mptm *.spc *.vgm *.vgz *.nsf *.nsfe *.gbs *.hes *.ay *.sap *.sid *.cue);;All Files (*)"));
    if (!files.isEmpty())
        addInputPaths(files);
}

void MainWindow::onAddFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Folder with Audio Files"), QDir::homePath());
    if (!dir.isEmpty())
        addInputPaths({dir});
}

void MainWindow::onAddCue()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Select CUE Sheets"),
        QDir::homePath(),
        tr("CUE Sheets (*.cue);;All Files (*)"));
    if (!files.isEmpty())
        addInputPaths(files);
}

void MainWindow::onRemoveSelected()
{
    QList<int> rows;
    for (const QModelIndex &idx : m_table->selectionModel()->selectedRows())
        rows.append(idx.row());
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    for (int r : rows) {
        m_table->removeRow(r);
        m_tracks.removeAt(r);
    }
    updateOutputPreview();
    m_statusLabel->setText(tr("%1 track(s) in queue").arg(m_tracks.size()));
}

void MainWindow::onClearList()
{
    if (m_tracks.isEmpty())
        return;

    QSettings s(QStringLiteral("qddbconv"), QStringLiteral("qddbconv"));
    if (s.value(QStringLiteral("confirmClearList"), false).toBool()) {
        if (QMessageBox::question(this, tr("Clear List"),
                tr("Remove all %1 track(s) from the queue?").arg(m_tracks.size())) != QMessageBox::Yes) {
            return;
        }
    }

    m_table->setRowCount(0);
    m_tracks.clear();
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("Ready"));
}

void MainWindow::onBrowseOutputFolder()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Output Folder"), m_outFolderEdit->text());
    if (!dir.isEmpty())
        m_outFolderEdit->setText(dir);
}

void MainWindow::onManagePresets()
{
    PresetListDialog dlg(m_presets, this);
    dlg.exec();
    m_presets = dlg.presets();
    populatePresetCombo();
    updateOutputPreview();
}

ConverterSettings MainWindow::gatherSettings() const
{
    ConverterSettings s;
    s.outputFolder = m_outFolderEdit->text().trimmed();
    s.outputFilePattern = m_outFilePatternEdit->text().trimmed();
    s.writeToSourceFolder = m_writeToSourceCheck->isChecked();
    s.preserveFolderStructure = m_preserveStructureCheck->isChecked();
    s.embedCoverArt = m_embedCoverCheck->isChecked();
    s.resizeCoverArt = m_resizeCoverArt;
    s.maxCoverSizePx = m_maxCoverSizePx;
    s.bypassSameFormat = m_bypassSameFormatCheck->isChecked();
    s.retagAfterCopy = m_retagAfterCopy;

    int fmtCode = m_sampleFormatCombo->currentData().toInt();
    if (fmtCode == 0) {
        s.outputBps = -1;
        s.outputIsFloat = false;
    } else if (fmtCode == 33) {
        s.outputBps = 32;
        s.outputIsFloat = true;
    } else {
        s.outputBps = fmtCode;
        s.outputIsFloat = false;
    }

    /* Parse sample rate either from item data or custom user text */
    int sr = 0;
    QString srText = m_sampleRateCombo->currentText().trimmed();
    QRegularExpression numRe(QStringLiteral("^(\\d+)"));
    QRegularExpressionMatch m = numRe.match(srText);
    if (m.hasMatch())
        sr = m.captured(1).toInt();
    s.targetSampleRate = (sr >= 1000 && sr <= 768000) ? sr : 0;

    s.targetChannels = m_channelsCombo->currentData().toInt();
    s.overwriteAction = m_overwriteCombo->currentData().toInt();

    int presetIdx = m_encoderCombo->currentIndex();
    if (presetIdx >= 0 && presetIdx < m_presets.size())
        s.encoderPreset = m_presets[presetIdx];

    return s;
}

void MainWindow::updateOutputPreview()
{
    if (m_tracks.isEmpty())
        return;

    ConverterSettings s = gatherSettings();
    QString rootFolder = s.preserveFolderStructure
        ? TrackProbe::commonRootFolder(m_tracks)
        : QString();

    for (int i = 0; i < m_tracks.size(); ++i) {
        QString outPath = TrackProbe::computeOutputPath(
            m_tracks[i],
            i,
            s.outputFolder,
            s.outputFilePattern,
            s.encoderPreset,
            s.preserveFolderStructure,
            rootFolder,
            s.writeToSourceFolder);
        if (QTableWidgetItem *cell = m_table->item(i, 2)) {
            cell->setText(outPath);
            cell->setToolTip(outPath);
        }
    }
}

void MainWindow::onStartConversion()
{
    if (m_tracks.isEmpty()) {
        QMessageBox::information(this, tr("No Tracks"),
            tr("Please add audio files, folders, or .cue sheets first."));
        return;
    }

    saveSettings();
    ConverterSettings settings = gatherSettings();
    QString rootFolder = settings.preserveFolderStructure
        ? TrackProbe::commonRootFolder(m_tracks)
        : QString();

    /* Pre-check overwrite policy */
    QVector<QString> targetPaths(m_tracks.size());
    QVector<bool> skipFlags(m_tracks.size(), false);

    for (int i = 0; i < m_tracks.size(); ++i) {
        QString outPath = TrackProbe::computeOutputPath(
            m_tracks[i],
            i,
            settings.outputFolder,
            settings.outputFilePattern,
            settings.encoderPreset,
            settings.preserveFolderStructure,
            rootFolder,
            settings.writeToSourceFolder);
        targetPaths[i] = outPath;

        if (QFileInfo(outPath).absoluteFilePath() == QFileInfo(m_tracks[i].sourcePath).absoluteFilePath()) {
            skipFlags[i] = true;
            continue;
        }

        if (QFileInfo::exists(outPath)) {
            if (settings.overwriteAction == 1) {
                skipFlags[i] = true;
            } else if (settings.overwriteAction == 0) {
                auto ans = QMessageBox::question(
                    this,
                    tr("Converter Warning"),
                    tr("The file already exists. Overwrite?\n%1").arg(outPath),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
                if (ans == QMessageBox::Cancel)
                    return;
                if (ans == QMessageBox::No)
                    skipFlags[i] = true;
            }
        }
    }

    m_cancelled.store(false);
    m_activeTasks = m_tracks.size();
    m_completedTasks = 0;
    m_failedTasks = 0;

    m_progressBar->setRange(0, m_tracks.size());
    m_progressBar->setValue(0);
    m_convertBtn->setEnabled(false);
    m_actStartConvert->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_actCancelConvert->setEnabled(true);
    m_statusLabel->setText(tr("Converting 0/%1...").arg(m_activeTasks));

    QThreadPool::globalInstance()->setMaxThreadCount(m_threadsSpin->value());

    for (int i = 0; i < m_tracks.size(); ++i) {
        const TrackItem track = m_tracks[i];
        const QString outPath = targetPaths[i];
        const bool skip = skipFlags[i];

        if (skip) {
            onTrackStatusUpdated(i, tr("Skipped"), true, true);
            continue;
        }

        onTrackStatusUpdated(i, tr("Queued..."), false, true);

        QThreadPool::globalInstance()->start([this, i, track, outPath, settings]() {
            if (m_cancelled.load()) {
                emit trackStatusChanged(i, tr("Cancelled"), true, false);
                return;
            }

            emit trackStatusChanged(i, tr("Converting..."), false, true);

            QString err;
            bool ok = AudioConverter::convertTrack(track, outPath, settings, &m_cancelled, err);
            QString status;
            if (ok) {
                status = err.isEmpty()
                    ? tr("Done")
                    : tr("Done (fallback: %1)").arg(err);
            } else {
                status = m_cancelled.load()
                    ? tr("Cancelled")
                    : tr("Error: %1").arg(err);
            }

            emit trackStatusChanged(i, status, true, ok);
        });
    }
}

void MainWindow::onCancelConversion()
{
    m_cancelled.store(true);
    m_statusLabel->setText(tr("Cancelling..."));
}

void MainWindow::onTrackStatusUpdated(int row, const QString &status, bool finished, bool ok)
{
    if (row >= 0 && row < m_table->rowCount()) {
        m_tracks[row].statusText = status;
        if (QTableWidgetItem *cell = m_table->item(row, 3)) {
            cell->setText(status);
            cell->setToolTip(status);
        }
    }

    if (!finished)
        return;

    ++m_completedTasks;
    if (!ok)
        ++m_failedTasks;

    m_progressBar->setValue(m_completedTasks);
    m_statusLabel->setText(tr("Converting %1/%2...").arg(m_completedTasks).arg(m_activeTasks));

    if (m_completedTasks >= m_activeTasks) {
        m_convertBtn->setEnabled(true);
        m_actStartConvert->setEnabled(true);
        m_cancelBtn->setEnabled(false);
        m_actCancelConvert->setEnabled(false);
        if (m_cancelled.load()) {
            m_statusLabel->setText(tr("Cancelled (%1/%2 completed)").arg(m_completedTasks - m_failedTasks).arg(m_activeTasks));
        } else if (m_failedTasks > 0) {
            m_statusLabel->setText(tr("Finished with %1 error(s) (hover Status column for details)").arg(m_failedTasks));
            QStringList errLines;
            for (int i = 0; i < m_tracks.size() && errLines.size() < 10; ++i) {
                if (m_tracks[i].statusText.startsWith(QLatin1String("Error:"))) {
                    errLines.append(QStringLiteral("• %1: %2")
                                        .arg(QFileInfo(m_tracks[i].sourcePath).fileName(),
                                             m_tracks[i].statusText));
                }
            }
            if (m_failedTasks > errLines.size()) {
                errLines.append(tr("...and %1 more track(s).").arg(m_failedTasks - errLines.size()));
            }
            QMessageBox::warning(
                this,
                tr("Conversion Completed with Errors"),
                tr("%1 of %2 track(s) failed to convert:\n\n%3\n\n"
                   "Note: Qddbconv automatically attempts fallbacks (standalone CLI encoder -> FFmpeg library encoder -> FFmpeg native encoder, "
                   "and openmpt123/gme2wav for tracker/chiptune files). Check that the required codec or tool is installed.")
                    .arg(m_failedTasks)
                    .arg(m_activeTasks)
                    .arg(errLines.join(QLatin1Char('\n'))));
        } else {
            m_statusLabel->setText(tr("Finished all %1 track(s) successfully").arg(m_activeTasks));
        }
    }
}
