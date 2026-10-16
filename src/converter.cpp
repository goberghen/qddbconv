/*
 * Qddbconv - Standalone Qt6 Audio Converter
 * Copyright (C) 2026 goberghen & Arena.ai Agent
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "converter.h"
#include "smaf2mid.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <cstdio>

static QString resolveBundledOrSystemBin(const QString &name)
{
    if (name.isEmpty() || QFileInfo(name).isAbsolute())
        return name;

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        const QStringList subDirs = { appDir, appDir + QLatin1String("/tools") };
        for (const QString &dir : subDirs) {
#ifdef Q_OS_WIN
            if (!name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) {
                QString candExe = dir + QLatin1Char('/') + name + QLatin1String(".exe");
                if (QFileInfo::exists(candExe))
                    return candExe;
            }
#endif
            QString cand = dir + QLatin1Char('/') + name;
            if (QFileInfo::exists(cand))
                return cand;
        }
    }
    return name;
}

/* Return absolute path to an executable if it exists in the bundle or system PATH,
 * or an empty string if not installed (uses fast stat() lookup, no process spawned). */
static QString findAvailableBinary(const QString &name)
{
    if (name.isEmpty())
        return QString();
    QString resolved = resolveBundledOrSystemBin(name);
    if (QFileInfo(resolved).isAbsolute())
        return QFileInfo::exists(resolved) ? resolved : QString();
    return QStandardPaths::findExecutable(resolved);
}

static QString configuredFfmpegBin()
{
    QSettings s(QStringLiteral("qddbconv"), QStringLiteral("qddbconv"));
    QString bin = s.value(QStringLiteral("ffmpegPath"), QStringLiteral("ffmpeg")).toString().trimmed();
    if (bin.isEmpty())
        bin = QStringLiteral("ffmpeg");
    return resolveBundledOrSystemBin(bin);
}

static QString configuredTempDir()
{
    QSettings s(QStringLiteral("qddbconv"), QStringLiteral("qddbconv"));
    QString dir = s.value(QStringLiteral("tempDir"), QDir::tempPath()).toString().trimmed();
    return (dir.isEmpty() || !QDir(dir).exists()) ? QDir::tempPath() : dir;
}

static QString shellEscape(const QString &path)
{
#ifdef Q_OS_WIN
    /* On Windows, backslashes are path separators and must not be doubled
     * inside quoted arguments parsed by QProcess::splitCommand(). */
    QString out = path;
    out.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return out;
#else
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
#endif
}

static void startEncoderCommand(QProcess &encProc, const QString &cmd)
{
#ifdef Q_OS_WIN
    if (cmd.contains(QLatin1Char('|')) || cmd.contains(QLatin1Char('>')) ||
        cmd.contains(QLatin1Char('<')) || cmd.contains(QLatin1Char('&'))) {
        encProc.start(QStringLiteral("cmd.exe"),
                      {QStringLiteral("/d"), QStringLiteral("/s"), QStringLiteral("/c"), cmd});
        return;
    }
    QStringList args = QProcess::splitCommand(cmd);
    if (args.isEmpty())
        return;
    QString prog = args.takeFirst();
    if (prog.compare(QLatin1String("ffmpeg"), Qt::CaseInsensitive) == 0 ||
        prog.compare(QLatin1String("ffmpeg.exe"), Qt::CaseInsensitive) == 0) {
        prog = configuredFfmpegBin();
    } else {
        prog = resolveBundledOrSystemBin(prog);
    }
    encProc.start(prog, args);
#else
    encProc.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cmd});
#endif
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

static bool isTrackerModuleExt(const QString &ext)
{
    static const QStringList trackerExts = {
        QStringLiteral("mod"), QStringLiteral("xm"), QStringLiteral("it"),
        QStringLiteral("s3m"), QStringLiteral("mptm"), QStringLiteral("stm"),
        QStringLiteral("med"), QStringLiteral("okt"), QStringLiteral("669"),
        QStringLiteral("far"), QStringLiteral("ult"), QStringLiteral("mtm"),
        QStringLiteral("amf")
    };
    return trackerExts.contains(ext.toLower());
}

static bool isChiptuneExt(const QString &ext)
{
    static const QStringList chipExts = {
        QStringLiteral("spc"), QStringLiteral("vgm"), QStringLiteral("vgz"),
        QStringLiteral("nsf"), QStringLiteral("nsfe"), QStringLiteral("gbs"),
        QStringLiteral("hes"), QStringLiteral("ay"), QStringLiteral("sap"),
        QStringLiteral("sid"), QStringLiteral("kss")
    };
    return chipExts.contains(ext.toLower());
}

