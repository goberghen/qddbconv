/*
 * Qddbconv - Standalone Qt6 Audio Converter
 * Copyright (C) 2026 goberghen & Arena.ai Agent
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QDDBCONV_MAINWINDOW_H
#define QDDBCONV_MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <atomic>

#include "preset.h"
#include "track.h"
#include "converter.h"

class QAction;
class QActionGroup;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QTableWidget;
class QToolButton;
class QProgressBar;
class QPushButton;
class QLabel;
class QMenu;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QStringList &initialPaths = {}, QWidget *parent = nullptr);
    ~MainWindow() override;

    void addInputPaths(const QStringList &paths);

signals:
    void trackStatusChanged(int row, const QString &status, bool finished, bool ok);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onAddFiles();
    void onAddFolder();
    void onAddCue();
    void onRemoveSelected();
    void onClearList();

    void onEditTrackMetadata();
    void onReloadMetadata();
    void onOpenSourceFolder();

    void onBrowseOutputFolder();
    void onManagePresets();
    void onOpenPreferences();
    void onAbout();
    void onToggleMenuBar(bool visible);
    void onChangeToolButtonStyle(Qt::ToolButtonStyle style);
    void updateOutputPreview();

    void onStartConversion();
    void onCancelConversion();
    void onTrackStatusUpdated(int row, const QString &status, bool finished, bool ok);

private:
    void setupActionsAndMenus();
    void setupUi();
    void loadSettings();
    void saveSettings();
    void populatePresetCombo();
    void updateControlTooltips();
    void refreshTableRow(int row);
    ConverterSettings gatherSettings() const;

    QList<EncoderPreset> m_presets;
    QList<TrackItem> m_tracks;

    /* Actions & Menus (KDE HIG style with Ctrl+M hamburger toggle & toolbar text modes) */
    QAction *m_actAddFiles = nullptr;
    QAction *m_actAddFolder = nullptr;
    QAction *m_actAddCue = nullptr;
    QAction *m_actRemoveSelected = nullptr;
    QAction *m_actClearList = nullptr;
    QAction *m_actSelectAll = nullptr;
    QAction *m_actEditMetadata = nullptr;
    QAction *m_actReloadMetadata = nullptr;
    QAction *m_actOpenSourceFolder = nullptr;
    QAction *m_actStartConvert = nullptr;
    QAction *m_actCancelConvert = nullptr;
    QAction *m_actPresets = nullptr;
    QAction *m_actPreferences = nullptr;
    QAction *m_actShowMenuBar = nullptr;
    QAction *m_actAbout = nullptr;
    QAction *m_actQuit = nullptr;

    QActionGroup *m_toolbarStyleGroup = nullptr;
    Qt::ToolButtonStyle m_toolbarStyle = Qt::ToolButtonTextBesideIcon;

    QList<QToolButton *> m_toolbarButtons;
    QMenu *m_hamburgerMenu = nullptr;
    QToolButton *m_hamburgerBtn = nullptr;

    /* Left panel controls (DeaDBeeF Converter settings layout) */
    QLineEdit *m_outFolderEdit = nullptr;
    QPushButton *m_browseFolderBtn = nullptr;
    QCheckBox *m_writeToSourceCheck = nullptr;
    QCheckBox *m_preserveStructureCheck = nullptr;
    QLineEdit *m_outFilePatternEdit = nullptr;
    QComboBox *m_encoderCombo = nullptr;
    QPushButton *m_presetsBtn = nullptr;
    QComboBox *m_sampleFormatCombo = nullptr;
    QComboBox *m_sampleRateCombo = nullptr;
    QComboBox *m_channelsCombo = nullptr;
    QCheckBox *m_embedCoverCheck = nullptr;
    QCheckBox *m_resizeCoverCheck = nullptr;
    QSpinBox *m_maxCoverSpin = nullptr;
    QCheckBox *m_bypassSameFormatCheck = nullptr;
    QCheckBox *m_retagAfterCopyCheck = nullptr;
    QComboBox *m_overwriteCombo = nullptr;
    QSpinBox *m_threadsSpin = nullptr;

    /* Right panel controls (Track list + Live Output Path Preview) */
    QTableWidget *m_table = nullptr;

    /* Bottom bar controls */
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_convertBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;

    std::atomic<bool> m_cancelled{false};
    int m_activeTasks = 0;
    int m_completedTasks = 0;
    int m_failedTasks = 0;
};

#endif /* QDDBCONV_MAINWINDOW_H */
