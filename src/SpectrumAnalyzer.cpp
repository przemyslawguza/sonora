#include "SpectrumAnalyzer.h"
#include <QLinearGradient>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr double kFallDbS  = 40.0;
constexpr double kPeakFall = 25.0;
constexpr qint64 kPeakHold = 700;

QString freqText(double f)
{
    return f >= 1000.0 ? QString::number(f / 1000.0, 'g', 3) + "k" : QString::number(int(std::lround(f)));
}
}

SpectrumAnalyzer::SpectrumAnalyzer(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(120);
    setToolTip(tr("Spectrum analyser"));
    setConfig(SpectrumConfig());
    m_clock.start();
    connect(&m_timer, &QTimer::timeout, this, &SpectrumAnalyzer::tick);
    m_timer.start(30);
}

void SpectrumAnalyzer::setConfig(const SpectrumConfig &c)
{
    m_cfg = c;
    m_cfg.sanitize();
    m_fmin = m_cfg.fMinHz;
    m_fmax = m_cfg.fMaxHz;
    m_fminLog = std::max(m_cfg.fMinHz, 20.0);
    if (m_fmax <= m_fminLog * 1.5) m_fmax = m_fminLog * 1.5;
    reset();
}

void SpectrumAnalyzer::reset()
{
    m_bar.fill(m_cfg.floorDb, m_cfg.bars);
    m_peak.fill(m_cfg.floorDb, m_cfg.bars);
    m_peakAt.fill(0, m_cfg.bars);
    update();
}

double SpectrumAnalyzer::edge(int k) const
{
    const double t = double(k) / m_cfg.bars;
    if (!m_cfg.logScale) return m_fmin + (m_fmax - m_fmin) * t;
    return m_fminLog * std::pow(m_fmax / m_fminLog, t);
}

void SpectrumAnalyzer::setSpectrum(const QVector<float> &mag, int rate)
{
    const int n = int(mag.size());
    if (n < 8 || rate <= 0) return;

    const double nyq = rate / 2.0;
    const double binW = nyq / n;                       // Hz per FFT band
    const double floorDb = m_cfg.floorDb, ceilDb = m_cfg.ceilDb;
    const qint64 now = m_clock.elapsed();

    for (int k = 0; k < m_cfg.bars; ++k) {
        const double fa = edge(k), fb = edge(k + 1);
        double v = floorDb;
        if (fa < nyq) {                                // bars above Nyquist stay empty
            const int ia = std::clamp(int(std::floor(fa / binW)), 0, n - 1);
            const int ib = std::clamp(int(std::ceil(std::min(fb, nyq) / binW)), ia + 1, n);
            if (ib - ia <= 1) {                        // narrower than one FFT band: interpolate
                const double pos = 0.5 * (fa + fb) / binW - 0.5;
                const int i0 = std::clamp(int(std::floor(pos)), 0, n - 1);
                const int i1 = std::min(i0 + 1, n - 1);
                const double fr = std::clamp(pos - i0, 0.0, 1.0);
                v = mag[i0] * (1.0 - fr) + mag[i1] * fr;
            } else {                                   // several FFT bands: loudest one
                v = mag[ia];
                for (int i = ia + 1; i < ib; ++i) v = std::max(v, double(mag[i]));
            }
        }
        v = std::clamp(v, floorDb, ceilDb);
        m_bar[k] = std::max(m_bar[k], v);
        if (v >= m_peak[k]) { m_peak[k] = v; m_peakAt[k] = now; }
    }
}

void SpectrumAnalyzer::tick()
{
    const qint64 now = m_clock.elapsed();
    double dt = (now - m_last) / 1000.0;
    m_last = now;
    if (dt <= 0 || dt > 1) dt = 0.03;
    const double floorDb = m_cfg.floorDb;
    for (int k = 0; k < m_bar.size(); ++k) {
        m_bar[k] = std::max(floorDb, m_bar[k] - kFallDbS * dt);
        if (now - m_peakAt[k] > kPeakHold)
            m_peak[k] = std::max(floorDb, m_peak[k] - kPeakFall * dt);
        m_peak[k] = std::max(m_peak[k], m_bar[k]);
    }
    update();
}

void SpectrumAnalyzer::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window));

    const QRect plot(4, 4, width() - 8, height() - 8 - 14);
    if (plot.width() < 40 || plot.height() < 20) return;
    p.fillRect(plot, QColor(25, 25, 25));

    const double floorDb = m_cfg.floorDb, ceilDb = m_cfg.ceilDb;
    auto yOf = [&](double db) {
        db = std::clamp(db, floorDb, ceilDb);
        return plot.bottom() + 1 - int(std::lround((db - floorDb) / (ceilDb - floorDb) * plot.height()));
    };
    auto xOf = [&](double f) {
        const double t = m_cfg.logScale ? std::log(f / m_fminLog) / std::log(m_fmax / m_fminLog)
                                        : (f - m_fmin) / (m_fmax - m_fmin);
        return plot.left() + int(std::lround(t * plot.width()));
    };

    // ---- frequency axis
    QFont f = font();
    f.setPointSizeF(std::max(6.0, f.pointSizeF() - 1.5));
    p.setFont(f);
    const QColor txt = palette().color(QPalette::WindowText);
    const QColor grid(255, 255, 255, 28);

    std::vector<double> marks;
    if (m_cfg.logScale) {
        for (double hz : {20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0, 40000.0})
            if (hz >= m_fminLog && hz <= m_fmax) marks.push_back(hz);
    } else {
        const double span = m_fmax - m_fmin;
        double step = 1000.0;
        for (double s : {500.0, 1000.0, 2000.0, 2500.0, 5000.0, 10000.0, 20000.0}) {
            step = s;
            if (span / s <= 12.0) break;
        }
        for (double hz = std::ceil(m_fmin / step) * step; hz <= m_fmax + 1.0; hz += step) marks.push_back(hz);
    }
    for (double hz : marks) {
        const int x = xOf(hz);
        p.setPen(grid);
        p.drawLine(x, plot.top(), x, plot.bottom());
        p.setPen(txt);
        QRect r(0, plot.bottom() + 2, 40, 12);
        r.moveLeft(std::clamp(x - 20, 0, std::max(0, width() - 40)));
        p.drawText(r, Qt::AlignCenter, freqText(hz));
    }

    // ---- bars
    QLinearGradient g(0, plot.bottom() + 1, 0, plot.top());
    g.setColorAt(0.0, QColor(0, 170, 60));
    g.setColorAt(0.6, QColor(0, 200, 70));
    g.setColorAt(0.85, QColor(230, 210, 0));
    g.setColorAt(1.0, QColor(230, 40, 30));
    const QBrush brush(g);

    const int bars = int(m_bar.size());
    const double w = plot.width() / double(bars);
    for (int k = 0; k < bars; ++k) {
        const int x0 = plot.left() + int(k * w);
        const int x1 = plot.left() + int((k + 1) * w) - 1;
        const int bw = std::max(1, x1 - x0);
        if (m_bar[k] > floorDb) {
            const int y = yOf(m_bar[k]);
            p.fillRect(QRect(x0, y, bw, plot.bottom() + 1 - y), brush);
        }
        if (m_peak[k] > floorDb + 0.5)
            p.fillRect(QRect(x0, yOf(m_peak[k]), bw, 2), QColor(255, 255, 255));
    }
}
