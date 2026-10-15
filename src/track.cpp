/*
 * Qddbconv - Standalone Qt6 Audio Converter
 * Copyright (C) 2026 goberghen & Arena.ai Agent
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "track.h"
#include "smaf2mid.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTextStream>

static QString sanitizeMetaForPath(const QString &in)
{
    /* Matches _metadata_transform() in DeaDBeeF's plugins/converter/converter.c */
    QString out = in;
    const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    for (int i = 0; i < out.size(); ++i) {
        if (forbidden.contains(out[i]))
            out[i] = QLatin1Char('-');
    }
    return out.trimmed();
}

static QString findTagInsensitive(const QJsonObject &tags, const QStringList &keys)
{
    for (const QString &wanted : keys) {
        for (auto it = tags.begin(); it != tags.end(); ++it) {
            if (it.key().compare(wanted, Qt::CaseInsensitive) == 0) {
                QString val = it.value().toString().trimmed();
                if (!val.isEmpty())
                    return val;
            }
        }
    }
    return QString();
}

QString TrackProbe::findFolderCoverArt(const QString &audioOrCueFilePath)
{
    QFileInfo fi(audioOrCueFilePath);
    QDir dir = fi.absoluteDir();
    if (!dir.exists())
        return QString();

    const QString baseName = fi.completeBaseName();
    const QStringList validExts = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("png"), QStringLiteral("webp"), QStringLiteral("bmp")
    };

    auto searchInDir = [&](const QDir &d) -> QString {
        const QFileInfoList allFiles = d.entryInfoList(QDir::Files, QDir::Name);
        QFileInfoList imgFiles;
        for (const QFileInfo &candidate : allFiles) {
            if (validExts.contains(candidate.suffix().toLower()))
                imgFiles.append(candidate);
        }
        if (imgFiles.isEmpty())
            return QString();

        /* Priority 1: exact match with audio/CUE base name (e.g. Album.jpg for Album.cue) */
        for (const QFileInfo &img : imgFiles) {
            if (img.completeBaseName().compare(baseName, Qt::CaseInsensitive) == 0)
                return img.absoluteFilePath();
        }

        /* Priority 2: standard cover filenames */
        const QStringList preferredNames = {
            QStringLiteral("cover"), QStringLiteral("folder"), QStringLiteral("front"),
            QStringLiteral("album"), QStringLiteral("artwork"), QStringLiteral("art"),
            QStringLiteral("thumb"), QStringLiteral("scan")
        };
        for (const QString &pref : preferredNames) {
            for (const QFileInfo &img : imgFiles) {
                if (img.completeBaseName().compare(pref, Qt::CaseInsensitive) == 0)
                    return img.absoluteFilePath();
            }
        }

        /* Priority 3: filename contains "cover" or "front" */
        for (const QFileInfo &img : imgFiles) {
            QString lower = img.completeBaseName().toLower();
            if (lower.contains(QLatin1String("cover")) || lower.contains(QLatin1String("front")))
                return img.absoluteFilePath();
        }

        /* Priority 4: if there is only a single image file in the album folder, use it */
        if (imgFiles.size() == 1)
            return imgFiles.first().absoluteFilePath();

        return QString();
    };

    QString found = searchInDir(dir);
    if (!found.isEmpty())
        return found;

    /* Also check common artwork subdirectories */
    const QStringList subDirs = {
        QStringLiteral("Covers"), QStringLiteral("covers"),
        QStringLiteral("Artwork"), QStringLiteral("artwork"),
        QStringLiteral("Scans"), QStringLiteral("scans"),
        QStringLiteral("Art"), QStringLiteral("art")
    };
    for (const QString &sub : subDirs) {
        QDir subDir(dir.filePath(sub));
        if (subDir.exists()) {
            found = searchInDir(subDir);
            if (!found.isEmpty())
                return found;
        }
    }

    return QString();
}

