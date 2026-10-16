/*
 * Qddbconv - Standalone Qt6 Audio Converter
 * Copyright (C) 2026 goberghen & Arena.ai Agent
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QDDBCONV_CONVERTER_H
#define QDDBCONV_CONVERTER_H

#include <QString>
#include <atomic>
#include "preset.h"
#include "track.h"

struct ConverterSettings {
    QString outputFolder;
    QString outputFilePattern = QStringLiteral("[%tracknumber%. ][%artist% - ]%title%");
    bool writeToSourceFolder = false;
    bool preserveFolderStructure = false;
    bool bypassSameFormat = false;
    bool retagAfterCopy = false;

    /* Cover Art options */
    bool embedCoverArt = true;
    bool resizeCoverArt = false;
    int maxCoverSizePx = 1000;

    /* Sample format: -1 = keep source, 8, 16, 24, 32 */
    int outputBps = -1;
    bool outputIsFloat = false;

    /* DSP options: 0 = keep source */
    int targetSampleRate = 0;
    int targetChannels = 0;

    /* Overwrite mode: 0 = Prompt, 1 = Skip, 2 = Overwrite */
    int overwriteAction = 0;

    EncoderPreset encoderPreset;
};

class AudioConverter {
public:
    /* Convert a single TrackItem to outPath according to settings.
     * Returns true on success, false on error or cancellation, and fills errorMsg. */
    static bool convertTrack(const TrackItem &item,
                             const QString &outPath,
                             const ConverterSettings &settings,
                             std::atomic<bool> *cancelled,
                             QString &errorMsg);

    /* Write metadata tags (and optional cover art) to an encoded file */
    static bool writeMetadataTags(const TrackItem &item,
                                  const QString &filePath,
                                  const EncoderPreset &preset,
                                  bool embedCoverArt,
                                  bool resizeCoverArt,
                                  int maxCoverSizePx,
                                  QString &errorMsg);

    /* Inspect how a preset will execute on the current system (standalone CLI binary,
     * FFmpeg library encoder, or FFmpeg built-in encoder, and whether it runs directly
     * as designed or via automatic fallback). Uses an in-memory cache of FFmpeg encoders. */
    static QString describePresetExecution(const EncoderPreset &preset);
};

#endif /* QDDBCONV_CONVERTER_H */
