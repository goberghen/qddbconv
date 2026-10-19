/*
 * qddbconv - Qt6 Audio Converter
 * Copyright (C) 2026 goberghen & Arena.ai Agent
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Built-in Yamaha MA-2 / MA-3 / MA-5 2-op and 4-op FM Synthesizer
 * (Hybrid Variant 2+3).
 */

#include "yamaha_ma_fm.h"
#include "yamaha_ma_tables.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

static const uint8_t kMulMap[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15
};

static const int8_t kBoMap[4] = {
    1, 0, -1, -2
};

static const uint8_t kKslMap[4] = {
    0, 2, 1, 3
};

static const uint8_t kFbMap[8] = {
    0, 10, 9, 8, 7, 6, 5, 4
};

// Triangle LFO AM & PM lookup matching M5_EmuHw.dll 0x1004942c and 0x1005942c
static inline uint32_t computeLfoAmFactor(uint8_t dam, uint32_t lfoIdx12) {
    static const uint32_t kMinAm[4] = { 0x6e35u, 0x5cbau, 0x41a5u, 0x20e7u };
    uint32_t minVal = kMinAm[dam & 3];
    uint32_t range = 0x8000u - minVal;
    uint32_t dist = (lfoIdx12 < 2048u) ? lfoIdx12 : (4096u - lfoIdx12);
    return 0x8000u - ((range * dist) >> 11);
}

static inline uint32_t computeLfoPmFactor(uint8_t dvb, uint32_t lfoIdx12) {
    static const int32_t kDepthPm[4] = { 0x809, 0xfda, 0x2001, 0x3fc6 };
    int32_t depth = kDepthPm[dvb & 3];
    int32_t offset = 0;
    if (lfoIdx12 < 1024u) {
        offset = (depth * int32_t(lfoIdx12)) >> 10;
    } else if (lfoIdx12 < 3072u) {
        offset = (depth * (2048 - int32_t(lfoIdx12))) >> 10;
    } else {
        offset = (depth * (int32_t(lfoIdx12) - 4096)) >> 10;
    }
    return uint32_t(0x100000 + offset);
}

} // namespace

YamahaMaFmSynth::YamahaMaFmSynth(int sampleRate, bool prefer4OpRom)
    : m_sampleRate(sampleRate > 0 ? sampleRate : 44100),
      m_prefer4OpRom(prefer4OpRom)
{
    reset();
}

void YamahaMaFmSynth::reset() {
    m_hasRenderedNotes = false;
    m_voiceCounter = 0;
    for (int i = 0; i < kMaxVoices; ++i) {
        m_voices[i] = VoiceState();
    }
    for (int c = 0; c < kMaxChannels; ++c) {
        m_channels[c] = ChannelState();
        if ((c & 0x0F) == 9) {
            m_channels[c].bankM = 0x7D;
            m_channels[c].hpsBank = 0x80;
        }
    }
    m_customMelodic.clear();
    m_customDrums.clear();
}

void YamahaMaFmSynth::setSampleRate(int sampleRate) {
    if (sampleRate > 0) {
        m_sampleRate = sampleRate;
    }
}

void YamahaMaFmSynth::setPrefer4OpRom(bool prefer4Op) {
    m_prefer4OpRom = prefer4Op;
}

YamahaMaFmPatch YamahaMaFmSynth::unpackHardwarePatch(const uint8_t *raw, size_t len, uint8_t drumPitch) {
    YamahaMaFmPatch patch;
    if (!raw || len < 16) {
        return patch;
    }
    patch.valid = true;
    patch.drumPitch = drumPitch;
    patch.panpot = (raw[0] >> 3) & 0x1F;
    patch.bo     = kBoMap[raw[0] & 0x03];
    patch.lfo    = (raw[1] >> 6) & 0x03;
    patch.pe     = (raw[1] >> 5) & 0x01;
    patch.vb     = (raw[1] >> 4) & 0x01;
    patch.alg    = raw[1] & 0x07;

    int numOps = (patch.alg >= 2 && len >= 30) ? 4 : 2;
    if (numOps == 2 && patch.alg >= 2) {
        patch.alg &= 0x01;
    }

    for (int i = 0; i < numOps; ++i) {
        const uint8_t *p = raw + 2 + i * 7;
        YamahaMaFmOperatorPatch &op = patch.op[i];
        op.sr  = (p[0] >> 4) & 0x0F;
        op.xof = (p[0] >> 3) & 0x01;
        op.sus = (p[0] >> 1) & 0x01;
        op.ksr = p[0] & 0x01;
        op.rr  = (p[1] >> 4) & 0x0F;
        op.dr  = p[1] & 0x0F;
        op.ar  = (p[2] >> 4) & 0x0F;
        op.sl  = p[2] & 0x0F;
        op.tl  = (p[3] >> 2) & 0x3F;
        op.ksl = kKslMap[p[3] & 0x03];
        op.dam = (p[4] >> 5) & 0x03;
        op.eam = (p[4] >> 4) & 0x01;
        op.dvb = (p[4] >> 1) & 0x03;
        op.vib = p[4] & 0x01;
        op.mul = kMulMap[(p[5] >> 4) & 0x0F];
        op.dt  = p[5] & 0x07;
        op.ws  = std::min<uint8_t>(28, (p[6] >> 3) & 0x1F);
        op.fb  = (i == 0 || i == 2) ? kFbMap[p[6] & 0x07] : 0;
    }
    return patch;
}

