/*
 * Qddbconv - Standalone Qt6 Audio Converter
 * Native Yamaha SMAF (.mmf) to Standard MIDI (.mid) & WAV Converter
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

#include "smaf2mid.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

namespace {

static QString resolveSynthToolBin(const QString &name,
                                   const QString &customBinOverride = QString(),
                                   bool useOverrides = false)
{
    QString configured;
    if (useOverrides) {
        configured = customBinOverride.trimmed();
    } else if (!customBinOverride.trimmed().isEmpty()) {
        configured = customBinOverride.trimmed();
    } else {
        QSettings s(QStringLiteral("qddbconv"), QStringLiteral("qddbconv"));
        const QString key = (name == QLatin1String("timidity"))
            ? QStringLiteral("timidityPath")
            : QStringLiteral("fluidsynthPath");
        configured = s.value(key, name).toString().trimmed();
    }

    if (!configured.isEmpty() &&
        configured.compare(name, Qt::CaseInsensitive) != 0 &&
        configured.compare(name + QLatin1String(".exe"), Qt::CaseInsensitive) != 0) {
        return configured;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        const QStringList subDirs = {
            appDir,
            appDir + QLatin1String("/tools"),
            appDir + QLatin1String("/bin")
        };
        for (const QString &dir : subDirs) {
#ifdef Q_OS_WIN
            QString candExe = dir + QLatin1Char('/') + name + QLatin1String(".exe");
            if (QFileInfo::exists(candExe))
                return candExe;
#endif
            QString cand = dir + QLatin1Char('/') + name;
            if (QFileInfo::exists(cand))
                return cand;
        }
    }

#ifdef Q_OS_WIN
    QStringList winCandidates;
    if (name == QLatin1String("timidity")) {
        winCandidates << QStringLiteral("C:/timidity/timidity.exe")
                      << QStringLiteral("C:/TiMidity++/timidity.exe")
                      << QStringLiteral("C:/Program Files/TiMidity++/timidity.exe")
                      << QStringLiteral("C:/Program Files (x86)/TiMidity++/timidity.exe");
    } else if (name == QLatin1String("fluidsynth")) {
        winCandidates << QStringLiteral("C:/fluidsynth/bin/fluidsynth.exe")
                      << QStringLiteral("C:/Program Files/FluidSynth/bin/fluidsynth.exe")
                      << QStringLiteral("C:/Program Files (x86)/FluidSynth/bin/fluidsynth.exe");
    }
    for (const QString &cand : winCandidates) {
        if (QFileInfo::exists(cand))
            return cand;
    }
#endif

    return configured.isEmpty() ? name : configured;
}

inline uint32_t readBe32(const uint8_t *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

inline uint16_t readBe16(const uint8_t *p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline bool matchPrefix3(const uint8_t *p, const char *s)
{
    return p[0] == static_cast<uint8_t>(s[0]) &&
           p[1] == static_cast<uint8_t>(s[1]) &&
           p[2] == static_cast<uint8_t>(s[2]);
}

inline bool isPrintableId3(const uint8_t *p)
{
    for (int i = 0; i < 3; ++i) {
        if (p[i] < 0x20 || p[i] > 0x7E)
            return false;
    }
    return true;
}

/* SMAF timebase code to milliseconds per tick (Yamaha SMAF spec / TrackChunk.java) */
double smafTimeBaseMs(int code)
{
    switch (code & 0xFF) {
    case 0x00: return 1.0;
    case 0x01: return 2.0;
    case 0x02: return 4.0;
    case 0x03: return 5.0;
    case 0x10: return 10.0;
    case 0x11: return 20.0;
    case 0x12: return 40.0;
    case 0x13: return 50.0;
    default:
        switch (code & 0x0F) {
        case 0x00: return 1.0;
        case 0x01: return 2.0;
        case 0x02: return 4.0;
        case 0x03: return 5.0;
        default:   return 4.0;
        }
    }
}

/* HandyPhone Standard 1-or-2-byte variable length quantity */
uint32_t readHpsVarLen(const uint8_t *&p, const uint8_t *end)
{
    if (p >= end)
        return 0;
    uint8_t b0 = *p++;
    if ((b0 & 0x80) == 0)
        return b0;
    if (p >= end)
        return b0 & 0x7F;
    uint8_t b1 = *p++;
    return (static_cast<uint32_t>((b0 & 0x7F) + 1) << 7) | b1;
}

/* Mobile Standard / MIDI 7-bit variable length quantity */
uint32_t readMidiVlq(const uint8_t *&p, const uint8_t *end)
{
    uint32_t val = 0;
    for (int i = 0; i < 5 && p < end; ++i) {
        uint8_t b = *p++;
        val = (val << 7) | (b & 0x7F);
        if ((b & 0x80) == 0)
            break;
    }
    return val;
}

void writeMidiVlq(QByteArray &out, uint32_t val)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = static_cast<uint8_t>(val & 0x7F);
    while ((val >>= 7) > 0 && n < 5)
        buf[n++] = static_cast<uint8_t>((val & 0x7F) | 0x80);
    for (int i = n - 1; i >= 0; --i)
        out.append(static_cast<char>(buf[i]));
}

