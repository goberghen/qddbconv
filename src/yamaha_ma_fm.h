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

#ifndef YAMAHA_MA_FM_H
#define YAMAHA_MA_FM_H

#include <cstdint>
#include <cstddef>
#include <vector>

struct YamahaMaFmOperatorPatch {
    uint8_t mul = 1;   // Frequency multiplier (0, 1..10, 12, 15)
    uint8_t vib = 0;   // Vibrato enable (0..1)
    uint8_t xof = 0;   // Ignore KeyOff flag (0..1)
    uint8_t sus = 0;   // Sustain pedal hold enable (0..1)
    uint8_t ar  = 15;  // Attack rate (0..15)
    uint8_t dr  = 0;   // Decay rate (0..15)
    uint8_t sr  = 0;   // Sustain rate (0..15)
    uint8_t rr  = 7;   // Release rate (0..15)
    uint8_t tl  = 0;   // Total level attenuation (0..63)
    uint8_t ksl = 0;   // Key scale level (0..3, mapped via {0, 2, 1, 3})
    uint8_t dam = 0;   // Amplitude modulation depth (0..3)
    uint8_t eam = 0;   // Amplitude modulation enable (0..1)
    uint8_t dvb = 0;   // Vibrato depth (0..3)
    uint8_t ksr = 0;   // Key scale rate (0..1)
    uint8_t sl  = 0;   // Sustain level (0..15)
    uint8_t dt  = 0;   // Detune (0..7)
    uint8_t ws  = 0;   // Waveform select (0..28)
    uint8_t fb  = 0;   // Feedback shift (0 or 4..10; active only on op 0 and op 2)
};

struct YamahaMaFmPatch {
    bool valid = false;
    bool isWaveTable = false;
    bool isStreamPcm = false;
    uint8_t panpot = 16;    // 0..31 (16 = center)
    uint8_t pe     = 1;     // Pan enable
    uint8_t vb     = 0;     // Global vibrato flag
    int8_t  bo     = 0;     // Basic octave shift ({0, 1, 2, -1})
    uint8_t lfo    = 0;     // LFO speed index (0..3)
    uint8_t alg    = 0;     // Algorithm (0..7; 0..1 = 2-op, 2..7 = 4-op)
    uint8_t drumPitch = 60; // Fixed MIDI note for drum voices
    YamahaMaFmOperatorPatch op[4];
};

class YamahaMaFmSynth {
public:
    explicit YamahaMaFmSynth(int sampleRate = 44100, bool prefer4OpRom = false);

    void reset();
    void setSampleRate(int sampleRate);
    void setPrefer4OpRom(bool prefer4Op);

    // Load custom voice or register SysEx payload (without leading F0, with or without trailing F7)
    // Returns true if the SysEx defined a patch or executed a real-time FM register write.
    bool loadSysEx(int trackBaseCh, const uint8_t *data, size_t len);

    // Parse EXVO chunk payload if present
    bool loadExvoChunk(const uint8_t *data, size_t len);

    // Query whether a (channel, note) should be synthesized by this FM engine
    // rather than routed to an external WaveTable MIDI synth (FluidSynth/TiMidity++).
    bool shouldRenderOnFm(int ch, int note, bool externalWtAvailable) const;
    bool isStreamPcmVoice(int ch, int note) const;

    // Channel control events
    void setBank(int ch, uint8_t bankM, uint8_t bankL);
    void setHpsBank(int ch, uint8_t bank);
    void setProgram(int ch, uint8_t prog);
    void setHpsDrumNote(int ch, uint8_t drumNote);
    void setVolume(int ch, uint8_t vol);
    void setExpression(int ch, uint8_t expr);
    void setPan(int ch, uint8_t pan);
    void setModulation(int ch, uint8_t mod);
    void setPitchBend(int ch, int bend14); // -8192 .. +8191
    void setPitchBendRange(int ch, uint8_t semitones);
    void allNotesOff(int ch);
    void allSoundOff(int ch);

    // Standard MIDI/SMAF Note On / Note Off
    void noteOn(int ch, int note, int velocity);
    void noteOff(int ch, int note);

    // Direct hardware F-Number / Block / KeyOn register writes (43 02|03 90 B<ch>/C<ch>)
    void writeRegB0(int ch, uint8_t val);
    void writeRegC0(int ch, uint8_t val);

    // Render stereo 16-bit PCM frames and mix (accumulate with saturation) into outInterleavedStereo
    void renderAndMix(int16_t *outInterleavedStereo, size_t frames);

