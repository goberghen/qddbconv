/*
 * Qddbconv - Standalone Qt6 Audio Converter
 * Copyright (C) 2026 goberghen & Arena.ai Agent
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "preset.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QSet>

bool EncoderPreset::loadFromFile(const QString &filePath, EncoderPreset &out)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    EncoderPreset p;
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;

        int sp = line.indexOf(QLatin1Char(' '));
        QString key = (sp >= 0) ? line.left(sp) : line;
        QString val = (sp >= 0) ? line.mid(sp + 1) : QString();

        if (key == QLatin1String("title"))
            p.title = val;
        else if (key == QLatin1String("ext"))
            p.ext = val;
        else if (key == QLatin1String("encoder"))
            p.encoder = val;
        else if (key == QLatin1String("method"))
            p.method = val.toInt();
        else if (key == QLatin1String("id3v2_version"))
            p.id3v2_version = val.toInt();
        else if (key == QLatin1String("tag_id3v2"))
            p.tag_id3v2 = (val.toInt() != 0);
        else if (key == QLatin1String("tag_id3v1"))
            p.tag_id3v1 = (val.toInt() != 0);
        else if (key == QLatin1String("tag_apev2"))
            p.tag_apev2 = (val.toInt() != 0);
        else if (key == QLatin1String("tag_flac"))
            p.tag_flac = (val.toInt() != 0);
        else if (key == QLatin1String("tag_oggvorbis"))
            p.tag_oggvorbis = (val.toInt() != 0);
        else if (key == QLatin1String("tag_mp4"))
            p.tag_mp4 = (val.toInt() != 0);
    }

    if (p.title.isEmpty())
        p.title = QFileInfo(filePath).completeBaseName();

    out = p;
    return true;
}

bool EncoderPreset::saveToFile(const QString &dirPath) const
{
    if (title.trimmed().isEmpty())
        return false;

    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QLatin1String(".")))
        return false;

    QString safeName = title;
    safeName.replace(QLatin1Char('/'), QLatin1Char('_'));
    safeName.replace(QLatin1Char('\\'), QLatin1Char('_'));

    QFile f(dir.filePath(safeName + QLatin1String(".txt")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;

    QTextStream out(&f);
    out << "title " << title << "\n";
    out << "ext " << ext << "\n";
    out << "encoder " << encoder << "\n";
    out << "method " << method << "\n";
    out << "id3v2_version " << id3v2_version << "\n";
    out << "tag_id3v2 " << (tag_id3v2 ? 1 : 0) << "\n";
    out << "tag_id3v1 " << (tag_id3v1 ? 1 : 0) << "\n";
    out << "tag_apev2 " << (tag_apev2 ? 1 : 0) << "\n";
    out << "tag_flac " << (tag_flac ? 1 : 0) << "\n";
    out << "tag_oggvorbis " << (tag_oggvorbis ? 1 : 0) << "\n";
    out << "tag_mp4 " << (tag_mp4 ? 1 : 0) << "\n";
    return true;
}

QString PresetManager::userPresetDir()
{
    QString cfg = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (cfg.isEmpty())
        cfg = QDir::homePath() + QLatin1String("/.config");
    /* Share user encoder presets directory with DeaDBeeF for 100% compatibility */
    return cfg + QLatin1String("/deadbeef/presets/encoders");
}