QImage TrackProbe::loadTrackCoverImage(const TrackItem &item, QString *sourceDescription)
{
    if (item.coverArtPath == QLatin1String("__none__")) {
        if (sourceDescription)
            *sourceDescription = QStringLiteral("None (disabled)");
        return QImage();
    }

    if (!item.coverArtPath.isEmpty() && QFileInfo::exists(item.coverArtPath)) {
        QImage img(item.coverArtPath);
        if (!img.isNull()) {
            if (sourceDescription) {
                *sourceDescription = QStringLiteral("%1 (%2x%3)")
                                         .arg(QFileInfo(item.coverArtPath).fileName())
                                         .arg(img.width())
                                         .arg(img.height());
            }
            return img;
        }
    }

    if (item.hasEmbeddedArt) {
        QSettings s(QStringLiteral("qddbconv"), QStringLiteral("qddbconv"));
        QString ffmpegBin = s.value(QStringLiteral("ffmpegPath"), QStringLiteral("ffmpeg")).toString().trimmed();
        if (ffmpegBin.isEmpty())
            ffmpegBin = QStringLiteral("ffmpeg");

        QProcess proc;
        QStringList args = {
            QStringLiteral("-v"), QStringLiteral("quiet"),
            QStringLiteral("-i"), item.sourcePath,
            QStringLiteral("-an"),
            QStringLiteral("-vcodec"), QStringLiteral("png"),
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-f"), QStringLiteral("image2pipe"),
            QStringLiteral("pipe:1")
        };
        proc.start(ffmpegBin, args);
        if (proc.waitForFinished(4000)) {
            QByteArray pngBytes = proc.readAllStandardOutput();
            QImage img = QImage::fromData(pngBytes);
            if (!img.isNull()) {
                if (sourceDescription) {
                    *sourceDescription = QStringLiteral("Embedded (%1x%2)")
                                             .arg(img.width())
                                             .arg(img.height());
                }
                return img;
            }
        }
    }

    /* Fallback: check folder of CUE sheet or audio file if coverArtPath was not pre-populated */
    QString fallbackCover = findFolderCoverArt(item.cuePath.isEmpty() ? item.sourcePath : item.cuePath);
    if (fallbackCover.isEmpty() && !item.cuePath.isEmpty())
        fallbackCover = findFolderCoverArt(item.sourcePath);
    if (!fallbackCover.isEmpty() && QFileInfo::exists(fallbackCover)) {
        QImage img(fallbackCover);
        if (!img.isNull()) {
            if (sourceDescription) {
                *sourceDescription = QStringLiteral("%1 (%2x%3)")
                                         .arg(QFileInfo(fallbackCover).fileName())
                                         .arg(img.width())
                                         .arg(img.height());
            }
            return img;
        }
    }

    if (sourceDescription)
        *sourceDescription = QStringLiteral("No cover art found");
    return QImage();
}

