#include "AudioOutput.h"
#include <QPointer>
#include <QTimer>

namespace {
struct Candidate { const char *factory, *label, *hint; };
const Candidate kCandidates[] = {
    {"pipewiresink", "PipeWire",   "Modern Linux sound server (default on current desktops)."},
    {"pulsesink",    "PulseAudio", "PulseAudio sound server (also served by pipewire-pulse)."},
    {"alsasink",     "ALSA",       "Talks to the kernel sound drivers directly. Choose a hw: device for bit-perfect output."},
    {"jackaudiosink","JACK",       "Low-latency pro-audio connection kit; needs a running JACK server."},
    {"oss4sink",     "OSS4",       "Open Sound System version 4."},
    {"osssink",      "OSS",        "Open Sound System (legacy)."},
    {"openalsink",   "OpenAL",     "OpenAL output."},
};

QString factoryOf(GstElement *e)
{
    GstElementFactory *f = e ? gst_element_get_factory(e) : nullptr;
    return f ? QString::fromUtf8(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(f))) : QString();
}

void dropFloating(GstElement *e)
{
    if (e) gst_object_unref(gst_object_ref_sink(e));
}

const char *providerFor(const QString &backend)
{
    if (backend == "pipewiresink")   return "pipewiredeviceprovider";
    if (backend == "pulsesink")      return "pulsedeviceprovider";
    if (backend == "alsasink")       return "alsadeviceprovider";
    if (backend == "jackaudiosink")  return "jackdeviceprovider";
    return nullptr;
}

// Calls fn(displayName, GstDevice*) for the Audio/Sink devices; stops when fn returns true.
// With a known backend only that backend's provider is asked (probing every provider, ALSA in
// particular, is slow); with an empty backend all providers are asked.
template <typename Fn>
void forEachDevice(const QString &backend, Fn fn)
{
    GList *list = nullptr;
    GstDeviceMonitor *mon = nullptr;
    GstDeviceProvider *prov = nullptr;
    if (const char *pn = providerFor(backend)) {
        prov = gst_device_provider_factory_get_by_name(pn);
        if (prov) list = gst_device_provider_get_devices(prov);
    }
    if (!prov) {
        mon = gst_device_monitor_new();
        gst_device_monitor_add_filter(mon, "Audio/Sink", nullptr);
        list = gst_device_monitor_get_devices(mon);
    }
    for (GList *i = list; i; i = i->next) {
        GstDevice *d = GST_DEVICE(i->data);
        if (!gst_device_has_classes(d, "Audio/Sink")) continue;
        gchar *dn = gst_device_get_display_name(d);
        const QString name = QString::fromUtf8(dn);
        g_free(dn);
        if (fn(name, d)) break;
    }
    g_list_free_full(list, gst_object_unref);
    if (mon) gst_object_unref(mon);
    if (prov) gst_object_unref(prov);
}
}

void AudioOutputConfig::load(QSettings &s)
{
    s.beginGroup("audio");
    backend       = s.value("backend", "auto").toString();
    device        = s.value("device").toString();
    bufferMs      = s.value("bufferMs", 0).toInt();
    latencyMs     = s.value("latencyMs", 0).toInt();
    useCustom     = s.value("useCustom", false).toBool();
    customPipeline = s.value("customPipeline").toString();
    s.endGroup();
}

void AudioOutputConfig::save(QSettings &s) const
{
    s.beginGroup("audio");
    s.setValue("backend", backend);
    s.setValue("device", device);
    s.setValue("bufferMs", bufferMs);
    s.setValue("latencyMs", latencyMs);
    s.setValue("useCustom", useCustom);
    s.setValue("customPipeline", customPipeline);
    s.endGroup();
}