void appendBe32(QByteArray &out, uint32_t v)
{
    out.append(static_cast<char>((v >> 24) & 0xFF));
    out.append(static_cast<char>((v >> 16) & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
    out.append(static_cast<char>(v & 0xFF));
}

void appendBe16(QByteArray &out, uint16_t v)
{
    out.append(static_cast<char>((v >> 8) & 0xFF));
    out.append(static_cast<char>(v & 0xFF));
}

void appendLe32(QByteArray &out, uint32_t v)
{
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
    out.append(static_cast<char>((v >> 16) & 0xFF));
    out.append(static_cast<char>((v >> 24) & 0xFF));
}

void appendLe16(QByteArray &out, uint16_t v)
{
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
}

/* Decompress Mobile Standard Format 0x01 (Okumura Huffman bitstream) */
std::vector<uint8_t> inflateSmafHuffman(const uint8_t *p, size_t n)
{
    std::vector<uint8_t> out;
    if (n < 4)
        return out;
    uint32_t outLen = readBe32(p);
    if (outLen == 0 || outLen > (8u << 20))
        return out;

    const uint8_t *bits = p + 4;
    const size_t bitBytes = n - 4;
    size_t bitPos = 0;

    auto readBit = [&]() -> int {
        if ((bitPos >> 3) >= bitBytes)
            return -1;
        int b = (bits[bitPos >> 3] >> (7 - (bitPos & 7))) & 1;
        ++bitPos;
        return b;
    };

    auto readByte = [&]() -> int {
        int v = 0;
        for (int i = 0; i < 8; ++i) {
            int b = readBit();
            if (b < 0)
                return -1;
            v = (v << 1) | b;
        }
        return v;
    };

    std::array<int, 512> leftChild{};
    std::array<int, 512> rightChild{};
    int nextInternal = 256;
    bool failed = false;

    std::function<int(int)> buildTree = [&](int depth) -> int {
        if (failed || depth > 256) {
            failed = true;
            return 0;
        }
        int b = readBit();
        if (b < 0) {
            failed = true;
            return 0;
        }
        if (b == 1) {
            if (nextInternal >= 511) {
                failed = true;
                return 0;
            }
            int idx = nextInternal++;
            leftChild[idx] = buildTree(depth + 1);
            rightChild[idx] = buildTree(depth + 1);
            return idx;
        }
        int leaf = readByte();
        if (leaf < 0) {
            failed = true;
            return 0;
        }
        return leaf;
    };

    int root = buildTree(0);
    if (failed)
        return {};

    out.reserve(outLen);
    for (uint32_t i = 0; i < outLen; ++i) {
        int node = root;
        int guard = 0;
        while (node >= 256 && guard++ < 256) {
            int b = readBit();
            if (b < 0)
                return out;
            node = b ? rightChild[node] : leftChild[node];
        }
        out.push_back(static_cast<uint8_t>(node & 0xFF));
    }
    return out;
}

struct SmafWaveChunk {
    int waveNumber = 0;
    uint32_t sampleRate = 8000;
    uint8_t channels = 1;  /* 1 = mono, 2 = planar stereo */
    uint8_t format = 2;    /* 0 = signed PCM, 1 = unsigned offset PCM, 2 = Yamaha MA ADPCM */
    uint8_t bits = 4;      /* 4, 8, 12, 16 */
    std::vector<uint8_t> rawBytes;
};

struct SmafWaveTrigger {
    uint64_t startMs = 0;
    uint64_t durationMs = 0;
    size_t trackIdx = 0;
    int waveNumber = 1;
    uint32_t sampleRateOverride = 0;
    int pitchSemitones = 0;
    uint8_t velocity = 100;
    uint8_t volume = 100;
    uint8_t expression = 127;
    uint8_t masterVolume = 127;
    uint8_t pan = 64;
};

struct SmafScoreTrack {
    int trackNumber = 0;
    bool isAudioTrack = false;
    int formatType = 0;
    int sequenceType = 0;
    int durationTimeBase = 2;
    int gateTimeBase = 2;
    uint8_t atrWaveType0 = 0;
    uint8_t atrWaveType1 = 0;
    bool hasStartPoint = false;
    bool hasStopPoint = false;
    uint32_t startPointOffset = 0;
    uint32_t stopPointOffset = 0;
    uint64_t parsedStartMs = 0;
    uint64_t parsedStopMs = 0;
    bool hasParsedStopMs = false;
    std::vector<uint8_t> channelStatus;
    std::vector<uint8_t> setupData;
    std::vector<uint8_t> sequenceData;
    std::vector<SmafWaveChunk> waves;
};

struct MidiTimedEvent {
    uint64_t tickMs = 0;
    int priority = 0; /* 0 = CC/PC/PitchBend, 1 = NoteOff, 2 = NoteOn */
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    int byteCount = 2; /* 2 for PC, 3 for Note/CC/PitchBend */
};

struct ParsedSmafContainer {
    QString title;
    QString artist;
    QString comment;
    std::vector<SmafScoreTrack> tracks;
    std::vector<MidiTimedEvent> midiEvents;
    std::vector<SmafWaveTrigger> waveTriggers;
    uint64_t durationMs = 0;
};

static QString decodeSmafString(const uint8_t *p, size_t n)
{
    QByteArray raw;
    raw.reserve(static_cast<int>(n));
    for (size_t i = 0; i < n && p[i] != 0; ++i) {
        if (p[i] >= 0x20 && p[i] < 0x7F)
            raw.append(static_cast<char>(p[i]));
        else if (p[i] >= 0x80)
            raw.append(static_cast<char>(p[i]));
    }
    QString utf8 = QString::fromUtf8(raw);
    if (!utf8.contains(QChar::ReplacementCharacter))
        return utf8.trimmed();
    return QString::fromLatin1(raw).trimmed();
}

static void parseOpdaChunk(const uint8_t *p, size_t n, ParsedSmafContainer &out)
{
    size_t pos = 0;
    while (pos + 8 <= n) {
        const uint8_t *c = p + pos;
        if (!matchPrefix3(c, "Dch")) {
            ++pos;
            continue;
        }
        uint32_t sz = readBe32(c + 4);
        size_t body = pos + 8;
        if (body + sz > n)
            sz = static_cast<uint32_t>(n - body);

        const uint8_t *b = p + body;
        size_t i = 0;
        while (i + 4 <= sz) {
            char k0 = static_cast<char>(b[i]);
            char k1 = static_cast<char>(b[i + 1]);
            if (!(k0 >= 'A' && k0 <= 'Z' && k1 >= 'A' && k1 <= 'Z')) {
                ++i;
                continue;
            }
            uint16_t vlen = readBe16(b + i + 2);
            if (i + 4 + vlen > sz) {
                ++i;
                continue;
            }
            QString val = decodeSmafString(b + i + 4, vlen);
            if (k0 == 'S' && k1 == 'T' && !val.isEmpty())
                out.title = val;
            else if (((k0 == 'A' && k1 == 'N') || (k0 == 'A' && k1 == 'R')) && out.artist.isEmpty() && !val.isEmpty())
                out.artist = val;
            else if (((k0 == 'C' && k1 == 'R') || (k0 == 'V' && k1 == 'N') || (k0 == 'S' && k1 == 'W')) && out.comment.isEmpty() && !val.isEmpty())
                out.comment = val;
            i += 4 + vlen;
        }
        pos = body + sz;
    }
}

static void parseWaveSubchunks(const uint8_t *p, size_t n, SmafScoreTrack &trk)
{
    size_t pos = 0;
    while (pos + 8 <= n) {
        const uint8_t *c = p + pos;
        if (!matchPrefix3(c, "Mwa") && !matchPrefix3(c, "Awa")) {
            ++pos;
            continue;
        }
        uint32_t sz = readBe32(c + 4);
        size_t body = pos + 8;
        if (body + sz > n)
            sz = static_cast<uint32_t>(n - body);

        if (matchPrefix3(c, "Mwa") && sz > 3) {
            /* StreamWaveDataChunk (Mwa* inside Mtsp): 3-byte WaveType header:
             *   b[0]: bit 7 = stereo (1=2ch, 0=1ch),
             *         bits 6..4 = format (0=signed PCM, 1=unsigned offset PCM, 2=Yamaha MA ADPCM),
             *         bits 3..0 = bit depth code ((code + 1) * 4 -> 4, 8, 12, 16 bits)
             *   b[1..2]: 16-bit big-endian sample rate in Hz (e.g. 8000, 11025, 16000, 22050, 44100) */
            SmafWaveChunk w;
            w.waveNumber = c[3];
            uint8_t fmtByte = p[body];
            w.channels = (fmtByte & 0x80) ? 2 : 1;
            w.format = (fmtByte >> 4) & 0x07;
            w.bits = static_cast<uint8_t>(((fmtByte & 0x0F) + 1) * 4);
            uint16_t beRate = readBe16(p + body + 1);
            w.sampleRate = (beRate >= 2000 && beRate <= 96000) ? beRate : 8000;
            w.rawBytes.assign(p + body + 3, p + body + sz);
            trk.waves.push_back(std::move(w));
        } else if (matchPrefix3(c, "Awa") && sz > 0) {
            /* WaveDataChunk (Awa* inside ATR*): 0-byte inner header; wave format and sample
             * rate come from the enclosing PcmAudioTrackChunk (ATR*) 2-byte WaveType header. */
            SmafWaveChunk w;
            w.waveNumber = c[3];
            w.channels = (trk.atrWaveType0 & 0x80) ? 2 : 1;
            uint8_t atrFmt = (trk.atrWaveType0 >> 4) & 0x07;
            /* In ATR WaveType, 0 = signed PCM, 1 = Yamaha ADPCM, 4 = unsigned PCM */
            if (atrFmt == 0)
                w.format = 0;
            else if (atrFmt == 4)
                w.format = 1;
            else
                w.format = 2;
            w.bits = static_cast<uint8_t>((((trk.atrWaveType1 >> 4) & 0x0F) + 1) * 4);
            switch (trk.atrWaveType0 & 0x0F) {
            case 0: w.sampleRate = 4000; break;
            case 1: w.sampleRate = 8000; break;
            case 2: w.sampleRate = 11025; break;
            case 3: w.sampleRate = 22050; break;
            case 4: w.sampleRate = 44100; break;
            default: w.sampleRate = 8000; break;
            }
            w.rawBytes.assign(p + body, p + body + sz);
            trk.waves.push_back(std::move(w));
        }
        pos = body + sz;
    }
}

static void parseSeekPhraseInfoChunk(const uint8_t *p, size_t n, SmafScoreTrack &trk)
{
    /* Seek & Phrase Info chunk (MspI inside MTR* / AspI inside ATR*):
     *   "st:" + 4-byte BE sequence byte offset + ',' (8 bytes)
     *   "sp:" + 4-byte BE sequence byte offset + ',' (8 bytes)
     *   "Px:" + 4-byte BE start offset + 4-byte BE stop offset + ',' (12 bytes) */
    size_t pos = 0;
    while (pos + 7 <= n) {
        if (p[pos] == 's' && p[pos + 1] == 't' && p[pos + 2] == ':') {
            trk.hasStartPoint = true;
            trk.startPointOffset = readBe32(p + pos + 3);
            pos += (pos + 8 <= n && p[pos + 7] == ',') ? 8 : 7;
        } else if (p[pos] == 's' && p[pos + 1] == 'p' && p[pos + 2] == ':') {
            trk.hasStopPoint = true;
            trk.stopPointOffset = readBe32(p + pos + 3);
            pos += (pos + 8 <= n && p[pos + 7] == ',') ? 8 : 7;
        } else if (p[pos] == 'P' && p[pos + 2] == ':' && pos + 11 <= n) {
            pos += (pos + 12 <= n && p[pos + 11] == ',') ? 12 : 11;
        } else {
            ++pos;
        }
    }
}

static void decodeAtrAudioTrack(SmafScoreTrack &t,
                                size_t trackIdx,
                                std::vector<SmafWaveTrigger> &waveTriggers)
{
    if (t.sequenceData.empty() || t.waves.empty())
        return;

    std::vector<uint8_t> inflated;
    const uint8_t *seqStart = t.sequenceData.data();
    size_t seqLen = t.sequenceData.size();
    if ((t.formatType & 0x0F) == 0x02) {
        inflated = inflateSmafHuffman(seqStart, seqLen);
        if (inflated.empty())
            return;
        seqStart = inflated.data();
        seqLen = inflated.size();
    }

    const uint8_t *p = seqStart;
    const uint8_t *end = seqStart + seqLen;
    const double tbD = smafTimeBaseMs(t.durationTimeBase);
    const double tbG = smafTimeBaseMs(t.gateTimeBase);

    uint8_t atrVel = 127;
    uint8_t chVol[4] = {100, 100, 100, 100};
    uint8_t chExp[4] = {127, 127, 127, 127};
    uint8_t chPan[4] = {64, 64, 64, 64};

    size_t firstTrigIdx = waveTriggers.size();
    double curMs = 0.0;
    int guard = 0;
    while (p < end && guard++ < 1000000) {
        size_t eventOffset = static_cast<size_t>(p - seqStart);
        if (t.hasStartPoint && eventOffset == t.startPointOffset)
            t.parsedStartMs = static_cast<uint64_t>(std::llround(curMs));
        if (t.hasStopPoint && eventOffset >= t.stopPointOffset) {
            t.parsedStopMs = static_cast<uint64_t>(std::llround(curMs));
            t.hasParsedStopMs = true;
            break;
        }

        uint32_t dur = readHpsVarLen(p, end);
        curMs += dur * tbD;
        if (p >= end)
            break;

        uint8_t e1 = *p++;
        uint64_t tick = static_cast<uint64_t>(std::llround(curMs));

        if (e1 == 0xFF) {
            if (p >= end)
                break;
            uint8_t e2 = *p++;
            if (e2 == 0x00)
                continue;
            if (e2 == 0x2F) {
                if (p < end && *p == 0x00)
                    ++p;
                t.parsedStopMs = tick;
                t.hasParsedStopMs = true;
                break;
            }
            if (e2 == 0xF0) {
                if (p >= end)
                    break;
                uint8_t len = *p++;
                p += std::min<size_t>(len, static_cast<size_t>(end - p));
                continue;
            }
            if (p >= end)
                break;
            uint8_t len = *p++;
            p += std::min<size_t>(len, static_cast<size_t>(end - p));
            continue;
        }

        if (e1 == 0x00) {
            if (p >= end)
                break;
            uint8_t e2 = *p++;
            if (e2 == 0x00) {
                if (p >= end)
                    break;
                uint8_t e3 = *p++;
                if (e3 == 0x00) {
                    t.parsedStopMs = tick;
                    t.hasParsedStopMs = true;
                    break;
                }
                continue;
            }
            int localCh = (e2 >> 6) & 3;
            int cls = (e2 >> 4) & 3;
            int data = e2 & 0x0F;
            if (cls == 3) {
                if (p >= end)
                    break;
                uint8_t v = *p++;
                if (e2 == 0x37) {
                    /* ATR stream velocity (00 37 <vel>) */
                    atrVel = v & 0x7F;
                } else if (data == 0x7) {
                    chVol[localCh] = v & 0x7F;
                } else if (data == 0xA) {
                    chPan[localCh] = v & 0x7F;
                } else if (data == 0xB) {
                    chExp[localCh] = v & 0x7F;
                }
            } else if (cls == 0) {
                chExp[localCh] = static_cast<uint8_t>((data <= 1) ? 0 : std::min(127, data * 8 + 15));
            }
            continue;
        }

        /* Atsq WaveMessage: read gate-time VLQ; bits 7..6 == 0 triggers Awa[bits 5..0] (1..62) */
        int localCh = (e1 >> 6) & 3;
        int waveNum = e1 & 0x3F;
        uint32_t gate = readHpsVarLen(p, end);
        if (gate == 0 || (e1 & 0xC0) != 0 || waveNum <= 0 || waveNum > 62)
            continue;
        if (t.hasStartPoint && eventOffset < t.startPointOffset)
            continue;

        SmafWaveTrigger trig;
        trig.startMs = tick;
        trig.durationMs = static_cast<uint64_t>(std::max(1.0, std::round(gate * tbG)));
        trig.trackIdx = trackIdx;
        trig.waveNumber = waveNum;
        trig.pitchSemitones = 0;
        trig.velocity = atrVel;
        trig.volume = chVol[localCh];
        trig.expression = chExp[localCh];
        trig.pan = chPan[localCh];
        waveTriggers.push_back(trig);
    }

    if (!t.hasParsedStopMs) {
        t.parsedStopMs = static_cast<uint64_t>(std::llround(curMs));
        t.hasParsedStopMs = true;
    }

    /* Clamp any ATR wave trigger extending past track stop point */
    for (size_t i = firstTrigIdx; i < waveTriggers.size(); ++i) {
        SmafWaveTrigger &trig = waveTriggers[i];
        if (trig.startMs >= t.parsedStopMs) {
            trig.durationMs = 0;
        } else if (trig.startMs + trig.durationMs > t.parsedStopMs) {
            trig.durationMs = t.parsedStopMs - trig.startMs;
        }
    }
}

static void decodeHandyPhoneTrackToMidi(SmafScoreTrack &t,
                                        size_t trackIdx,
                                        int baseCh,
                                        std::vector<MidiTimedEvent> &events,
                                        std::vector<SmafWaveTrigger> &waveTriggers)
{
    const uint8_t *seqStart = t.sequenceData.data();
    const uint8_t *p = seqStart;
    const uint8_t *end = seqStart + t.sequenceData.size();
    const double tbD = smafTimeBaseMs(t.durationTimeBase);
    const double tbG = smafTimeBaseMs(t.gateTimeBase);

    bool isDrum[4] = {false, false, false, false};
    uint8_t chBank[4] = {0, 0, 0, 0};
    uint8_t chProg[4] = {0, 0, 0, 0};
    int octaveShiftSemis[4] = {0, 0, 0, 0};
    uint8_t chVol[4] = {100, 100, 100, 100};
    uint8_t chExp[4] = {127, 127, 127, 127};
    uint8_t chPan[4] = {64, 64, 64, 64};

    for (int i = 0; i < 4 && (i / 2) < static_cast<int>(t.channelStatus.size()); ++i) {
        uint8_t nib = (i % 2 == 0) ? (t.channelStatus[i / 2] >> 4) : (t.channelStatus[i / 2] & 0x0F);
        if ((nib & 0x03) == 3) {
            isDrum[i] = true;
            chBank[i] = 0x80;
        }
    }

    auto mapMidiCh = [&](int localCh) -> uint8_t {
        int c = localCh & 3;
        if (isDrum[c] || (chBank[c] & 0x80) != 0)
            return 9; /* Standard MIDI Channel 10 for percussion */
        int ch = baseCh + c;
        if (ch >= 9)
            ++ch; /* Skip MIDI channel 9 (reserved for GM percussion) */
        return static_cast<uint8_t>(ch & 0x0F);
    };

    size_t firstEventIdx = events.size();
    size_t firstTrigIdx = waveTriggers.size();
    double curMs = 0.0;
    int guard = 0;
    while (p < end && guard++ < 1000000 && events.size() < 400000) {
        size_t eventOffset = static_cast<size_t>(p - seqStart);
        if (t.hasStartPoint && eventOffset == t.startPointOffset)
            t.parsedStartMs = static_cast<uint64_t>(std::llround(curMs));
        if (t.hasStopPoint && eventOffset >= t.stopPointOffset) {
            t.parsedStopMs = static_cast<uint64_t>(std::llround(curMs));
            t.hasParsedStopMs = true;
            break;
        }

        uint32_t dur = readHpsVarLen(p, end);
        curMs += dur * tbD;
        if (p >= end)
            break;

        uint8_t e1 = *p++;
        uint64_t tick = static_cast<uint64_t>(std::llround(curMs));

        if (e1 == 0xFF) {
            if (p >= end)
                break;
            uint8_t e2 = *p++;
            if (e2 == 0x00)
                continue;
            if (e2 == 0x2F) {
                if (p < end && *p == 0x00)
                    ++p;
                t.parsedStopMs = tick;
                t.hasParsedStopMs = true;
                break;
            }
            if (e2 == 0xF0) {
                uint32_t len = readHpsVarLen(p, end);
                if (len > static_cast<size_t>(end - p))
                    break;
                p += len;
                continue;
            }
            if (p >= end)
                break;
            uint8_t len = *p++;
            p += std::min<size_t>(len, static_cast<size_t>(end - p));
            continue;
        }

        if (e1 == 0x00) {
            if (p >= end)
                break;
            uint8_t e2 = *p++;
            if (e2 == 0x00) {
                if (p >= end)
                    break;
                uint8_t e3 = *p++;
                if (e3 == 0x00) {
                    t.parsedStopMs = tick;
                    t.hasParsedStopMs = true;
                    break; /* End of sequence */
                }
                continue;
            }

            int localCh = (e2 >> 6) & 3;
            int cls = (e2 >> 4) & 3;
            int data = e2 & 0x0F;
            uint8_t mch = mapMidiCh(localCh);

            if (cls == 3) {
                if (p >= end)
                    break;
                uint8_t v = *p++;
                switch (data) {
                case 0x0: { /* Program Change */
                    chProg[localCh] = v & 0x7F;
                    if (mch == 9) {
                        /* In HPS Drum mode (chBank & 0x80), Program Change selects the GM
                         * percussion note number (24..84) for this channel; keep Drum Kit 0 on ch 9. */
                        events.push_back({tick, 0, static_cast<uint8_t>(0xC0 | mch), 0, 0, 2});
                    } else {
                        events.push_back({tick, 0, static_cast<uint8_t>(0xC0 | mch), chProg[localCh], 0, 2});
                    }
                    break;
                }
                case 0x1: /* Bank Select (bit 7 indicates drum bank 0x80) */
                    if (v & 0x80) {
                        chBank[localCh] = 0x80;
                        isDrum[localCh] = true;
                    } else {
                        chBank[localCh] = v & 0x7F;
                        isDrum[localCh] = false;
                    }
                    mch = mapMidiCh(localCh);
                    if (mch != 9) {
                        events.push_back({tick, 0, static_cast<uint8_t>(0xB0 | mch), 0, 0, 3});
                        events.push_back({tick, 0, static_cast<uint8_t>(0xB0 | mch), 32, 0, 3});
                    }
                    break;
                case 0x2: /* Octave shift */
                    if (v >= 0x81 && v <= 0x84)
                        octaveShiftSemis[localCh] = -12 * (v - 0x80);
                    else if (v >= 0x01 && v <= 0x04)
                        octaveShiftSemis[localCh] = 12 * v;
                    else
                        octaveShiftSemis[localCh] = 0;
                    break;
                case 0x4: { /* Pitch Bend */
                    int pb = std::clamp(static_cast<int>(v) * 128, 0, 16383);
                    events.push_back({tick, 0, static_cast<uint8_t>(0xE0 | mch),
                                      static_cast<uint8_t>(pb & 0x7F), static_cast<uint8_t>((pb >> 7) & 0x7F), 3});
                    break;
                }
                case 0x7: /* Volume */
                    chVol[localCh] = v & 0x7F;
                    events.push_back({tick, 0, static_cast<uint8_t>(0xB0 | mch), 7, static_cast<uint8_t>(v & 0x7F), 3});
                    break;
                case 0xA: /* Pan */
                    chPan[localCh] = v & 0x7F;
                    events.push_back({tick, 0, static_cast<uint8_t>(0xB0 | mch), 10, static_cast<uint8_t>(v & 0x7F), 3});
                    break;
                case 0xB: /* Expression */
                    chExp[localCh] = v & 0x7F;
                    events.push_back({tick, 0, static_cast<uint8_t>(0xB0 | mch), 11, static_cast<uint8_t>(v & 0x7F), 3});
                    break;
                default:
                    break;
                }
            } else if (cls == 0) {
                int v = (data <= 1) ? 0 : std::min(127, data * 8 + 15);
                chExp[localCh] = static_cast<uint8_t>(v);
                events.push_back({tick, 0, static_cast<uint8_t>(0xB0 | mch), 11, static_cast<uint8_t>(v), 3});
            } else if (cls == 1) {
                int pb = std::clamp((data * 8) << 7, 0, 16383);
                events.push_back({tick, 0, static_cast<uint8_t>(0xE0 | mch),
                                  static_cast<uint8_t>(pb & 0x7F), static_cast<uint8_t>((pb >> 7) & 0x7F), 3});
            } else if (cls == 2) {
                static const uint8_t modTab[16] = {0, 0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 96, 112, 127, 127};
                events.push_back({tick, 0, static_cast<uint8_t>(0xB0 | mch), 1, modTab[data & 15], 3});
            }
            continue;
        }

        /* Note event: cc oo nnnn */
        int localCh = (e1 >> 6) & 3;
        int octave = (e1 >> 4) & 3;
        int noteNib = e1 & 0x0F;
        uint32_t gate = readHpsVarLen(p, end);
        if (gate == 0)
            continue;
        if (t.hasStartPoint && eventOffset < t.startPointOffset)
            continue;

        uint64_t gateMs = static_cast<uint64_t>(std::max(1.0, std::round(gate * tbG)));

        /* If HandyPhone MTR track carries embedded Mwa/Awa waves and triggers low wave index on drum/stream ch */
        if (!t.waves.empty() && (isDrum[localCh] || (chBank[localCh] & 0x80) != 0) && octave == 0 && noteNib > 0) {
            bool hasMatchingWave = false;
            for (const SmafWaveChunk &w : t.waves) {
                if (w.waveNumber == noteNib) {
                    hasMatchingWave = true;
                    break;
                }
            }
            if (hasMatchingWave) {
                SmafWaveTrigger trig;
                trig.startMs = tick;
                trig.durationMs = gateMs;
                trig.trackIdx = trackIdx;
                trig.waveNumber = noteNib;
                trig.pitchSemitones = 0;
                trig.velocity = 100;
                trig.volume = chVol[localCh];
                trig.expression = chExp[localCh];
                trig.pan = chPan[localCh];
                waveTriggers.push_back(trig);
                continue;
            }
        }

        uint8_t mch = mapMidiCh(localCh);
        int midiNote = 0;
        if ((chBank[localCh] & 0x80) != 0 || isDrum[localCh]) {
            /* In HandyPhone Standard Drum Mode, Program Change (24..84) specifies the GM percussion note */
            if (chProg[localCh] >= 24 && chProg[localCh] <= 84)
                midiNote = chProg[localCh];
            else
                midiNote = std::clamp(noteNib + octave * 12 + 36 + octaveShiftSemis[localCh], 24, 84);
        } else {
            midiNote = std::clamp(noteNib + octave * 12 + 36 + octaveShiftSemis[localCh], 0, 127);
        }
        uint64_t offTick = tick + gateMs;

        events.push_back({tick,    2, static_cast<uint8_t>(0x90 | mch), static_cast<uint8_t>(midiNote), 100, 3});
        events.push_back({offTick, 1, static_cast<uint8_t>(0x80 | mch), static_cast<uint8_t>(midiNote), 0,   3});
    }

    if (!t.hasParsedStopMs) {
        t.parsedStopMs = static_cast<uint64_t>(std::llround(curMs));
        t.hasParsedStopMs = true;
    }

    /* Clamp Note-Off and wave trigger durations to the track stop point */
    for (size_t i = firstEventIdx; i < events.size(); ++i) {
        if (events[i].tickMs > t.parsedStopMs)
            events[i].tickMs = t.parsedStopMs;
    }
    for (size_t i = firstTrigIdx; i < waveTriggers.size(); ++i) {
        SmafWaveTrigger &trig = waveTriggers[i];
        if (trig.startMs >= t.parsedStopMs)
            trig.durationMs = 0;
        else if (trig.startMs + trig.durationMs > t.parsedStopMs)
            trig.durationMs = t.parsedStopMs - trig.startMs;
    }
}

static QString locateSynthExecutable(const QString &name,
                                     const QString &customBinOverride = QString(),
                                     bool useOverrides = false)
{
    const QString candidate = resolveSynthToolBin(name, customBinOverride, useOverrides);
    if (candidate.contains(QLatin1Char('/')) ||
        candidate.contains(QLatin1Char('\\')) ||
        QFileInfo(candidate).isAbsolute()) {
        return QFileInfo::exists(candidate) ? QFileInfo(candidate).absoluteFilePath() : QString();
    }
    return QStandardPaths::findExecutable(candidate);
}

static QStringList timidityConfigCandidates(const QString &timidityBinOverride = QString(),
                                            bool useOverrides = false)
{
    QStringList cfgCandidates;
    const QString timidityExe = locateSynthExecutable(QStringLiteral("timidity"), timidityBinOverride, useOverrides);
    if (!timidityExe.isEmpty()) {
        const QString exeDir = QFileInfo(timidityExe).absolutePath();
        cfgCandidates << exeDir + QLatin1String("/timidity.cfg")
                      << QDir::cleanPath(exeDir + QLatin1String("/../timidity.cfg"));
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        cfgCandidates << appDir + QLatin1String("/timidity.cfg")
                      << appDir + QLatin1String("/tools/timidity.cfg");
    }

    cfgCandidates << QDir::homePath() + QLatin1String("/.timidity.cfg")
                  << QDir::homePath() + QLatin1String("/timidity.cfg");

#ifdef Q_OS_WIN
    cfgCandidates << QStringLiteral("C:/timidity/timidity.cfg")
                  << QStringLiteral("C:/TiMidity++/timidity.cfg")
                  << QStringLiteral("C:/Windows/timidity.cfg");
#else
    cfgCandidates << QStringLiteral("/etc/timidity/timidity.cfg")
                  << QStringLiteral("/etc/timidity.cfg")
                  << QStringLiteral("/usr/local/share/timidity/timidity.cfg")
                  << QStringLiteral("/usr/share/timidity/timidity.cfg");
#endif
    cfgCandidates.removeDuplicates();
    return cfgCandidates;
}

static QString findDefaultSoundFont(const QString &timidityBinOverride = QString(),
                                    const QString &fluidsynthBinOverride = QString(),
                                    bool useOverrides = false)
{
    QStringList searchDirs;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        searchDirs << appDir
                   << appDir + QLatin1String("/soundfonts")
                   << appDir + QLatin1String("/sf2")
                   << appDir + QLatin1String("/tools")
                   << appDir + QLatin1String("/tools/soundfonts");
    }

    const QString fsExe = locateSynthExecutable(QStringLiteral("fluidsynth"), fluidsynthBinOverride, useOverrides);
    if (!fsExe.isEmpty()) {
        const QString fsDir = QFileInfo(fsExe).absolutePath();
        searchDirs << fsDir
                   << fsDir + QLatin1String("/soundfonts")
                   << QDir::cleanPath(fsDir + QLatin1String("/../sf2"))
                   << QDir::cleanPath(fsDir + QLatin1String("/../share/soundfonts"))
                   << QDir::cleanPath(fsDir + QLatin1String("/../share/sounds/sf2"));
    }

    const QString timExe = locateSynthExecutable(QStringLiteral("timidity"), timidityBinOverride, useOverrides);
    if (!timExe.isEmpty()) {
        const QString timDir = QFileInfo(timExe).absolutePath();
        searchDirs << timDir
                   << timDir + QLatin1String("/soundfonts")
                   << timDir + QLatin1String("/sf2");
    }

#ifdef Q_OS_WIN
    searchDirs << QStringLiteral("C:/soundfonts")
               << QStringLiteral("C:/timidity/soundfonts")
               << QStringLiteral("C:/TiMidity++/soundfonts")
               << QStringLiteral("C:/timidity")
               << QStringLiteral("C:/TiMidity++");
#else
    /* Prefer distribution-managed default symlinks (e.g. Debian/Ubuntu update-alternatives
     * /usr/share/sounds/sf2/default-GM.sf2 or Arch/Fedora /usr/share/soundfonts/default.sf2)
     * before specific hardcoded SoundFont filenames. */
    const QStringList sysDefaults = {
        QStringLiteral("/usr/share/sounds/sf2/default-GM.sf2"),
        QStringLiteral("/usr/share/soundfonts/default.sf2"),
        QStringLiteral("/usr/share/sounds/sf2/FluidR3_GM.sf2"),
        QStringLiteral("/usr/share/soundfonts/FluidR3_GM.sf2"),
        QStringLiteral("/usr/share/sounds/sf2/TimGM6mb.sf2")
    };
#endif

    searchDirs.removeDuplicates();
    const QStringList preferredNames = {
        QStringLiteral("default.sf2"),
        QStringLiteral("default-GM.sf2"),
        QStringLiteral("soundfont.sf2"),
        QStringLiteral("FluidR3_GM.sf2"),
        QStringLiteral("GeneralUser_GS.sf2"),
        QStringLiteral("TimGM6mb.sf2"),
        QStringLiteral("WINDOWS.sf2")
    };

    for (const QString &dirPath : searchDirs) {
        for (const QString &sfName : preferredNames) {
            const QString cand = dirPath + QLatin1Char('/') + sfName;
            if (QFileInfo::exists(cand))
                return cand;
        }
        QDir d(dirPath);
        if (d.exists()) {
            const QFileInfoList sfFiles = d.entryInfoList(
                {QStringLiteral("*.sf2"), QStringLiteral("*.SF2"),
                 QStringLiteral("*.sf3"), QStringLiteral("*.SF3")},
                QDir::Files, QDir::Name);
            if (!sfFiles.isEmpty())
                return sfFiles.first().absoluteFilePath();
        }
    }

#ifndef Q_OS_WIN
    for (const QString &c : sysDefaults) {
        if (QFileInfo::exists(c))
            return c;
    }
    for (const QString &sysDir : {QStringLiteral("/usr/share/sounds/sf2"), QStringLiteral("/usr/share/soundfonts")}) {
        QDir d(sysDir);
        if (d.exists()) {
            const QFileInfoList sfFiles = d.entryInfoList(
                {QStringLiteral("*.sf2"), QStringLiteral("*.SF2"),
                 QStringLiteral("*.sf3"), QStringLiteral("*.SF3")},
                QDir::Files, QDir::Name);
            if (!sfFiles.isEmpty())
                return sfFiles.first().absoluteFilePath();
        }
    }
#endif
    return QString();
}

static QString configuredMidiSynthMode(const QString &overrideMode = QString(),
                                       bool useOverrides = false)
{
    QString mode = overrideMode.trimmed().toLower();
    if (!useOverrides && mode.isEmpty()) {
        QSettings s(QStringLiteral("qddbconv"), QStringLiteral("qddbconv"));
        mode = s.value(QStringLiteral("midiSynthMode"), QStringLiteral("auto")).toString().trimmed().toLower();
    }
    if (mode == QLatin1String("fluidsynth_auto"))
        return QStringLiteral("auto");
    if (mode != QLatin1String("timidity_auto") &&
        mode != QLatin1String("timidity") &&
        mode != QLatin1String("fluidsynth")) {
        return QStringLiteral("auto");
    }
    return mode;
}

static QString configuredMidiSoundFontPath(const QString &overridePath = QString(),
                                           bool useOverrides = false)
{
    if (useOverrides)
        return overridePath.trimmed();
    QString path = overridePath.trimmed();
    if (path.isEmpty()) {
        QSettings s(QStringLiteral("qddbconv"), QStringLiteral("qddbconv"));
        path = s.value(QStringLiteral("midiSoundFontPath")).toString().trimmed();
    }
    return path;
}

static QString formatSoundFontDisplay(const QString &sf2Path)
{
    if (sf2Path.isEmpty())
        return QStringLiteral("system default");
    QFileInfo fi(sf2Path);
    if (!fi.exists())
        return QStringLiteral("%1 (not found)").arg(sf2Path);
    QString canon = fi.canonicalFilePath();
    if (!canon.isEmpty() && canon != fi.absoluteFilePath())
        return QStringLiteral("%1 -> %2").arg(sf2Path, QFileInfo(canon).fileName());
    return sf2Path;
}

static QString findExistingTimidityConfig(const QString &timidityBinOverride = QString(),
                                          bool useOverrides = false)
{
    for (const QString &cfgPath : timidityConfigCandidates(timidityBinOverride, useOverrides)) {
        if (QFileInfo::exists(cfgPath))
            return cfgPath;
    }
    return QString();
}

static QString inspectTimidityConfigSummary(const QString &timidityBinOverride = QString(),
                                            bool useOverrides = false)
{
    for (const QString &cfgPath : timidityConfigCandidates(timidityBinOverride, useOverrides)) {
        QFile f(cfgPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QStringList activeDirectives;
        while (!f.atEnd()) {
            QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            int hashIdx = line.indexOf(QLatin1Char('#'));
            if (hashIdx >= 0)
                line = line.left(hashIdx).trimmed();
            if (line.startsWith(QLatin1String("soundfont "), Qt::CaseInsensitive) ||
                line.startsWith(QLatin1String("source "), Qt::CaseInsensitive)) {
                activeDirectives.append(line);
            }
        }
        if (!activeDirectives.isEmpty())
            return QStringLiteral("%1 [%2]").arg(cfgPath, activeDirectives.last());
        return cfgPath;
    }
    return QStringLiteral("default timidity.cfg");
}

/* Build a temporary TiMidity++ configuration file that clears all per-program
 * tonebank (0..127) and drumset (0..127) patch mappings loaded from the system
 * timidity.cfg (such as fluidr3_gm.cfg or freepats.cfg) and forces TiMidity++
 * to synthesize all instruments exclusively from the user-selected .sf2 SoundFont. */
static QByteArray buildTimidityCustomSf2Config(const QString &sf2Path)
{
    QByteArray cfg;
    cfg.reserve(360000);

    /* Remove any global soundfont directives from default timidity.cfg files */
    for (const QString &cfgPath : timidityConfigCandidates()) {
        QFile f(cfgPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        while (!f.atEnd()) {
            QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.startsWith(QLatin1String("soundfont "), Qt::CaseInsensitive)) {
                int hashIdx = line.indexOf(QLatin1Char('#'));
                if (hashIdx >= 0)
                    line = line.left(hashIdx).trimmed();
                QString rest = line.mid(10).trimmed();
                if (!rest.isEmpty()) {
                    cfg.append("soundfont ");
                    cfg.append(rest.toUtf8());
                    cfg.append(" remove\n");
                }
            }
        }
    }

    /* Undefine all explicit bank 0..127 and drumset 0..127 instrument mappings
     * (e.g. from /etc/timidity/fluidr3_gm.cfg or /etc/timidity/freepats.cfg)
     * so they do not override the custom SoundFont. */
    for (int b = 0; b < 128; ++b) {
        cfg.append("bank ").append(QByteArray::number(b)).append('\n');
        for (int p = 0; p < 128; ++p)
            cfg.append("undef ").append(QByteArray::number(p)).append('\n');
    }
    for (int d = 0; d < 128; ++d) {
        cfg.append("drumset ").append(QByteArray::number(d)).append('\n');
        for (int p = 0; p < 128; ++p)
            cfg.append("undef ").append(QByteArray::number(p)).append('\n');
    }

    const QString cleanSf2 = QDir::fromNativeSeparators(QFileInfo(sf2Path).absoluteFilePath());
    cfg.append("soundfont \"").append(cleanSf2.toUtf8()).append("\" order=0\n");
    return cfg;
}

static bool synthesizeMidiWithConfiguredBackends(const QString &midPath,
                                                 const QString &wavPath,
                                                 int sampleRate,
                                                 int timeoutMs,
                                                 QString &fallbackNote,
                                                 QString &errorMsg)
{
    fallbackNote.clear();
    const QString mode = configuredMidiSynthMode();
    const QString customSf2 = configuredMidiSoundFontPath();
    const bool hasCustomSf2 = !customSf2.isEmpty() && QFileInfo::exists(customSf2);

    auto runTimidity = [&](QString &usedDesc) -> bool {
        const QString bin = resolveSynthToolBin(QStringLiteral("timidity"));
        QStringList args;
        QTemporaryFile tmpCfg(QDir::tempPath() + QLatin1String("/qddbconv_timidity_XXXXXX.cfg"));
        if (hasCustomSf2) {
            if (tmpCfg.open()) {
                tmpCfg.write(buildTimidityCustomSf2Config(customSf2));
                tmpCfg.flush();
                tmpCfg.close();
                args << QStringLiteral("-c") << tmpCfg.fileName();
            } else {
                args << QStringLiteral("-x")
                     << QStringLiteral("soundfont \"%1\" order=0").arg(QDir::fromNativeSeparators(customSf2));
            }
            usedDesc = QStringLiteral("timidity (SoundFont: %1)").arg(formatSoundFontDisplay(customSf2));
        } else {
            const QString existingCfg = findExistingTimidityConfig();
            if (!existingCfg.isEmpty()) {
#ifdef Q_OS_WIN
                args << QStringLiteral("-c") << existingCfg;
#endif
                usedDesc = QStringLiteral("timidity (%1)").arg(inspectTimidityConfigSummary());
            } else {
                const QString defaultSf2 = findDefaultSoundFont();
                if (!defaultSf2.isEmpty() && tmpCfg.open()) {
                    tmpCfg.write(buildTimidityCustomSf2Config(defaultSf2));
                    tmpCfg.flush();
                    tmpCfg.close();
                    args << QStringLiteral("-c") << tmpCfg.fileName();
                    usedDesc = QStringLiteral("timidity (SoundFont: %1)").arg(formatSoundFontDisplay(defaultSf2));
                } else {
                    usedDesc = QStringLiteral("timidity (%1)").arg(inspectTimidityConfigSummary());
                }
            }
        }
        args << midPath
             << QStringLiteral("--preserve-silence")
             << QStringLiteral("-Ow")
             << QStringLiteral("-s") << QString::number(sampleRate)
             << QStringLiteral("-o") << wavPath;

        QProcess proc;
        proc.start(bin, args);
        if (proc.waitForFinished(timeoutMs) && proc.exitCode() == 0 && QFileInfo(wavPath).size() > 44) {
            std::fprintf(stdout, "[qddbconv] Synthesized MIDI via %s\n", usedDesc.toUtf8().constData());
            std::fflush(stdout);
            return true;
        }
        return false;
    };

    auto runFluidSynth = [&](QString &usedDesc) -> bool {
        const QString bin = resolveSynthToolBin(QStringLiteral("fluidsynth"));
        const QString sf2 = hasCustomSf2 ? customSf2 : findDefaultSoundFont();
        /* FluidSynth requires a valid .sf2/.sf3 SoundFont file; without one it exits with
         * status 0 while writing a completely silent WAV file. Hand off to fallback if none exists. */
        if (sf2.isEmpty() || !QFileInfo::exists(sf2))
            return false;

        QStringList args = {
            QStringLiteral("-ni"),
            QStringLiteral("-F"), wavPath,
            QStringLiteral("-r"), QString::number(sampleRate),
            sf2,
            midPath
        };
        usedDesc = QStringLiteral("fluidsynth (SoundFont: %1)").arg(formatSoundFontDisplay(sf2));

        QProcess proc;
        proc.start(bin, args);
        if (proc.waitForFinished(timeoutMs) && proc.exitCode() == 0 && QFileInfo(wavPath).size() > 44) {
            const QString errOut = QString::fromUtf8(proc.readAllStandardError());
            if (errOut.contains(QLatin1String("Failed to load SoundFont"), Qt::CaseInsensitive) ||
                errOut.contains(QLatin1String("Unable to open file"), Qt::CaseInsensitive)) {
                QFile::remove(wavPath);
                return false;
            }
            std::fprintf(stdout, "[qddbconv] Synthesized MIDI via %s\n", usedDesc.toUtf8().constData());
            std::fflush(stdout);
            return true;
        }
        return false;
    };

    QString usedDesc;
    if (mode == QLatin1String("timidity")) {
        if (runTimidity(usedDesc))
            return true;
        errorMsg = QStringLiteral("Failed to synthesize MIDI via TiMidity++ (check that 'timidity' and a SoundFont/patchset are installed).");
        return false;
    }
    if (mode == QLatin1String("fluidsynth")) {
        if (runFluidSynth(usedDesc))
            return true;
        errorMsg = QStringLiteral("Failed to synthesize MIDI via FluidSynth (check that 'fluidsynth' and a valid .sf2 SoundFont are installed).");
        return false;
    }
    if (mode == QLatin1String("timidity_auto")) {
        if (runTimidity(usedDesc))
            return true;
        if (runFluidSynth(usedDesc)) {
            fallbackNote = QStringLiteral("MIDI synthesized via %1 (timidity unavailable/failed)").arg(usedDesc);
            return true;
        }
    } else {
        /* Default "auto": FluidSynth -> TiMidity++ fallback */
        if (runFluidSynth(usedDesc))
            return true;
        if (runTimidity(usedDesc)) {
            fallbackNote = QStringLiteral("MIDI synthesized via %1 (fluidsynth or .sf2 unavailable/failed)").arg(usedDesc);
            return true;
        }
    }

    errorMsg = QStringLiteral("Failed to synthesize MIDI to WAV (install 'timidity' or 'fluidsynth' with a General MIDI SoundFont, or configure them in Settings -> Configure qddbconv...).");
    return false;
}

static void decodeMobileTrackToMidi(const uint8_t *p,
                                    size_t n,
                                    size_t trackIdx,
                                    SmafScoreTrack &t,
                                    std::vector<MidiTimedEvent> &events,
                                    std::vector<SmafWaveTrigger> &waveTriggers)
{
    const uint8_t *seqStart = p;
    const uint8_t *end = p + n;
    const double tbD = smafTimeBaseMs(t.durationTimeBase);
    const double tbG = smafTimeBaseMs(t.gateTimeBase);

    /* Per Yamaha SMAF Mobile Standard (MA-3 / MA-5) bank & channel state:
     * Channel 9 defaults to Drum/Stream mode (bank 0x80 / MSB 0x7D) unless switched
     * to Melody mode via CC#0 = 0x7C. Any channel 0..15 switched to CC#0 = 0x7D enters
     * Drum/Stream mode; CC#0 = 0x00 switches channels != 9 to Melody mode while leaving
     * channel 9 in Drum/Stream mode. */
    bool isDrum[16] = {};
    isDrum[9] = true;
    for (size_t i = 0; i < 16 && i < t.channelStatus.size(); ++i) {
        uint8_t chType = t.channelStatus[i] & 0x03;
        if (chType == 3)
            isDrum[i] = true;
        else if (chType == 1 && i != 9)
            isDrum[i] = false;
    }

    auto mapMidiCh = [&](int smafCh) -> uint8_t {
        int c = smafCh & 0x0F;
        if (isDrum[c])
            return 9;
        if (c == 9)
            return 15; /* Melodic notes on SMAF channel 9 (CC#0=0x7C) route to melodic MIDI channel 15 */
        return static_cast<uint8_t>(c);
    };

    auto hasWaveNumber = [&](int wn) -> bool {
        for (const SmafWaveChunk &w : t.waves) {
            if (w.waveNumber == wn)
                return true;
        }
        return false;
    };

    uint8_t runVel[16];
    uint8_t chVol[16];
    uint8_t chExp[16];
    uint8_t chPan[16];
    uint8_t chBankMsb[16] = {};
    uint8_t chBankLsb[16] = {};
    uint8_t chProg[16] = {};
    std::fill(std::begin(runVel), std::end(runVel), 64);
    std::fill(std::begin(chVol), std::end(chVol), 100);
    std::fill(std::begin(chExp), std::end(chExp), 127);
    std::fill(std::begin(chPan), std::end(chPan), 64);
    for (int i = 0; i < 16; ++i)
        chBankMsb[i] = isDrum[i] ? 0x7D : 0x00;

    /* Per-wave Stream Panpot table (waveNumber 1..32):
     *   panMode: 0 = explicit panVal, 1 = center (64), 2 = follow channel CC#10 (default) */
    uint8_t wavePanMode[33];
    uint8_t wavePanVal[33];
    std::fill(std::begin(wavePanMode), std::end(wavePanMode), 2);
    std::fill(std::begin(wavePanVal), std::end(wavePanVal), 64);

    /* Stereo Wave Pair table (up to 2 pairs: pairIdx 1..2 -> waveL, waveR) */
    uint8_t stereoPairL[3] = {0, 0, 0};
    uint8_t stereoPairR[3] = {0, 0, 0};
    uint8_t masterVol = 127;

    auto parseSmafSysExPayload = [&](const uint8_t *sx, size_t avail) {
        if (avail >= 6 && sx[0] == 0x43 && sx[1] == 0x79 && (sx[2] == 0x06 || sx[2] == 0x07) && sx[3] == 0x7F) {
            const uint8_t devId = sx[2]; /* 0x06 = MA-3, 0x07 = MA-5 */
            const uint8_t cmd = sx[4];
            if ((devId == 0x06 && cmd == 0x07) || (devId == 0x07 && cmd == 0x00)) {
                /* Master Volume: 43 79 06 7F 07 [vol] F7 / 43 79 07 7F 00 [vol] F7 */
                masterVol = sx[5] & 0x7F;
            } else if (((devId == 0x06 && cmd == 0x0A) || (devId == 0x07 && cmd == 0x0B)) && avail >= 8) {
                /* Stream Panpot: 43 79 06 7F 0A [waveId] [panMode] [panVal] F7 (MA-3)
                 *                43 79 07 7F 0B [waveId] [panMode] [panVal] F7 (MA-5) */
                int wid = sx[5] & 0x7F;
                uint8_t pmode = sx[6] & 0x7F;
                uint8_t pval = sx[7] & 0x7F;
                if (wid >= 1 && wid <= 32 && pmode <= 2) {
                    wavePanMode[wid] = pmode;
                    wavePanVal[wid] = (pval == 0) ? 64 : pval;
                }
            } else if (((devId == 0x06 && cmd == 0x09) || (devId == 0x07 && cmd == 0x0A)) && avail >= 8) {
                /* Stereo Wave Pair: 43 79 06 7F 09 [pairIdx] [waveL] [waveR] F7 (MA-3)
                 *                   43 79 07 7F 0A [pairIdx] [waveL] [waveR] F7 (MA-5) */
                int pairIdx = sx[5] & 0x7F;
                uint8_t wL = sx[6] & 0x7F;
                uint8_t wR = sx[7] & 0x7F;
                if (pairIdx >= 1 && pairIdx <= 2) {
                    if (wL == 0 && wR == 0) {
                        stereoPairL[pairIdx] = 0;
                        stereoPairR[pairIdx] = 0;
                    } else if (wL >= 1 && wL <= 32 && wR >= 1 && wR <= 32 && wL != wR) {
                        stereoPairL[pairIdx] = wL;
                        stereoPairR[pairIdx] = wR;
                    }
                }
            }
        }
    };

    /* Also scan Mtsu setupData for any initial SysEx settings */
    if (!t.setupData.empty()) {
        const uint8_t *sp = t.setupData.data();
        const uint8_t *send = sp + t.setupData.size();
        while (sp < send) {
            if (*sp == 0xF0) {
                ++sp;
                uint32_t slen = readMidiVlq(sp, send);
                size_t savail = std::min<size_t>(slen, static_cast<size_t>(send - sp));
                parseSmafSysExPayload(sp, savail);
                sp += savail;
            } else {
                ++sp;
            }
        }
    }

    size_t firstEventIdx = events.size();
    size_t firstTrigIdx = waveTriggers.size();
    double curMs = 0.0;
    int guard = 0;
    while (p < end && guard++ < 2000000 && events.size() < 400000) {
        size_t eventOffset = static_cast<size_t>(p - seqStart);
        if (t.hasStartPoint && eventOffset == t.startPointOffset)
            t.parsedStartMs = static_cast<uint64_t>(std::llround(curMs));
        if (t.hasStopPoint && eventOffset >= t.stopPointOffset) {
            t.parsedStopMs = static_cast<uint64_t>(std::llround(curMs));
            t.hasParsedStopMs = true;
            break;
        }

        uint32_t dur = readMidiVlq(p, end);
        curMs += dur * tbD;
        if (p >= end)
            break;

        uint8_t st = *p++;
        if (st < 0x80)
            continue;

        uint64_t tick = static_cast<uint64_t>(std::llround(curMs));
        int smafCh = st & 0x0F;
        uint8_t mch = mapMidiCh(smafCh);

        switch (st & 0xF0) {
        case 0x80:
        case 0x90: {
            if (p >= end)
                break;
            uint8_t note = *p++ & 0x7F;
            uint8_t vel = runVel[smafCh];
            if ((st & 0xF0) == 0x90) {
                if (p >= end)
                    break;
                vel = *p++ & 0x7F;
                runVel[smafCh] = vel;
            }
            uint32_t gate = readMidiVlq(p, end);
            if (gate == 0 || vel == 0)
                break;
            if (t.hasStartPoint && eventOffset < t.startPointOffset)
                break;
            uint64_t gateMs = static_cast<uint64_t>(std::max(1.0, std::round(gate * tbG)));
            uint64_t offTick = tick + gateMs;

            /* Check if this note triggers an embedded Mtsp/Mwa* Stream PCM chunk.
             * In MA-3 / MA-5 Mobile Standard, on any channel in Drum/Stream mode
             * (isDrum[smafCh] or CC#0 == 0x7D):
             *   - notes 0..12 trigger Mwa1..Mwa13 (waveId = note + 1)
             *   - notes 13..91 play Drum Kit percussion notes
             *   - notes 92..110 trigger Mwa14..Mwa32 (waveId = note - 78) */
            if (!t.waves.empty() && (isDrum[smafCh] || chBankMsb[smafCh] == 0x7D)) {
                int candidateId = 0;
                if (note <= 12)
                    candidateId = static_cast<int>(note) + 1;
                else if (note >= 92 && note <= 110)
                    candidateId = static_cast<int>(note) - 78;

                if (candidateId >= 1 && candidateId <= 32 && hasWaveNumber(candidateId)) {
                    uint8_t trigPan = chPan[smafCh];
                    if (wavePanMode[candidateId] == 0)
                        trigPan = wavePanVal[candidateId];
                    else if (wavePanMode[candidateId] == 1)
                        trigPan = 64;

                    int pairedRightWave = 0;
                    for (int pi = 1; pi <= 2; ++pi) {
                        if (stereoPairL[pi] == candidateId && stereoPairR[pi] >= 1 && hasWaveNumber(stereoPairR[pi])) {
                            pairedRightWave = stereoPairR[pi];
                            break;
                        }
                    }

                    if (pairedRightWave > 0) {
                        SmafWaveTrigger trigL;
                        trigL.startMs = tick;
                        trigL.durationMs = gateMs;
                        trigL.trackIdx = trackIdx;
                        trigL.waveNumber = candidateId;
                        trigL.pitchSemitones = 0;
                        trigL.velocity = vel;
                        trigL.volume = chVol[smafCh];
                        trigL.expression = chExp[smafCh];
                        trigL.masterVolume = masterVol;
                        trigL.pan = 0; /* Left channel of stereo wave pair */
                        waveTriggers.push_back(trigL);

                        SmafWaveTrigger trigR = trigL;
                        trigR.waveNumber = pairedRightWave;
                        trigR.pan = 127; /* Right channel of stereo wave pair */
                        waveTriggers.push_back(trigR);
                    } else {
                        SmafWaveTrigger trig;
                        trig.startMs = tick;
                        trig.durationMs = gateMs;
                        trig.trackIdx = trackIdx;
                        trig.waveNumber = candidateId;
                        trig.pitchSemitones = 0; /* Stream PCM always plays at native 1.0x rate */
                        trig.velocity = vel;
                        trig.volume = chVol[smafCh];
                        trig.expression = chExp[smafCh];
                        trig.masterVolume = masterVol;
                        trig.pan = trigPan;
                        waveTriggers.push_back(trig);
                    }
                    break;
                }
            }

            events.push_back({tick,    2, static_cast<uint8_t>(0x90 | mch), note, vel, 3});
            events.push_back({offTick, 1, static_cast<uint8_t>(0x80 | mch), note, 0,   3});
            break;
        }
        case 0xA0:
            p += std::min<size_t>(2, static_cast<size_t>(end - p));
            break;
        case 0xB0: {
            if (p + 1 >= end) {
                p = end;
                break;
            }
            uint8_t cc = *p++ & 0x7F;
            uint8_t val = *p++ & 0x7F;
            if (cc == 0) {
                chBankMsb[smafCh] = val;
                if (val == 0x7D && chBankLsb[smafCh] == 0) {
                    isDrum[smafCh] = true;
                } else if (val == 0x7C && chBankLsb[smafCh] == 0) {
                    isDrum[smafCh] = false;
                } else if (val == 0x00 && chBankLsb[smafCh] < 0x10 && smafCh != 9) {
                    isDrum[smafCh] = false;
                }
                mch = mapMidiCh(smafCh);
                /* Normalize SMAF internal Bank Select MSB (0x7C/0x7D) to GM Bank 0 */
                if (mch != 9)
                    events.push_back({tick, 0, static_cast<uint8_t>(0xB0 | mch), 0, 0, 3});
                break;
            } else if (cc == 32) {
                chBankLsb[smafCh] = val;
                if (chBankMsb[smafCh] == 0x7D && val == 0)
                    isDrum[smafCh] = true;
                else if (chBankMsb[smafCh] == 0x7C && val == 0)
                    isDrum[smafCh] = false;
                else if (chBankMsb[smafCh] == 0x00 && val < 0x10 && smafCh != 9)
                    isDrum[smafCh] = false;
                mch = mapMidiCh(smafCh);
                /* Normalize SMAF custom Bank Select LSB to GM Bank 0 */
                if (mch != 9)
                    events.push_back({tick, 0, static_cast<uint8_t>(0xB0 | mch), 32, 0, 3});
                break;
            } else if (cc == 7) {
                chVol[smafCh] = val;
            } else if (cc == 11) {
                chExp[smafCh] = val;
            } else if (cc == 10) {
                chPan[smafCh] = val;
            }
            events.push_back({tick, 0, static_cast<uint8_t>(0xB0 | mapMidiCh(smafCh)), cc, val, 3});
            break;
        }
        case 0xC0: {
            if (p >= end)
                break;
            uint8_t pc = *p++ & 0x7F;
            chProg[smafCh] = pc;
            if (chBankMsb[smafCh] == 0x7D && chBankLsb[smafCh] == 0 && chProg[smafCh] < 10)
                isDrum[smafCh] = true;
            else if (chBankMsb[smafCh] == 0x7C && chBankLsb[smafCh] == 0)
                isDrum[smafCh] = false;
            else if (chBankMsb[smafCh] == 0x00 && chBankLsb[smafCh] < 0x10 && smafCh != 9)
                isDrum[smafCh] = false;
            mch = mapMidiCh(smafCh);
            /* On MIDI Percussion Channel 9, force Program 0 (Standard GM Drum Kit) so
             * TiMidity++ does not mute percussion with 'Drum set N is undefined'. */
            uint8_t outPc = (mch == 9) ? 0 : pc;
            events.push_back({tick, 0, static_cast<uint8_t>(0xC0 | mch), outPc, 0, 2});
            break;
        }
        case 0xD0:
            if (p < end)
                ++p;
            break;
        case 0xE0: {
            if (p + 1 >= end) {
                p = end;
                break;
            }
            uint8_t lsb = *p++ & 0x7F;
            uint8_t msb = *p++ & 0x7F;
            events.push_back({tick, 0, static_cast<uint8_t>(0xE0 | mch), lsb, msb, 3});
            break;
        }
        case 0xF0: {
            if (st == 0xF0) {
                uint32_t len = readMidiVlq(p, end);
                size_t avail = std::min<size_t>(len, static_cast<size_t>(end - p));
                parseSmafSysExPayload(p, avail);
                p += avail;
            } else if (st == 0xFF) {
                if (p >= end)
                    break;
                uint8_t metaType = *p++;
                if (metaType == 0x00)
                    break;
                if (metaType == 0x2F) {
                    if (p < end && *p == 0x00)
                        ++p;
                    t.parsedStopMs = tick;
                    t.hasParsedStopMs = true;
                    p = end;
                    break;
                }
                if (p < end) {
                    uint8_t len = *p++;
                    p += std::min<size_t>(len, static_cast<size_t>(end - p));
                }
            }
            break;
        }
        default:
            break;
        }
    }

    if (!t.hasParsedStopMs) {
        t.parsedStopMs = static_cast<uint64_t>(std::llround(curMs));
        t.hasParsedStopMs = true;
    }

    /* Clamp Note-Off and wave trigger durations to the track stop point (FF 2F / sp:) */
    for (size_t i = firstEventIdx; i < events.size(); ++i) {
        if (events[i].tickMs > t.parsedStopMs)
            events[i].tickMs = t.parsedStopMs;
    }
    for (size_t i = firstTrigIdx; i < waveTriggers.size(); ++i) {
        SmafWaveTrigger &trig = waveTriggers[i];
        if (trig.startMs >= t.parsedStopMs)
            trig.durationMs = 0;
        else if (trig.startMs + trig.durationMs > t.parsedStopMs)
            trig.durationMs = t.parsedStopMs - trig.startMs;
    }
}

static bool parseFullSmaf(const QByteArray &bytes, ParsedSmafContainer &out)
{
    const uint8_t *d = reinterpret_cast<const uint8_t *>(bytes.constData());
    const size_t n = static_cast<size_t>(bytes.size());
    if (n < 12 || !(d[0] == 'M' && d[1] == 'M' && d[2] == 'M' && d[3] == 'D'))
        return false;

    uint32_t declared = readBe32(d + 4);
    size_t end = std::min<size_t>(8 + static_cast<size_t>(declared), n);
    size_t pos = 8;

    while (pos + 8 <= end) {
        const uint8_t *c = d + pos;
        if (!isPrintableId3(c)) {
            ++pos;
            continue;
        }
        uint32_t sz = readBe32(c + 4);
        size_t body = pos + 8;
        if (body + sz > n)
            sz = static_cast<uint32_t>(n - body);

        if (matchPrefix3(c, "OPD")) {
            parseOpdaChunk(d + body, sz, out);
        } else if (matchPrefix3(c, "MTR") && sz >= 4) {
            SmafScoreTrack t;
            t.trackNumber = c[3];
            t.isAudioTrack = false;
            t.formatType = d[body];
            t.sequenceType = d[body + 1];
            t.durationTimeBase = d[body + 2];
            t.gateTimeBase = d[body + 3];

            size_t chLen = (t.formatType == 0x00) ? 2 : ((t.formatType == 0x03) ? 32 : 16);
            size_t hdr = std::min<size_t>(4 + chLen, sz);
            t.channelStatus.assign(d + body + 4, d + body + hdr);

            size_t spos = hdr;
            while (spos + 8 <= sz) {
                const uint8_t *sc = d + body + spos;
                if (!isPrintableId3(sc)) {
                    ++spos;
                    continue;
                }
                uint32_t ssz = readBe32(sc + 4);
                size_t sbody = spos + 8;
                if (sbody + ssz > sz)
                    ssz = static_cast<uint32_t>(sz - sbody);

                if (matchPrefix3(sc, "Msp") && sc[3] == 'I')
                    parseSeekPhraseInfoChunk(d + body + sbody, ssz, t);
                else if (matchPrefix3(sc, "Mts") && sc[3] == 'u')
                    t.setupData.assign(d + body + sbody, d + body + sbody + ssz);
                else if (matchPrefix3(sc, "Mts") && sc[3] == 'q')
                    t.sequenceData.assign(d + body + sbody, d + body + sbody + ssz);
                else if (matchPrefix3(sc, "Mts") && sc[3] == 'p')
                    parseWaveSubchunks(d + body + sbody, ssz, t);

                spos = sbody + ssz;
                if (ssz == 0)
                    ++spos;
            }
            out.tracks.push_back(std::move(t));
        } else if (matchPrefix3(c, "ATR") && sz >= 6) {
            /* PcmAudioTrackChunk (ATR*): 6-byte header:
             *   b[0] = formatType, b[1] = sequenceType,
             *   b[2..3] = 2-byte WaveType, b[4] = durationTimeBase, b[5] = gateTimeBase */
            SmafScoreTrack t;
            t.trackNumber = c[3];
            t.isAudioTrack = true;
            t.formatType = d[body];
            t.sequenceType = d[body + 1];
            t.atrWaveType0 = d[body + 2];
            t.atrWaveType1 = d[body + 3];
            t.durationTimeBase = d[body + 4];
            t.gateTimeBase = d[body + 5];
            size_t spos = 6;
            while (spos + 8 <= sz) {
                const uint8_t *sc = d + body + spos;
                if (!isPrintableId3(sc)) {
                    ++spos;
                    continue;
                }
                uint32_t ssz = readBe32(sc + 4);
                size_t sbody = spos + 8;
                if (sbody + ssz > sz)
                    ssz = static_cast<uint32_t>(sz - sbody);
                if (matchPrefix3(sc, "Asp") && sc[3] == 'I')
                    parseSeekPhraseInfoChunk(d + body + sbody, ssz, t);
                else if (matchPrefix3(sc, "Ats") && sc[3] == 'u')
                    t.setupData.assign(d + body + sbody, d + body + sbody + ssz);
                else if (matchPrefix3(sc, "Ats") && sc[3] == 'q')
                    t.sequenceData.assign(d + body + sbody, d + body + sbody + ssz);
                else if (matchPrefix3(sc, "Awa"))
                    parseWaveSubchunks(sc, 8 + static_cast<size_t>(ssz), t);
                spos = sbody + ssz;
                if (ssz == 0)
                    ++spos;
            }
            out.tracks.push_back(std::move(t));
        }

        pos = body + sz;
        if (sz == 0)
            ++pos;
    }

    int hpsIdx = 0;
    for (size_t ti = 0; ti < out.tracks.size(); ++ti) {
        SmafScoreTrack &t = out.tracks[ti];
        size_t trigBefore = out.waveTriggers.size();
        if (t.isAudioTrack) {
            decodeAtrAudioTrack(t, ti, out.waveTriggers);
        } else if (!t.sequenceData.empty()) {
            if (t.formatType == 0x00) {
                decodeHandyPhoneTrackToMidi(t, ti, (hpsIdx++ * 4) & 0x0F, out.midiEvents, out.waveTriggers);
            } else if (t.formatType == 0x01) {
                std::vector<uint8_t> inflated = inflateSmafHuffman(t.sequenceData.data(), t.sequenceData.size());
                if (!inflated.empty())
                    decodeMobileTrackToMidi(inflated.data(), inflated.size(), ti, t, out.midiEvents, out.waveTriggers);
            } else {
                decodeMobileTrackToMidi(t.sequenceData.data(), t.sequenceData.size(), ti, t, out.midiEvents, out.waveTriggers);
            }
        }
        /* If track contains embedded wave chunks (e.g. pure PCM/ADPCM .mmf with no sequence
         * triggers), schedule wave 1 at startMs = 0 */
        if (!t.waves.empty() && out.waveTriggers.size() == trigBefore) {
            SmafWaveTrigger trig;
            trig.startMs = 0;
            trig.durationMs = 0;
            trig.trackIdx = ti;
            trig.waveNumber = t.waves.front().waveNumber;
            trig.velocity = 100;
            trig.volume = 100;
            trig.expression = 127;
            trig.masterVolume = 127;
            trig.pan = 64;
            out.waveTriggers.push_back(trig);
        }
    }

    /* Remove any wave triggers that were clamped to 0 duration by stopPointOffset / FF 2F
     * (except unsequenced pure-PCM triggers which have sequenceData.empty()) */
    out.waveTriggers.erase(
        std::remove_if(out.waveTriggers.begin(), out.waveTriggers.end(),
                       [&](const SmafWaveTrigger &trig) {
                           return trig.durationMs == 0 &&
                                  trig.trackIdx < out.tracks.size() &&
                                  !out.tracks[trig.trackIdx].sequenceData.empty();
                       }),
        out.waveTriggers.end());

    /* Apply container-wide MspI/AspI start-point offset (st:) if present across active tracks */
    uint64_t globalStartMs = UINT64_MAX;
    bool anyStartPoint = false;
    for (const SmafScoreTrack &t : out.tracks) {
        if (!t.sequenceData.empty() && t.hasStartPoint) {
            anyStartPoint = true;
            globalStartMs = std::min(globalStartMs, t.parsedStartMs);
        }
    }
    if (anyStartPoint && globalStartMs > 0 && globalStartMs != UINT64_MAX) {
        for (MidiTimedEvent &ev : out.midiEvents)
            ev.tickMs = (ev.tickMs > globalStartMs) ? (ev.tickMs - globalStartMs) : 0;
        for (SmafWaveTrigger &trig : out.waveTriggers)
            trig.startMs = (trig.startMs > globalStartMs) ? (trig.startMs - globalStartMs) : 0;
        for (SmafScoreTrack &t : out.tracks) {
            if (t.hasParsedStopMs)
                t.parsedStopMs = (t.parsedStopMs > globalStartMs) ? (t.parsedStopMs - globalStartMs) : 0;
        }
    }

    std::stable_sort(out.midiEvents.begin(), out.midiEvents.end(),
                     [](const MidiTimedEvent &a, const MidiTimedEvent &b) {
                         if (a.tickMs != b.tickMs)
                             return a.tickMs < b.tickMs;
                         return a.priority < b.priority;
                     });

    /* Compute container duration from track stop points (FF 2F / sp:), MIDI events, and wave triggers */
    for (const SmafScoreTrack &t : out.tracks) {
        if (t.hasParsedStopMs && t.parsedStopMs > out.durationMs)
            out.durationMs = t.parsedStopMs;
    }
    if (!out.midiEvents.empty() && out.midiEvents.back().tickMs > out.durationMs)
        out.durationMs = out.midiEvents.back().tickMs;

    for (const SmafWaveTrigger &trig : out.waveTriggers) {
        if (trig.durationMs > 0) {
            if (trig.startMs + trig.durationMs > out.durationMs)
                out.durationMs = trig.startMs + trig.durationMs;
        } else if (trig.trackIdx < out.tracks.size()) {
            /* Unsequenced pure-PCM fallback trigger: use raw wave duration */
            const SmafScoreTrack &t = out.tracks[trig.trackIdx];
            for (const SmafWaveChunk &w : t.waves) {
                if (w.waveNumber == trig.waveNumber && w.sampleRate > 0 && !w.rawBytes.empty()) {
                    uint64_t bitsPerFrame = static_cast<uint64_t>(w.bits > 0 ? w.bits : 4) *
                                            static_cast<uint64_t>(w.channels > 0 ? w.channels : 1);
                    uint64_t frames = (w.rawBytes.size() * 8ULL) / bitsPerFrame;
                    uint64_t waveMs = (frames * 1000ULL) / w.sampleRate;
                    if (trig.startMs + waveMs > out.durationMs)
                        out.durationMs = trig.startMs + waveMs;
                }
            }
        }
    }

    return !out.tracks.empty();
}

/* Decode a single-channel stream (4-bit Yamaha MA ADPCM, 8/16-bit Signed PCM, or 8/16-bit Offset PCM) */
static void decodeSingleChannelStream(const uint8_t *data,
                                      size_t len,
                                      uint8_t format,
                                      uint8_t bits,
                                      std::vector<int16_t> &out)
{
    out.clear();
    if (!data || len == 0)
        return;

    if (format == 0 && bits == 8) {
        /* 8-bit 2's complement signed PCM */
        out.reserve(len);
        for (size_t i = 0; i < len; ++i)
            out.push_back(static_cast<int16_t>(static_cast<int8_t>(data[i]) * 256));
        return;
    }
    if (format == 1 && bits == 8) {
        /* 8-bit unsigned offset binary PCM */
        out.reserve(len);
        for (size_t i = 0; i < len; ++i)
            out.push_back(static_cast<int16_t>((static_cast<int>(data[i]) - 128) * 256));
        return;
    }
    if (format == 0 && bits == 16) {
        /* 16-bit signed little-endian PCM (BasicAudioEngine) */
        size_t samples = len / 2;
        out.reserve(samples);
        for (size_t i = 0; i < samples; ++i) {
            uint16_t u = static_cast<uint16_t>(data[i * 2] | (data[i * 2 + 1] << 8));
            out.push_back(static_cast<int16_t>(u));
        }
        return;
    }
    if (format == 1 && bits == 16) {
        /* 16-bit unsigned offset little-endian PCM */
        size_t samples = len / 2;
        out.reserve(samples);
        for (size_t i = 0; i < samples; ++i) {
            uint16_t u = static_cast<uint16_t>(data[i * 2] | (data[i * 2 + 1] << 8));
            out.push_back(static_cast<int16_t>(u ^ 0x8000U));
        }
        return;
    }

    /* Default: 4-bit Yamaha MA ADPCM (vavi.sound.adpcm.ma.Ma) */
    out.reserve(len * 2);
    int sample = 0;
    int step = 127;

    auto decodeNib = [&](uint8_t code) -> int16_t {
        int delta = step >> 3;
        if (code & 1) delta += step >> 2;
        if (code & 2) delta += step >> 1;
        if (code & 4) delta += step;
        if (code & 8) delta = -delta;
        sample = std::clamp(sample + delta, -32768, 32767);
        switch (code & 7) {
        case 0: case 1: case 2: case 3: step = (step * 0xE6) >> 8; break;
        case 4: step = (step * 0x133) >> 8; break;
        case 5: step = (step * 0x199) >> 8; break;
        case 6: step = (step * 0x200) >> 8; break;
        default: step = (step * 0x266) >> 8; break;
        }
        step = std::clamp(step, 127, 24576);
        return static_cast<int16_t>(sample);
    };

    for (size_t i = 0; i < len; ++i) {
        uint8_t b = data[i];
        /* SMAF Mwa/Awa nibbles are low-nibble first */
        out.push_back(decodeNib(b & 0x0F));
        out.push_back(decodeNib((b >> 4) & 0x0F));
    }
}

/* Decode SmafWaveChunk into Left and Right 16-bit PCM channel vectors.
 * Per YamahaAudioEngine / PcmAudioEngine in vavi.sound.mobile, stereo Mwa/Awa chunks store
 * Left in the first half of rawBytes and Right in the second half (planar stereo). */
static void decodeWaveToStereoFrames(const SmafWaveChunk &w,
                                     std::vector<int16_t> &outL,
                                     std::vector<int16_t> &outR)
{
    if (w.channels == 2 && w.rawBytes.size() >= 2) {
        size_t half = w.rawBytes.size() / 2;
        decodeSingleChannelStream(w.rawBytes.data(), half, w.format, w.bits, outL);
        decodeSingleChannelStream(w.rawBytes.data() + half, half, w.format, w.bits, outR);
    } else {
        decodeSingleChannelStream(w.rawBytes.data(), w.rawBytes.size(), w.format, w.bits, outL);
        outR = outL;
    }
}

} // namespace

bool SmafConverter::isSmafFile(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QByteArray hdr = f.read(12);
    return hdr.size() >= 12 && hdr.startsWith("MMMD");
}

bool SmafConverter::probeSmafFile(const QString &filePath, TrackItem &item)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    ParsedSmafContainer smaf;
    if (!parseFullSmaf(f.readAll(), smaf))
        return false;

    item.codecName = QStringLiteral("smaf (mmf)");
    item.durationSec = static_cast<double>(smaf.durationMs) / 1000.0;
    item.sampleRate = 44100;
    item.bitsPerSample = 16;
    item.channels = 2;

    if (!smaf.title.isEmpty())
        item.title = smaf.title;
    if (!smaf.artist.isEmpty())
        item.artist = smaf.artist;
    if (!smaf.comment.isEmpty())
        item.comment = smaf.comment;

    return true;
}