int YamahaMaFmSynth::convertMa2SysExToHardware(const uint8_t *src, size_t len, uint8_t outHw[30]) {
    if (!src || len < 15) {
        return 0;
    }
    uint8_t fb = (src[3] >> 3) & 0x07;
    uint8_t alg = src[3] & 0x07;
    uint8_t numOps = ((alg & 0x06) != 0 && len >= 25) ? 4 : 2;
    outHw[0] = (src[4] & 0x03) | 0x80;
    outHw[1] = (src[3] & 0xC0) | alg;
    const uint8_t *s = src + 5;
    uint8_t *d = outHw + 2;
    int outLen = 2;
    for (uint8_t i = 0; i < numOps; ++i, s += 5, d += 7, outLen += 7) {
        uint8_t b0 = s[0];
        uint8_t b1 = s[1];
        uint8_t b4 = s[4];
        uint8_t vib = (b0 >> 3) & 0x01;
        uint8_t egt = (b0 >> 1) & 0x01;
        uint8_t sus = b0 & 0x01;
        uint8_t hi1 = b1 >> 4;
        uint8_t lo1 = b1 & 0x0F;
        uint8_t dam = (b4 >> 6) & 0x03;
        uint8_t dvb = (b4 >> 4) & 0x03;
        uint8_t eam = (b4 >> 3) & 0x01;
        uint8_t ws  = b4 & 0x07;
        uint8_t mul = (b0 & 0x04) ? 0 : hi1;
        uint8_t ar  = egt ? 4 : hi1;
        d[0] = uint8_t((mul << 4) | 0x04 | sus);
        d[1] = uint8_t((ar << 4) | lo1);
        d[2] = s[2];
        d[3] = s[3];
        d[4] = uint8_t(((((dvb << 1) | eam) << 3) | dam) << 1 | vib);
        d[5] = uint8_t(b0 & 0xF0);
        d[6] = uint8_t((ws << 3) | ((i == 0) ? fb : 0));
    }
    return outLen;
}

