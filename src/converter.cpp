/*
 * Qddbconv - Standalone Qt6 Audio Converter
 * Copyright (C) 2026 goberghen & Arena.ai Agent
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "converter.h"
#include "smaf2mid.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QSettings>
#include <QTemporaryFile>
#include <cstdio>

static QString configuredFfmpegBin()
{
    QSettings s(QStringLiteral("qddbconv"), QStringLiteral("qddbconv"));
    QString bin = s.value(QStringLiteral("ffmpegPath"), QStringLiteral("ffmpeg")).toString().trimmed();
    return bin.isEmpty() ? QStringLiteral("ffmpeg") : bin;
}

static QString configuredTempDir()
{
    QSettings s(QStringLiteral("qddbconv"), QStringLiteral("qddbconv"));
    QString dir = s.value(QStringLiteral("tempDir"), QDir::tempPath()).toString().trimmed();
    return (dir.isEmpty() || !QDir(dir).exists()) ? QDir::tempPath() : dir;
}

static QString shellEscape(const QString &path)
{
    /* Matches escape_filepath() in DeaDBeeF's plugins/converter/converter.c */
    QString out;
    out.reserve(path.size() + 16);
    const QString special = QStringLiteral("$\"`\\");
    for (int i = 0; i < path.size(); ++i) {
        if (special.contains(path[i]))
            out.append(QLatin1Char('\\'));
        out.append(path[i]);
    }
    return out;
}

static QString buildEncoderCmdLine(const EncoderPreset &preset,
                                   const QString &outPath,
                                   const QString &inputPath)
{
    /* Substitutes %o with escaped output file and %i with escaped input file */
    QString cmd;
    const QString &tpl = preset.encoder;
    const QString escOut = QStringLiteral("\"%1\"").arg(shellEscape(outPath));
    const QString escIn = (inputPath == QLatin1String("-"))
        ? QStringLiteral("-")
        : QStringLiteral("\"%1\"").arg(shellEscape(inputPath));

    for (int i = 0; i < tpl.size(); ++i) {
        if (tpl[i] == QLatin1Char('%') && i + 1 < tpl.size()) {
            QChar next = tpl[i + 1];
            if (next == QLatin1Char('o')) {
                cmd += escOut;
                ++i;
                continue;
            } else if (next == QLatin1Char('i')) {
                cmd += escIn;
                ++i;
                continue;
            }
        }
        cmd += tpl[i];
    }
    return cmd;
}

static QString resolvePcmCodec(const TrackItem &item, int outputBps, bool outputIsFloat)
{
    int bps = (outputBps == -1) ? item.bitsPerSample : outputBps;
    if (outputIsFloat)
        return QStringLiteral("pcm_f32le");
    if (bps <= 8)
        return QStringLiteral("pcm_u8");
    if (bps == 24)
        return QStringLiteral("pcm_s24le");
    if (bps >= 32)
        return QStringLiteral("pcm_s32le");
    return QStringLiteral("pcm_s16le");
}

static QStringList buildFfmpegDecodeArgs(const TrackItem &item,
                                         const ConverterSettings &settings,
                                         const QString &wavDest)
{
    QStringList args;
    args << QStringLiteral("-v") << QStringLiteral("error")
         << QStringLiteral("-y");

    /* CUE track start/end slicing */
    if (item.startSec > 0.0)
        args << QStringLiteral("-ss") << QString::number(item.startSec, 'f', 3);
    if (item.endSec > item.startSec)
        args << QStringLiteral("-to") << QString::number(item.endSec, 'f', 3);

    args << QStringLiteral("-i") << item.sourcePath
         << QStringLiteral("-vn");

    if (settings.targetSampleRate > 0)
        args << QStringLiteral("-ar") << QString::number(settings.targetSampleRate);
    if (settings.targetChannels > 0)
        args << QStringLiteral("-ac") << QString::number(settings.targetChannels);

    args << QStringLiteral("-c:a")
         << resolvePcmCodec(item, settings.outputBps, settings.outputIsFloat)
         << QStringLiteral("-f") << QStringLiteral("wav")
         << wavDest;

    return args;
}

