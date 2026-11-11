#include "LevelMeter.h"
#include <QPainter>
#include <QPalette>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
constexpr int    kScaleW   = 30;
constexpr int    kTop      = 14;
constexpr int    kBottom   = 22;
constexpr int    kGap      = 3;
constexpr double kFallDbS  = 40.0;
constexpr double kHoldFall = 25.0;
constexpr qint64 kHoldMs   = 1500;
constexpr qint64 kOverHoldMs = 3000;      // overs (> 0 dB) stay visible longer

QColor ledColor(double loDb)
{
    if (loDb >= 0.0)   return QColor(235, 40, 30);     // over 0 dB: red
    if (loDb >= -6.0)  return QColor(255, 140, 0);     // orange
    if (loDb >= -18.0) return QColor(240, 215, 0);     // yellow
    return QColor(0, 205, 70);                          // green
}
}

LevelMeter::LevelMeter(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setChannels(2);
    m_clock.start();
    connect(&m_timer, &QTimer::timeout, this, &LevelMeter::tick);
    m_timer.start(30);
}

void LevelMeter::setConfig(const MeterConfig &c)
{
    m_cfg = c;
    m_cfg.sanitize();
    setChannels(int(m_disp.size()));
    update();
}

void LevelMeter::setChannels(int n)
{
    n = std::max(1, n);
    m_disp.fill(m_cfg.minDb, n);
    m_hold.fill(m_cfg.minDb, n);
    m_holdAt.fill(0, n);
    updateGeometry();
}

int LevelMeter::scaleWidth() const { return m_cfg.showScale ? kScaleW : 0; }

// LED k spans [bottom + k*step, bottom + (k+1)*step). The grid is anchored at 0 dB, so there is
// always a LED whose lower edge is exactly 0 dB - that LED (and everything above) is red.
double LevelMeter::gridTop() const
{
    const double s = m_cfg.ledStepDb;
    return std::max(std::ceil(m_cfg.maxDb / s - 1e-9) * s, s);
}

int LevelMeter::ledCount() const
{
    return std::max(1, int(std::ceil((gridTop() - m_cfg.minDb) / m_cfg.ledStepDb - 1e-9)));
}

double LevelMeter::gridBottom() const { return gridTop() - ledCount() * m_cfg.ledStepDb; }

QSize LevelMeter::sizeHint() const
{
    return QSize(scaleWidth() + int(m_disp.size()) * (m_cfg.barWidth + kGap) + 6, 260);
}

QSize LevelMeter::minimumSizeHint() const
{
    return QSize(sizeHint().width(), 240);     // keeps the (many, thin) LEDs readable
}

void LevelMeter::setLevels(const QVector<double> &peak, const QVector<double> &rms)
{
    Q_UNUSED(rms);                      // LED ladder shows peak only
    if (peak.isEmpty()) return;
    if (peak.size() != m_disp.size()) setChannels(int(peak.size()));
    const qint64 now = m_clock.elapsed();
    for (int i = 0; i < peak.size(); ++i) {
        const double pk = std::isnan(peak[i]) ? m_cfg.minDb : std::clamp(peak[i], m_cfg.minDb, gridTop());
        m_disp[i] = std::max(m_disp[i], pk);
        if (pk >= m_hold[i]) { m_hold[i] = pk; m_holdAt[i] = now; }
    }
}

void LevelMeter::reset()
{
    setChannels(int(m_disp.size()));
    update();
}

void LevelMeter::tick()
{
    const qint64 now = m_clock.elapsed();
    double dt = (now - m_last) / 1000.0;
    m_last = now;
    if (dt <= 0 || dt > 1) dt = 0.03;
    for (int i = 0; i < m_disp.size(); ++i) {
        m_disp[i] = std::max(m_cfg.minDb, m_disp[i] - kFallDbS * dt);
        if (now - m_holdAt[i] > (m_hold[i] > 0.0 ? kOverHoldMs : kHoldMs))
            m_hold[i] = std::max(m_cfg.minDb, m_hold[i] - kHoldFall * dt);
        m_hold[i] = std::max(m_hold[i], m_disp[i]);
    }
    update();
}

void LevelMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window));

    const int n = int(m_disp.size());
    const int sw = scaleWidth();
    const QRect plot(sw, kTop, width() - sw - 4, height() - kTop - kBottom);
    if (plot.height() < 20 || plot.width() < n * 3) return;

    const double top = gridTop(), bot = gridBottom();
    const double range = top - bot;
    const int nLeds = ledCount();
    const double ledH = double(plot.height()) / nLeds;
    auto yOf = [&](double db) {
        db = std::clamp(db, bot, top);
        return plot.top() + int(std::lround((top - db) / range * plot.height()));
    };

    QFont f = font();
    f.setPointSizeF(std::max(6.0, f.pointSizeF() - 2.0));
    p.setFont(f);
    const QColor txt = palette().color(QPalette::WindowText);
    QColor grid = txt; grid.setAlpha(60);

    // ---- dB scale (0 dB gets priority, top value sits above its tick, close labels are skipped)
    if (m_cfg.showScale) {
        static const int ticks[] = {0, -6, -12, -24, -36, -48, -60, -75, -90, -105, -120};
        QVector<int> usedY;
        auto drawLabel = [&](int db, int y, bool above) {
            const QString s = db > 0 ? QStringLiteral("+%1").arg(db) : QString::number(db);
            p.setPen(grid);
            p.drawLine(plot.left() - 3, y, plot.left() - 1, y);
            p.setPen(txt);
            if (above) p.drawText(QRect(0, y - 13, sw - 5, 12), Qt::AlignRight | Qt::AlignBottom, s);
            else       p.drawText(QRect(0, y - 6, sw - 5, 12), Qt::AlignRight | Qt::AlignVCenter, s);
        };
        for (int db : ticks) {
            if (db > top || db < m_cfg.minDb) continue;
            const int y = yOf(db);
            bool clash = false;
            for (int u : usedY) clash = clash || std::abs(u - y) < 11;
            if (clash) continue;
            usedY.append(y);
            drawLabel(db, y, false);
        }
        const int topDb = int(std::lround(top));
        if (topDb > 0) drawLabel(topDb, plot.top(), true);
    }

    // ---- LED ladders
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const int slot = m_cfg.barWidth + kGap;
    // thin LEDs get a thinner gap so that 124 of them still fit and look like a ladder
    const double gap = ledH >= 4.0 ? 1.0 : (ledH >= 2.5 ? 0.6 : 0.0);
    const double radius = std::min(1.5, std::max(0.0, (ledH - gap) / 2.0));
    for (int i = 0; i < n; ++i) {
        const int x0 = plot.left() + i * slot;
        const int holdLed = m_hold[i] > m_cfg.minDb + 0.5
            ? std::clamp(int((m_hold[i] - bot) / m_cfg.ledStepDb), 0, nLeds - 1) : -1;

        for (int j = 0; j < nLeds; ++j) {
            const double lo = bot + j * m_cfg.ledStepDb;
            const QColor c = ledColor(lo);
            const bool lit = m_disp[i] > std::max(lo, m_cfg.minDb) + 1e-6;
            const double yBottom = plot.bottom() + 1 - j * ledH;
            const QRectF r(x0, yBottom - ledH + gap, m_cfg.barWidth, std::max(1.0, ledH - gap));

            if (lit)               p.setBrush(c);
            else if (j == holdLed) p.setBrush(c.lighter(115));   // peak-hold LED
            else                   p.setBrush(c.darker(500));    // unlit LED
            p.drawRoundedRect(r, radius, radius);
        }

        const QString label = n == 1 ? QStringLiteral("M")
                            : n == 2 ? (i == 0 ? QStringLiteral("L") : QStringLiteral("R"))
                                     : QString::number(i + 1);
        p.setPen(txt);
        p.drawText(QRect(x0 - 2, plot.bottom() + 4, m_cfg.barWidth + 4, 14), Qt::AlignCenter, label);
        p.setPen(Qt::NoPen);
    }
}
