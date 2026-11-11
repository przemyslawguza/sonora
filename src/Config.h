#pragma once
#include <QSettings>
#include <algorithm>
#include "AudioOutput.h"

struct SpectrumConfig
{
    int bars = 224;               // number of bars
    double fMinHz = 0.0;          // displayed frequency range fMinHz ... fMaxHz
    double fMaxHz = 22000.0;
    bool logScale = false;        // false = linear frequency axis
    int floorDb = -60;
    int ceilDb = -20;
    int fftBands = 4096;          // FFT bands: frequency resolution (512 ... 8192)

    void sanitize();
    void load(QSettings &s);
    void save(QSettings &s) const;
};

struct MeterConfig
{
    double minDb = -60.0;
    double maxDb = 1.0;
    int barWidth = 10;            // px per channel (narrow LED column)
    double ledStepDb = 0.33;      // dB per LED: number of LEDs = range / step
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
    int vizDelayMs = 0;           // ONE delay for level meters and spectrum analyser (sync with the sound)

    void load(QSettings &s)
    {
        audio.load(s); spectrum.load(s); meter.load(s);
        const AppConfig d;                  // defaults live here only
        vizDelayMs = std::clamp(s.value("visualisation/delayMs", d.vizDelayMs).toInt(), 0, 2000);
    }
    void save(QSettings &s) const
    {
        audio.save(s); spectrum.save(s); meter.save(s);
        s.setValue("visualisation/delayMs", vizDelayMs);
    }
};