bool SmafConverter::convertMmfToMid(const QString &mmfPath,
                                    const QString &midPath,
                                    QString &errorMsg)
{
    QFile f(mmfPath);
    if (!f.open(QIODevice::ReadOnly)) {
        errorMsg = QStringLiteral("Cannot open input .mmf file: %1").arg(mmfPath);
        return false;
    }

    ParsedSmafContainer smaf;
    if (!parseFullSmaf(f.readAll(), smaf)) {
        errorMsg = QStringLiteral("Not a valid Yamaha SMAF (.mmf) file: %1").arg(mmfPath);
        return false;
    }

    if (smaf.midiEvents.empty()) {
        errorMsg = QStringLiteral("SMAF file contains no score/MIDI note events (PCM-only .mmf): %1").arg(mmfPath);
        return false;
    }

    /* Build MTrk payload with 480 ticks/quarter-note and Tempo = 480,000 us/qn (1 tick = 1 ms) */
    QByteArray trkData;
    trkData.reserve(static_cast<int>(smaf.midiEvents.size() * 4 + 128));

    /* Tempo Meta Event: 00 FF 51 03 07 53 00 (480,000 us per quarter note) */
    trkData.append('\x00');
    trkData.append('\xFF');
    trkData.append('\x51');
    trkData.append('\x03');
    trkData.append('\x07');
    trkData.append('\x53');
    trkData.append('\x00');

    /* Optional Track Name Meta Event (FF 03) */
    QString title = smaf.title.isEmpty() ? QFileInfo(mmfPath).completeBaseName() : smaf.title;
    QByteArray titleUtf8 = title.toUtf8();
    if (!titleUtf8.isEmpty() && titleUtf8.size() < 127) {
        trkData.append('\x00');
        trkData.append('\xFF');
        trkData.append('\x03');
        writeMidiVlq(trkData, static_cast<uint32_t>(titleUtf8.size()));
        trkData.append(titleUtf8);
    }

    uint64_t lastTick = 0;
    for (const MidiTimedEvent &ev : smaf.midiEvents) {
        uint32_t delta = (ev.tickMs >= lastTick) ? static_cast<uint32_t>(ev.tickMs - lastTick) : 0;
        lastTick = ev.tickMs;
        writeMidiVlq(trkData, delta);
        trkData.append(static_cast<char>(ev.status));
        trkData.append(static_cast<char>(ev.data1 & 0x7F));
        if (ev.byteCount == 3)
            trkData.append(static_cast<char>(ev.data2 & 0x7F));
    }

    /* End of Track Meta Event: FF 2F 00 at container stop point (smaf.durationMs) */
    uint32_t endDelta = (smaf.durationMs > lastTick)
                            ? static_cast<uint32_t>(smaf.durationMs - lastTick)
                            : 0;
    writeMidiVlq(trkData, endDelta);
    trkData.append('\xFF');
    trkData.append('\x2F');
    trkData.append('\x00');

    QByteArray smf;
    smf.reserve(22 + trkData.size());
    /* MThd header */
    smf.append("MThd", 4);
    appendBe32(smf, 6);
    appendBe16(smf, 0);   /* Format 0 (single multi-channel track) */
    appendBe16(smf, 1);   /* 1 track */
    appendBe16(smf, 480); /* 480 ticks per quarter note */

    /* MTrk chunk */
    smf.append("MTrk", 4);
    appendBe32(smf, static_cast<uint32_t>(trkData.size()));
    smf.append(trkData);

    QFile outF(midPath);
    if (!outF.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorMsg = QStringLiteral("Failed to write output MIDI file: %1").arg(midPath);
        return false;
    }
    outF.write(smf);
    outF.close();
    return true;
}

