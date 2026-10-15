/*
 * Qddbconv - Standalone Qt6 Audio Converter
 * Copyright (C) 2026 goberghen & Arena.ai Agent
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QDDBCONV_TRACK_H
#define QDDBCONV_TRACK_H

#include <QString>
#include <QList>
#include <QImage>
#include "preset.h"

struct TrackItem {
    QString sourcePath;      /* Path to the audio file on disk */
    QString cuePath;         /* Non-empty if loaded from a .cue sheet */
    double startSec = 0.0;   /* Start offset in seconds (for CUE tracks) */
    double endSec = 0.0;     /* End offset in seconds (0 = until end of file) */
    double durationSec = 0.0;

    int sampleRate = 44100;
    int channels = 2;
    int bitsPerSample = 16;
    QString codecName;

    /* Metadata fields */
    QString title;
    QString artist;
    QString album;
    QString albumArtist;
    QString trackNumber;
    QString totalTracks;
    QString discNumber;
    QString year;
    QString genre;
    QString comment;

    /* Cover Art fields:
     * - hasEmbeddedArt: true if source audio file contains an attached picture stream
     * - coverArtPath: path to external cover image (or "__none__" if explicitly cleared by user) */
    bool hasEmbeddedArt = false;
    QString coverArtPath;

    /* UI runtime state */
    QString statusText;
};

class TrackProbe {
public:
    /* Probe a single audio file using ffprobe (with filename fallback and cover art detection) */
    static TrackItem probeFile(const QString &filePath);

    /* Search for cover art image (cover.jpg, folder.jpg, front.png, etc.) in the track/CUE directory */
    static QString findFolderCoverArt(const QString &audioOrCueFilePath);

    /* Load cover image for preview or processing (from coverArtPath or extracted from embedded stream) */
    static QImage loadTrackCoverImage(const TrackItem &item, QString *sourceDescription = nullptr);

    /* Parse a CUE sheet into individual track items */
    static QList<TrackItem> parseCueSheet(const QString &cueFilePath);

    /* Expand files/directories/CUE sheets recursively into TrackItems */
    static QList<TrackItem> expandInputs(const QStringList &paths);

    /* Evaluate DeaDBeeF Title Formatting pattern (e.g. "[%tracknumber%. ][%artist% - ]%title%") */
    static QString evaluateTitleFormat(const TrackItem &item, int index, const QString &pattern);

    /* Compute common root folder across all items (for Preserve Folder Structure) */
    static QString commonRootFolder(const QList<TrackItem> &items);

    /* Compute final destination file path (matches get_output_path_int in converter.c) */
    static QString computeOutputPath(const TrackItem &item,
                                     int index,
                                     const QString &outFolderUser,
                                     const QString &outFilePattern,
                                     const EncoderPreset &preset,
                                     bool preserveFolderStructure,
                                     const QString &rootFolder,
                                     bool writeToSourceFolder);
};

#endif /* QDDBCONV_TRACK_H */
