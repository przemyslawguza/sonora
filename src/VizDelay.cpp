#include "VizDelay.h"
#include <algorithm>

namespace {
constexpr qint64 kMaxHoldMs = 8000;         // safety: nothing is held longer than this (+ the delay)
}

VizDelay::VizDelay(QObject *parent) : QObject(parent)
{
    m_clock.start();
    m_timer.setInterval(8);
    connect(&m_timer, &QTimer::timeout, this, &VizDelay::pump);
}

void VizDelay::setDelayMs(int ms)
{
    m_delay = std::clamp(ms, -500, 4000);
}

bool VizDelay::isDue(const Item &it, qint64 now, qint64 pos) const
{
    if (now - it.arrived > kMaxHoldMs + std::max(0, m_delay)) return true;
    if (it.streamMs < 0 || pos < 0)                          // no timestamp / no position: plain delay
        return now - it.arrived >= std::max(0, m_delay);
    return it.streamMs <= pos - m_delay;                     // the sound has reached this moment
}

void VizDelay::enqueue(std::deque<Item> &q, Item &&it)
{
    it.arrived = m_clock.elapsed();
    q.push_back(std::move(it));
    if (!m_timer.isActive()) m_timer.start();
}

void VizDelay::pushLevels(const QVector<double> &peak, const QVector<double> &rms, qint64 streamMs)
{
    if (streamMs < 0 && m_delay <= 0) { emit levels(peak, rms); return; }
    Item it;
    it.streamMs = streamMs;
    it.peak = peak;
    it.rms = rms;
    enqueue(m_levels, std::move(it));
}

void VizDelay::pushSpectrum(const QVector<float> &mag, int rate, qint64 streamMs)
{
    if (streamMs < 0 && m_delay <= 0) { emit spectrum(mag, rate); return; }
    Item it;
    it.streamMs = streamMs;
    it.mag = mag;
    it.rate = rate;
    enqueue(m_spectra, std::move(it));
}

void VizDelay::clear()
{
    m_levels.clear();
    m_spectra.clear();
    m_timer.stop();
}

void VizDelay::pump()
{
    const qint64 now = m_clock.elapsed();
    const qint64 pos = m_pos ? m_pos() : -1;
    while (!m_levels.empty() && isDue(m_levels.front(), now, pos)) {
        const Item it = std::move(m_levels.front());         // copy out first: a slot may call clear()
        m_levels.pop_front();
        emit levels(it.peak, it.rms);
    }
    while (!m_spectra.empty() && isDue(m_spectra.front(), now, pos)) {
        const Item it = std::move(m_spectra.front());
        m_spectra.pop_front();
        emit spectrum(it.mag, it.rate);
    }
    if (m_levels.empty() && m_spectra.empty()) m_timer.stop();
}
