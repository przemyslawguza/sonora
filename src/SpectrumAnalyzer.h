#pragma once
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include "Config.h"

// Bar spectrum analyser. Defaults: 0 ... 24 kHz, linear axis, 112 bars.
// Frequency range, scale, bar count and dB range come from SpectrumConfig.
class SpectrumAnalyzer : public QWidget
{
    Q_OBJECT
public:
    explicit SpectrumAnalyzer(QWidget *parent = nullptr);
    void setConfig(const SpectrumConfig &c);
    QSize sizeHint() const override { return QSize(480, 120); }
    QSize minimumSizeHint() const override { return QSize(160, 70); }

public slots:
    void setSpectrum(const QVector<float> &magnitudeDb, int sampleRate);
    void reset();

protected:
    void paintEvent(QPaintEvent *) override;

private slots:
    void tick();

private:
    double edge(int k) const;      // frequency (Hz) at bar boundary k

    SpectrumConfig m_cfg;
    double m_fmin = 0, m_fmax = 24000, m_fminLog = 20;
    QVector<double> m_bar, m_peak;
    QVector<qint64> m_peakAt;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_last = 0;
};