bool SmafConverter::decodeMmfToWav(const QString &mmfPath,
                                   const QString &wavPath,
                                   QString &errorMsg)
{
    QFile f(mmfPath);
    if (!f.open(QIODevice::ReadOnly)) {
        errorMsg = QStringLiteral("Cannot open input .mmf file: %1").arg(mmfPath);
        return false;
    }

    ParsedSmafContainer smaf;
    if (!parseFullSmaf(f.readAll(), smaf)) {
        errorMsg = QStringLiteral("Not a valid Yamaha SMAF (.mmf) file: %1").arg(mmfPath);
        return false;
    }

    const uint32_t outRate = 44100;
    std::vector<int16_t> stereoMix; /* Interleaved L, R at 44100 Hz */
    bool renderedMidi = false;

    auto loadRenderedWavIntoMix = [&]() -> bool {
        QFile wf(wavPath);
        if (!wf.open(QIODevice::ReadOnly))
            return false;
        QByteArray wavBytes = wf.readAll();
        wf.close();
        int dataPos = wavBytes.indexOf("data");
        if (dataPos < 12 || dataPos + 8 > wavBytes.size())
            return false;
        const uint8_t *hdr = reinterpret_cast<const uint8_t *>(wavBytes.constData());
        uint16_t ch = static_cast<uint16_t>(hdr[22] | (hdr[23] << 8));
        uint32_t dataSz = static_cast<uint32_t>(
            hdr[dataPos + 4] | (hdr[dataPos + 5] << 8) |
            (hdr[dataPos + 6] << 16) | (hdr[dataPos + 7] << 24));
        size_t pcmOffset = static_cast<size_t>(dataPos + 8);
        size_t availBytes = std::min<size_t>(dataSz, static_cast<size_t>(wavBytes.size()) - pcmOffset);
        const int16_t *srcPcm = reinterpret_cast<const int16_t *>(wavBytes.constData() + pcmOffset);
        size_t totalSamples = availBytes / sizeof(int16_t);
        if (ch == 2) {
            stereoMix.assign(srcPcm, srcPcm + totalSamples);
            return !stereoMix.empty();
        } else if (ch == 1) {
            stereoMix.resize(totalSamples * 2);
            for (size_t i = 0; i < totalSamples; ++i) {
                stereoMix[i * 2] = srcPcm[i];
                stereoMix[i * 2 + 1] = srcPcm[i];
            }
            return !stereoMix.empty();
        }
        return false;
    };

    /* 1. When the .mmf file contains a musical score (MTR/Mtsq), render the full
     *    MIDI score via the configured synthesizer (TiMidity++ / FluidSynth) into 44100 Hz 16-bit stereo PCM first. */
    QString midiFallbackNote;
    if (!smaf.midiEvents.empty()) {
        QTemporaryFile tmpMid(QFileInfo(wavPath).absolutePath() + QLatin1String("/qddbconv_smaf_XXXXXX.mid"));
        if (tmpMid.open()) {
            QString midFile = tmpMid.fileName();
            tmpMid.close();
            if (convertMmfToMid(mmfPath, midFile, errorMsg)) {
                QString synthErr;
                if (synthesizeMidiWithConfiguredBackends(midFile, wavPath, static_cast<int>(outRate),
                                                         60000, midiFallbackNote, synthErr)) {
                    renderedMidi = loadRenderedWavIntoMix();
                } else {
                    errorMsg = synthErr;
                }
            }
            QFile::remove(midFile);
        }
    }

    /* Sort wave triggers chronologically and cut off any wave instance retriggered
     * before its previous gate finishes (matching hardware stream voice retrigger behavior). */
    std::stable_sort(smaf.waveTriggers.begin(), smaf.waveTriggers.end(),
                     [](const SmafWaveTrigger &a, const SmafWaveTrigger &b) {
                         return a.startMs < b.startMs;
                     });
    for (size_t i = 0; i < smaf.waveTriggers.size(); ++i) {
        SmafWaveTrigger &cur = smaf.waveTriggers[i];
        for (size_t j = i + 1; j < smaf.waveTriggers.size(); ++j) {
            const SmafWaveTrigger &nxt = smaf.waveTriggers[j];
            if (nxt.trackIdx == cur.trackIdx && nxt.waveNumber == cur.waveNumber && nxt.startMs >= cur.startMs) {
                uint64_t maxDur = nxt.startMs - cur.startMs;
                if (cur.durationMs == 0 || cur.durationMs > maxDur)
                    cur.durationMs = maxDur;
                break;
            }
        }
    }

    /* 2. Mix all triggered embedded voice/stream waves (Mwa/Awa: ADPCM or PCM, mono or stereo)
     *    into the 44100 Hz stereo buffer at their exact startMs timestamps and native sample rates. */
    bool mixedAnyWave = false;
    for (const SmafWaveTrigger &trig : smaf.waveTriggers) {
        if (trig.trackIdx >= smaf.tracks.size())
            continue;
        const SmafScoreTrack &trk = smaf.tracks[trig.trackIdx];
        if (!trk.sequenceData.empty() && trig.durationMs == 0)
            continue;
        const SmafWaveChunk *wave = nullptr;
        for (const SmafWaveChunk &w : trk.waves) {
            if (w.waveNumber == trig.waveNumber) {
                wave = &w;
                break;
            }
        }
        if (!wave && !trk.waves.empty())
            wave = &trk.waves.front();
        if (!wave || wave->rawBytes.empty())
            continue;

        std::vector<int16_t> waveL;
        std::vector<int16_t> waveR;
        decodeWaveToStereoFrames(*wave, waveL, waveR);
        size_t srcFrames = std::min(waveL.size(), waveR.size());
        if (srcFrames == 0)
            continue;

        uint32_t baseSr = (trig.sampleRateOverride >= 2000) ? trig.sampleRateOverride
                                                            : (wave->sampleRate >= 2000 ? wave->sampleRate : 8000);
        double effSr = static_cast<double>(baseSr) * std::pow(2.0, static_cast<double>(trig.pitchSemitones) / 12.0);
        double step = effSr / static_cast<double>(outRate);
        if (step <= 0.0)
            continue;

        size_t fullWaveFrames = static_cast<size_t>(std::floor(static_cast<double>(srcFrames) / step));
        size_t outFramesFromWave = fullWaveFrames;
        bool gatedEarly = false;
        if (trig.durationMs > 0) {
            size_t gateFrames = static_cast<size_t>((trig.durationMs * outRate) / 1000ULL);
            if (gateFrames < outFramesFromWave) {
                outFramesFromWave = gateFrames;
                gatedEarly = true;
            }
        }
        if (outFramesFromWave == 0)
            continue;

        size_t startFrame = static_cast<size_t>((trig.startMs * outRate) / 1000ULL);
        size_t neededSamples = (startFrame + outFramesFromWave) * 2;
        if (stereoMix.size() < neededSamples)
            stereoMix.resize(neededSamples, 0);

        double gain = (static_cast<double>(trig.velocity) / 127.0) *
                      (static_cast<double>(trig.volume) / 100.0) *
                      (static_cast<double>(trig.expression) / 127.0) *
                      (static_cast<double>(trig.masterVolume) / 127.0) * 1.4;
        gain = std::clamp(gain, 0.25, 2.0);

        double gainL = gain;
        double gainR = gain;
        if (wave->channels == 1) {
            double panNorm = std::clamp(static_cast<double>(trig.pan) / 127.0, 0.0, 1.0);
            gainL = gain * std::cos(panNorm * 1.57079632679);
            gainR = gain * std::sin(panNorm * 1.57079632679);
        }

        const size_t fadeFrames = gatedEarly ? std::min<size_t>(220, outFramesFromWave / 4) : 0;
        for (size_t fIdx = 0; fIdx < outFramesFromWave; ++fIdx) {
            double srcPos = static_cast<double>(fIdx) * step;
            size_t idx0 = static_cast<size_t>(srcPos);
            if (idx0 >= srcFrames)
                break;
            size_t idx1 = std::min(idx0 + 1, srcFrames - 1);
            double frac = srcPos - static_cast<double>(idx0);
            double sampL = static_cast<double>(waveL[idx0]) * (1.0 - frac) +
                           static_cast<double>(waveL[idx1]) * frac;
            double sampR = static_cast<double>(waveR[idx0]) * (1.0 - frac) +
                           static_cast<double>(waveR[idx1]) * frac;

            double env = 1.0;
            if (fadeFrames > 0 && fIdx + fadeFrames > outFramesFromWave) {
                env = static_cast<double>(outFramesFromWave - fIdx) / static_cast<double>(fadeFrames);
            }

            size_t dst = (startFrame + fIdx) * 2;
            int mixL = static_cast<int>(stereoMix[dst]) + static_cast<int>(std::lround(sampL * gainL * env));
            int mixR = static_cast<int>(stereoMix[dst + 1]) + static_cast<int>(std::lround(sampR * gainR * env));
            stereoMix[dst] = static_cast<int16_t>(std::clamp(mixL, -32768, 32767));
            stereoMix[dst + 1] = static_cast<int16_t>(std::clamp(mixR, -32768, 32767));
        }
        mixedAnyWave = true;
    }

    if (renderedMidi || mixedAnyWave) {
        /* Clamp excess post-stop synthesizer tail to the container duration when known */
        if (smaf.durationMs > 0) {
            size_t targetFrames = static_cast<size_t>((smaf.durationMs * outRate) / 1000ULL);
            size_t targetSamples = targetFrames * 2;
            if (targetSamples > 0 && stereoMix.size() > targetSamples) {
                size_t fadeFrames = std::min<size_t>(441, targetFrames / 4);
                for (size_t fIdx = 0; fIdx < fadeFrames; ++fIdx) {
                    size_t frame = targetFrames - fadeFrames + fIdx;
                    double env = static_cast<double>(fadeFrames - fIdx) / static_cast<double>(fadeFrames);
                    stereoMix[frame * 2] = static_cast<int16_t>(std::lround(stereoMix[frame * 2] * env));
                    stereoMix[frame * 2 + 1] = static_cast<int16_t>(std::lround(stereoMix[frame * 2 + 1] * env));
                }
                stereoMix.resize(targetSamples);
            }
        }

        uint32_t dataBytes = static_cast<uint32_t>(stereoMix.size() * sizeof(int16_t));
        QByteArray wav;
        wav.reserve(static_cast<int>(44 + dataBytes));
        wav.append("RIFF", 4);
        appendLe32(wav, 36 + dataBytes);
        wav.append("WAVEfmt ", 8);
        appendLe32(wav, 16);
        appendLe16(wav, 1); /* PCM */
        appendLe16(wav, 2); /* Stereo */
        appendLe32(wav, outRate);
        appendLe32(wav, outRate * 4);
        appendLe16(wav, 4);
        appendLe16(wav, 16);
        wav.append("data", 4);
        appendLe32(wav, dataBytes);
        wav.append(reinterpret_cast<const char *>(stereoMix.data()), static_cast<int>(dataBytes));

        QFile outF(wavPath);
        if (outF.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            errorMsg = midiFallbackNote;
            outF.write(wav);
            outF.close();
            return true;
        }
    }

    if (errorMsg.isEmpty())
        errorMsg = QStringLiteral("Failed to render .mmf to audio (install 'timidity' or 'fluidsynth' to synthesize FM/MIDI .mmf scores to audio, or select the 'Standard MIDI (.mid)' preset).");
    return false;
}

