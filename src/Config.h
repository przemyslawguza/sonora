#pragma once
#include <QSettings>
#include "AudioOutput.h"

struct SpectrumConfig
{
    int bars = 224;               // 2x the previous 56
    double fMinHz = 0.0;          // default display range 0 ... 24 kHz
    double fMaxHz = 22000.0;
    bool logScale = false;        // false = linear frequency axis
    int floorDb = -60;
    int ceilDb = -20;
    int fftBands = 4096;          // 2x the previous 1024: finer frequency resolution

    void sanitize();
    void load(QSettings &s);
    void save(QSettings &s) const;
};

struct MeterConfig
{
    double minDb = -60.0;
    double maxDb = 1.0;
    int barWidth = 10;            // px per channel (narrow LED column)
    double ledStepDb = 0.33;      // dB per LED: 0.75 dB = 124 LEDs over -90...+3 dB (4x the former 31)
    bool showScale = true;
    bool truePeak = true;         // inter-sample (true) peak detection, 192 kHz oversampled

    void sanitize();
    void load(QSettings &s);
    void save(QSettings &s) const;
};

struct AppConfig
{
    AudioOutputConfig audio;
    SpectrumConfig spectrum;
    MeterConfig meter;

    void load(QSettings &s) { audio.load(s); spectrum.load(s); meter.load(s); }
    void save(QSettings &s) const { audio.save(s); spectrum.save(s); meter.save(s); }
};
