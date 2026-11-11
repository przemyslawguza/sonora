#include "Config.h"
#include <algorithm>

void SpectrumConfig::sanitize()
{
    bars = std::clamp(bars, 8, 512);
    fMinHz = std::clamp(fMinHz, 0.0, 90000.0);
    fMaxHz = std::clamp(fMaxHz, 500.0, 96000.0);
    if (fMaxHz < fMinHz + 400.0) fMaxHz = fMinHz + 400.0;
    ceilDb = std::clamp(ceilDb, -60, 12);
    floorDb = std::clamp(floorDb, -120, ceilDb - 10);
    if (fftBands != 512 && fftBands != 1024 && fftBands != 2048 && fftBands != 4096 && fftBands != 8192)
        fftBands = 2048;
}

void SpectrumConfig::load(QSettings &s)
{
    s.beginGroup("spectrum");
    bars     = s.value("bars", 112).toInt();
    fMinHz   = s.value("fMinHz", 0.0).toDouble();
    fMaxHz   = s.value("fMaxHz", 24000.0).toDouble();
    logScale = s.value("log", false).toBool();
    floorDb  = s.value("floorDb", -80).toInt();
    ceilDb   = s.value("ceilDb", -10).toInt();
    fftBands = s.value("fftBands", 2048).toInt();
    s.endGroup();
    sanitize();
}

void SpectrumConfig::save(QSettings &s) const
{
    s.beginGroup("spectrum");
    s.setValue("bars", bars);
    s.setValue("fMinHz", fMinHz);
    s.setValue("fMaxHz", fMaxHz);
    s.setValue("log", logScale);
    s.setValue("floorDb", floorDb);
    s.setValue("ceilDb", ceilDb);
    s.setValue("fftBands", fftBands);
    s.endGroup();
}

void MeterConfig::sanitize()
{
    maxDb = std::clamp(maxDb, 0.0, 12.0);        // the red "over 0 dB" LED must always exist
    minDb = std::clamp(minDb, -120.0, maxDb - 12.0);
    barWidth = std::clamp(barWidth, 4, 40);
    ledStepDb = std::clamp(ledStepDb, 0.25, 6.0);
}

void MeterConfig::load(QSettings &s)
{
    s.beginGroup("meter");
    minDb     = s.value("minDb", -90.0).toDouble();
    maxDb     = s.value("maxDb", 3.0).toDouble();
    barWidth  = s.value("barWidth", 10).toInt();
    ledStepDb = s.value("ledStep", 0.75).toDouble();     // new key: old integer setting is not reused
    showScale = s.value("showScale", true).toBool();
    truePeak  = s.value("truePeak", true).toBool();
    s.endGroup();
    sanitize();
}

void MeterConfig::save(QSettings &s) const
{
    s.beginGroup("meter");
    s.setValue("minDb", minDb);
    s.setValue("maxDb", maxDb);
    s.setValue("barWidth", barWidth);
    s.setValue("ledStep", ledStepDb);
    s.setValue("showScale", showScale);
    s.setValue("truePeak", truePeak);
    s.endGroup();
}