TrackItem TrackProbe::probeFile(const QString &filePath)
{
    TrackItem item;
    QFileInfo fi(filePath);
    item.sourcePath = fi.absoluteFilePath();
    item.title = fi.completeBaseName();
    item.statusText = QStringLiteral("Ready");

    /* Native probe for Yamaha SMAF (.mmf) ringtone files */
    if (SmafConverter::isSmafFile(item.sourcePath)) {
        SmafConverter::probeSmafFile(item.sourcePath, item);
        item.coverArtPath = findFolderCoverArt(item.sourcePath);
        return item;
    }

    /* Native probe for Standard MIDI (.mid/.midi/.kar/.rmi) files */
    if (SmafConverter::isMidiFile(item.sourcePath)) {
        SmafConverter::probeMidiFile(item.sourcePath, item);
        item.coverArtPath = findFolderCoverArt(item.sourcePath);
        return item;
    }

    QProcess proc;
    QStringList args;
    args << QStringLiteral("-v") << QStringLiteral("quiet")
         << QStringLiteral("-print_format") << QStringLiteral("json")
         << QStringLiteral("-show_format")
         << QStringLiteral("-show_streams")
         << item.sourcePath;

    QSettings s(QStringLiteral("qddbconv"), QStringLiteral("qddbconv"));
    QString ffprobeBin = s.value(QStringLiteral("ffprobePath"), QStringLiteral("ffprobe")).toString().trimmed();
    if (ffprobeBin.isEmpty())
        ffprobeBin = QStringLiteral("ffprobe");

    proc.start(ffprobeBin, args);
    if (!proc.waitForFinished(5000)) {
        item.coverArtPath = findFolderCoverArt(item.sourcePath);
        return item;
    }

    QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    if (!doc.isObject()) {
        item.coverArtPath = findFolderCoverArt(item.sourcePath);
        return item;
    }

    QJsonObject root = doc.object();
    QJsonObject fmt = root.value(QStringLiteral("format")).toObject();
    if (fmt.contains(QStringLiteral("duration")))
        item.durationSec = fmt.value(QStringLiteral("duration")).toString().toDouble();

    QJsonObject mergedTags = fmt.value(QStringLiteral("tags")).toObject();

    /* Inspect streams for audio parameters, stream tags, and attached cover art */
    bool foundAudio = false;
    QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    for (const QJsonValue &sv : streams) {
        QJsonObject st = sv.toObject();
        QString codecType = st.value(QStringLiteral("codec_type")).toString();

        if (!foundAudio && codecType == QLatin1String("audio")) {
            foundAudio = true;
            item.codecName = st.value(QStringLiteral("codec_name")).toString();
            int sr = st.value(QStringLiteral("sample_rate")).toString().toInt();
            if (sr > 0)
                item.sampleRate = sr;
            int ch = st.value(QStringLiteral("channels")).toInt();
            if (ch > 0)
                item.channels = ch;
            int bps = st.value(QStringLiteral("bits_per_raw_sample")).toString().toInt();
            if (bps <= 0)
                bps = st.value(QStringLiteral("bits_per_sample")).toInt();
            if (bps > 0)
                item.bitsPerSample = bps;

            QJsonObject stags = st.value(QStringLiteral("tags")).toObject();
            for (auto it = stags.begin(); it != stags.end(); ++it) {
                if (!mergedTags.contains(it.key()))
                    mergedTags.insert(it.key(), it.value());
            }
        } else if (codecType == QLatin1String("video")) {
            QJsonObject disp = st.value(QStringLiteral("disposition")).toObject();
            QString vcodec = st.value(QStringLiteral("codec_name")).toString();
            if (disp.value(QStringLiteral("attached_pic")).toInt() == 1 ||
                vcodec == QLatin1String("mjpeg") ||
                vcodec == QLatin1String("png") ||
                vcodec == QLatin1String("webp") ||
                vcodec == QLatin1String("bmp")) {
                item.hasEmbeddedArt = true;
            }
        }
    }

    QString t = findTagInsensitive(mergedTags, {QStringLiteral("title")});
    if (!t.isEmpty())
        item.title = t;

    item.artist = findTagInsensitive(mergedTags, {QStringLiteral("artist"), QStringLiteral("performer")});
    item.album = findTagInsensitive(mergedTags, {QStringLiteral("album")});
    item.albumArtist = findTagInsensitive(mergedTags, {QStringLiteral("album_artist"), QStringLiteral("albumartist")});
    item.genre = findTagInsensitive(mergedTags, {QStringLiteral("genre")});
    item.comment = findTagInsensitive(mergedTags, {QStringLiteral("comment"), QStringLiteral("description")});

    QString dateStr = findTagInsensitive(mergedTags, {QStringLiteral("date"), QStringLiteral("year"), QStringLiteral("tyer")});
    if (dateStr.size() >= 4)
        item.year = dateStr.left(4);
    else
        item.year = dateStr;

    QString trk = findTagInsensitive(mergedTags, {QStringLiteral("track"), QStringLiteral("tracknumber")});
    if (!trk.isEmpty()) {
        int slash = trk.indexOf(QLatin1Char('/'));
        if (slash > 0) {
            item.totalTracks = trk.mid(slash + 1).trimmed();
            trk = trk.left(slash).trimmed();
        }
        bool ok = false;
        int n = trk.toInt(&ok);
        item.trackNumber = ok ? QStringLiteral("%1").arg(n, 2, 10, QLatin1Char('0')) : trk;
    }

    QString dsc = findTagInsensitive(mergedTags, {QStringLiteral("disc"), QStringLiteral("discnumber")});
    if (!dsc.isEmpty()) {
        int slash = dsc.indexOf(QLatin1Char('/'));
        if (slash > 0)
            dsc = dsc.left(slash).trimmed();
        item.discNumber = dsc;
    }

    /* If no embedded cover art exists in the audio file, look for cover image in the folder */
    if (!item.hasEmbeddedArt)
        item.coverArtPath = findFolderCoverArt(item.sourcePath);

    return item;
}