QList<EncoderPreset> PresetManager::builtinPresets()
{
    QList<EncoderPreset> list;

    auto add = [&](const char *title, const char *ext, const char *enc,
                   int method, bool id3v2, bool id3v1, bool ape, bool flac, bool ogg, bool mp4, int id3v2_ver = 0) {
        EncoderPreset p;
        p.title = QString::fromUtf8(title);
        p.ext = QString::fromUtf8(ext);
        p.encoder = QString::fromUtf8(enc);
        p.method = method;
        p.tag_id3v2 = id3v2;
        p.tag_id3v1 = id3v1;
        p.tag_apev2 = ape;
        p.tag_flac = flac;
        p.tag_oggvorbis = ogg;
        p.tag_mp4 = mp4;
        p.id3v2_version = id3v2_ver;
        p.readonly = true;
        list.append(p);
    };

    /* Exact built-in presets shipped with DeaDBeeF's converter plugin + FFmpeg universal presets */
    add("FLAC (compression level 5)", "flac",
        "flac -f -o %o -5 -",
        DDB_ENCODER_METHOD_PIPE, false, false, false, true, false, false, 1);

    add("FLAC (compression level 8 - max)", "flac",
        "flac -f -o %o -8 -",
        DDB_ENCODER_METHOD_PIPE, false, false, false, true, false, false, 1);

    add("MP3 CBR 320 Kbps (Lame)", "mp3",
        "lame -q4 -b320 - %o",
        DDB_ENCODER_METHOD_PIPE, true, true, false, false, false, false, 0);

    add("MP3 VBR 192 Kbps / V2 (Lame)", "mp3",
        "lame -V2 - %o",
        DDB_ENCODER_METHOD_PIPE, true, true, false, false, false, false, 0);

    add("MP3 CBR 128 Kbps (Lame)", "mp3",
        "lame -b128 - %o",
        DDB_ENCODER_METHOD_PIPE, true, true, false, false, false, false, 0);

    add("MP3 CBR 320 Kbps (FFmpeg)", "mp3",
        "ffmpeg -y -i %i -c:a libmp3lame -b:a 320k %o",
        DDB_ENCODER_METHOD_FILE, true, true, false, false, false, false, 0);

    add("Opus 96 Kbps (opusenc)", "opus",
        "opusenc --bitrate 96 - %o",
        DDB_ENCODER_METHOD_PIPE, false, false, false, false, true, false, 0);

    add("Opus 128 Kbps (opusenc)", "opus",
        "opusenc --bitrate 128 - %o",
        DDB_ENCODER_METHOD_PIPE, false, false, false, false, true, false, 0);

    add("Opus 192 Kbps (opusenc)", "opus",
        "opusenc --bitrate 192 - %o",
        DDB_ENCODER_METHOD_PIPE, false, false, false, false, true, false, 0);

    add("Opus 256 Kbps (opusenc)", "opus",
        "opusenc --bitrate 256 - %o",
        DDB_ENCODER_METHOD_PIPE, false, false, false, false, true, false, 0);

    add("Opus 320 Kbps (opusenc)", "opus",
        "opusenc --bitrate 320 - %o",
        DDB_ENCODER_METHOD_PIPE, false, false, false, false, true, false, 0);

    add("Opus 160 Kbps (FFmpeg)", "opus",
        "ffmpeg -y -i %i -c:a libopus -b:a 160k %o",
        DDB_ENCODER_METHOD_FILE, false, false, false, false, true, false, 0);

    add("Ogg Vorbis VBR 160 Kbps (oggenc -q 5)", "ogg",
        "oggenc -q 5 -o %o -",
        DDB_ENCODER_METHOD_PIPE, false, false, false, false, true, false, 0);

    add("Ogg Vorbis VBR 192 Kbps (oggenc)", "ogg",
        "oggenc -q 6 -o %o -",
        DDB_ENCODER_METHOD_PIPE, false, false, false, false, true, false, 0);

    add("Ogg Vorbis VBR 256 Kbps (oggenc)", "ogg",
        "oggenc -q 8 -o %o -",
        DDB_ENCODER_METHOD_PIPE, false, false, false, false, true, false, 0);

    add("Ogg Vorbis VBR 320 Kbps (oggenc)", "ogg",
        "oggenc -q 9 -o %o -",
        DDB_ENCODER_METHOD_PIPE, false, false, false, false, true, false, 0);

    add("Ogg FLAC", "ogg",
        "flac -f --ogg -o %o -5 -",
        DDB_ENCODER_METHOD_PIPE, false, false, false, false, true, false, 0);

    add("AAC 256 Kbps (FFmpeg M4A)", "m4a",
        "ffmpeg -y -i %i -c:a aac -b:a 256k %o",
        DDB_ENCODER_METHOD_FILE, false, false, false, false, false, true, 0);

    add("AAC VBR 192 Kbps (Nero / FAAC)", "m4a",
        "faac -q 150 -o %o -w -",
        DDB_ENCODER_METHOD_PIPE, false, false, false, false, false, true, 0);

    add("ALAC (Apple Lossless M4A)", "m4a",
        "ffmpeg -y -i %i -acodec alac %o",
        DDB_ENCODER_METHOD_FILE, false, false, false, false, false, true, 0);

    add("WavPack", "wv",
        "wavpack -y -i - -o %o",
        DDB_ENCODER_METHOD_PIPE, false, false, true, false, false, false, 0);

    add("MusePack", "mpc",
        "mpcenc --overwrite - %o",
        DDB_ENCODER_METHOD_PIPE, false, false, true, false, false, false, 0);

    add("TTA (True Audio)", "tta",
        "ttaenc -e -o %o -",
        DDB_ENCODER_METHOD_PIPE, false, false, true, false, false, false, 0);

    add("AC3 / Dolby Digital 384 Kbps (FFmpeg)", "ac3",
        "ffmpeg -y -i %i -c:a ac3 -b:a 384k %o",
        DDB_ENCODER_METHOD_FILE, false, false, false, false, false, false, 0);

    add("AIFF (uncompressed)", "aiff",
        "ffmpeg -y -i %i -c:a pcm_s16be %o",
        DDB_ENCODER_METHOD_FILE, true, false, false, false, false, false, 0);

    add("MS RIFF WAV (uncompressed)", "wav",
        "",
        DDB_ENCODER_METHOD_FILE, false, false, false, false, false, false, 0);

    add("Standard MIDI (from Yamaha SMAF .mmf)", "mid",
        "@mmf2mid",
        DDB_ENCODER_METHOD_FILENAME, false, false, false, false, false, false, 0);

    add("File Copy (cp)", "",
        "cp -f %i %o",
        DDB_ENCODER_METHOD_FILENAME, false, false, false, false, false, false, 0);

    return list;
}

