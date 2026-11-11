#pragma once
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <gst/gst.h>
#include "AudioOutput.h"

// Thin GStreamer playbin wrapper. A bin "level ! spectrum" is inserted as
// audio-filter; the audio sink ("sound service") is user selectable.
class Player : public QObject
{
    Q_OBJECT
public:
    enum State { Stopped, Playing, Paused };
    Q_ENUM(State)

    explicit Player(QObject *parent = nullptr);
    ~Player() override;

    State state() const { return m_state; }
    qint64 position() const;   // ms
    qint64 duration() const;   // ms

public slots:
    void play(const QUrl &url);
    void resume();
    void pause();
    void stop();
    void seek(qint64 ms);
    void setVolume(double linear);          // 0.0 .. 1.0
    void setOutput(const AudioOutputConfig &cfg);   // switches sink, keeps position when playing
    void setSpectrumBands(int bands);       // FFT resolution, applied from the next track
    void setTruePeak(bool on);              // meter shows inter-sample (true) peaks

signals:
    void levels(const QVector<double> &peakDb, const QVector<double> &rmsDb);
    void spectrum(const QVector<float> &magnitudeDb, int sampleRate);
    void stateChanged(Player::State s);
    void endOfStream();
    void errorMessage(const QString &msg);      // playback problem
    void outputProblem(const QString &msg);     // output selection problem (not a track error)

private slots:
    void pollBus();

private:
    void setState(State s);
    int sampleRate();
    void drainBus();
    bool ensurePipeline();                  // plugins are loaded only when first needed

    GstElement *m_pipe = nullptr;
    GstElement *m_spec = nullptr;
    int m_rate = 0;
    int m_specBands = 2048;
    bool m_truePeak = true;
    double m_volume = 0.5;
    AudioOutputConfig m_outCfg;             // applied when the pipeline is created
    bool m_haveTp = false;                  // oversampled "tp" level element exists
    qint64 m_pendingSeek = -1;
    State m_state = Stopped;
    QTimer m_poll;
};