static QString unquoteCue(const QString &s)
{
    QString t = s.trimmed();
    if (t.size() >= 2 && t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"')))
        return t.mid(1, t.size() - 2);
    return t;
}

static double parseCueIndexTime(const QString &mmssff)
{
    /* Format: MM:SS:FF where FF is 1/75th of a second (CD audio frames) */
    QStringList parts = mmssff.trimmed().split(QLatin1Char(':'));
    if (parts.size() != 3)
        return 0.0;
    int mm = parts[0].toInt();
    int ss = parts[1].toInt();
    int ff = parts[2].toInt();
    return (mm * 60.0) + ss + (ff / 75.0);
}

QList<TrackItem> TrackProbe::parseCueSheet(const QString &cueFilePath)
{
    QList<TrackItem> result;
    QFile f(cueFilePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    QFileInfo cueFi(cueFilePath);
    QDir cueDir = cueFi.absoluteDir();
    QString cueFolderArt = findFolderCoverArt(cueFi.absoluteFilePath());

    QString albumTitle;
    QString albumPerformer;
    QString albumYear;
    QString albumGenre;
    QString currentAudioFile;
    TrackItem baseProbe;
    bool hasBaseProbe = false;

    struct CueTrackEntry {
        QString audioFile;
        int trackNum = 0;
        QString title;
        QString performer;
        double startSec = 0.0;
        bool hasIndex01 = false;
    };

    QList<CueTrackEntry> entries;
    CueTrackEntry cur;
    bool inTrack = false;

    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1String("REM "), Qt::CaseInsensitive)) {
            QString rem = line.mid(4).trimmed();
            if (rem.startsWith(QLatin1String("DATE "), Qt::CaseInsensitive))
                albumYear = unquoteCue(rem.mid(5));
            else if (rem.startsWith(QLatin1String("GENRE "), Qt::CaseInsensitive))
                albumGenre = unquoteCue(rem.mid(6));
            continue;
        }

        if (line.startsWith(QLatin1String("FILE "), Qt::CaseInsensitive)) {
            if (inTrack && cur.hasIndex01) {
                entries.append(cur);
                inTrack = false;
            }
            /* Extract filename between quotes or first token */
            int q1 = line.indexOf(QLatin1Char('"'));
            int q2 = line.lastIndexOf(QLatin1Char('"'));
            QString relFile;
            if (q1 >= 0 && q2 > q1)
                relFile = line.mid(q1 + 1, q2 - q1 - 1);
            else
                relFile = line.mid(5).section(QLatin1Char(' '), 0, 0);

            currentAudioFile = cueDir.filePath(relFile);
            continue;
        }

        if (line.startsWith(QLatin1String("TRACK "), Qt::CaseInsensitive)) {
            if (inTrack && cur.hasIndex01)
                entries.append(cur);

            QStringList tok = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            cur = CueTrackEntry();
            cur.audioFile = currentAudioFile;
            if (tok.size() >= 2)
                cur.trackNum = tok[1].toInt();
            inTrack = true;
            continue;
        }

        if (line.startsWith(QLatin1String("TITLE "), Qt::CaseInsensitive)) {
            QString val = unquoteCue(line.mid(6));
            if (inTrack)
                cur.title = val;
            else
                albumTitle = val;
            continue;
        }

        if (line.startsWith(QLatin1String("PERFORMER "), Qt::CaseInsensitive)) {
            QString val = unquoteCue(line.mid(10));
            if (inTrack)
                cur.performer = val;
            else
                albumPerformer = val;
            continue;
        }

        if (inTrack && line.startsWith(QLatin1String("INDEX 01 "), Qt::CaseInsensitive)) {
            cur.startSec = parseCueIndexTime(line.mid(9));
            cur.hasIndex01 = true;
            continue;
        }
    }

    if (inTrack && cur.hasIndex01)
        entries.append(cur);

    for (int i = 0; i < entries.size(); ++i) {
        const CueTrackEntry &e = entries[i];
        if (!hasBaseProbe || baseProbe.sourcePath != QFileInfo(e.audioFile).absoluteFilePath()) {
            baseProbe = probeFile(e.audioFile);
            hasBaseProbe = true;
        }

        TrackItem item = baseProbe;
        item.cuePath = cueFi.absoluteFilePath();
        item.title = e.title.isEmpty() ? QStringLiteral("Track %1").arg(e.trackNum) : e.title;
        item.artist = e.performer.isEmpty() ? albumPerformer : e.performer;
        item.albumArtist = albumPerformer;
        item.album = albumTitle;
        item.year = albumYear;
        item.genre = albumGenre;
        item.trackNumber = QStringLiteral("%1").arg(e.trackNum, 2, 10, QLatin1Char('0'));
        item.totalTracks = QString::number(entries.size());
        item.startSec = e.startSec;

        if (!item.hasEmbeddedArt && item.coverArtPath.isEmpty())
            item.coverArtPath = cueFolderArt;

        if (i + 1 < entries.size() && entries[i + 1].audioFile == e.audioFile) {
            item.endSec = entries[i + 1].startSec;
            item.durationSec = qMax(0.0, item.endSec - item.startSec);
        } else {
            item.endSec = 0.0;
            item.durationSec = qMax(0.0, baseProbe.durationSec - item.startSec);
        }
        item.statusText = QStringLiteral("Ready");
        result.append(item);
    }

    return result;
}

