#pragma once
#include <QList>
#include <QObject>
#include <QSettings>
#include <QString>
#include <functional>
#include <gst/gst.h>

// User-selectable audio output ("sound service"), the counterpart of
// Audacious' output plugin settings.
struct AudioOutputConfig
{
    QString backend = "auto";      // "auto" or a GStreamer sink factory: pipewiresink, pulsesink, alsasink, ...
    QString device;                // display name of the device, empty = backend's default device
    int bufferMs = 0;              // 0 = sink default
    int latencyMs = 0;             // 0 = sink default
    bool useCustom = false;        // use customPipeline instead of the settings above
    QString customPipeline;        // e.g. "alsasink device=hw:1,0"

    void load(QSettings &s);
    void save(QSettings &s) const;

    bool operator==(const AudioOutputConfig &o) const {
        return backend == o.backend && device == o.device && bufferMs == o.bufferMs &&
               latencyMs == o.latencyMs && useCustom == o.useCustom && customPipeline == o.customPipeline;
    }
};

struct AudioBackend    { QString factory, label; };
struct AudioDeviceInfo { QString backend, name; };

namespace AudioOutput {
QString backendLabel(const QString &factory);
QString backendHint(const QString &factory);
QList<AudioBackend> availableBackends();          // only sinks that are installed
QList<AudioDeviceInfo> devices();                 // all output devices, tagged with their sink factory
QString describe(const AudioOutputConfig &cfg);   // short text for the status bar

// Returns a new (floating) sink element, or nullptr for "automatic" / on error
// (then *error is set when something went wrong).
GstElement *createSink(const AudioOutputConfig &cfg, QString *error = nullptr);

// Plays ~1 s 440 Hz tone through the configured output. done("") on success.
void playTestTone(const AudioOutputConfig &cfg, QObject *context,
                  std::function<void(const QString &)> done);
}
