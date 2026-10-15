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
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QThreadPool>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(const QStringList &initialPaths, QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Qddbconv audio converter %1").arg(QCoreApplication::applicationVersion()));
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

    /* Settings actions */
    m_actPresets = new QAction(QIcon::fromTheme(QStringLiteral("configure")),
                               tr("Encoder Presets..."), this);
    m_actPresets->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    m_actPresets->setToolTip(tr("Open Encoder Presets manager (Ctrl+P)"));
    connect(m_actPresets, &QAction::triggered, this, &MainWindow::onManagePresets);

    m_actPreferences = new QAction(QIcon::fromTheme(QStringLiteral("preferences-system")),
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

    QMenu *settingsMenu = mb->addMenu(tr("&Settings"));
    settingsMenu->addAction(m_actPresets);
    settingsMenu->addAction(m_actPreferences);
    settingsMenu->addSeparator();
    settingsMenu->addMenu(toolbarTextMenu);
    settingsMenu->addAction(m_actShowMenuBar);

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
    auto makeToolBtn = [this](QAction *act) {
        QToolButton *btn = new QToolButton(this);
        btn->setDefaultAction(act);
        btn->setToolButtonStyle(m_toolbarStyle);
        btn->setAutoRaise(false);
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
    m_hamburgerBtn->setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
    m_hamburgerBtn->setText(tr("Menu"));
    m_hamburgerBtn->setToolButtonStyle(m_toolbarStyle);
    m_hamburgerBtn->setPopupMode(QToolButton::InstantPopup);
    m_hamburgerBtn->setMenu(m_hamburgerMenu);
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

    QHBoxLayout *presetRow = new QHBoxLayout();
    m_encoderCombo = new QComboBox(encGroup);
    m_encoderCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_presetsBtn = new QPushButton(tr("Presets..."), encGroup);
    m_presetsBtn->setToolTip(tr("Add, edit, copy, or remove encoder presets (Ctrl+P)"));
    presetRow->addWidget(m_encoderCombo, 1);
    presetRow->addWidget(m_presetsBtn);
    encForm->addRow(tr("Encoder:"), presetRow);

    m_sampleFormatCombo = new QComboBox(encGroup);
    m_sampleFormatCombo->addItem(tr("Keep source format"), 0);
    m_sampleFormatCombo->addItem(tr("8-bit unsigned int"), 8);
    m_sampleFormatCombo->addItem(tr("16-bit signed int"), 16);
    m_sampleFormatCombo->addItem(tr("24-bit signed int"), 24);
    m_sampleFormatCombo->addItem(tr("32-bit signed int"), 32);
    m_sampleFormatCombo->addItem(tr("32-bit IEEE float"), 33);
    m_sampleFormatCombo->setToolTip(tr("Select target PCM bit depth passed to the encoder (or keep original bit depth)"));
    encForm->addRow(tr("Output sample format:"), m_sampleFormatCombo);

    m_sampleRateCombo = new QComboBox(encGroup);
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
    m_channelsCombo->addItem(tr("Keep source channels"), 0);
    m_channelsCombo->addItem(tr("Stereo (2.0)"), 2);
    m_channelsCombo->addItem(tr("Mono (1.0)"), 1);
    m_channelsCombo->setToolTip(tr("Keep original channel count or downmix/upmix to Stereo or Mono"));
    encForm->addRow(tr("Channels:"), m_channelsCombo);

    leftLayout->addWidget(encGroup);

    /* 3. File Handling, Cover Art & Threading Group */
    QGroupBox *optGroup = new QGroupBox(tr("Options && Cover Art"), leftPane);
    QVBoxLayout *optLayout = new QVBoxLayout(optGroup);

    m_embedCoverCheck = new QCheckBox(tr("Embed cover art (from source tags or album folder)"), optGroup);
    m_embedCoverCheck->setChecked(true);
    m_embedCoverCheck->setToolTip(
        tr("Automatically embed cover art from source file tags or cover.jpg/folder.jpg/front.png in the track directory"));
    optLayout->addWidget(m_embedCoverCheck);

    QHBoxLayout *resizeCoverRow = new QHBoxLayout();
    m_resizeCoverCheck = new QCheckBox(tr("Resize cover if larger than:"), optGroup);
    m_resizeCoverCheck->setToolTip(tr("Downscale large high-resolution cover scans before embedding"));
    m_maxCoverSpin = new QSpinBox(optGroup);
    m_maxCoverSpin->setRange(100, 4096);
    m_maxCoverSpin->setSingleStep(100);
    m_maxCoverSpin->setValue(1000);
    m_maxCoverSpin->setSuffix(tr(" px"));
    m_maxCoverSpin->setToolTip(tr("Maximum width or height in pixels for embedded cover art"));
    resizeCoverRow->addWidget(m_resizeCoverCheck);
    resizeCoverRow->addWidget(m_maxCoverSpin);
    resizeCoverRow->addStretch();
    optLayout->addLayout(resizeCoverRow);

    m_bypassSameFormatCheck = new QCheckBox(tr("Copy file without conversion if format is not changing"), optGroup);
    m_bypassSameFormatCheck->setToolTip(
        tr("Copy file without conversion if the source extension matches the target preset extension"));

    m_retagAfterCopyCheck = new QCheckBox(tr("Re-tag after copy"), optGroup);
    m_retagAfterCopyCheck->setToolTip(tr("Rewrite metadata tags on copied files when bypassing conversion"));

    optLayout->addWidget(m_bypassSameFormatCheck);
    optLayout->addWidget(m_retagAfterCopyCheck);

    QFormLayout *optForm = new QFormLayout();
    m_overwriteCombo = new QComboBox(optGroup);
    m_overwriteCombo->addItem(tr("Prompt"), 0);
    m_overwriteCombo->addItem(tr("Skip existing file"), 1);
    m_overwriteCombo->addItem(tr("Overwrite existing file"), 2);
    m_overwriteCombo->setToolTip(tr("Action to take when the destination file already exists on disk"));
    optForm->addRow(tr("When file exists:"), m_overwriteCombo);

    m_threadsSpin = new QSpinBox(optGroup);
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
    connect(m_presetsBtn, &QPushButton::clicked, this, &MainWindow::onManagePresets);
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

void MainWindow::refreshTableRow(int row)
{
    if (row < 0 || row >= m_tracks.size())
        return;

    const TrackItem &item = m_tracks[row];
    QString srcDisplay = QFileInfo(item.sourcePath).fileName();
    if (!item.cuePath.isEmpty())
        srcDisplay = QStringLiteral("%1 [Track %2]").arg(QFileInfo(item.cuePath).fileName(), item.trackNumber);

    QString srcTooltip = item.cuePath.isEmpty()
        ? tr("Source file: %1\nCodec: %2 (%3 Hz, %4-bit, %5 ch)\nDuration: %6 s")
              .arg(item.sourcePath,
                   item.codecName.isEmpty() ? QStringLiteral("audio") : item.codecName)
              .arg(item.sampleRate)
              .arg(item.bitsPerSample)
              .arg(item.channels)
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
            tr("Images (*.jpg *.jpeg *.png *.webp *.bmp);;All Files (*)"));
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
                m_resizeCoverCheck->isChecked(),
                m_maxCoverSpin->value(),
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
    int mins = static_cast<int>(first.durationSec) / 60;
    int secs = static_cast<int>(first.durationSec) % 60;
    addProp(tr("Duration"), QStringLiteral("%1:%2 (%3 s)")
                                .arg(mins)
                                .arg(secs, 2, 10, QLatin1Char('0'))
                                .arg(first.durationSec, 0, 'f', 2));
    addProp(tr("Codec"), first.codecName.isEmpty() ? QStringLiteral("Unknown") : first.codecName);
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
    dlg.setMinimumWidth(480);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    QFormLayout *form = new QFormLayout();

    QLineEdit *ffmpegEdit = new QLineEdit(
        s.value(QStringLiteral("ffmpegPath"), QStringLiteral("ffmpeg")).toString(), &dlg);
    ffmpegEdit->setToolTip(tr("Path or command name for ffmpeg executable"));

    QLineEdit *ffprobeEdit = new QLineEdit(
        s.value(QStringLiteral("ffprobePath"), QStringLiteral("ffprobe")).toString(), &dlg);
    ffprobeEdit->setToolTip(tr("Path or command name for ffprobe executable"));

    QLineEdit *tempDirEdit = new QLineEdit(
        s.value(QStringLiteral("tempDir"), QDir::tempPath()).toString(), &dlg);
    tempDirEdit->setToolTip(tr("Directory used for temporary decoded WAV files (e.g. /tmp)"));

    QCheckBox *confirmClearCheck = new QCheckBox(tr("Ask for confirmation before clearing track queue"), &dlg);
    confirmClearCheck->setChecked(s.value(QStringLiteral("confirmClearList"), false).toBool());

    form->addRow(tr("FFmpeg binary:"), ffmpegEdit);
    form->addRow(tr("FFprobe binary:"), ffprobeEdit);
    form->addRow(tr("Temporary WAV directory:"), tempDirEdit);
    form->addRow(QString(), confirmClearCheck);
    layout->addLayout(form);

    QDialogButtonBox *box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(box);

    if (dlg.exec() == QDialog::Accepted) {
        s.setValue(QStringLiteral("ffmpegPath"), ffmpegEdit->text().trimmed());
        s.setValue(QStringLiteral("ffprobePath"), ffprobeEdit->text().trimmed());
        s.setValue(QStringLiteral("tempDir"), tempDirEdit->text().trimmed());
        s.setValue(QStringLiteral("confirmClearList"), confirmClearCheck->isChecked());
    }
}

void MainWindow::onAbout()
{
    QMessageBox::about(
        this,
        tr("About Qddbconv"),
        tr("<h3>Qddbconv audio converter %1</h3>"
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
            tr("Preset: %1\nExtension: .%2\nCommand: %3")
                .arg(p.title, p.ext, p.encoder.isEmpty() ? tr("(Internal RIFF WAVE writer)") : p.encoder));
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
            tr("Preset: %1\nExtension: .%2\nCommand: %3")
                .arg(p.title, p.ext, p.encoder.isEmpty() ? tr("(Internal RIFF WAVE writer)") : p.encoder),
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
    m_resizeCoverCheck->setChecked(s.value(QStringLiteral("resizeCoverArt"), false).toBool());
    m_maxCoverSpin->setValue(qBound(100, s.value(QStringLiteral("maxCoverSizePx"), 1000).toInt(), 4096));
    m_bypassSameFormatCheck->setChecked(s.value(QStringLiteral("bypassSameFormat"), false).toBool());
    m_retagAfterCopyCheck->setChecked(s.value(QStringLiteral("retagAfterCopy"), false).toBool());

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
}

void MainWindow::saveSettings()
{
    QSettings s(QStringLiteral("qddbconv"), QStringLiteral("qddbconv"));
    s.setValue(QStringLiteral("outputFolder"), m_outFolderEdit->text());
    s.setValue(QStringLiteral("outputFilePattern"), m_outFilePatternEdit->text());
    s.setValue(QStringLiteral("writeToSourceFolder"), m_writeToSourceCheck->isChecked());
    s.setValue(QStringLiteral("preserveFolderStructure"), m_preserveStructureCheck->isChecked());
    s.setValue(QStringLiteral("embedCoverArt"), m_embedCoverCheck->isChecked());
    s.setValue(QStringLiteral("resizeCoverArt"), m_resizeCoverCheck->isChecked());
    s.setValue(QStringLiteral("maxCoverSizePx"), m_maxCoverSpin->value());
    s.setValue(QStringLiteral("bypassSameFormat"), m_bypassSameFormatCheck->isChecked());
    s.setValue(QStringLiteral("retagAfterCopy"), m_retagAfterCopyCheck->isChecked());
    s.setValue(QStringLiteral("encoderPresetIndex"), m_encoderCombo->currentIndex());
    s.setValue(QStringLiteral("sampleFormatIndex"), m_sampleFormatCombo->currentIndex());
    s.setValue(QStringLiteral("sampleRateIndex"), m_sampleRateCombo->currentIndex());
    s.setValue(QStringLiteral("channelsIndex"), m_channelsCombo->currentIndex());
    s.setValue(QStringLiteral("overwriteAction"), m_overwriteCombo->currentIndex());
    s.setValue(QStringLiteral("showMenuBar"), m_actShowMenuBar->isChecked());
    s.setValue(QStringLiteral("toolButtonStyle"), static_cast<int>(m_toolbarStyle));
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
    s.resizeCoverArt = m_resizeCoverCheck->isChecked();
    s.maxCoverSizePx = m_maxCoverSpin->value();
    s.bypassSameFormat = m_bypassSameFormatCheck->isChecked();
    s.retagAfterCopy = m_retagAfterCopyCheck->isChecked();

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
            QString status = ok
                ? tr("Done")
                : (m_cancelled.load() ? tr("Cancelled") : tr("Error: %1").arg(err));

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
        } else {
            m_statusLabel->setText(tr("Finished all %1 track(s) successfully").arg(m_activeTasks));
        }
    }
}