QList<EncoderPreset> PresetManager::loadAllPresets()
{
    QList<EncoderPreset> result = builtinPresets();
    QSet<QString> seenTitles;
    for (const EncoderPreset &p : result)
        seenTitles.insert(p.title);

    /* Load user presets from ~/.config/deadbeef/presets/encoders */
    QDir userDir(userPresetDir());
    if (userDir.exists()) {
        const QFileInfoList files = userDir.entryInfoList(QStringList() << "*.txt", QDir::Files, QDir::Name);
        for (const QFileInfo &fi : files) {
            EncoderPreset p;
            if (EncoderPreset::loadFromFile(fi.absoluteFilePath(), p)) {
                p.readonly = false;
                result.append(p);
            }
        }
    }

    return result;
}

PresetEditDialog::PresetEditDialog(const EncoderPreset &initial, QWidget *parent)
    : QDialog(parent), m_preset(initial)
{
    setWindowTitle(tr("Encoder Preset"));
    setMinimumWidth(520);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout();

    m_titleEdit = new QLineEdit(initial.title, this);
    m_extEdit = new QLineEdit(initial.ext, this);
    m_encoderEdit = new QLineEdit(initial.encoder, this);
    m_encoderEdit->setPlaceholderText(tr("e.g. lame -V2 - %o   (%o = output file, %i = input file)"));

    /* Qmmp-style popup button to insert %o / %i placeholders into command line */
    QWidget *cmdRowWidget = new QWidget(this);
    QHBoxLayout *cmdRowLayout = new QHBoxLayout(cmdRowWidget);
    cmdRowLayout->setContentsMargins(0, 0, 0, 0);
    QToolButton *cmdVarBtn = new QToolButton(cmdRowWidget);
    cmdVarBtn->setText(QStringLiteral("%"));
    cmdVarBtn->setToolTip(tr("Insert placeholder (%o = output file, %i = input file)"));
    cmdVarBtn->setPopupMode(QToolButton::InstantPopup);
    QMenu *cmdMenu = new QMenu(cmdVarBtn);
    cmdMenu->addAction(tr("Output file (%o)"), this, [this]() { m_encoderEdit->insert(QStringLiteral("%o")); });
    cmdMenu->addAction(tr("Input file (%i)"), this, [this]() { m_encoderEdit->insert(QStringLiteral("%i")); });
    cmdVarBtn->setMenu(cmdMenu);
    cmdRowLayout->addWidget(m_encoderEdit, 1);
    cmdRowLayout->addWidget(cmdVarBtn);

    m_methodCombo = new QComboBox(this);
    m_methodCombo->addItem(tr("Pipe uncompressed WAV to stdin (-)"), DDB_ENCODER_METHOD_PIPE);
    m_methodCombo->addItem(tr("Temporary WAV file (%i)"), DDB_ENCODER_METHOD_FILE);
    m_methodCombo->addItem(tr("Pass original source file directly (%i)"), DDB_ENCODER_METHOD_FILENAME);
    m_methodCombo->setCurrentIndex(qBound(0, initial.method, 2));

    form->addRow(tr("Title:"), m_titleEdit);
    form->addRow(tr("Output file extension:"), m_extEdit);
    form->addRow(tr("Command line:"), cmdRowWidget);
    form->addRow(tr("Execution method:"), m_methodCombo);
    mainLayout->addLayout(form);

    QGroupBox *tagGroup = new QGroupBox(tr("Write Tags After Encoding"), this);
    QVBoxLayout *tagLayout = new QVBoxLayout(tagGroup);

    QHBoxLayout *row1 = new QHBoxLayout();
    m_id3v2Check = new QCheckBox(tr("ID3v2"), tagGroup);
    m_id3v2Check->setChecked(initial.tag_id3v2);
    m_id3v1Check = new QCheckBox(tr("ID3v1"), tagGroup);
    m_id3v1Check->setChecked(initial.tag_id3v1);
    m_apev2Check = new QCheckBox(tr("APEv2"), tagGroup);
    m_apev2Check->setChecked(initial.tag_apev2);
    row1->addWidget(m_id3v2Check);
    row1->addWidget(m_id3v1Check);
    row1->addWidget(m_apev2Check);
    tagLayout->addLayout(row1);

    QHBoxLayout *row2 = new QHBoxLayout();
    m_flacCheck = new QCheckBox(tr("FLAC"), tagGroup);
    m_flacCheck->setChecked(initial.tag_flac);
    m_oggCheck = new QCheckBox(tr("Ogg Vorbis / Opus"), tagGroup);
    m_oggCheck->setChecked(initial.tag_oggvorbis);
    m_mp4Check = new QCheckBox(tr("MP4 / M4A"), tagGroup);
    m_mp4Check->setChecked(initial.tag_mp4);
    row2->addWidget(m_flacCheck);
    row2->addWidget(m_oggCheck);
    row2->addWidget(m_mp4Check);
    tagLayout->addLayout(row2);

    QHBoxLayout *row3 = new QHBoxLayout();
    row3->addWidget(new QLabel(tr("ID3v2 version:"), tagGroup));
    m_id3v2VerCombo = new QComboBox(tagGroup);
    m_id3v2VerCombo->addItem(tr("ID3v2.3"), 0);
    m_id3v2VerCombo->addItem(tr("ID3v2.4"), 1);
    m_id3v2VerCombo->setCurrentIndex(initial.id3v2_version == 1 ? 1 : 0);
    row3->addWidget(m_id3v2VerCombo);
    row3->addStretch();
    tagLayout->addLayout(row3);

    mainLayout->addWidget(tagGroup);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &PresetEditDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

void PresetEditDialog::onAccepted()
{
    if (m_titleEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Validation"), tr("Preset title cannot be empty."));
        return;
    }
    m_preset.title = m_titleEdit->text().trimmed();
    m_preset.ext = m_extEdit->text().trimmed();
    if (m_preset.ext.startsWith(QLatin1Char('.')))
        m_preset.ext.remove(0, 1);
    m_preset.encoder = m_encoderEdit->text().trimmed();
    m_preset.method = m_methodCombo->currentData().toInt();
    m_preset.tag_id3v2 = m_id3v2Check->isChecked();
    m_preset.tag_id3v1 = m_id3v1Check->isChecked();
    m_preset.tag_apev2 = m_apev2Check->isChecked();
    m_preset.tag_flac = m_flacCheck->isChecked();
    m_preset.tag_oggvorbis = m_oggCheck->isChecked();
    m_preset.tag_mp4 = m_mp4Check->isChecked();
    m_preset.id3v2_version = m_id3v2VerCombo->currentData().toInt();
    m_preset.readonly = false;
    accept();
}

EncoderPreset PresetEditDialog::resultPreset() const
{
    return m_preset;
}

PresetListDialog::PresetListDialog(const QList<EncoderPreset> &presets, QWidget *parent)
    : QDialog(parent), m_presets(presets)
{
    setWindowTitle(tr("Encoder Presets"));
    resize(560, 400);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *bodyLayout = new QHBoxLayout();

    m_listWidget = new QListWidget(this);
    bodyLayout->addWidget(m_listWidget, 1);

    QVBoxLayout *btnLayout = new QVBoxLayout();
    QPushButton *addBtn = new QPushButton(tr("Add..."), this);
    QPushButton *editBtn = new QPushButton(tr("Edit..."), this);
    QPushButton *copyBtn = new QPushButton(tr("Copy..."), this);
    QPushButton *removeBtn = new QPushButton(tr("Remove"), this);

    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(editBtn);
    btnLayout->addWidget(copyBtn);
    btnLayout->addWidget(removeBtn);
    btnLayout->addStretch();
    bodyLayout->addLayout(btnLayout);

    mainLayout->addLayout(bodyLayout, 1);

    QDialogButtonBox *closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::accept);
    mainLayout->addWidget(closeBox);

    connect(addBtn, &QPushButton::clicked, this, &PresetListDialog::onAdd);
    connect(editBtn, &QPushButton::clicked, this, &PresetListDialog::onEdit);
    connect(copyBtn, &QPushButton::clicked, this, &PresetListDialog::onCopy);
    connect(removeBtn, &QPushButton::clicked, this, &PresetListDialog::onRemove);
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, [this]() { onEdit(); });

    refreshList();
}

