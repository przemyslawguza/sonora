#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <deque>

// One shared delay line for the level meters AND the spectrum analyser.
// The analysis runs on the audio *before* the sound card plays it, so the pictures would
// lead the sound by the output latency; delaying both by the same amount re-synchronises them.
class VizDelay : public QObject
{
    Q_OBJECT
public:
    explicit VizDelay(QObject *parent = nullptr);
    void setDelayMs(int ms);                // 0 = no delay (messages are forwarded immediately)
    int delayMs() const { return m_delay; }

public slots:
    void pushLevels(const QVector<double> &peakDb, const QVector<double> &rmsDb);
    void pushSpectrum(const QVector<float> &magnitudeDb, int sampleRate);
    void clear();                           // drop everything that is still waiting (new track, stop)

signals:
    void levels(const QVector<double> &peakDb, const QVector<double> &rmsDb);
    void spectrum(const QVector<float> &magnitudeDb, int sampleRate);

private slots:
    void pump();

private:
    struct Item {
        qint64 due = 0;
        bool isSpectrum = false;
        QVector<double> peak, rms;
        QVector<float> mag;
        int rate = 0;
    };
    void emitItem(const Item &it);
    void enqueue(Item &&it);

    std::deque<Item> m_queue;
    QTimer m_timer;
    QElapsedTimer m_clock;
    int m_delay = 0;
};
