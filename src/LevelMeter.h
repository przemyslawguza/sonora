#pragma once
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include "Config.h"

// Narrow per-channel LED ladder (like a tape recorder): green / yellow / orange,
// red above 0 dB, with peak-hold LED. Range, width and LED step are configurable.
class LevelMeter : public QWidget
{
    Q_OBJECT
public:
    explicit LevelMeter(QWidget *parent = nullptr);
    void setConfig(const MeterConfig &c);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public slots:
    void setLevels(const QVector<double> &peakDb, const QVector<double> &rmsDb);
    void reset();

protected:
    void paintEvent(QPaintEvent *) override;

private slots:
    void tick();

private:
    void setChannels(int n);
    int scaleWidth() const;
    double gridTop() const;       // LED grid is aligned to 0 dB: a LED starts exactly at 0 dB and is red
    int ledCount() const;
    double gridBottom() const;

    MeterConfig m_cfg;
    QVector<double> m_disp, m_hold;
    QVector<qint64> m_holdAt;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_last = 0;
};