void PresetListDialog::refreshList()
{
    int row = m_listWidget->currentRow();
    m_listWidget->clear();
    for (const EncoderPreset &p : m_presets) {
        QString label = p.readonly ? QStringLiteral("[Built-in] %1").arg(p.title) : p.title;
        m_listWidget->addItem(label);
    }
    if (row >= 0 && row < m_listWidget->count())
        m_listWidget->setCurrentRow(row);
    else if (m_listWidget->count() > 0)
        m_listWidget->setCurrentRow(0);
}

void PresetListDialog::onAdd()
{
    EncoderPreset p;
    p.title = QStringLiteral("Custom Encoder");
    p.ext = QStringLiteral("mp3");
    p.encoder = QStringLiteral("lame -V2 - %o");
    p.method = DDB_ENCODER_METHOD_PIPE;
    p.tag_id3v2 = true;

    PresetEditDialog dlg(p, this);
    if (dlg.exec() == QDialog::Accepted) {
        EncoderPreset res = dlg.resultPreset();
        res.saveToFile(PresetManager::userPresetDir());
        m_presets.append(res);
        refreshList();
        m_listWidget->setCurrentRow(m_presets.size() - 1);
    }
}

void PresetListDialog::onEdit()
{
    int idx = m_listWidget->currentRow();
    if (idx < 0 || idx >= m_presets.size())
        return;

    EncoderPreset cur = m_presets[idx];
    if (cur.readonly) {
        QMessageBox::information(this, tr("Built-in Preset"),
            tr("Built-in presets are read-only. A editable copy will be created."));
        cur.title += QStringLiteral(" (Copy)");
        cur.readonly = false;
        PresetEditDialog dlg(cur, this);
        if (dlg.exec() == QDialog::Accepted) {
            EncoderPreset res = dlg.resultPreset();
            res.saveToFile(PresetManager::userPresetDir());
            m_presets.append(res);
            refreshList();
            m_listWidget->setCurrentRow(m_presets.size() - 1);
        }
        return;
    }

    PresetEditDialog dlg(cur, this);
    if (dlg.exec() == QDialog::Accepted) {
        EncoderPreset res = dlg.resultPreset();
        res.saveToFile(PresetManager::userPresetDir());
        m_presets[idx] = res;
        refreshList();
    }
}

void PresetListDialog::onCopy()
{
    int idx = m_listWidget->currentRow();
    if (idx < 0 || idx >= m_presets.size())
        return;

    EncoderPreset copy = m_presets[idx];
    copy.title += QStringLiteral(" (Copy)");
    copy.readonly = false;

    PresetEditDialog dlg(copy, this);
    if (dlg.exec() == QDialog::Accepted) {
        EncoderPreset res = dlg.resultPreset();
        res.saveToFile(PresetManager::userPresetDir());
        m_presets.append(res);
        refreshList();
        m_listWidget->setCurrentRow(m_presets.size() - 1);
    }
}

void PresetListDialog::onRemove()
{
    int idx = m_listWidget->currentRow();
    if (idx < 0 || idx >= m_presets.size())
        return;

    if (m_presets[idx].readonly) {
        QMessageBox::warning(this, tr("Built-in Preset"),
            tr("Built-in presets cannot be removed."));
        return;
    }

    QString safeName = m_presets[idx].title;
    safeName.replace(QLatin1Char('/'), QLatin1Char('_'));
    QFile::remove(QDir(PresetManager::userPresetDir()).filePath(safeName + QLatin1String(".txt")));

    m_presets.removeAt(idx);
    refreshList();
}
