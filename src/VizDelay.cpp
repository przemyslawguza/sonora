#include "VizDelay.h"
#include <algorithm>

VizDelay::VizDelay(QObject *parent) : QObject(parent)
{
    m_clock.start();
    m_timer.setInterval(5);
    connect(&m_timer, &QTimer::timeout, this, &VizDelay::pump);
}

void VizDelay::setDelayMs(int ms)
{
    m_delay = std::clamp(ms, 0, 5000);
    if (m_delay == 0) {                     // delay switched off: release what is waiting right now
        std::deque<Item> rest;
        rest.swap(m_queue);
        for (const Item &it : rest) emitItem(it);
        m_timer.stop();
    }
}

void VizDelay::emitItem(const Item &it)
{
    if (it.isSpectrum) emit spectrum(it.mag, it.rate);
    else               emit levels(it.peak, it.rms);
}

void VizDelay::enqueue(Item &&it)
{
    it.due = m_clock.elapsed() + m_delay;
    m_queue.push_back(std::move(it));
    if (!m_timer.isActive()) m_timer.start();
}

void VizDelay::pushLevels(const QVector<double> &peak, const QVector<double> &rms)
{
    if (m_delay <= 0) { emit levels(peak, rms); return; }
    Item it;
    it.peak = peak;
    it.rms = rms;
    enqueue(std::move(it));
}

void VizDelay::pushSpectrum(const QVector<float> &mag, int rate)
{
    if (m_delay <= 0) { emit spectrum(mag, rate); return; }
    Item it;
    it.isSpectrum = true;
    it.mag = mag;
    it.rate = rate;
    enqueue(std::move(it));
}

void VizDelay::clear()
{
    m_queue.clear();
    m_timer.stop();
}

void VizDelay::pump()
{
    const qint64 now = m_clock.elapsed();
    while (!m_queue.empty() && m_queue.front().due <= now) {
        const Item it = std::move(m_queue.front());     // copy out first: a slot may call clear()
        m_queue.pop_front();
        emitItem(it);
    }
    if (m_queue.empty()) m_timer.stop();
}