size_t YamahaMaFmSynth::unpackYamaha7to8(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t maxDstLen) {
    static const uint8_t kBitMask[8] = { 0x00, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
    size_t outPos = 0;
    size_t inPos = 0;
    while (inPos < srcLen && outPos < maxDstLen) {
        size_t groupLen = std::min<size_t>(8, srcLen - inPos);
        uint8_t maskByte = src[inPos];
        for (size_t j = 1; j < groupLen && outPos < maxDstLen; ++j) {
            uint8_t b = src[inPos + j] & 0x7F;
            if (maskByte & kBitMask[j]) {
                b |= 0x80;
            }
            dst[outPos++] = b;
        }
        inPos += groupLen;
    }
    return outPos;
}

bool YamahaMaFmSynth::loadSysEx(int trackBaseCh, const uint8_t *data, size_t len) {
    if (!data || len < 4) {
        return false;
    }
    // Strip trailing F7 if present
    size_t effLen = len;
    if (data[effLen - 1] == 0xF7) {
        --effLen;
    }
    if (effLen < 4 || data[0] != 0x43) {
        return false;
    }

    // 1. Direct hardware FM register write: 43 02|03 90 <reg> <val> [F7]
    if ((data[1] == 0x02 || data[1] == 0x03) && data[2] == 0x90 && effLen >= 5) {
        uint8_t reg = data[3];
        uint8_t val = data[4];
        uint8_t regHi = reg & 0xF0;
        int ch = trackBaseCh + (reg & 0x0F);
        if (ch >= 0 && ch < kMaxChannels) {
            if (regHi == 0xB0) {
                writeRegB0(ch, val);
                return true;
            } else if (regHi == 0xC0) {
                writeRegC0(ch, val);
                return true;
            }
        }
        return false;
    }

    // 2. MA-2 custom voice definition: 43 03 <slot> <bank> <prog> <alg_fb_lfo> <bo> <ops...> [F7]
    if (data[1] == 0x03 && (effLen == 17 || effLen == 27)) {
        uint8_t bank = data[3];
        uint8_t prog = data[4];
        uint8_t hwBuf[30] = {0};
        int hwLen = convertMa2SysExToHardware(data + 2, effLen - 2, hwBuf);
        if (hwLen >= 16) {
            YamahaMaFmPatch patch = unpackHardwarePatch(hwBuf, size_t(hwLen), (bank & 0x80) ? prog : 60);
            if (bank & 0x80) {
                CustomDrumEntry d;
                d.drumNote = prog;
                d.isHps = true;
                d.patch = patch;
                m_customDrums.push_back(d);
            } else {
                CustomMelodicEntry m;
                m.hpsBank = bank;
                m.isHps = true;
                m.program = prog;
                m.patch = patch;
                m_customMelodic.push_back(m);
            }
            return true;
        }
    }

    // 3. MA-2 raw hardware patch: 43 04 01 <slot> <bank> <prog> <16_or_30_bytes>
    if (data[1] == 0x04 && data[2] == 0x01 && effLen >= 22) {
        uint8_t bank = data[4];
        uint8_t prog = data[5];
        YamahaMaFmPatch patch = unpackHardwarePatch(data + 6, effLen - 6, (bank & 0x80) ? prog : 60);
        if (patch.valid) {
            if (bank & 0x80) {
                CustomDrumEntry d;
                d.drumNote = prog;
                d.isHps = true;
                d.patch = patch;
                m_customDrums.push_back(d);
            } else {
                CustomMelodicEntry m;
                m.hpsBank = bank;
                m.isHps = true;
                m.program = prog;
                m.patch = patch;
                m_customMelodic.push_back(m);
            }
            return true;
        }
    }

    // 4. EXVO voice definition: 43 05 01 <bank> <prog> <drumPitch> <16_or_30_bytes>
    if (data[1] == 0x05 && data[2] == 0x01 && effLen >= 22) {
        uint8_t bank = data[3];
        uint8_t prog = data[4];
        uint8_t drumPitch = data[5];
        YamahaMaFmPatch patch = unpackHardwarePatch(data + 6, effLen - 6, drumPitch ? drumPitch : 60);
        if (patch.valid) {
            if (bank & 0x80) {
                CustomDrumEntry d;
                d.drumNote = prog;
                d.isHps = true;
                d.patch = patch;
                m_customDrums.push_back(d);
            } else {
                CustomMelodicEntry m;
                m.hpsBank = bank;
                m.isHps = true;
                m.program = prog;
                m.patch = patch;
                m_customMelodic.push_back(m);
            }
            return true;
        }
    }

    // 5. MA-3 / MA-5 Mtsu voice definition: 43 79 06|07 7F 01 <bankM> <bankL> <pc> <dn> <type> ...
    if (data[1] == 0x79 && (data[2] == 0x06 || data[2] == 0x07) &&
        data[3] == 0x7F && data[4] == 0x01 && effLen >= 10) {
        uint8_t bankM = data[5];
        uint8_t bankL = data[6];
        uint8_t pc    = data[7];
        uint8_t dn    = data[8];
        uint8_t vType = data[9];

        if (vType == 0x01 || vType == 0x02) {
            YamahaMaFmPatch marker;
            marker.valid = true;
            marker.isWaveTable = (vType == 0x01);
            marker.isStreamPcm = (vType == 0x02);
            if (bankM == 0x7D || dn != 0) {
                CustomDrumEntry d;
                d.bankL = bankL;
                d.kit = pc;
                d.drumNote = dn;
                d.patch = marker;
                m_customDrums.push_back(d);
            } else {
                CustomMelodicEntry m;
                m.bankM = bankM;
                m.bankL = bankL;
                m.program = pc;
                m.patch = marker;
                m_customMelodic.push_back(m);
            }
            return true;
        }

        if (vType == 0x00 && effLen > 11) {
            const uint8_t *body = data + 10;
            size_t bodyLen = effLen - 10;
            // In MA-5 with name header (bodyLen >= 43), skip 16-byte voice name if present
            if (data[2] == 0x07 && bodyLen >= 33 && bodyLen != 17 && bodyLen != 31) {
                // Check if 7-bit packed or has 16B name
            }
            uint8_t unpacked[64] = {0};
            const uint8_t *patchSrc = body;
            size_t patchSrcLen = bodyLen;

            // If body is 7-bit MIDI packed (e.g., 20 bytes for 17 bytes or 36 bytes for 31 bytes),
            // check if any byte has bit 7 set; if not and length matches 7-bit group size, unpack
            bool hasHighBit = false;
            for (size_t i = 0; i < bodyLen; ++i) {
                if (body[i] & 0x80) {
                    hasHighBit = true;
                    break;
                }
            }
            if (!hasHighBit && (bodyLen == 20 || bodyLen == 21 || bodyLen == 36 || bodyLen == 37)) {
                patchSrcLen = unpackYamaha7to8(body, bodyLen, unpacked, sizeof(unpacked));
                patchSrc = unpacked;
            }

            if (patchSrcLen >= 17) {
                uint8_t drumPitch = patchSrc[0] ? patchSrc[0] : 60;
                YamahaMaFmPatch patch = unpackHardwarePatch(patchSrc + 1, patchSrcLen - 1, drumPitch);
                if (patch.valid) {
                    if (bankM == 0x7D || dn != 0) {
                        CustomDrumEntry d;
                        d.bankL = bankL;
                        d.kit = pc;
                        d.drumNote = dn;
                        d.patch = patch;
                        m_customDrums.push_back(d);
                    } else {
                        CustomMelodicEntry m;
                        m.bankM = bankM;
                        m.bankL = bankL;
                        m.program = pc;
                        m.patch = patch;
                        m_customMelodic.push_back(m);
                    }
                    return true;
                }
            }
        }
    }

    return false;
}

bool YamahaMaFmSynth::loadExvoChunk(const uint8_t *data, size_t len) {
    if (!data || len < 8) {
        return false;
    }
    if (data[0] == 0xFF && data[1] == 0xF0) {
        uint8_t syxLen = data[2];
        if (size_t(syxLen) + 3 <= len) {
            return loadSysEx(0, data + 3, syxLen);
        }
    }
    return false;
}

const YamahaMaFmPatch *YamahaMaFmSynth::findCustomPatch(int ch, int note, bool *outIsDrum) const {
    if (ch < 0 || ch >= kMaxChannels) {
        return nullptr;
    }
    const ChannelState &cs = m_channels[ch];
    bool isDrum = cs.useHpsBank ? ((cs.hpsBank & 0x80) != 0) : (cs.bankM == 0x7D);
    if (outIsDrum) {
        *outIsDrum = isDrum;
    }

    if (isDrum) {
        uint8_t targetNote = (cs.useHpsBank && cs.hpsDrumNote != 0) ? cs.hpsDrumNote : uint8_t(note & 0x7F);
        for (int i = int(m_customDrums.size()) - 1; i >= 0; --i) {
            const CustomDrumEntry &d = m_customDrums[size_t(i)];
            if (d.drumNote == targetNote) {
                return &d.patch;
            }
        }
        return nullptr;
    } else {
        for (int i = int(m_customMelodic.size()) - 1; i >= 0; --i) {
            const CustomMelodicEntry &m = m_customMelodic[size_t(i)];
            if (m.program == cs.program) {
                if (cs.useHpsBank && m.isHps && (m.hpsBank == cs.hpsBank || m.hpsBank == 0)) {
                    return &m.patch;
                }
                if (!cs.useHpsBank && !m.isHps && (m.bankL == cs.bankL || cs.bankL == 0)) {
                    return &m.patch;
                }
            }
        }
        for (int i = int(m_customMelodic.size()) - 1; i >= 0; --i) {
            const CustomMelodicEntry &m = m_customMelodic[size_t(i)];
            if (m.program == cs.program) {
                return &m.patch;
            }
        }
        return nullptr;
    }
}

bool YamahaMaFmSynth::isStreamPcmVoice(int ch, int note) const {
    const YamahaMaFmPatch *custom = findCustomPatch(ch, note);
    return custom && custom->isStreamPcm;
}

bool YamahaMaFmSynth::shouldRenderOnFm(int ch, int note, bool externalWtAvailable) const {
    if (ch < 0 || ch >= kMaxChannels) {
        return false;
    }
    bool isDrum = false;
    const YamahaMaFmPatch *custom = findCustomPatch(ch, note, &isDrum);
    if (custom) {
        if (custom->isStreamPcm) {
            return false;
        }
        if (custom->isWaveTable) {
            return !externalWtAvailable;
        }
        return true; // Explicit custom FM voice in Mtsu/MspI/EXVO
    }

    if (!externalWtAvailable) {
        return true;
    }

    const ChannelState &cs = m_channels[ch];
    // For HPS (MA-1 / MA-2) tracks without WaveTable voices, default GM patches are FM
    if (cs.useHpsBank || m_prefer4OpRom) {
        if (isDrum) {
            int drmNote = (cs.useHpsBank && cs.hpsDrumNote != 0) ? int(cs.hpsDrumNote) : (note & 0x7F);
            if (drmNote >= 35 && drmNote <= 81) {
                return kYamahaMa2Drum4Op[drmNote - 35][0] == 0;
            }
        }
        return true;
    }

    // For Mobile Standard (MA-3 / MA-5) tracks without an explicit custom FM patch on this
    // channel/note: delegate standard GM melodic and drum voices to FluidSynth / TiMidity++
    // when available, and fall back to the built-in MA-3 FM ROM only when no external WT
    // synthesizer is available.
    return false;
}

YamahaMaFmPatch YamahaMaFmSynth::resolvePatch(int ch, int note, int *outPlayNote) const {
    bool isDrum = false;
    const YamahaMaFmPatch *custom = findCustomPatch(ch, note, &isDrum);
    const ChannelState &cs = m_channels[ch];

    if (custom && custom->valid && !custom->isWaveTable && !custom->isStreamPcm) {
        if (outPlayNote) {
            *outPlayNote = isDrum ? int(custom->drumPitch) : note;
        }
        return *custom;
    }

    if (isDrum) {
        int drmNote = (cs.useHpsBank && cs.hpsDrumNote != 0) ? int(cs.hpsDrumNote) : (note & 0x7F);
        if (m_prefer4OpRom && drmNote >= 35 && drmNote <= 81) {
            const uint8_t *entry = kYamahaMa2Drum4Op[drmNote - 35];
            uint8_t pitch = entry[1] ? entry[1] : 60;
            if (outPlayNote) {
                *outPlayNote = int(pitch);
            }
            return unpackHardwarePatch(entry + 2, 30, pitch);
        }
        int idx = std::clamp(drmNote, 24, 84) - 24;
        uint8_t pitch = kYamahaMa3DrumPitch[idx];
        if (outPlayNote) {
            *outPlayNote = int(pitch);
        }
        return unpackHardwarePatch(kYamahaMa3Drum2Op[idx], 16, pitch);
    }

    uint8_t prog = cs.program & 0x7F;
    int octShift = 0;
    uint8_t octCode = kYamahaMaProgOctTable[prog];
    if (octCode == 1) octShift = 12;
    else if (octCode == 2) octShift = 24;
    else if (octCode == 3) octShift = -12;
    else if (octCode == 4) octShift = -24;

    if (outPlayNote) {
        *outPlayNote = std::clamp(note + octShift, 0, 127);
    }

    if (m_prefer4OpRom) {
        return unpackHardwarePatch(kYamahaMa2Melodic4Op[prog], 30, 60);
    }
    return unpackHardwarePatch(kYamahaMa3Melodic2Op[prog], 16, 60);
}

void YamahaMaFmSynth::setBank(int ch, uint8_t bankM, uint8_t bankL) {
    if (ch >= 0 && ch < kMaxChannels) {
        m_channels[ch].bankM = bankM;
        m_channels[ch].bankL = bankL;
        m_channels[ch].useHpsBank = false;
    }
}

void YamahaMaFmSynth::setHpsBank(int ch, uint8_t bank) {
    if (ch >= 0 && ch < kMaxChannels) {
        m_channels[ch].hpsBank = bank;
        m_channels[ch].useHpsBank = true;
    }
}

void YamahaMaFmSynth::setProgram(int ch, uint8_t prog) {
    if (ch >= 0 && ch < kMaxChannels) {
        m_channels[ch].program = prog & 0x7F;
        if (m_channels[ch].useHpsBank && (m_channels[ch].hpsBank & 0x80)) {
            m_channels[ch].hpsDrumNote = prog & 0x7F;
        }
    }
}

void YamahaMaFmSynth::setHpsDrumNote(int ch, uint8_t drumNote) {
    if (ch >= 0 && ch < kMaxChannels) {
        m_channels[ch].hpsDrumNote = drumNote & 0x7F;
    }
}

void YamahaMaFmSynth::setVolume(int ch, uint8_t vol) {
    if (ch >= 0 && ch < kMaxChannels) {
        m_channels[ch].volume = vol & 0x7F;
        for (int i = 0; i < kMaxVoices; ++i) {
            if (m_voices[i].active && m_voices[i].ch == ch) {
                updateVoiceGains(m_voices[i]);
            }
        }
    }
}

void YamahaMaFmSynth::setExpression(int ch, uint8_t expr) {
    if (ch >= 0 && ch < kMaxChannels) {
        m_channels[ch].expression = expr & 0x7F;
        for (int i = 0; i < kMaxVoices; ++i) {
            if (m_voices[i].active && m_voices[i].ch == ch) {
                updateVoiceGains(m_voices[i]);
            }
        }
    }
}

void YamahaMaFmSynth::setPan(int ch, uint8_t pan) {
    if (ch >= 0 && ch < kMaxChannels) {
        m_channels[ch].pan = pan & 0x7F;
        for (int i = 0; i < kMaxVoices; ++i) {
            if (m_voices[i].active && m_voices[i].ch == ch) {
                updateVoiceGains(m_voices[i]);
            }
        }
    }
}

void YamahaMaFmSynth::setModulation(int ch, uint8_t mod) {
    if (ch >= 0 && ch < kMaxChannels) {
        m_channels[ch].modulation = mod & 0x7F;
    }
}

void YamahaMaFmSynth::setPitchBend(int ch, int bend14) {
    if (ch >= 0 && ch < kMaxChannels) {
        m_channels[ch].pitchBend = std::clamp(bend14, -8192, 8191);
        for (int i = 0; i < kMaxVoices; ++i) {
            if (m_voices[i].active && m_voices[i].ch == ch && !m_voices[i].regControlled) {
                updateVoiceFrequencyAndRates(m_voices[i], false);
            }
        }
    }
}

void YamahaMaFmSynth::setPitchBendRange(int ch, uint8_t semitones) {
    if (ch >= 0 && ch < kMaxChannels) {
        m_channels[ch].pitchBendRange = std::clamp<uint8_t>(semitones, 1, 24);
    }
}

void YamahaMaFmSynth::allNotesOff(int ch) {
    for (int i = 0; i < kMaxVoices; ++i) {
        if (m_voices[i].active && (ch < 0 || m_voices[i].ch == ch) && m_voices[i].keyOn) {
            releaseVoice(m_voices[i]);
        }
    }
}

void YamahaMaFmSynth::allSoundOff(int ch) {
    for (int i = 0; i < kMaxVoices; ++i) {
        if (m_voices[i].active && (ch < 0 || m_voices[i].ch == ch)) {
            m_voices[i] = VoiceState();
        }
    }
}

int YamahaMaFmSynth::allocateVoice(int ch, int note) {
    // 1. Re-use same channel + note if already active
    for (int i = 0; i < kMaxVoices; ++i) {
        if (m_voices[i].active && m_voices[i].ch == ch && m_voices[i].note == note) {
            return i;
        }
    }
    // 2. Find inactive voice
    for (int i = 0; i < kMaxVoices; ++i) {
        if (!m_voices[i].active) {
            return i;
        }
    }
    // 3. Steal oldest released voice
    int bestIdx = -1;
    uint32_t oldestAge = UINT32_MAX;
    for (int i = 0; i < kMaxVoices; ++i) {
        if (!m_voices[i].keyOn && m_voices[i].age < oldestAge) {
            oldestAge = m_voices[i].age;
            bestIdx = i;
        }
    }
    if (bestIdx >= 0) {
        return bestIdx;
    }
    // 4. Steal oldest voice overall
    bestIdx = 0;
    oldestAge = m_voices[0].age;
    for (int i = 1; i < kMaxVoices; ++i) {
        if (m_voices[i].age < oldestAge) {
            oldestAge = m_voices[i].age;
            bestIdx = i;
        }
    }
    return bestIdx;
}

void YamahaMaFmSynth::noteOn(int ch, int note, int velocity) {
    if (ch < 0 || ch >= kMaxChannels || note < 0 || note > 127) {
        return;
    }
    if (velocity <= 0) {
        noteOff(ch, note);
        return;
    }
    if (isStreamPcmVoice(ch, note)) {
        return;
    }

    int playNote = note;
    YamahaMaFmPatch patch = resolvePatch(ch, note, &playNote);
    if (!patch.valid) {
        return;
    }

    uint16_t bf = kYamahaMaFnumTable[0][std::clamp(playNote, 0, 127)];
    uint8_t block = (bf >> 10) & 0x07;
    uint16_t fnum = bf & 0x03FF;

    int vIdx = allocateVoice(ch, note);
    startVoiceWithBlockFnum(vIdx, ch, note, velocity, patch, block, fnum, false);
}

void YamahaMaFmSynth::noteOff(int ch, int note) {
    for (int i = 0; i < kMaxVoices; ++i) {
        if (m_voices[i].active && m_voices[i].ch == ch && m_voices[i].note == note && m_voices[i].keyOn) {
            releaseVoice(m_voices[i]);
        }
    }
}

void YamahaMaFmSynth::writeRegB0(int ch, uint8_t val) {
    if (ch < 0 || ch >= kMaxChannels) {
        return;
    }
    m_channels[ch].regB0 = val;
    int vIdx = m_channels[ch].regVoiceIdx;
    if (vIdx >= 0 && vIdx < kMaxVoices && m_voices[vIdx].active && m_voices[vIdx].regControlled) {
        uint8_t block = (m_channels[ch].regC0 >> 2) & 0x07;
        uint32_t fnum = ((uint32_t(m_channels[ch].regC0 & 0x03) << 8) | val);
        fnum = (fnum * 67857u) >> 16;
        while (fnum > 0x3FFu) {
            fnum >>= 1;
            if (block < 7) ++block;
        }
        m_voices[vIdx].block = block;
        m_voices[vIdx].fnum = uint16_t(fnum & 0x3FFu);
        updateVoiceFrequencyAndRates(m_voices[vIdx], false);
    }
}

void YamahaMaFmSynth::writeRegC0(int ch, uint8_t val) {
    if (ch < 0 || ch >= kMaxChannels) {
        return;
    }
    m_channels[ch].regC0 = val;
    uint8_t block = (val >> 2) & 0x07;
    uint32_t rawFnum = (uint32_t(val & 0x03) << 8) | m_channels[ch].regB0;
    uint32_t scaledFnum = (rawFnum * 67857u) >> 16;
    while (scaledFnum > 0x3FFu) {
        scaledFnum >>= 1;
        if (block < 7) ++block;
    }
    uint16_t fnum = uint16_t(scaledFnum & 0x3FFu);
    bool newKeyOn = (val & 0x20) != 0;

    int vIdx = m_channels[ch].regVoiceIdx;
    if (newKeyOn) {
        int dummyNote = int(m_channels[ch].hpsDrumNote ? m_channels[ch].hpsDrumNote : 60);
        int playNote = dummyNote;
        YamahaMaFmPatch patch = resolvePatch(ch, dummyNote, &playNote);
        if (!patch.valid) {
            return;
        }
        if (vIdx < 0 || vIdx >= kMaxVoices || !m_voices[vIdx].active || m_voices[vIdx].ch != ch) {
            vIdx = allocateVoice(ch, -2);
            m_channels[ch].regVoiceIdx = vIdx;
        }
        startVoiceWithBlockFnum(vIdx, ch, -2, 127, patch, block, fnum, true);
    } else {
        if (vIdx >= 0 && vIdx < kMaxVoices && m_voices[vIdx].active && m_voices[vIdx].ch == ch) {
            m_voices[vIdx].block = block;
            m_voices[vIdx].fnum = fnum;
            if (m_voices[vIdx].keyOn) {
                releaseVoice(m_voices[vIdx]);
            }
        }
    }
}

void YamahaMaFmSynth::startVoiceWithBlockFnum(int vIdx, int ch, int note, int velocity,
                                              const YamahaMaFmPatch &patch,
                                              uint8_t block, uint16_t fnum, bool regControlled) {
    VoiceState &v = m_voices[vIdx];
    v.active = true;
    v.keyOn = true;
    v.regControlled = regControlled;
    v.ch = ch;
    v.note = note;
    v.velocity = uint8_t(std::clamp(velocity, 1, 127));
    v.block = block & 0x07;
    v.fnum = fnum & 0x03FF;
    v.lfoPhase = 0;
    v.age = ++m_voiceCounter;
    v.patch = patch;

    uint32_t lfo44 = kYamahaMaLfoStep44100[patch.lfo & 3];
    v.lfoStep = (m_sampleRate == 44100)
        ? lfo44
        : uint32_t((uint64_t(lfo44) * 44100u) / uint32_t(m_sampleRate));

    int numOps = (patch.alg >= 2) ? 4 : 2;
    for (int i = 0; i < numOps; ++i) {
        v.op[i] = OperatorState();
        v.op[i].cfg = patch.op[i];
        v.op[i].egState = 1; // Attack
        v.op[i].egLevel = 0;
    }
    for (int i = numOps; i < 4; ++i) {
        v.op[i] = OperatorState();
    }

    updateVoiceFrequencyAndRates(v, true);
    updateVoiceGains(v);
    m_hasRenderedNotes = true;
}

void YamahaMaFmSynth::updateVoiceFrequencyAndRates(VoiceState &v, bool /*isKeyOnTransition*/) {
    static const uint8_t kDtNoteMap[16] = {
        0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3
    };
    const ChannelState &cs = m_channels[v.ch];

    int effBlock = std::clamp(int(v.block) + int(v.patch.bo), 0, 7);
    uint8_t kcode = uint8_t(((effBlock & 7) << 1) | ((v.fnum >> 9) & 1));
    uint8_t kcodeKsl = uint8_t(((v.fnum >> 6) << 3) | (effBlock & 7));
    uint8_t dtKcode = uint8_t((effBlock << 2) + kDtNoteMap[(v.fnum >> 6) & 0x0F]);

    uint32_t pitchScale = 0x10000u;
    if (!v.regControlled && cs.pitchBend != 0) {
        double semis = (double(cs.pitchBend) / 8192.0) * double(cs.pitchBendRange);
        pitchScale = uint32_t(std::llround(65536.0 * std::pow(2.0, semis / 12.0)));
    }

    uint32_t freqCoeff = 0xbb80000u / uint32_t(m_sampleRate);
    uint64_t baseShifted = (uint64_t(v.fnum) << effBlock) * uint64_t(freqCoeff);
    uint32_t phaseIncBase = uint32_t((baseShifted * uint64_t(pitchScale)) >> 16);

    int numOps = (v.patch.alg >= 2) ? 4 : 2;
    for (int i = 0; i < numOps; ++i) {
        OperatorState &op = v.op[i];
        op.tlLevel = kYamahaMaTlTable[op.cfg.tl & 0x3F];
        op.slLevel = kYamahaMaSlTable[op.cfg.sl & 0x0F];
        if (op.cfg.ksl == 0) {
            op.kslLevel = 0x8000u;
        } else if (op.tlLevel == 0) {
            op.kslLevel = 0;
        } else {
            op.kslLevel = kYamahaMaKslTable[op.cfg.ksl & 3][kcodeKsl & 0x7F];
        }
        op.totalGain = (op.tlLevel * op.kslLevel) >> 15;
        op.modTotalGain = op.totalGain;

        uint8_t rof = op.cfg.ksr ? kcode : (kcode >> 2);
        auto rateIdx = [rof](uint8_t r) -> int {
            if (r == 0) return 0;
            return std::min(63, int(r) * 4 + int(rof));
        };

        auto scaleAr = [this](uint32_t step44) -> uint32_t {
            if (m_sampleRate == 44100 || step44 == 0 || step44 >= 0x80000000u) return step44;
            uint64_t s = (uint64_t(step44) * 44100u) / uint32_t(m_sampleRate);
            return uint32_t(std::min<uint64_t>(0x80000000u, s));
        };

        auto scaleDr = [this](uint32_t mult44) -> uint32_t {
            if (m_sampleRate == 44100 || mult44 == 0 || mult44 >= 0x40000000u) return mult44;
            double ratio = double(mult44) / double(0x40000000u);
            double scaled = std::pow(ratio, 44100.0 / double(m_sampleRate));
            return uint32_t(std::clamp<int64_t>(std::llround(scaled * double(0x40000000u)), 1, 0x40000000));
        };

        op.arStep = scaleAr(kYamahaMaArTable44100[rateIdx(op.cfg.ar)]);
        op.drStep = scaleDr(kYamahaMaDrTable44100[rateIdx(op.cfg.dr)]);
        op.srStep = scaleDr(kYamahaMaDrTable44100[rateIdx(op.cfg.sr)]);
        op.rrStep = scaleDr(kYamahaMaDrTable44100[rateIdx(op.cfg.rr)]);

        uint32_t inc = phaseIncBase;
        if (op.cfg.dt != 0) {
            uint32_t dtMag = kYamahaMaDtTable[op.cfg.dt & 3][dtKcode & 31];
            uint32_t denom = std::max<uint32_t>((phaseIncBase >> 16) * uint32_t(m_sampleRate), 0x10000u);
            uint32_t numer = (op.cfg.dt & 4) ? ((denom > dtMag) ? (denom - dtMag) : 0u) : (denom + dtMag);
            inc = uint32_t((uint64_t(numer) * uint64_t(phaseIncBase)) / uint64_t(denom));
        }
        if (op.cfg.mul == 0) {
            op.phaseInc = inc >> 1;
        } else {
            op.phaseInc = inc * uint32_t(op.cfg.mul);
        }
        op.modPhaseInc = op.phaseInc;
    }
}

void YamahaMaFmSynth::updateVoiceGains(VoiceState &v) {
    const ChannelState &cs = m_channels[v.ch];
    int dbIdx = int(kYamahaMaVolDbTable[cs.volume & 0x7F])
              + int(kYamahaMaVolDbTable[cs.expression & 0x7F])
              + int(kYamahaMaVolDbTable[v.velocity & 0x7F]);
    dbIdx = std::clamp(dbIdx, 0, 192);
    uint32_t volGain = kYamahaMaPanAttTable[dbIdx];

    int pan5 = int(cs.pan >> 2);
    if (v.patch.pe) {
        pan5 = std::clamp(int(v.patch.panpot) + (pan5 - 16), 0, 31);
    } else {
        pan5 = std::clamp(pan5, 0, 31);
    }

    v.panGainL = (uint32_t(kYamahaMaPanGainL[pan5]) * volGain) >> 15;
    v.panGainR = (uint32_t(kYamahaMaPanGainR[pan5]) * volGain) >> 15;
}

void YamahaMaFmSynth::releaseVoice(VoiceState &v) {
    v.keyOn = false;
    int numOps = (v.patch.alg >= 2) ? 4 : 2;
    for (int i = 0; i < numOps; ++i) {
        if (v.op[i].egState != 0 && !v.op[i].cfg.xof) {
            v.op[i].egState = 4; // Release
        }
    }
}

void YamahaMaFmSynth::stepEnvelope(OperatorState &op, bool keyOn) {
    switch (op.egState) {
    case 1: { // Attack
        uint64_t next = uint64_t(op.egLevel) + uint64_t(op.arStep);
        if (next >= 0x80000000ull) {
            op.egLevel = 0x80000000u;
            op.egState = (op.slLevel == 0x80000000u) ? 3 : 2;
        } else {
            op.egLevel = uint32_t(next);
        }
        break;
    }
    case 2: { // Decay
        uint32_t next = uint32_t((uint64_t(op.egLevel) * uint64_t(op.drStep)) >> 30);
        if (next <= op.slLevel) {
            op.egLevel = op.slLevel;
            if (op.egLevel <= 0x00100000u) {
                op.egLevel = 0;
                op.egState = 0;
            } else {
                op.egState = 3;
            }
        } else {
            op.egLevel = next;
        }
        break;
    }
    case 3: { // Sustain
        uint32_t step = keyOn ? op.srStep : op.rrStep;
        uint32_t next = uint32_t((uint64_t(op.egLevel) * uint64_t(step)) >> 30);
        if (next <= 0x00100000u) {
            op.egLevel = 0;
            op.egState = 0;
        } else {
            op.egLevel = next;
        }
        break;
    }
    case 4: { // Release
        uint32_t next = uint32_t((uint64_t(op.egLevel) * uint64_t(op.rrStep)) >> 30);
        if (next <= 0x00100000u) {
            op.egLevel = 0;
            op.egState = 0;
        } else {
            op.egLevel = next;
        }
        break;
    }
    default:
        op.egLevel = 0;
        break;
    }
}

int32_t YamahaMaFmSynth::computeOpSample(OperatorState &op, int32_t modInput, bool useFeedback) {
    if (op.egState == 0 || op.egLevel == 0) {
        op.phase += op.modPhaseInc;
        op.prev1 = op.prev0;
        op.prev0 = 0;
        return 0;
    }

    uint32_t waveIdx = 0;
    if (useFeedback) {
        if (op.cfg.fb != 0) {
            int32_t fbSum = op.prev0 + op.prev1;
            waveIdx = ((op.phase >> 22) + uint32_t((fbSum >> op.cfg.fb) & 0x3FF)) & 0x3FFu;
        } else {
            waveIdx = (op.phase >> 22) & 0x3FFu;
        }
    } else {
        int32_t modScaled = (modInput << 14) >> 15;
        waveIdx = (((op.phase >> 20) + uint32_t(modScaled)) >> 2) & 0x3FFu;
    }

    op.phase += op.modPhaseInc;

    int32_t waveSample = kYamahaMaWaveforms[op.cfg.ws][waveIdx];
    uint32_t envGain = ((op.egLevel >> 16) * op.modTotalGain) >> 15;
    int32_t out = (waveSample * int32_t(envGain)) >> 15;

    op.prev1 = op.prev0;
    op.prev0 = out;
    return out;
}

void YamahaMaFmSynth::renderAndMix(int16_t *outInterleavedStereo, size_t frames) {
    if (!outInterleavedStereo || frames == 0) {
        return;
    }

    for (size_t f = 0; f < frames; ++f) {
        int32_t mixL = outInterleavedStereo[f * 2];
        int32_t mixR = outInterleavedStereo[f * 2 + 1];

        for (int vIdx = 0; vIdx < kMaxVoices; ++vIdx) {
            VoiceState &v = m_voices[vIdx];
            if (!v.active) {
                continue;
            }

            int numOps = (v.patch.alg >= 2) ? 4 : 2;
            uint32_t lfoIdx12 = (v.lfoPhase >> 20) & 0xFFFu;
            v.lfoPhase += v.lfoStep;

            bool anyAlive = false;
            for (int i = 0; i < numOps; ++i) {
                OperatorState &op = v.op[i];
                if (op.cfg.vib) {
                    uint32_t pm = computeLfoPmFactor(op.cfg.dvb, lfoIdx12);
                    op.modPhaseInc = uint32_t((uint64_t(pm) * uint64_t(op.phaseInc)) >> 20);
                } else {
                    op.modPhaseInc = op.phaseInc;
                }
                if (op.cfg.eam) {
                    uint32_t am = computeLfoAmFactor(op.cfg.dam, lfoIdx12);
                    op.modTotalGain = uint32_t((uint64_t(am) * uint64_t(op.totalGain)) >> 15);
                } else {
                    op.modTotalGain = op.totalGain;
                }
                stepEnvelope(op, v.keyOn);
                if (op.egState != 0) {
                    anyAlive = true;
                }
            }

            if (!anyAlive) {
                v.active = false;
                continue;
            }

            int32_t voiceOut = 0;
            switch (v.patch.alg & 0x07) {
            case 0: { // 2-op: Op0(FB) -> Op1
                int32_t s0 = computeOpSample(v.op[0], 0, true);
                voiceOut = computeOpSample(v.op[1], s0, false);
                break;
            }
            case 1: { // 2-op: Op0(FB) + Op1
                int32_t s0 = computeOpSample(v.op[0], 0, true);
                int32_t s1 = computeOpSample(v.op[1], 0, false);
                voiceOut = s0 + s1;
                break;
            }
            case 2: { // 4-op: Op0(FB) + Op1 + Op2(FB) + Op3
                int32_t s0 = computeOpSample(v.op[0], 0, true);
                int32_t s1 = computeOpSample(v.op[1], 0, false);
                int32_t s2 = computeOpSample(v.op[2], 0, true);
                int32_t s3 = computeOpSample(v.op[3], 0, false);
                voiceOut = s0 + s1 + s2 + s3;
                break;
            }
            case 3: { // 4-op: (Op0(FB) + (Op1 -> Op2)) -> Op3
                int32_t s0 = computeOpSample(v.op[0], 0, true);
                int32_t s1 = computeOpSample(v.op[1], 0, false);
                int32_t s2 = computeOpSample(v.op[2], s1, false);
                voiceOut = computeOpSample(v.op[3], s0 + s2, false);
                break;
            }
            case 4: { // 4-op: Op0(FB) -> Op1 -> Op2 -> Op3
                int32_t s0 = computeOpSample(v.op[0], 0, true);
                int32_t s1 = computeOpSample(v.op[1], s0, false);
                int32_t s2 = computeOpSample(v.op[2], s1, false);
                voiceOut = computeOpSample(v.op[3], s2, false);
                break;
            }
            case 5: { // 4-op: (Op0(FB) -> Op1) + (Op2(FB) -> Op3)
                int32_t s0 = computeOpSample(v.op[0], 0, true);
                int32_t s1 = computeOpSample(v.op[1], s0, false);
                int32_t s2 = computeOpSample(v.op[2], 0, true);
                int32_t s3 = computeOpSample(v.op[3], s2, false);
                voiceOut = s1 + s3;
                break;
            }
            case 6: { // 4-op: Op0(FB) + (Op1 -> Op2 -> Op3)
                int32_t s0 = computeOpSample(v.op[0], 0, true);
                int32_t s1 = computeOpSample(v.op[1], 0, false);
                int32_t s2 = computeOpSample(v.op[2], s1, false);
                int32_t s3 = computeOpSample(v.op[3], s2, false);
                voiceOut = s0 + s3;
                break;
            }
            case 7: { // 4-op: Op0(FB) + (Op1 -> Op2) + Op3
                int32_t s0 = computeOpSample(v.op[0], 0, true);
                int32_t s1 = computeOpSample(v.op[1], 0, false);
                int32_t s2 = computeOpSample(v.op[2], s1, false);
                int32_t s3 = computeOpSample(v.op[3], 0, false);
                voiceOut = s0 + s2 + s3;
                break;
            }
            }

            mixL += (voiceOut * int32_t(v.panGainL)) >> 15;
            mixR += (voiceOut * int32_t(v.panGainR)) >> 15;
        }

        // Apply master FM headroom scale (0.4818x = 15788 / 32768, matching M5_EmuHw.dll at vol=91)
        mixL = (mixL * 15788) >> 15;
        mixR = (mixR * 15788) >> 15;

        outInterleavedStereo[f * 2]     = int16_t(std::clamp(mixL, -32767, 32767));
        outInterleavedStereo[f * 2 + 1] = int16_t(std::clamp(mixR, -32767, 32767));
    }
}