/* Decode an input audio file to a RIFF WAVE file via FFmpeg, with automatic
 * fallback to openmpt123/xmp (for tracker modules when FFmpeg lacks --enable-libopenmpt)
 * or gme2wav (for console chiptunes when FFmpeg lacks --enable-libgme). */
static bool decodeInputToWavWithFallback(const TrackItem &item,
                                         const ConverterSettings &settings,
                                         const QString &wavDest,
                                         std::atomic<bool> *cancelled,
                                         QStringList &fallbackNotes,
                                         QString &errorMsg)
{
    const QString ext = QFileInfo(item.sourcePath).suffix().toLower();
    const int sr = (settings.targetSampleRate > 0) ? settings.targetSampleRate : 44100;

    QStringList decArgs = buildFfmpegDecodeArgs(item, settings, wavDest);
    QProcess decProc;
    decProc.start(configuredFfmpegBin(), decArgs);
    bool ffStarted = decProc.waitForStarted(5000);
    if (ffStarted) {
        if (!waitProcessCancelable(decProc, cancelled)) {
            QFile::remove(wavDest);
            return false;
        }
        if (decProc.exitCode() == 0 && QFileInfo(wavDest).size() > 44)
            return true;
    }

    const QString ffErr = ffStarted
        ? QString::fromUtf8(decProc.readAllStandardError()).trimmed()
        : QStringLiteral("ffmpeg executable not found");
    QFile::remove(wavDest);

    /* Fallback 1: Demoscene Tracker modules (.mod/.xm/.it/.s3m/...) via openmpt123 or xmp */
    if (isTrackerModuleExt(ext)) {
        std::fprintf(stderr,
                     "[qddbconv] Fallback: active FFmpeg build cannot decode tracker module '.%s' (built without --enable-libopenmpt); trying 'openmpt123'...\n",
                     ext.toUtf8().constData());

        QProcess mptProc;
        mptProc.start(resolveBundledOrSystemBin(QStringLiteral("openmpt123")),
                      {QStringLiteral("--samplerate"), QString::number(sr),
                       QStringLiteral("--output"), wavDest,
                       item.sourcePath});
        if (mptProc.waitForStarted(4000)) {
            if (!waitProcessCancelable(mptProc, cancelled)) {
                QFile::remove(wavDest);
                return false;
            }
            if (mptProc.exitCode() == 0 && QFileInfo(wavDest).size() > 44) {
                fallbackNotes.append(QStringLiteral("decoded via openmpt123 (FFmpeg lacks libopenmpt)"));
                return true;
            }
        }

        QProcess xmpProc;
        xmpProc.start(resolveBundledOrSystemBin(QStringLiteral("xmp")),
                      {QStringLiteral("-f"), QString::number(sr),
                       QStringLiteral("-d"), QStringLiteral("wav"),
                       QStringLiteral("-o"), wavDest,
                       item.sourcePath});
        if (xmpProc.waitForStarted(4000)) {
            if (!waitProcessCancelable(xmpProc, cancelled)) {
                QFile::remove(wavDest);
                return false;
            }
            if (xmpProc.exitCode() == 0 && QFileInfo(wavDest).size() > 44) {
                fallbackNotes.append(QStringLiteral("decoded via xmp (FFmpeg lacks libopenmpt)"));
                return true;
            }
        }

        errorMsg = QStringLiteral("Cannot decode tracker module (.%1): active FFmpeg build was compiled without '--enable-libopenmpt' and fallback 'openmpt123'/'xmp' is unavailable.")
                       .arg(ext);
        std::fprintf(stderr, "[qddbconv] Decode error: %s\n", errorMsg.toUtf8().constData());
        return false;
    }

    /* Fallback 2: Console Chiptunes (.spc/.vgm/.vgz/.nsf/.gbs/...) via gme2wav */
    if (isChiptuneExt(ext)) {
        std::fprintf(stderr,
                     "[qddbconv] Fallback: active FFmpeg build cannot decode chiptune '.%s' (built without --enable-libgme); trying 'gme2wav'...\n",
                     ext.toUtf8().constData());

        QProcess gmeProc;
        gmeProc.start(resolveBundledOrSystemBin(QStringLiteral("gme2wav")),
                      {QStringLiteral("--samplerate"), QString::number(sr),
                       QStringLiteral("--output"), wavDest,
                       item.sourcePath});
        if (gmeProc.waitForStarted(4000)) {
            if (!waitProcessCancelable(gmeProc, cancelled)) {
                QFile::remove(wavDest);
                return false;
            }
            if (gmeProc.exitCode() == 0 && QFileInfo(wavDest).size() > 44) {
                fallbackNotes.append(QStringLiteral("decoded via gme2wav (FFmpeg lacks libgme)"));
                return true;
            }
        }

        errorMsg = QStringLiteral("Cannot decode console chiptune (.%1): active FFmpeg build was compiled without '--enable-libgme' and fallback 'gme2wav' is unavailable.")
                       .arg(ext);
        std::fprintf(stderr, "[qddbconv] Decode error: %s\n", errorMsg.toUtf8().constData());
        return false;
    }

    if (!ffStarted) {
        errorMsg = QStringLiteral("Failed to start 'ffmpeg' decoder. Please install ffmpeg or set its path in Settings -> Configure qddbconv...");
    } else {
        errorMsg = ffErr.isEmpty()
            ? QStringLiteral("FFmpeg failed to decode '%1'.").arg(QFileInfo(item.sourcePath).fileName())
            : QStringLiteral("FFmpeg decode failed: %1").arg(ffErr);
    }
    std::fprintf(stderr, "[qddbconv] Decode error: %s\n", errorMsg.toUtf8().constData());
    return false;
}

