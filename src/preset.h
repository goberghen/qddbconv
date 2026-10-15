/*
 * Qddbconv - Standalone Qt6 Audio Converter
 * Copyright (C) 2026 goberghen & Arena.ai Agent
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QDDBCONV_PRESET_H
#define QDDBCONV_PRESET_H

#include <QString>
#include <QList>
#include <QDialog>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QListWidget;

/* Matches DeaDBeeF's ddb_encoder_preset_t structure and file format */
enum EncoderMethod {
    DDB_ENCODER_METHOD_PIPE = 0,     /* Feed uncompressed RIFF WAVE to encoder's stdin */
    DDB_ENCODER_METHOD_FILE = 1,     /* Decode to temporary .wav file (%i) and pass to encoder */
    DDB_ENCODER_METHOD_FILENAME = 2  /* Pass original source file path directly as %i */
};

struct EncoderPreset {
    QString title;
    QString ext;
    QString encoder;
    int method = DDB_ENCODER_METHOD_PIPE;
    int id3v2_version = 0; /* 0 = ID3v2.3, 1 = ID3v2.4 */
    bool tag_id3v2 = false;
    bool tag_id3v1 = false;
    bool tag_apev2 = false;
    bool tag_flac = false;
    bool tag_oggvorbis = false;
    bool tag_mp4 = false;
    bool readonly = false;

    static bool loadFromFile(const QString &filePath, EncoderPreset &out);
    bool saveToFile(const QString &dirPath) const;
};

class PresetManager {
public:
    static QList<EncoderPreset> loadAllPresets();
    static QList<EncoderPreset> builtinPresets();
    static QString userPresetDir();
};

/* Dialog for editing a single EncoderPreset (mirrors DeaDBeeF's Encoder Preset dialog) */
class PresetEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit PresetEditDialog(const EncoderPreset &initial, QWidget *parent = nullptr);
    EncoderPreset resultPreset() const;

private slots:
    void onAccepted();

private:
    QLineEdit *m_titleEdit;
    QLineEdit *m_extEdit;
    QLineEdit *m_encoderEdit;
    QComboBox *m_methodCombo;
    QCheckBox *m_id3v2Check;
    QCheckBox *m_id3v1Check;
    QCheckBox *m_apev2Check;
    QCheckBox *m_flacCheck;
    QCheckBox *m_oggCheck;
    QCheckBox *m_mp4Check;
    QComboBox *m_id3v2VerCombo;
    EncoderPreset m_preset;
};

/* Dialog for managing the list of EncoderPresets (Add / Edit / Copy / Remove) */
class PresetListDialog : public QDialog {
    Q_OBJECT
public:
    explicit PresetListDialog(const QList<EncoderPreset> &presets, QWidget *parent = nullptr);
    QList<EncoderPreset> presets() const { return m_presets; }

private slots:
    void onAdd();
    void onEdit();
    void onCopy();
    void onRemove();
    void refreshList();

private:
    QList<EncoderPreset> m_presets;
    QListWidget *m_listWidget;
};

#endif /* QDDBCONV_PRESET_H */
