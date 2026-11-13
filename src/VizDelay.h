#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <deque>
#include <functional>

// Synchronises the level meters AND the spectrum analyser with the sound.
//
// The analysis runs on the audio long before the sound card plays it (decoder / queue buffering:
// measured 1.3 s for the sample-peak and spectrum messages, 2.3 s for the true-peak branch). Every
// result carries the stream time of the audio it describes; it is held back until the position of
// the audio sink - i.e. what is being played - has reached that time. So the pictures follow the
// sound whatever the buffering, and a fixed delay is only needed for a remaining offset
// (Bluetooth speakers, an external amplifier): positive = pictures later, negative = earlier.
class VizDelay : public QObject
{
    Q_OBJECT
public:
    explicit VizDelay(QObject *parent = nullptr);
    void setDelayMs(int ms);                                 // fine-tuning, -500 ... 4000
    int delayMs() const { return m_delay; }
    void setPositionProvider(std::function<qint64()> p) { m_pos = std::move(p); }   // sink position, ms (-1: unknown)

public slots:
    void pushLevels(const QVector<double> &peakDb, const QVector<double> &rmsDb, qint64 streamMs);
    void pushSpectrum(const QVector<float> &magnitudeDb, int sampleRate, qint64 streamMs);
    void clear();                                            // seek, new track, stop: queued data is obsolete

signals:
    void levels(const QVector<double> &peakDb, const QVector<double> &rmsDb);
    void spectrum(const QVector<float> &magnitudeDb, int sampleRate);

private slots:
    void pump();

private:
    struct Item {
        qint64 streamMs = -1;               // stream time of the audio the data describes (-1: unknown)
        qint64 arrived = 0;                 // wall clock (m_clock) when it arrived
        QVector<double> peak, rms;
        QVector<float> mag;
        int rate = 0;
    };
    bool isDue(const Item &it, qint64 now, qint64 pos) const;
    void enqueue(std::deque<Item> &q, Item &&it);

    std::deque<Item> m_levels, m_spectra;
    std::function<qint64()> m_pos;
    QTimer m_timer;
    QElapsedTimer m_clock;
    int m_delay = 0;
};