namespace AudioOutput {

QString backendLabel(const QString &factory)
{
    for (const Candidate &c : kCandidates)
        if (factory == QLatin1String(c.factory)) return QString::fromUtf8(c.label);
    return factory;
}

QString backendHint(const QString &factory)
{
    if (factory == "auto" || factory.isEmpty())
        return QObject::tr("GStreamer picks an output automatically (normally PipeWire or PulseAudio, otherwise ALSA).");
    for (const Candidate &c : kCandidates)
        if (factory == QLatin1String(c.factory)) return QString::fromUtf8(c.hint);
    return {};
}

QList<AudioBackend> availableBackends()
{
    QList<AudioBackend> out;
    for (const Candidate &c : kCandidates) {
        if (GstElementFactory *f = gst_element_factory_find(c.factory)) {
            gst_object_unref(f);
            out.append({QString::fromUtf8(c.factory), QString::fromUtf8(c.label)});
        }
    }
    return out;
}

QList<AudioDeviceInfo> devices()
{
    QList<AudioDeviceInfo> out;
    forEachDevice(QString(), [&](const QString &name, GstDevice *d) {
        GstElement *e = gst_device_create_element(d, "audiosink");
        if (!e) return false;
        const AudioDeviceInfo info{factoryOf(e), name};
        dropFloating(e);
        if (!info.backend.isEmpty()) {
            bool dup = false;
            for (const auto &o : out) dup = dup || (o.backend == info.backend && o.name == info.name);
            if (!dup) out.append(info);
        }
        return false;
    });
    return out;
}

QString describe(const AudioOutputConfig &c)
{
    if (c.useCustom && !c.customPipeline.trimmed().isEmpty()) return QObject::tr("custom pipeline");
    if (c.backend.isEmpty() || c.backend == "auto") return QObject::tr("automatic");
    QString s = backendLabel(c.backend);
    if (!c.device.isEmpty()) s += QStringLiteral(" – ") + c.device;
    return s;
}

GstElement *createSink(const AudioOutputConfig &c, QString *error)
{
    auto setErr = [&](const QString &m) { if (error) *error = m; };

    if (c.useCustom && !c.customPipeline.trimmed().isEmpty()) {
        GError *ge = nullptr;
        GstElement *bin = gst_parse_bin_from_description(c.customPipeline.toUtf8().constData(), TRUE, &ge);
        if (!bin)
            setErr(QObject::tr("Invalid custom sink pipeline: %1")
                       .arg(ge ? QString::fromUtf8(ge->message) : QStringLiteral("?")));
        if (ge) g_error_free(ge);
        return bin;                          // buffer tuning is not applied to custom pipelines
    }

    if (c.backend.isEmpty() || c.backend == "auto") return nullptr;

    GstElement *sink = nullptr;
    bool deviceMissing = false;
    if (!c.device.isEmpty()) {
        forEachDevice(c.backend, [&](const QString &name, GstDevice *d) {
            if (name != c.device) return false;            // only the wanted device gets an element
            GstElement *e = gst_device_create_element(d, "audiosink");
            if (!e) return false;
            if (factoryOf(e) == c.backend) { sink = e; return true; }
            dropFloating(e);
            return false;
        });
        deviceMissing = (sink == nullptr);
    }
    if (!sink) sink = gst_element_factory_make(c.backend.toUtf8().constData(), "audiosink");
    if (!sink) {
        setErr(QObject::tr("Sound service “%1” is not installed (missing GStreamer plugin). "
                           "Using the automatic output.").arg(backendLabel(c.backend)));
        return nullptr;
    }
    if (deviceMissing)
        setErr(QObject::tr("Device “%1” was not found; using the default device of %2.")
                   .arg(c.device, backendLabel(c.backend)));

    GObjectClass *k = G_OBJECT_GET_CLASS(sink);
    if (c.bufferMs > 0 && g_object_class_find_property(k, "buffer-time"))
        g_object_set(sink, "buffer-time", (gint64)c.bufferMs * 1000, nullptr);
    if (c.latencyMs > 0 && g_object_class_find_property(k, "latency-time"))
        g_object_set(sink, "latency-time", (gint64)c.latencyMs * 1000, nullptr);
    return sink;
}

void playTestTone(const AudioOutputConfig &cfg, QObject *context,
                  std::function<void(const QString &)> done)
{
    QString err;
    GstElement *sink = createSink(cfg, &err);
    if (!sink && !err.isEmpty() && cfg.useCustom) { done(err); return; }
    if (!sink) sink = gst_element_factory_make("autoaudiosink", nullptr);

    GstElement *src  = gst_element_factory_make("audiotestsrc", nullptr);
    GstElement *conv = gst_element_factory_make("audioconvert", nullptr);
    GstElement *res  = gst_element_factory_make("audioresample", nullptr);
    GstElement *pipe = gst_pipeline_new("outputtest");
    if (!sink || !src || !conv || !res || !pipe) {
        for (GstElement *e : {sink, src, conv, res, pipe}) dropFloating(e);
        done(QObject::tr("Required GStreamer elements are missing."));
        return;
    }
    g_object_set(src, "freq", 440.0, "volume", 0.25, "num-buffers", 50, nullptr);
    gst_bin_add_many(GST_BIN(pipe), src, conv, res, sink, nullptr);
    if (!gst_element_link_many(src, conv, res, sink, nullptr)) {
        gst_object_unref(pipe);
        done(QObject::tr("Cannot connect the test tone to this output."));
        return;
    }
    if (gst_element_set_state(pipe, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        gst_element_set_state(pipe, GST_STATE_NULL);
        gst_object_unref(pipe);
        done(QObject::tr("Cannot open the audio output."));
        return;
    }

    QPointer<QObject> guard(context);
    QTimer::singleShot(1800, [pipe, guard, done] {
        QString e;
        GstBus *bus = gst_element_get_bus(pipe);
        if (GstMessage *m = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR)) {
            GError *ge = nullptr;
            gst_message_parse_error(m, &ge, nullptr);
            e = ge ? QString::fromUtf8(ge->message) : QStringLiteral("error");
            if (ge) g_error_free(ge);
            gst_message_unref(m);
        }
        gst_object_unref(bus);
        gst_element_set_state(pipe, GST_STATE_NULL);
        gst_object_unref(pipe);
        if (guard) done(e);
    });
}

}  // namespace AudioOutput