/* Map external FFmpeg library encoder names to built-in native FFmpeg encoders
 * when the runtime FFmpeg binary was compiled without that external library. */
static QString mapFfmpegLibCodecToNative(const QString &codec, bool *needsStrictExperimental = nullptr)
{
    if (needsStrictExperimental)
        *needsStrictExperimental = false;

    const QString c = codec.toLower();
    if (c == QLatin1String("libfdk_aac") || c == QLatin1String("libfaac") || c == QLatin1String("libvo_aacenc"))
        return QStringLiteral("aac");
    if (c == QLatin1String("libopus")) {
        if (needsStrictExperimental) *needsStrictExperimental = true;
        return QStringLiteral("opus");
    }
    if (c == QLatin1String("libvorbis")) {
        if (needsStrictExperimental) *needsStrictExperimental = true;
        return QStringLiteral("vorbis");
    }
    if (c == QLatin1String("libtwolame"))
        return QStringLiteral("mp2");
    if (c == QLatin1String("libwavpack"))
        return QStringLiteral("wavpack");
    if (c == QLatin1String("libmp3lame"))
        return QStringLiteral("libshine");
    if (c == QLatin1String("libshine"))
        return QStringLiteral("libmp3lame");
    if (c == QLatin1String("libdca")) {
        if (needsStrictExperimental) *needsStrictExperimental = true;
        return QStringLiteral("dca");
    }
    return QString();
}

/* Query and cache the list of audio encoders supported by the active FFmpeg binary.
 * Runs 'ffmpeg -hide_banner -encoders' at most ONCE per session (or if the configured
 * FFmpeg binary path changes), so all subsequent lookups are instantaneous in-memory checks. */
