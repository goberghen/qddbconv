/*
 * Qddbconv - Standalone Qt6 Audio Converter
 * Copyright (C) 2026 goberghen & Arena.ai Agent
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef QDDBCONV_SMAF2MID_H
#define QDDBCONV_SMAF2MID_H

#include <QString>
#include "track.h"

class SmafConverter
{
public:
    /* Returns true if the file starts with the 'MMMD' Yamaha SMAF signature */
    static bool isSmafFile(const QString &filePath);

    /* Extracts metadata (title, artist, comment, duration) from a .mmf file */
    static bool probeSmafFile(const QString &filePath, TrackItem &item);

    /* Converts a Yamaha SMAF (.mmf) score into a Standard MIDI File (.mid) */
    static bool convertMmfToMid(const QString &mmfPath,
                                const QString &midPath,
                                QString &errorMsg);

    /* Extracts embedded Yamaha ADPCM / PCM streams (Mwa/Awa) or renders via timidity to WAV */
    static bool decodeMmfToWav(const QString &mmfPath,
                               const QString &wavPath,
                               QString &errorMsg);

    /* Returns true if the file is a Standard MIDI File ('MThd' or RIFF 'RMID') */
    static bool isMidiFile(const QString &filePath);

    /* Extracts duration and sequence/track name metadata from a .mid/.midi/.kar/.rmi file */
    static bool probeMidiFile(const QString &filePath, TrackItem &item);

    /* Synthesizes a Standard MIDI File (.mid) to a 16-bit PCM WAV file via timidity/fluidsynth */
    static bool decodeMidToWav(const QString &midPath,
                               const QString &wavPath,
                               int sampleRate,
                               QString &errorMsg);
};

#endif /* QDDBCONV_SMAF2MID_H */