bool SmafConverter::isMidiFile(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QByteArray hdr = f.read(16);
    if (hdr.size() >= 4 && hdr.startsWith("MThd"))
        return true;
    if (hdr.size() >= 12 && hdr.startsWith("RIFF") && hdr.mid(8, 4) == "RMID")
        return true;
    return false;
}

bool SmafConverter::probeMidiFile(const QString &filePath, TrackItem &item)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    QByteArray bytes = f.readAll();
    const uint8_t *d = reinterpret_cast<const uint8_t *>(bytes.constData());
    size_t n = static_cast<size_t>(bytes.size());

    size_t mthdOffset = 0;
    if (n >= 20 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "RMID") {
        int idx = bytes.indexOf("MThd", 12);
        if (idx < 0)
            return false;
        mthdOffset = static_cast<size_t>(idx);
    }
    if (mthdOffset + 14 > n || std::memcmp(d + mthdOffset, "MThd", 4) != 0)
        return false;

    uint32_t hdrLen = readBe32(d + mthdOffset + 4);
    uint16_t numTracks = readBe16(d + mthdOffset + 10);
    uint16_t division = readBe16(d + mthdOffset + 12);
    size_t pos = mthdOffset + 8 + hdrLen;

    struct TempoChange {
        uint64_t tick;
        uint32_t usPerQn;
    };
    std::vector<TempoChange> tempos;
    tempos.push_back({0, 500000}); /* Default 120 BPM = 500,000 us/quarter-note */

    uint64_t maxTick = 0;
    QString seqTitle;
    QString copyrightText;

    for (uint16_t trk = 0; trk < numTracks && pos + 8 <= n; ++trk) {
        if (std::memcmp(d + pos, "MTrk", 4) != 0) {
            uint32_t skip = readBe32(d + pos + 4);
            pos += 8 + skip;
            continue;
        }
        uint32_t trkLen = readBe32(d + pos + 4);
        const uint8_t *p = d + pos + 8;
        const uint8_t *end = p + std::min<size_t>(trkLen, n - (pos + 8));
        pos += 8 + trkLen;

        uint64_t curTick = 0;
        uint8_t runningStatus = 0;
        while (p < end) {
            curTick += readMidiVlq(p, end);
            if (p >= end)
                break;

            uint8_t st = *p;
            if (st < 0x80) {
                if (runningStatus < 0x80) {
                    ++p;
                    continue;
                }
                st = runningStatus;
            } else {
                ++p;
                if (st < 0xF0)
                    runningStatus = st;
            }

            uint8_t hi = st & 0xF0;
            if (hi == 0x80 || hi == 0x90 || hi == 0xA0 || hi == 0xB0 || hi == 0xE0) {
                p += std::min<size_t>(2, static_cast<size_t>(end - p));
            } else if (hi == 0xC0 || hi == 0xD0) {
                p += std::min<size_t>(1, static_cast<size_t>(end - p));
            } else if (st == 0xF0 || st == 0xF7) {
                uint32_t len = readMidiVlq(p, end);
                p += std::min<size_t>(len, static_cast<size_t>(end - p));
            } else if (st == 0xFF) {
                if (p >= end)
                    break;
                uint8_t metaType = *p++;
                uint32_t len = readMidiVlq(p, end);
                if (len > static_cast<size_t>(end - p))
                    break;
                if (metaType == 0x51 && len == 3) {
                    uint32_t us = (static_cast<uint32_t>(p[0]) << 16) |
                                  (static_cast<uint32_t>(p[1]) << 8) |
                                   static_cast<uint32_t>(p[2]);
                    if (us > 0)
                        tempos.push_back({curTick, us});
                } else if (metaType == 0x03 && seqTitle.isEmpty() && len > 0) {
                    QString s = decodeSmafString(p, len);
                    if (!s.isEmpty())
                        seqTitle = s;
                } else if (metaType == 0x02 && copyrightText.isEmpty() && len > 0) {
                    QString s = decodeSmafString(p, len);
                    if (!s.isEmpty())
                        copyrightText = s;
                } else if (metaType == 0x2F) {
                    p += len;
                    break;
                }
                p += len;
            }
        }
        if (curTick > maxTick)
            maxTick = curTick;
    }

    double durationSec = 0.0;
    if ((division & 0x8000) == 0 && division > 0 && maxTick > 0) {
        std::stable_sort(tempos.begin(), tempos.end(),
                         [](const TempoChange &a, const TempoChange &b) {
                             return a.tick < b.tick;
                         });
        uint64_t prevTick = 0;
        uint32_t curUsPerQn = 500000;
        double totalUs = 0.0;
        for (const TempoChange &tc : tempos) {
            if (tc.tick > maxTick)
                break;
            if (tc.tick > prevTick) {
                totalUs += static_cast<double>(tc.tick - prevTick) *
                           (static_cast<double>(curUsPerQn) / static_cast<double>(division));
                prevTick = tc.tick;
            }
            curUsPerQn = tc.usPerQn;
        }
        if (maxTick > prevTick) {
            totalUs += static_cast<double>(maxTick - prevTick) *
                       (static_cast<double>(curUsPerQn) / static_cast<double>(division));
        }
        durationSec = totalUs / 1000000.0;
    }

    item.codecName = QStringLiteral("midi (smf)");
    item.durationSec = durationSec;
    item.sampleRate = 44100;
    item.bitsPerSample = 16;
    item.channels = 2;

    if (!seqTitle.isEmpty())
        item.title = seqTitle;
    if (!copyrightText.isEmpty())
        item.comment = copyrightText;

    return true;
}