static bool isSupportedAudioExt(const QString &ext)
{
    static const QStringList exts = {
        "flac", "mp3", "mp2", "mpa", "ogg", "oga", "opus", "spx", "m4a", "m4b", "aac", "mp4",
        "wav", "wave", "w64", "wv", "ape", "mpc", "mp+", "mpp", "tta", "tak", "shn", "ofr", "ofs",
        "aiff", "aif", "aifc", "au", "snd", "caf", "voc", "wma", "ac3", "eac3", "dts", "thd", "mlp",
        "amr", "awb", "gsm", "mmf", "mid", "midi", "kar", "rmi", "ra", "rm", "qoa", "dsf", "dff", "mka", "webm", "3gp",
        /* Tracker & chiptune formats supported by FFmpeg / libopenmpt / libgme */
        "mod", "xm", "it", "s3m", "mptm", "stm", "med", "okt", "669", "far", "ult", "mtm", "amf",
        "spc", "vgm", "vgz", "nsf", "nsfe", "gbs", "hes", "ay", "sap", "sid", "kss"
    };
    return exts.contains(ext.toLower());
}

QList<TrackItem> TrackProbe::expandInputs(const QStringList &paths)
{
    QList<TrackItem> items;
    for (const QString &p : paths) {
        QFileInfo fi(p);
        if (!fi.exists())
            continue;

        if (fi.isDir()) {
            QDirIterator it(fi.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
            QStringList foundFiles;
            while (it.hasNext())
                foundFiles.append(it.next());
            foundFiles.sort();

            for (const QString &f : foundFiles) {
                QFileInfo sfi(f);
                if (sfi.suffix().compare(QLatin1String("cue"), Qt::CaseInsensitive) == 0) {
                    items.append(parseCueSheet(f));
                } else if (isSupportedAudioExt(sfi.suffix())) {
                    items.append(probeFile(f));
                }
            }
        } else if (fi.suffix().compare(QLatin1String("cue"), Qt::CaseInsensitive) == 0) {
            items.append(parseCueSheet(fi.absoluteFilePath()));
        } else {
            items.append(probeFile(fi.absoluteFilePath()));
        }
    }
    return items;
}

static QString lookupTagField(const TrackItem &item, int index, const QString &fieldName, bool &hadValue)
{
    QString f = fieldName.toLower();
    QString val;

    if (f == QLatin1String("title") || f == QLatin1String("t"))
        val = item.title;
    else if (f == QLatin1String("artist") || f == QLatin1String("a"))
        val = item.artist;
    else if (f == QLatin1String("album") || f == QLatin1String("b"))
        val = item.album;
    else if (f == QLatin1String("albumartist") || f == QLatin1String("band"))
        val = item.albumArtist.isEmpty() ? item.artist : item.albumArtist;
    else if (f == QLatin1String("tracknumber") || f == QLatin1String("track") || f == QLatin1String("n"))
        val = item.trackNumber;
    else if (f == QLatin1String("totaltracks"))
        val = item.totalTracks;
    else if (f == QLatin1String("disc") || f == QLatin1String("discnumber"))
        val = item.discNumber;
    else if (f == QLatin1String("year") || f == QLatin1String("date") || f == QLatin1String("y"))
        val = item.year;
    else if (f == QLatin1String("genre") || f == QLatin1String("g"))
        val = item.genre;
    else if (f == QLatin1String("comment") || f == QLatin1String("c"))
        val = item.comment;
    else if (f == QLatin1String("filename") || f == QLatin1String("f"))
        val = QFileInfo(item.sourcePath).completeBaseName();
    else if (f == QLatin1String("list_index"))
        val = QString::number(index + 1);

    val = sanitizeMetaForPath(val);
    if (!val.isEmpty())
        hadValue = true;
    return val;
}

/* Recursive evaluator supporting DeaDBeeF conditional brackets [...] and %field% tags */
static QString evalSegment(const TrackItem &item, int index, const QString &seg, bool &anyFieldNonEmpty)
{
    QString out;
    int i = 0;
    while (i < seg.size()) {
        if (seg[i] == QLatin1Char('[')) {
            /* Find matching closing bracket ']' */
            int depth = 1;
            int j = i + 1;
            while (j < seg.size() && depth > 0) {
                if (seg[j] == QLatin1Char('['))
                    ++depth;
                else if (seg[j] == QLatin1Char(']'))
                    --depth;
                ++j;
            }
            QString inner = seg.mid(i + 1, (j - i - 2));
            bool innerHadVal = false;
            QString evaluatedInner = evalSegment(item, index, inner, innerHadVal);
            if (innerHadVal) {
                out += evaluatedInner;
                anyFieldNonEmpty = true;
            }
            i = j;
        } else if (seg[i] == QLatin1Char('%')) {
            int endPct = seg.indexOf(QLatin1Char('%'), i + 1);
            if (endPct > i + 1) {
                QString fieldName = seg.mid(i + 1, endPct - i - 1);
                bool fieldNonEmpty = false;
                out += lookupTagField(item, index, fieldName, fieldNonEmpty);
                if (fieldNonEmpty)
                    anyFieldNonEmpty = true;
                i = endPct + 1;
            } else {
                out += seg[i];
                ++i;
            }
        } else {
            out += seg[i];
            ++i;
        }
    }
    return out;
}

QString TrackProbe::evaluateTitleFormat(const TrackItem &item, int index, const QString &pattern)
{
    QString pat = pattern.trimmed().isEmpty()
        ? QStringLiteral("[%tracknumber%. ][%artist% - ]%title%")
        : pattern;
    bool dummy = false;
    QString res = evalSegment(item, index, pat, dummy).trimmed();
    if (res.isEmpty())
        res = QFileInfo(item.sourcePath).completeBaseName();
    return res;
}

QString TrackProbe::commonRootFolder(const QList<TrackItem> &items)
{
    if (items.isEmpty())
        return QString();

    QString root = QFileInfo(items[0].sourcePath).absolutePath();
    for (int i = 1; i < items.size(); ++i) {
        QString path = QFileInfo(items[i].sourcePath).absolutePath();
        while (!path.startsWith(root) && !root.isEmpty()) {
            int slash = root.lastIndexOf(QLatin1Char('/'));
            if (slash <= 0) {
                root.clear();
                break;
            }
            root = root.left(slash);
        }
    }

    /* If all tracks reside inside a single album folder (so root equals that folder itself),
     * step up one parent level so Preserve Folder Structure keeps the album folder's name! */
    for (const TrackItem &it : items) {
        if (QFileInfo(it.sourcePath).absolutePath() == root) {
            int slash = root.lastIndexOf(QLatin1Char('/'));
            if (slash > 0)
                root = root.left(slash);
            break;
        }
    }

    return root;
}

QString TrackProbe::computeOutputPath(const TrackItem &item,
                                      int index,
                                      const QString &outFolderUser,
                                      const QString &outFilePattern,
                                      const EncoderPreset &preset,
                                      bool preserveFolderStructure,
                                      const QString &rootFolder,
                                      bool writeToSourceFolder)
{
    QFileInfo srcInfo(item.sourcePath);
    QString baseOutFolder;

    if (writeToSourceFolder) {
        baseOutFolder = srcInfo.absolutePath();
    } else if (preserveFolderStructure) {
        QString userBase = outFolderUser.trimmed().isEmpty() ? QDir::homePath() : outFolderUser.trimmed();
        QString srcDir = srcInfo.absolutePath();
        QString subPath = srcDir.mid(rootFolder.size());
        if (!subPath.startsWith(QLatin1Char('/')))
            subPath.prepend(QLatin1Char('/'));
        baseOutFolder = userBase + subPath;
    } else {
        baseOutFolder = outFolderUser.trimmed().isEmpty() ? QDir::homePath() : outFolderUser.trimmed();
    }

    QString relName = evaluateTitleFormat(item, index, outFilePattern);
    while (relName.startsWith(QLatin1Char('/')))
        relName.remove(0, 1);

    QString ext = preset.ext.trimmed();
    if (ext.isEmpty())
        ext = srcInfo.suffix();

    QString fullPath = QDir::cleanPath(baseOutFolder + QLatin1Char('/') + relName);
    if (!ext.isEmpty())
        fullPath += QLatin1Char('.') + ext;

    return fullPath;
}