static bool hasFfmpegAudioEncoder(const QString &codec)
{
    if (codec.isEmpty())
        return false;

    static QMutex s_mutex;
    static QString s_cachedFfmpegBin;
    static bool s_probed = false;
    static QSet<QString> s_audioEncoders;

    const QString ffBin = findAvailableBinary(configuredFfmpegBin());
    if (ffBin.isEmpty())
        return false;

    QMutexLocker locker(&s_mutex);
    if (!s_probed || s_cachedFfmpegBin != ffBin) {
        s_cachedFfmpegBin = ffBin;
        s_probed = true;
        s_audioEncoders.clear();

        QProcess proc;
        proc.start(ffBin, {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
        if (proc.waitForFinished(3000) && proc.exitCode() == 0) {
            const QString out = QString::fromUtf8(proc.readAllStandardOutput());
            const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            QRegularExpression encRe(QStringLiteral("^\\s*A[^\\s]{5}\\s+([A-Za-z0-9_]+)"));
            for (const QString &line : lines) {
                QRegularExpressionMatch m = encRe.match(line);
                if (m.hasMatch())
                    s_audioEncoders.insert(m.captured(1).toLower());
            }
        }
    }

    return s_audioEncoders.contains(codec.toLower());
}

static QString formatFfmpegCodecKind(const QString &codec)
{
    if (codec.startsWith(QLatin1String("lib"), Qt::CaseInsensitive))
        return QStringLiteral("FFmpeg library encoder '%1'").arg(codec);
    return QStringLiteral("FFmpeg built-in encoder '%1'").arg(codec);
}

/* Map a standalone CLI encoder preset to its equivalent FFmpeg audio codec and bitrate/quality flags */
static void resolveCliFallbackTarget(const EncoderPreset &preset,
                                     const QString &progName,
                                     QString &ffPrimaryCodec,
                                     QStringList &ffExtraArgs)
{
    ffPrimaryCodec.clear();
    ffExtraArgs.clear();

    const QString ext = preset.ext.trimmed().toLower();
    QRegularExpression brRe(QStringLiteral("(?:--bitrate|-b)\\s+(\\d+)"));
    QRegularExpression qRe(QStringLiteral("(?:--quality|-q|-V)\\s*([0-9]+(?:\\.[0-9]+)?)"));
    QRegularExpression compRe(QStringLiteral("(?:^|\\s)-([0-8])(?:\\s|$)"));

    const QRegularExpressionMatch brMatch = brRe.match(preset.encoder);
    const QRegularExpressionMatch qMatch = qRe.match(preset.encoder);
    const QRegularExpressionMatch compMatch = compRe.match(preset.encoder);

    if (ext == QLatin1String("mp3") || progName == QLatin1String("lame")) {
        ffPrimaryCodec = QStringLiteral("libmp3lame");
        if (brMatch.hasMatch())
            ffExtraArgs << QStringLiteral("-b:a") << (brMatch.captured(1) + QStringLiteral("k"));
        else if (qMatch.hasMatch())
            ffExtraArgs << QStringLiteral("-q:a") << qMatch.captured(1);
        else
            ffExtraArgs << QStringLiteral("-b:a") << QStringLiteral("320k");
    } else if (ext == QLatin1String("ogg") || ext == QLatin1String("oga") || progName.startsWith(QLatin1String("oggenc"))) {
        if (preset.encoder.contains(QLatin1String("--ogg")) && progName == QLatin1String("flac")) {
            ffPrimaryCodec = QStringLiteral("flac");
        } else {
            ffPrimaryCodec = QStringLiteral("libvorbis");
            if (brMatch.hasMatch())
                ffExtraArgs << QStringLiteral("-b:a") << (brMatch.captured(1) + QStringLiteral("k"));
            else if (qMatch.hasMatch())
                ffExtraArgs << QStringLiteral("-qscale:a") << qMatch.captured(1);
            else
                ffExtraArgs << QStringLiteral("-qscale:a") << QStringLiteral("6");
        }
    } else if (ext == QLatin1String("opus") || progName == QLatin1String("opusenc")) {
        ffPrimaryCodec = QStringLiteral("libopus");
        if (brMatch.hasMatch()) {
            int brVal = brMatch.captured(1).toInt();
            ffExtraArgs << QStringLiteral("-b:a")
                        << (brVal > 1000 ? QString::number(brVal) : (QString::number(brVal) + QStringLiteral("k")));
        } else {
            ffExtraArgs << QStringLiteral("-b:a") << QStringLiteral("128k");
        }
    } else if (ext == QLatin1String("flac") || progName == QLatin1String("flac")) {
        ffPrimaryCodec = QStringLiteral("flac");
        if (compMatch.hasMatch())
            ffExtraArgs << QStringLiteral("-compression_level") << compMatch.captured(1);
        else
            ffExtraArgs << QStringLiteral("-compression_level") << QStringLiteral("5");
    } else if (ext == QLatin1String("wv") || progName == QLatin1String("wavpack")) {
        ffPrimaryCodec = hasFfmpegAudioEncoder(QStringLiteral("libwavpack"))
                             ? QStringLiteral("libwavpack")
                             : QStringLiteral("wavpack");
    } else if (ext == QLatin1String("m4a") || ext == QLatin1String("aac") || ext == QLatin1String("mp4") ||
               progName == QLatin1String("faac") || progName == QLatin1String("fdkaac") || progName == QLatin1String("neroaacenc")) {
        ffPrimaryCodec = hasFfmpegAudioEncoder(QStringLiteral("libfdk_aac"))
                             ? QStringLiteral("libfdk_aac")
                             : QStringLiteral("aac");
        if (brMatch.hasMatch())
            ffExtraArgs << QStringLiteral("-b:a") << (brMatch.captured(1) + QStringLiteral("k"));
        else
            ffExtraArgs << QStringLiteral("-b:a") << QStringLiteral("256k");
    } else if (ext == QLatin1String("tta") || progName == QLatin1String("ttaenc")) {
        ffPrimaryCodec = QStringLiteral("tta");
    } else if (ext == QLatin1String("mp2") || progName == QLatin1String("twolame")) {
        ffPrimaryCodec = hasFfmpegAudioEncoder(QStringLiteral("libtwolame"))
                             ? QStringLiteral("libtwolame")
                             : QStringLiteral("mp2");
        ffExtraArgs << QStringLiteral("-b:a") << QStringLiteral("256k");
    } else if (ext == QLatin1String("ac3") || progName == QLatin1String("aften")) {
        ffPrimaryCodec = QStringLiteral("ac3");
        ffExtraArgs << QStringLiteral("-b:a") << QStringLiteral("384k");
    }
}

QString AudioConverter::describePresetExecution(const EncoderPreset &preset)
{
    const QString encCmd = preset.encoder.trimmed();
    const QString ext = preset.ext.trimmed().toLower();

    if (encCmd.isEmpty())
        return QStringLiteral("Backend: Built-in RIFF WAVE writer (direct, as designed)");

    if (encCmd == QLatin1String("@mmf2mid") ||
        ext == QLatin1String("mid") || ext == QLatin1String("midi")) {
        return QStringLiteral("Backend: Built-in Yamaha SMAF to Standard MIDI engine (direct, as designed)");
    }

    const QStringList splitArgs = QProcess::splitCommand(encCmd);
    const QString progToken = splitArgs.isEmpty() ? QStringLiteral("encoder") : splitArgs.first();
    const QString progName = QFileInfo(progToken).baseName().toLower();

    /* Case 1: Preset directly invokes FFmpeg */
    if (progName == QLatin1String("ffmpeg")) {
        if (findAvailableBinary(configuredFfmpegBin()).isEmpty())
            return QStringLiteral("Backend: Unavailable ('ffmpeg' binary not found)");

        QRegularExpression codecRe(QStringLiteral("(?:-c:a|-acodec)\\s+([A-Za-z0-9_]+)"));
        QRegularExpressionMatch m = codecRe.match(encCmd);
        if (!m.hasMatch())
            return QStringLiteral("Backend: FFmpeg default container encoder (direct, as designed)");

        const QString reqCodec = m.captured(1);
        if (hasFfmpegAudioEncoder(reqCodec)) {
            return QStringLiteral("Backend: %1 (direct, as designed)")
                .arg(formatFfmpegCodecKind(reqCodec));
        }

        const QString nativeCodec = mapFfmpegLibCodecToNative(reqCodec);
        if (!nativeCodec.isEmpty() && hasFfmpegAudioEncoder(nativeCodec)) {
            return QStringLiteral("Backend: Fallback — FFmpeg lacks '%1', will use %2")
                .arg(reqCodec, formatFfmpegCodecKind(nativeCodec));
        }

        return QStringLiteral("Backend: Unavailable — active FFmpeg build lacks encoder '%1'")
            .arg(reqCodec);
    }

    /* Case 2: Preset invokes a standalone CLI binary (lame, oggenc, opusenc, flac, faac, ttaenc, etc.) */
    const QString cliBinPath = findAvailableBinary(progToken);
    if (!cliBinPath.isEmpty()) {
        return QStringLiteral("Backend: CLI binary '%1' [%2] (direct, as designed)")
            .arg(progName, cliBinPath);
    }

    /* CLI binary is not installed -> check what automatic fallback will be used */
    QString ffPrimaryCodec;
    QStringList ffExtraArgs;
    resolveCliFallbackTarget(preset, progName, ffPrimaryCodec, ffExtraArgs);

    if (ffPrimaryCodec.isEmpty()) {
        return QStringLiteral("Backend: Unavailable — CLI binary '%1' not found (no FFmpeg fallback for .%2)")
            .arg(progName, ext);
    }

    if (findAvailableBinary(configuredFfmpegBin()).isEmpty()) {
        return QStringLiteral("Backend: Unavailable — neither CLI binary '%1' nor 'ffmpeg' found")
            .arg(progName);
    }

    if (hasFfmpegAudioEncoder(ffPrimaryCodec)) {
        return QStringLiteral("Backend: Fallback — CLI binary '%1' not found, will use %2")
            .arg(progName, formatFfmpegCodecKind(ffPrimaryCodec));
    }

    const QString nativeCodec = mapFfmpegLibCodecToNative(ffPrimaryCodec);
    if (!nativeCodec.isEmpty() && hasFfmpegAudioEncoder(nativeCodec)) {
        return QStringLiteral("Backend: Fallback — CLI binary '%1' not found, will use %2")
            .arg(progName, formatFfmpegCodecKind(nativeCodec));
    }

    return QStringLiteral("Backend: Unavailable — CLI binary '%1' not found and FFmpeg lacks '%2'")
        .arg(progName, ffPrimaryCodec);
}

/* Run an FFmpeg audio encoding command with automatic fallback from an external
 * library encoder (libopus, libvorbis, libfdk_aac, etc.) to FFmpeg's built-in native encoder. */
static bool runFfmpegEncodeWithNativeFallback(const QString &inputWavPath,
                                              const QString &outPath,
                                              const QString &primaryCodec,
                                              const QStringList &extraCodecArgs,
                                              std::atomic<bool> *cancelled,
                                              QString &usedCodecOut,
                                              QString &ffErrOut)
{
    auto tryCodec = [&](const QString &codecName, bool strictExp) -> bool {
        QFile::remove(outPath);
        QStringList args = {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-y"),
            QStringLiteral("-i"), inputWavPath,
            QStringLiteral("-vn"),
            QStringLiteral("-c:a"), codecName
        };
        if (strictExp)
            args << QStringLiteral("-strict") << QStringLiteral("-2");
        args.append(extraCodecArgs);
        args << outPath;

        QProcess proc;
        proc.start(configuredFfmpegBin(), args);
        if (!proc.waitForStarted(5000)) {
            ffErrOut = QStringLiteral("ffmpeg executable not found");
            return false;
        }
        if (!waitProcessCancelable(proc, cancelled)) {
            QFile::remove(outPath);
            return false;
        }
        if (proc.exitCode() == 0 && QFileInfo(outPath).size() > 0) {
            usedCodecOut = codecName;
            return true;
        }
        ffErrOut = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        QFile::remove(outPath);
        return false;
    };

    if (tryCodec(primaryCodec, false))
        return true;

    if (cancelled && cancelled->load())
        return false;

    bool strictExp = false;
    QString nativeCodec = mapFfmpegLibCodecToNative(primaryCodec, &strictExp);
    if (!nativeCodec.isEmpty()) {
        std::fprintf(stderr,
                     "[qddbconv] Fallback: FFmpeg encoder '%s' is unavailable in active FFmpeg build (%s); retrying with native FFmpeg encoder '%s'...\n",
                     primaryCodec.toUtf8().constData(),
                     ffErrOut.toUtf8().constData(),
                     nativeCodec.toUtf8().constData());
        if (tryCodec(nativeCodec, strictExp))
            return true;
    }
    return false;
}

/* Execute the preset's encoder command, with a two-stage automatic fallback system:
 * 1) If the preset runs 'ffmpeg -c:a <libcodec>' and the active FFmpeg build lacks <libcodec>,
 *    retry with FFmpeg's built-in native encoder (e.g. libfdk_aac -> aac, libopus -> opus, libvorbis -> vorbis).
 * 2) If the preset runs an external CLI binary (lame, oggenc, opusenc, flac, wavpack, faac, ttaenc, twolame)
 *    that is not installed or fails to start, fall back to FFmpeg with equivalent bitrate/quality parameters
 *    (and chain to FFmpeg's native encoder if FFmpeg's library wrapper is also absent). */
static bool executeEncoderWithFallbacks(const EncoderPreset &preset,
                                        const QString &cmd,
                                        const QString &inputWavPath,
                                        const QString &outPath,
                                        bool feedStdInFromWav,
                                        std::atomic<bool> *cancelled,
                                        QStringList &fallbackNotes,
                                        QString &errorMsg)
{
    QStringList splitArgs = QProcess::splitCommand(preset.encoder.trimmed());
    QString progName = splitArgs.isEmpty() ? QStringLiteral("encoder") : QFileInfo(splitArgs.first()).baseName().toLower();

    QProcess encProc;
    if (feedStdInFromWav && !inputWavPath.isEmpty())
        encProc.setStandardInputFile(inputWavPath);

    startEncoderCommand(encProc, cmd);
    bool started = encProc.waitForStarted(5000);
    int exitCode = -1;
    QString primaryErr;

    if (started) {
        if (!waitProcessCancelable(encProc, cancelled)) {
            QFile::remove(outPath);
            return false;
        }
        exitCode = encProc.exitCode();
        primaryErr = QString::fromUtf8(encProc.readAllStandardError()).trimmed();
        if (exitCode == 0 && QFileInfo(outPath).size() > 0)
            return true;
    } else {
        primaryErr = QStringLiteral("executable '%1' not found").arg(progName);
    }

    QFile::remove(outPath);
    if (cancelled && cancelled->load())
        return false;

    /* Case A: Preset directly invoked FFmpeg, but the requested external library codec
     * (e.g. -c:a libfdk_aac, -c:a libopus, -c:a libvorbis) is missing in this FFmpeg build */
    if (progName == QLatin1String("ffmpeg")) {
        QRegularExpression codecRe(QStringLiteral("(?:-c:a|-acodec)\\s+([A-Za-z0-9_]+)"));
        QRegularExpressionMatch m = codecRe.match(cmd);
        if (m.hasMatch()) {
            QString reqCodec = m.captured(1);
            bool strictExp = false;
            QString nativeCodec = mapFfmpegLibCodecToNative(reqCodec, &strictExp);
            if (!nativeCodec.isEmpty()) {
                QString fallbackCmd = cmd;
                QString replacement = strictExp
                    ? QStringLiteral("-c:a %1 -strict -2").arg(nativeCodec)
                    : QStringLiteral("-c:a %1").arg(nativeCodec);
                fallbackCmd.replace(m.captured(0), replacement);

                std::fprintf(stderr,
                             "[qddbconv] Fallback: FFmpeg encoder '%s' failed (%s); retrying with native FFmpeg encoder '%s'...\n",
                             reqCodec.toUtf8().constData(),
                             primaryErr.toUtf8().constData(),
                             nativeCodec.toUtf8().constData());

                QProcess fbProc;
                if (feedStdInFromWav && !inputWavPath.isEmpty())
                    fbProc.setStandardInputFile(inputWavPath);
                startEncoderCommand(fbProc, fallbackCmd);
                if (fbProc.waitForStarted(5000)) {
                    if (!waitProcessCancelable(fbProc, cancelled)) {
                        QFile::remove(outPath);
                        return false;
                    }
                    if (fbProc.exitCode() == 0 && QFileInfo(outPath).size() > 0) {
                        fallbackNotes.append(
                            QStringLiteral("FFmpeg '%1' unavailable -> used native '%2'")
                                .arg(reqCodec, nativeCodec));
                        return true;
                    }
                    primaryErr = QString::fromUtf8(fbProc.readAllStandardError()).trimmed();
                }
            }
        }
        errorMsg = primaryErr.isEmpty()
            ? QStringLiteral("FFmpeg encoder command failed: %1").arg(cmd)
            : QStringLiteral("FFmpeg encoder error: %1").arg(primaryErr);
        std::fprintf(stderr, "[qddbconv] Encoder error: %s\n", errorMsg.toUtf8().constData());
        return false;
    }

    /* Case B: Preset invoked a standalone CLI encoder (lame, oggenc, opusenc, flac, wavpack,
     * faac, fdkaac, neroaacenc, ttaenc, twolame, aften) that is missing or failed -> fall back to FFmpeg */
    const QString ext = preset.ext.trimmed().toLower();
    QString ffPrimaryCodec;
    QStringList ffExtraArgs;
    resolveCliFallbackTarget(preset, progName, ffPrimaryCodec, ffExtraArgs);

    if (!ffPrimaryCodec.isEmpty() && !inputWavPath.isEmpty()) {
        std::fprintf(stderr,
                     "[qddbconv] Fallback: CLI encoder '%s' failed or is not installed (%s); falling back to FFmpeg ('-c:a %s')...\n",
                     progName.toUtf8().constData(),
                     primaryErr.toUtf8().constData(),
                     ffPrimaryCodec.toUtf8().constData());

        QString usedCodec;
        QString ffErr;
        if (runFfmpegEncodeWithNativeFallback(inputWavPath, outPath, ffPrimaryCodec, ffExtraArgs,
                                              cancelled, usedCodec, ffErr)) {
            fallbackNotes.append(
                QStringLiteral("'%1' unavailable -> encoded via FFmpeg (%2)")
                    .arg(progName, usedCodec));
            return true;
        }

        errorMsg = QStringLiteral("Primary encoder '%1' failed (%2), and FFmpeg fallback ('-c:a %3') also failed: %4")
                       .arg(progName,
                            primaryErr.isEmpty() ? QStringLiteral("not found") : primaryErr,
                            ffPrimaryCodec,
                            ffErr.isEmpty() ? QStringLiteral("unknown error") : ffErr);
        std::fprintf(stderr, "[qddbconv] Encoder error: %s\n", errorMsg.toUtf8().constData());
        return false;
    }

    /* No FFmpeg equivalent exists for this format (e.g., Musepack mpcenc or Monkey's Audio mac) */
    errorMsg = QStringLiteral("Standalone encoder '%1' failed or is not installed (%2), and FFmpeg has no built-in '.%3' encoder for fallback.")
                   .arg(progName,
                        primaryErr.isEmpty() ? QStringLiteral("command not found") : primaryErr,
                        ext);
    std::fprintf(stderr, "[qddbconv] Encoder error: %s\n", errorMsg.toUtf8().constData());
    return false;
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

    QStringList fallbackNotes;

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

        if (!decodeInputToWavWithFallback(item, settings, outPath, cancelled, fallbackNotes, errorMsg))
            return false;

        errorMsg = fallbackNotes.join(QStringLiteral("; "));
        std::fprintf(stdout, "[qddbconv] Done (WAV): %s\n", outPath.toUtf8().constData());
        std::fflush(stdout);
        return true;
    }

    const QString srcExt = srcFi.suffix().toLower();
    const bool needsWavSynthFirst = isSmaf || isMidi || isTrackerModuleExt(srcExt) || isChiptuneExt(srcExt);

    /* 3. Method 2: DDB_ENCODER_METHOD_FILENAME (pass source file directly as %i,
     *    except for .mmf / .mid / tracker / chiptune which may need fallback synthesis to a temp WAV first) */
    if (preset.method == DDB_ENCODER_METHOD_FILENAME && !needsWavSynthFirst) {
        QString cmd = buildEncoderCmdLine(preset, outPath, item.sourcePath);
        if (!executeEncoderWithFallbacks(preset, cmd, item.sourcePath, outPath, false,
                                         cancelled, fallbackNotes, errorMsg)) {
            return false;
        }
        QString tagErr;
        writeMetadataTags(item, outPath, preset,
                          settings.embedCoverArt, settings.resizeCoverArt, settings.maxCoverSizePx,
                          tagErr);
        errorMsg = fallbackNotes.join(QStringLiteral("; "));
        std::fprintf(stdout, "[qddbconv] Done: %s\n", outPath.toUtf8().constData());
        std::fflush(stdout);
        return true;
    }

    /* 4. Method 0 (PIPE), Method 1 (FILE), or Method 2 on .mmf/.mid/tracker/chiptune inputs:
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
        if (!decodeInputToWavWithFallback(item, settings, tmpWavPath, cancelled, fallbackNotes, errorMsg)) {
            QFile::remove(tmpWavPath);
            return false;
        }
    }

    const bool useFileArg = (preset.method == DDB_ENCODER_METHOD_FILE ||
                             preset.method == DDB_ENCODER_METHOD_FILENAME);
    const QString cmd = useFileArg
        ? buildEncoderCmdLine(preset, outPath, tmpWavPath)
        : buildEncoderCmdLine(preset, outPath, QStringLiteral("-"));

    bool encOk = executeEncoderWithFallbacks(preset, cmd, tmpWavPath, outPath, !useFileArg,
                                             cancelled, fallbackNotes, errorMsg);
    QFile::remove(tmpWavPath);
    if (!encOk)
        return false;

    QString tagErr;
    writeMetadataTags(item, outPath, preset,
                      settings.embedCoverArt, settings.resizeCoverArt, settings.maxCoverSizePx,
                      tagErr);
    errorMsg = fallbackNotes.join(QStringLiteral("; "));
    std::fprintf(stdout, "[qddbconv] Done: %s\n", outPath.toUtf8().constData());
    std::fflush(stdout);
    return true;
}