/* Helper to wait for a single QProcess while checking cancellation flag */
static bool waitProcessCancelable(QProcess &proc, std::atomic<bool> *cancelled)
{
    while (proc.state() != QProcess::NotRunning) {
        if (cancelled && cancelled->load()) {
            proc.kill();
            proc.waitForFinished(1000);
            return false;
        }
        if (proc.waitForFinished(200))
            break;
    }
    return !(cancelled && cancelled->load());
}

static void appendBigEndianU32(QByteArray &buf, quint32 val)
{
    buf.append(static_cast<char>((val >> 24) & 0xFF));
    buf.append(static_cast<char>((val >> 16) & 0xFF));
    buf.append(static_cast<char>((val >> 8) & 0xFF));
    buf.append(static_cast<char>(val & 0xFF));
}

/* Build RFC 7845 / Xiph FLAC METADATA_BLOCK_PICTURE Base64 string for Ogg Vorbis & Opus */
static QString buildVorbisMetadataBlockPicture(const QByteArray &jpegBytes, int width, int height)
{
    const QByteArray mime = QByteArrayLiteral("image/jpeg");
    const QByteArray desc = QByteArrayLiteral("Cover (front)");
    QByteArray block;
    block.reserve(32 + mime.size() + desc.size() + jpegBytes.size());

    appendBigEndianU32(block, 3); /* Picture type 3 = Cover (front) */
    appendBigEndianU32(block, static_cast<quint32>(mime.size()));
    block.append(mime);
    appendBigEndianU32(block, static_cast<quint32>(desc.size()));
    block.append(desc);
    appendBigEndianU32(block, static_cast<quint32>(width));
    appendBigEndianU32(block, static_cast<quint32>(height));
    appendBigEndianU32(block, 24); /* 24-bit RGB */
    appendBigEndianU32(block, 0);  /* 0 = non-indexed color */
    appendBigEndianU32(block, static_cast<quint32>(jpegBytes.size()));
    block.append(jpegBytes);

    return QString::fromLatin1(block.toBase64());
}