bool SmafConverter::decodeMidToWav(const QString &midPath,
                                   const QString &wavPath,
                                   int sampleRate,
                                   QString &errorMsg)
{
    if (sampleRate < 4000 || sampleRate > 192000)
        sampleRate = 44100;

    QString fallbackNote;
    if (synthesizeMidiWithConfiguredBackends(midPath, wavPath, sampleRate, 120000, fallbackNote, errorMsg)) {
        errorMsg = fallbackNote;
        return true;
    }
    return false;
}

QString SmafConverter::describeActiveMidiSynth(const QString &synthModeOverride,
                                               const QString &soundFontOverride,
                                               bool useOverrides,
                                               const QString &timidityBinOverride,
                                               const QString &fluidsynthBinOverride)
{
    const QString mode = configuredMidiSynthMode(synthModeOverride, useOverrides);
    const QString customSf2 = configuredMidiSoundFontPath(soundFontOverride, useOverrides);
    const bool hasCustomSf2 = !customSf2.isEmpty();
    const QString defaultSf2 = findDefaultSoundFont(timidityBinOverride, fluidsynthBinOverride, useOverrides);

    const QString timidityExe = locateSynthExecutable(QStringLiteral("timidity"), timidityBinOverride, useOverrides);
    const QString fluidsynthExe = locateSynthExecutable(QStringLiteral("fluidsynth"), fluidsynthBinOverride, useOverrides);
    const bool hasTimidity = !timidityExe.isEmpty();
    const bool hasFluidsynth = !fluidsynthExe.isEmpty();

    const QString existingTimCfg = findExistingTimidityConfig(timidityBinOverride, useOverrides);
    const QString timiditySf2Summary = hasCustomSf2
        ? QStringLiteral("SoundFont: %1").arg(formatSoundFontDisplay(customSf2))
        : (!existingTimCfg.isEmpty()
               ? inspectTimidityConfigSummary(timidityBinOverride, useOverrides)
               : (!defaultSf2.isEmpty()
                      ? QStringLiteral("SoundFont: %1").arg(formatSoundFontDisplay(defaultSf2))
                      : QStringLiteral("default timidity.cfg")));
    const QString fluidsynthSf2Summary = hasCustomSf2
        ? QStringLiteral("SoundFont: %1").arg(formatSoundFontDisplay(customSf2))
        : (!defaultSf2.isEmpty()
               ? QStringLiteral("SoundFont: %1").arg(formatSoundFontDisplay(defaultSf2))
               : QStringLiteral("no default .sf2 detected"));

    const bool hasFluidsynthSf2 = hasCustomSf2
        ? QFileInfo::exists(customSf2)
        : !defaultSf2.isEmpty();

    const QString timLabel = hasTimidity
        ? QStringLiteral("TiMidity++ [%1]").arg(timidityExe)
        : QStringLiteral("TiMidity++");
    const QString fsLabel = hasFluidsynth
        ? QStringLiteral("FluidSynth [%1]").arg(fluidsynthExe)
        : QStringLiteral("FluidSynth");

    if (mode == QLatin1String("timidity")) {
        if (hasTimidity)
            return QStringLiteral("%1 only — %2").arg(timLabel, timiditySf2Summary);
        return QStringLiteral("TiMidity++ only (WARNING: 'timidity' binary not found)");
    }
    if (mode == QLatin1String("fluidsynth")) {
        if (!hasFluidsynth)
            return QStringLiteral("FluidSynth only (WARNING: 'fluidsynth' binary not found)");
        if (!hasFluidsynthSf2)
            return QStringLiteral("%1 only — WARNING: no valid .sf2 SoundFont found (%2)")
                .arg(fsLabel, fluidsynthSf2Summary);
        return QStringLiteral("%1 only — %2").arg(fsLabel, fluidsynthSf2Summary);
    }
    if (mode == QLatin1String("timidity_auto")) {
        if (hasTimidity)
            return QStringLiteral("%1 (%2)%3")
                .arg(timLabel,
                     timiditySf2Summary,
                     (hasFluidsynth && hasFluidsynthSf2)
                         ? QStringLiteral(" [fallback: FluidSynth (%1)]").arg(fluidsynthSf2Summary)
                         : QString());
        if (hasFluidsynth && hasFluidsynthSf2)
            return QStringLiteral("%1 fallback (%2) [TiMidity++ not found]")
                .arg(fsLabel, fluidsynthSf2Summary);
        return QStringLiteral("Neither 'timidity' nor a ready 'fluidsynth' (.sf2) backend found");
    }

    /* Default "auto": FluidSynth -> TiMidity++ fallback */
    if (hasFluidsynth && hasFluidsynthSf2) {
        return QStringLiteral("%1 (%2)%3")
            .arg(fsLabel,
                 fluidsynthSf2Summary,
                 hasTimidity ? QStringLiteral(" [fallback: TiMidity++ (%1)]").arg(timiditySf2Summary) : QString());
    }
    if (hasTimidity) {
        const QString reason = !hasFluidsynth
            ? QStringLiteral("FluidSynth not found")
            : QStringLiteral("no .sf2 found for FluidSynth");
        return QStringLiteral("%1 fallback (%2) [%3]")
            .arg(timLabel, timiditySf2Summary, reason);
    }
    return QStringLiteral("Neither 'fluidsynth' (with .sf2) nor 'timidity' binary found");
}