    bool hasRenderedNotes() const { return m_hasRenderedNotes; }

    // Unpack 16-byte (2-op) or 30-byte (4-op) hardware patch into YamahaMaFmPatch
    static YamahaMaFmPatch unpackHardwarePatch(const uint8_t *raw, size_t len, uint8_t drumPitch = 60);
    // Convert 15-byte (2-op) or 25-byte (4-op) MA-2 43 03 SysEx body into 16/30-byte hardware patch
    static int convertMa2SysExToHardware(const uint8_t *src, size_t len, uint8_t outHw[30]);
    // Unpack Yamaha 7-bit MIDI encoding (8 bytes -> 7 bytes) used in format 3 Mtsu
    static size_t unpackYamaha7to8(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t maxDstLen);

private:
    struct OperatorState {
        YamahaMaFmOperatorPatch cfg;
        uint32_t phase = 0;
        uint32_t phaseInc = 0;
        uint32_t modPhaseInc = 0;
        uint32_t tlLevel = 0x8000;
        uint32_t kslLevel = 0x8000;
        uint32_t totalGain = 0x8000;
        uint32_t modTotalGain = 0x8000;
        uint32_t egLevel = 0;
        uint32_t slLevel = 0;
        uint32_t arStep = 0;
        uint32_t drStep = 0;
        uint32_t srStep = 0;
        uint32_t rrStep = 0;
        uint8_t  egState = 0; // 0=Off, 1=Attack, 2=Decay, 3=Sustain, 4=Release
        int32_t  prev0 = 0;
        int32_t  prev1 = 0;
    };

    struct VoiceState {
        bool     active = false;
        bool     keyOn = false;
        bool     regControlled = false;
        int      ch = 0;
        int      note = -1;
        uint8_t  velocity = 100;
        uint8_t  block = 4;
        uint16_t fnum = 0;
        uint32_t lfoPhase = 0;
        uint32_t lfoStep = 0;
        uint32_t panGainL = 23198;
        uint32_t panGainR = 23198;
        uint32_t age = 0;
        YamahaMaFmPatch patch;
        OperatorState op[4];
    };

    struct ChannelState {
        uint8_t  bankM = 0;
        uint8_t  bankL = 0;
        uint8_t  hpsBank = 0;
        bool     useHpsBank = false;
        uint8_t  program = 0;
        uint8_t  hpsDrumNote = 0;
        uint8_t  volume = 100;
        uint8_t  expression = 127;
        uint8_t  pan = 64;
        uint8_t  modulation = 0;
        int      pitchBend = 0;
        uint8_t  pitchBendRange = 2;
        uint8_t  regB0 = 0;
        uint8_t  regC0 = 0;
        int      regVoiceIdx = -1;
    };

    struct CustomMelodicEntry {
        uint8_t bankM = 0;
        uint8_t bankL = 0;
        uint8_t hpsBank = 0;
        bool    isHps = false;
        uint8_t program = 0;
        YamahaMaFmPatch patch;
    };

    struct CustomDrumEntry {
        uint8_t bankL = 0;
        uint8_t kit = 0;
        uint8_t drumNote = 0;
        bool    isHps = false;
        YamahaMaFmPatch patch;
    };

    int m_sampleRate = 44100;
    bool m_prefer4OpRom = false;
    bool m_hasRenderedNotes = false;
    uint32_t m_voiceCounter = 0;

    static constexpr int kMaxVoices = 32;
    static constexpr int kMaxChannels = 32;

    VoiceState m_voices[kMaxVoices];
    ChannelState m_channels[kMaxChannels];
    std::vector<CustomMelodicEntry> m_customMelodic;
    std::vector<CustomDrumEntry> m_customDrums;

    const YamahaMaFmPatch *findCustomPatch(int ch, int note, bool *outIsDrum = nullptr) const;
    YamahaMaFmPatch resolvePatch(int ch, int note, int *outPlayNote) const;
    int allocateVoice(int ch, int note);
    void startVoiceWithBlockFnum(int vIdx, int ch, int note, int velocity, const YamahaMaFmPatch &patch,
                                 uint8_t block, uint16_t fnum, bool regControlled);
    void updateVoiceFrequencyAndRates(VoiceState &v, bool isKeyOnTransition);
    void updateVoiceGains(VoiceState &v);
    void releaseVoice(VoiceState &v);
    void stepEnvelope(OperatorState &op, bool keyOn);
    int32_t computeOpSample(OperatorState &op, int32_t modInput, bool useFeedback);
};

#endif // YAMAHA_MA_FM_H