bool AudioConverter::writeMetadataTags(const TrackItem &item,
                                       const QString &filePath,
                                       const EncoderPreset &preset,
                                       bool embedCoverArt,
                                       bool resizeCoverArt,
                                       int maxCoverSizePx,
                                       QString &errorMsg)
{
    if (!(preset.tag_id3v2 || preset.tag_id3v1 || preset.tag_apev2 ||
          preset.tag_flac || preset.tag_oggvorbis || preset.tag_mp4)) {
        return true;
    }

    QFileInfo fi(filePath);
    if (!fi.exists() || fi.size() == 0)
        return false;

    const QString ext = fi.suffix().toLower();
    const bool isOggOrOpus = (ext == QLatin1String("ogg") || ext == QLatin1String("oga") || ext == QLatin1String("opus"));
    const bool supportsStreamArt = (ext == QLatin1String("mp3") || ext == QLatin1String("flac") ||
                                    ext == QLatin1String("m4a") || ext == QLatin1String("mp4"));

    /* Prepare cover art if requested and available */
    QImage coverImg;
    QByteArray coverJpegBytes;
    QString tempCoverPath;
    QString tempFfmetaPath;

    if (embedCoverArt && (isOggOrOpus || supportsStreamArt)) {
        coverImg = TrackProbe::loadTrackCoverImage(item);
        if (!coverImg.isNull()) {
            if (resizeCoverArt && maxCoverSizePx >= 64 &&
                (coverImg.width() > maxCoverSizePx || coverImg.height() > maxCoverSizePx)) {
                coverImg = coverImg.scaled(maxCoverSizePx, maxCoverSizePx,
                                           Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            QBuffer buf(&coverJpegBytes);
            buf.open(QIODevice::WriteOnly);
            coverImg.save(&buf, "JPEG", 92);

            if (supportsStreamArt && !coverJpegBytes.isEmpty()) {
                QTemporaryFile tmpImg(configuredTempDir() + QLatin1String("/qddbconv_cover_XXXXXX.jpg"));
                tmpImg.setAutoRemove(false);
                if (tmpImg.open()) {
                    tmpImg.write(coverJpegBytes);
                    tempCoverPath = tmpImg.fileName();
                    tmpImg.close();
                }
            } else if (isOggOrOpus && !coverJpegBytes.isEmpty()) {
                /* Write METADATA_BLOCK_PICTURE via an ffmetadata file rather than argv
                 * so cover images > 96 KB never hit Linux MAX_ARG_STRLEN (128 KB) in execve() */
                QTemporaryFile tmpMeta(configuredTempDir() + QLatin1String("/qddbconv_meta_XXXXXX.txt"));
                tmpMeta.setAutoRemove(false);
                if (tmpMeta.open()) {
                    QString b64Pic = buildVorbisMetadataBlockPicture(coverJpegBytes, coverImg.width(), coverImg.height());
                    tmpMeta.write(";FFMETADATA1\nMETADATA_BLOCK_PICTURE=");
                    tmpMeta.write(b64Pic.toLatin1());
                    tmpMeta.write("\n");
                    tempFfmetaPath = tmpMeta.fileName();
                    tmpMeta.close();
                }
            }
        }
    }

    QString tmpTagged = filePath + QStringLiteral(".tagged.") + fi.suffix();
    QFile::remove(tmpTagged);

    QStringList args;
    args << QStringLiteral("-v") << QStringLiteral("error")
         << QStringLiteral("-y")
         << QStringLiteral("-i") << filePath;

    if (!tempCoverPath.isEmpty()) {
        args << QStringLiteral("-i") << tempCoverPath
             << QStringLiteral("-map") << QStringLiteral("0:a")
             << QStringLiteral("-map") << QStringLiteral("1:v")
             << QStringLiteral("-c") << QStringLiteral("copy")
             << QStringLiteral("-disposition:v:0") << QStringLiteral("attached_pic")
             << QStringLiteral("-metadata:s:v") << QStringLiteral("title=Album cover")
             << QStringLiteral("-metadata:s:v") << QStringLiteral("comment=Cover (front)")
             << QStringLiteral("-map_metadata") << QStringLiteral("-1");
    } else if (!tempFfmetaPath.isEmpty()) {
        args << QStringLiteral("-f") << QStringLiteral("ffmetadata")
             << QStringLiteral("-i") << tempFfmetaPath
             << QStringLiteral("-map") << QStringLiteral("0:a")
             << QStringLiteral("-c") << QStringLiteral("copy")
             << QStringLiteral("-map_metadata") << QStringLiteral("1")
             << QStringLiteral("-map_metadata:s:a:0") << QStringLiteral("1");
    } else {
        args << QStringLiteral("-c") << QStringLiteral("copy")
             << QStringLiteral("-map_metadata") << QStringLiteral("-1");
    }

    auto addMeta = [&](const char *key, const QString &val) {
        if (!val.trimmed().isEmpty()) {
            const QString kv = QStringLiteral("%1=%2").arg(QLatin1String(key), val);
            args << QStringLiteral("-metadata") << kv;
            if (isOggOrOpus)
                args << QStringLiteral("-metadata:s:a:0") << kv;
        }
    };

    addMeta("title", item.title);
    addMeta("artist", item.artist);
    addMeta("album", item.album);
    addMeta("album_artist", item.albumArtist);
    addMeta("date", item.year);
    addMeta("genre", item.genre);
    addMeta("comment", item.comment);

    if (!item.trackNumber.isEmpty()) {
        QString trk = item.totalTracks.isEmpty()
            ? item.trackNumber
            : QStringLiteral("%1/%2").arg(item.trackNumber, item.totalTracks);
        addMeta("track", trk);
    }
    if (!item.discNumber.isEmpty())
        addMeta("disc", item.discNumber);

    if (preset.tag_id3v2) {
        args << QStringLiteral("-id3v2_version")
             << QString::number(preset.id3v2_version == 1 ? 4 : 3);
    }
    if (preset.tag_id3v1)
        args << QStringLiteral("-write_id3v1") << QStringLiteral("1");

    args << tmpTagged;

    QProcess proc;
    proc.start(configuredFfmpegBin(), args);
    bool finished = proc.waitForFinished(30000);
    int exitCode = proc.exitCode();

    if (!tempCoverPath.isEmpty())
        QFile::remove(tempCoverPath);
    if (!tempFfmetaPath.isEmpty())
        QFile::remove(tempFfmetaPath);

    if (!finished || exitCode != 0) {
        errorMsg = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        std::fprintf(stderr, "[qddbconv] Warning: failed to write tags/cover to %s: %s\n",
                     filePath.toUtf8().constData(), errorMsg.toUtf8().constData());
        QFile::remove(tmpTagged);
        /* Non-fatal if container doesn't support remuxing */
        return true;
    }

    QFile::remove(filePath);
    QFile::rename(tmpTagged, filePath);
    return true;
}

bool AudioConverter::convertTrack(const TrackItem &item,
                                  const QString &outPath,
                                  const ConverterSettings &settings,
                                  std::atomic<bool> *cancelled,
                                  QString &errorMsg)
{
    if (cancelled && cancelled->load())
        return false;

    std::fprintf(stdout, "[qddbconv] Converting: %s -> %s\n",
                 item.sourcePath.toUtf8().constData(),
                 outPath.toUtf8().constData());
    std::fflush(stdout);

    QFileInfo outFi(outPath);
    QDir outDir = outFi.absoluteDir();
    if (!outDir.exists() && !outDir.mkpath(QLatin1String("."))) {
        errorMsg = QStringLiteral("Failed to create output directory: %1").arg(outDir.absolutePath());
        std::fprintf(stderr, "[qddbconv] Error: %s\n", errorMsg.toUtf8().constData());
        return false;
    }

    QFileInfo srcFi(item.sourcePath);
    const EncoderPreset &preset = settings.encoderPreset;
    const bool isSmaf = SmafConverter::isSmafFile(item.sourcePath);
    const bool isMidi = !isSmaf && SmafConverter::isMidiFile(item.sourcePath);
    const int targetSr = (settings.targetSampleRate > 0)
                             ? settings.targetSampleRate
                             : 44100;

    /* 1. Bypass conversion if format is identical and not a CUE slice */
    if (settings.bypassSameFormat &&
        item.cuePath.isEmpty() &&
        !preset.ext.isEmpty() &&
        srcFi.suffix().compare(preset.ext, Qt::CaseInsensitive) == 0) {
        QFile::remove(outPath);
        if (!QFile::copy(item.sourcePath, outPath)) {
            errorMsg = QStringLiteral("Failed to copy source file to %1").arg(outPath);
            std::fprintf(stderr, "[qddbconv] Error: %s\n", errorMsg.toUtf8().constData());
            return false;
        }
        if (settings.retagAfterCopy) {
            writeMetadataTags(item, outPath, preset,
                              settings.embedCoverArt, settings.resizeCoverArt, settings.maxCoverSizePx,
                              errorMsg);
        }
        std::fprintf(stdout, "[qddbconv] Copied: %s\n", outPath.toUtf8().constData());
        std::fflush(stdout);
        return true;
    }

    /* 1b. Native Yamaha SMAF (.mmf) -> Standard MIDI (.mid) conversion */
    if (preset.encoder.trimmed() == QLatin1String("@mmf2mid") ||
        preset.ext.compare(QLatin1String("mid"), Qt::CaseInsensitive) == 0 ||
        preset.ext.compare(QLatin1String("midi"), Qt::CaseInsensitive) == 0) {
        if (isSmaf) {
            if (!SmafConverter::convertMmfToMid(item.sourcePath, outPath, errorMsg)) {
                std::fprintf(stderr, "[qddbconv] SMAF->MIDI error: %s\n", errorMsg.toUtf8().constData());
                return false;
            }
            std::fprintf(stdout, "[qddbconv] Done (SMAF->MIDI): %s\n", outPath.toUtf8().constData());
            std::fflush(stdout);
            return true;
        }
        if (isMidi) {
            QFile::remove(outPath);
            if (!QFile::copy(item.sourcePath, outPath)) {
                errorMsg = QStringLiteral("Failed to copy MIDI file to %1").arg(outPath);
                return false;
            }
            std::fprintf(stdout, "[qddbconv] Copied (MIDI): %s\n", outPath.toUtf8().constData());
            std::fflush(stdout);
            return true;
        }
        errorMsg = QStringLiteral("Only .mmf (Yamaha SMAF) or .mid files can be converted to Standard MIDI (.mid).");
        return false;
    }

    /* 2. Direct uncompressed RIFF WAVE output when encoder command is empty */
    if (preset.encoder.trimmed().isEmpty()) {
        if (isMidi) {
            if (!SmafConverter::decodeMidToWav(item.sourcePath, outPath, targetSr, errorMsg)) {
                std::fprintf(stderr, "[qddbconv] MIDI->WAV error: %s\n", errorMsg.toUtf8().constData());
                return false;
            }
            std::fprintf(stdout, "[qddbconv] Done (MIDI->WAV): %s\n", outPath.toUtf8().constData());
            std::fflush(stdout);
            return true;
        }
        if (isSmaf) {
            if (!SmafConverter::decodeMmfToWav(item.sourcePath, outPath, errorMsg)) {
                std::fprintf(stderr, "[qddbconv] SMAF->WAV error: %s\n", errorMsg.toUtf8().constData());
                return false;
            }
            std::fprintf(stdout, "[qddbconv] Done (SMAF->WAV): %s\n", outPath.toUtf8().constData());
            std::fflush(stdout);
            return true;
        }

        QStringList args = buildFfmpegDecodeArgs(item, settings, outPath);
        QProcess decProc;
        decProc.start(configuredFfmpegBin(), args);
        if (!decProc.waitForStarted(5000)) {
            errorMsg = QStringLiteral("Failed to start ffmpeg.");
            return false;
        }
        if (!waitProcessCancelable(decProc, cancelled)) {
            QFile::remove(outPath);
            return false;
        }
        if (decProc.exitCode() != 0) {
            errorMsg = QString::fromUtf8(decProc.readAllStandardError()).trimmed();
            std::fprintf(stderr, "[qddbconv] FFmpeg error: %s\n", errorMsg.toUtf8().constData());
            QFile::remove(outPath);
            return false;
        }
        std::fprintf(stdout, "[qddbconv] Done (WAV): %s\n", outPath.toUtf8().constData());
        std::fflush(stdout);
        return true;
    }

    /* 3. Method 2: DDB_ENCODER_METHOD_FILENAME (pass source file directly as %i,
     *    except for .mmf / .mid which FFmpeg cannot demux directly — synthesize to a temp WAV first) */
    if (preset.method == DDB_ENCODER_METHOD_FILENAME && !isSmaf && !isMidi) {
        QString cmd = buildEncoderCmdLine(preset, outPath, item.sourcePath);
        QProcess encProc;
        encProc.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cmd});
        if (!encProc.waitForStarted(5000)) {
            errorMsg = QStringLiteral("Failed to start encoder command.");
            return false;
        }
        if (!waitProcessCancelable(encProc, cancelled)) {
            QFile::remove(outPath);
            return false;
        }
        if (encProc.exitCode() != 0) {
            errorMsg = QString::fromUtf8(encProc.readAllStandardError()).trimmed();
            if (errorMsg.isEmpty())
                errorMsg = QStringLiteral("Command failed: %1").arg(cmd);
            std::fprintf(stderr, "[qddbconv] Encoder error: %s\n", errorMsg.toUtf8().constData());
            return false;
        }
        writeMetadataTags(item, outPath, preset,
                          settings.embedCoverArt, settings.resizeCoverArt, settings.maxCoverSizePx,
                          errorMsg);
        std::fprintf(stdout, "[qddbconv] Done: %s\n", outPath.toUtf8().constData());
        std::fflush(stdout);
        return true;
    }

    /* 4. Method 0 (PIPE), Method 1 (FILE), or Method 2 on .mmf/.mid inputs:
     * First decode/synthesize to a temporary WAV file with a complete, valid RIFF header
     * (prevents pipe deadlocks and 0xFFFFFFFF chunk-size rejections in CLI encoders),
     * then either feed it via standard input (Method 0) or pass its path as %i (Method 1/2). */
    QTemporaryFile tmpWav(configuredTempDir() + QLatin1String("/qddbconv_XXXXXX.wav"));
    tmpWav.setAutoRemove(true);
    if (!tmpWav.open()) {
        errorMsg = QStringLiteral("Failed to create temporary WAV file in %1").arg(configuredTempDir());
        std::fprintf(stderr, "[qddbconv] Error: %s\n", errorMsg.toUtf8().constData());
        return false;
    }
    QString tmpWavPath = tmpWav.fileName();
    tmpWav.close();

    if (isMidi) {
        if (!SmafConverter::decodeMidToWav(item.sourcePath, tmpWavPath, targetSr, errorMsg)) {
            std::fprintf(stderr, "[qddbconv] MIDI decode error: %s\n", errorMsg.toUtf8().constData());
            QFile::remove(tmpWavPath);
            return false;
        }
    } else if (isSmaf) {
        if (!SmafConverter::decodeMmfToWav(item.sourcePath, tmpWavPath, errorMsg)) {
            std::fprintf(stderr, "[qddbconv] SMAF decode error: %s\n", errorMsg.toUtf8().constData());
            QFile::remove(tmpWavPath);
            return false;
        }
    } else {
        QStringList decArgs = buildFfmpegDecodeArgs(item, settings, tmpWavPath);
        QProcess decProc;
        decProc.start(configuredFfmpegBin(), decArgs);
        if (!decProc.waitForStarted(5000)) {
            errorMsg = QStringLiteral("Failed to start ffmpeg decoder.");
            std::fprintf(stderr, "[qddbconv] Error: %s\n", errorMsg.toUtf8().constData());
            return false;
        }
        if (!waitProcessCancelable(decProc, cancelled)) {
            QFile::remove(tmpWavPath);
            return false;
        }
        if (decProc.exitCode() != 0) {
            errorMsg = QString::fromUtf8(decProc.readAllStandardError()).trimmed();
            if (errorMsg.isEmpty())
                errorMsg = QStringLiteral("FFmpeg failed to decode input file.");
            std::fprintf(stderr, "[qddbconv] Decode error: %s\n", errorMsg.toUtf8().constData());
            QFile::remove(tmpWavPath);
            return false;
        }
    }

    QString cmd;
    QProcess encProc;
    if (preset.method == DDB_ENCODER_METHOD_FILE || preset.method == DDB_ENCODER_METHOD_FILENAME) {
        cmd = buildEncoderCmdLine(preset, outPath, tmpWavPath);
    } else {
        cmd = buildEncoderCmdLine(preset, outPath, QStringLiteral("-"));
        encProc.setStandardInputFile(tmpWavPath);
    }

    encProc.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cmd});
    if (!encProc.waitForStarted(5000)) {
        errorMsg = QStringLiteral("Failed to start encoder: %1").arg(cmd);
        std::fprintf(stderr, "[qddbconv] Error: %s\n", errorMsg.toUtf8().constData());
        QFile::remove(tmpWavPath);
        return false;
    }
    if (!waitProcessCancelable(encProc, cancelled)) {
        QFile::remove(tmpWavPath);
        QFile::remove(outPath);
        return false;
    }

    int encExit = encProc.exitCode();
    QString encErr = QString::fromUtf8(encProc.readAllStandardError()).trimmed();
    QFile::remove(tmpWavPath);

    if (encExit != 0) {
        errorMsg = encErr.isEmpty()
            ? QStringLiteral("Encoder exited with code %1 (%2)").arg(encExit).arg(cmd)
            : encErr;
        std::fprintf(stderr, "[qddbconv] Encoder error: %s\n", errorMsg.toUtf8().constData());
        QFile::remove(outPath);
        return false;
    }

    writeMetadataTags(item, outPath, preset,
                      settings.embedCoverArt, settings.resizeCoverArt, settings.maxCoverSizePx,
                      errorMsg);
    std::fprintf(stdout, "[qddbconv] Done: %s\n", outPath.toUtf8().constData());
    std::fflush(stdout);
    return true;
}
