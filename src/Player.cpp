#include "Player.h"
#include <cmath>

namespace {
QVector<double> readDbArray(const GstStructure *s, const char *name)
{
    QVector<double> out;
    const GValue *val = gst_structure_get_value(s, name);
    if (!val) return out;

    auto push = [&](const GValue *v) {
        const double d = g_value_get_double(v);
        out.append(std::isfinite(d) ? d : -90.0);   // silence is reported as -inf
    };

    if (GST_VALUE_HOLDS_ARRAY(val)) {
        for (guint i = 0; i < gst_value_array_get_size(val); ++i)
            push(gst_value_array_get_value(val, i));
    } else if (G_VALUE_HOLDS_BOXED(val)) {
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        auto *arr = static_cast<GValueArray *>(g_value_get_boxed(val));
        if (arr)
            for (guint i = 0; i < arr->n_values; ++i)
                push(g_value_array_get_nth(arr, i));
        G_GNUC_END_IGNORE_DEPRECATIONS
    }
    return out;
}

QVector<float> readMagnitudes(const GstStructure *s)
{
    QVector<float> out;
    const GValue *val = gst_structure_get_value(s, "magnitude");
    if (!val || !GST_VALUE_HOLDS_LIST(val)) return out;
    const guint n = gst_value_list_get_size(val);
    out.reserve(int(n));
    for (guint i = 0; i < n; ++i) {
        const GValue *v = gst_value_list_get_value(val, i);
        if (!G_VALUE_HOLDS_FLOAT(v)) return {};
        const float d = g_value_get_float(v);
        out.append(std::isfinite(d) ? d : -90.f);
    }
    return out;
}
}

Player::Player(QObject *parent) : QObject(parent)
{
    // Nothing from GStreamer is touched here: loading playbin + the filter plugins takes a while
    // and is not needed to show the window. The pipeline is created by ensurePipeline().
    connect(&m_poll, &QTimer::timeout, this, &Player::pollBus);
    m_poll.start(20);
}

bool Player::ensurePipeline()
{
    if (m_pipe) return true;
    m_pipe = gst_element_factory_make("playbin", "player");
    if (!m_pipe) return false;

    GError *err = nullptr;
    // "lv" = sample peak, "tp" = peak of the signal oversampled to 192 kHz (>= 4x for 44.1/48 kHz
    // material, as in ITU-R BS.1770 true-peak metering): it sees overs *between* the samples.
    const QByteArray full = QStringLiteral(
        "tee name=split "
        "split. ! queue ! level name=lv interval=30000000 post-messages=true ! "
        "spectrum name=spec bands=%1 threshold=-90 interval=40000000 "
        "post-messages=true message-magnitude=true "
        "split. ! queue leaky=downstream max-size-buffers=8 max-size-time=0 max-size-bytes=0 ! "
        "audioconvert ! audioresample quality=8 ! audio/x-raw,rate=192000 ! "
        "level name=tp interval=30000000 post-messages=true ! fakesink sync=false async=false")
        .arg(m_specBands).toUtf8();
    GstElement *bin = gst_parse_bin_from_description(full.constData(), TRUE, &err);
    m_haveTp = bin != nullptr;
    if (err) { g_error_free(err); err = nullptr; }
    if (!bin) {   // fall back: no oversampling branch
        const QByteArray simple = QStringLiteral(
            "level name=lv interval=30000000 post-messages=true ! "
            "spectrum name=spec bands=%1 threshold=-90 interval=40000000 "
            "post-messages=true message-magnitude=true").arg(m_specBands).toUtf8();
        bin = gst_parse_bin_from_description(simple.constData(), TRUE, &err);
        if (err) { g_error_free(err); err = nullptr; }
    }
    if (!bin) {   // spectrum plugin missing: meters only
        bin = gst_parse_bin_from_description(
            "level name=lv interval=30000000 post-messages=true", TRUE, &err);
        if (err) g_error_free(err);
    }
    if (bin) {
        m_spec = gst_bin_get_by_name(GST_BIN(bin), "spec");   // may be null
        g_object_set(m_pipe, "audio-filter", bin, nullptr);
    }
    if (GstElement *vsink = gst_element_factory_make("fakesink", nullptr))
        g_object_set(m_pipe, "video-sink", vsink, nullptr);    // audio only

    g_object_set(m_pipe, "volume", m_volume, nullptr);

    QString outErr;                                            // chosen sound service / device
    GstElement *sink = AudioOutput::createSink(m_outCfg, &outErr);
    g_object_set(m_pipe, "audio-sink", sink, nullptr);
    if (!outErr.isEmpty()) emit outputProblem(outErr);
    return true;
}

Player::~Player()
{
    if (m_pipe) {
        gst_element_set_state(m_pipe, GST_STATE_NULL);
        if (m_spec) gst_object_unref(m_spec);
        gst_object_unref(m_pipe);
    }
}

void Player::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

void Player::drainBus()
{
    GstBus *bus = gst_element_get_bus(m_pipe);
    while (GstMessage *m = gst_bus_pop(bus)) gst_message_unref(m);
    gst_object_unref(bus);
}

int Player::sampleRate()
{
    if (m_rate > 0) return m_rate;
    if (m_spec) {
        if (GstPad *pad = gst_element_get_static_pad(m_spec, "sink")) {
            if (GstCaps *caps = gst_pad_get_current_caps(pad)) {
                int r = 0;
                if (gst_caps_get_size(caps) > 0 &&
                    gst_structure_get_int(gst_caps_get_structure(caps, 0), "rate", &r))
                    m_rate = r;
                gst_caps_unref(caps);
            }
            gst_object_unref(pad);
        }
    }
    return m_rate > 0 ? m_rate : 44100;
}

void Player::setTruePeak(bool on)
{
    m_truePeak = on;
}

void Player::setSpectrumBands(int bands)
{
    m_specBands = bands;      // applied in play(), while the pipeline is in NULL state
}

void Player::setOutput(const AudioOutputConfig &cfg)
{
    m_outCfg = cfg;
    if (!m_pipe) return;                // applied when the pipeline is created (first play)
    const State prev = m_state;
    const qint64 pos = prev != Stopped ? position() : 0;
    if (prev != Stopped) gst_element_set_state(m_pipe, GST_STATE_NULL);

    QString err;
    GstElement *sink = AudioOutput::createSink(cfg, &err);    // nullptr = automatic
    g_object_set(m_pipe, "audio-sink", sink, nullptr);
    if (!err.isEmpty()) emit outputProblem(err);

    if (prev != Stopped) {                                    // resume where we were
        drainBus();
        m_pendingSeek = pos;
        gst_element_set_state(m_pipe, prev == Paused ? GST_STATE_PAUSED : GST_STATE_PLAYING);
    }
}

void Player::play(const QUrl &url)
{
    if (!ensurePipeline()) { emit errorMessage(tr("GStreamer 'playbin' element is missing")); return; }
    gst_element_set_state(m_pipe, GST_STATE_NULL);
    m_rate = 0;
    m_pendingSeek = -1;
    if (m_spec) g_object_set(m_spec, "bands", guint(m_specBands), nullptr);
    drainBus();                                               // drop stale messages

    g_object_set(m_pipe, "uri", url.toEncoded().constData(), nullptr);
    if (gst_element_set_state(m_pipe, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        emit errorMessage(tr("Cannot play %1").arg(url.toString()));
        gst_element_set_state(m_pipe, GST_STATE_NULL);
        setState(Stopped);
        return;
    }
    setState(Playing);
}

void Player::resume()
{
    if (m_pipe && m_state == Paused) {
        gst_element_set_state(m_pipe, GST_STATE_PLAYING);
        setState(Playing);
    }
}

void Player::pause()
{
    if (m_pipe && m_state == Playing) {
        gst_element_set_state(m_pipe, GST_STATE_PAUSED);
        setState(Paused);
    }
}

void Player::stop()
{
    if (m_pipe) gst_element_set_state(m_pipe, GST_STATE_NULL);
    m_pendingSeek = -1;
    setState(Stopped);
}

void Player::seek(qint64 ms)
{
    if (!m_pipe || m_state == Stopped) return;
    gst_element_seek_simple(m_pipe, GST_FORMAT_TIME,
                           GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                           ms * GST_MSECOND);
}

void Player::setVolume(double v)
{
    m_volume = v;
    if (m_pipe) g_object_set(m_pipe, "volume", v, nullptr);
}

qint64 Player::position() const
{
    gint64 pos = 0;
    if (m_pipe && gst_element_query_position(m_pipe, GST_FORMAT_TIME, &pos)) return pos / GST_MSECOND;
    return 0;
}

qint64 Player::duration() const
{
    gint64 dur = 0;
    if (m_pipe && gst_element_query_duration(m_pipe, GST_FORMAT_TIME, &dur)) return dur / GST_MSECOND;
    return 0;
}

void Player::pollBus()
{
    if (!m_pipe) return;
    GstBus *bus = gst_element_get_bus(m_pipe);
    while (GstMessage *msg = gst_bus_pop(bus)) {
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ELEMENT: {
            const GstStructure *s = gst_message_get_structure(msg);
            if (!s || m_state != Playing) break;
            if (gst_structure_has_name(s, "level")) {
                const QByteArray src = GST_MESSAGE_SRC(msg) ? QByteArray(GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)))
                                                            : QByteArray();
                const bool wantTp = m_truePeak && m_haveTp;
                if ((src == "tp") == wantTp)          // only the selected measurement drives the meter
                    emit levels(readDbArray(s, "peak"), readDbArray(s, "rms"));
            } else if (gst_structure_has_name(s, "spectrum")) {
                const QVector<float> mag = readMagnitudes(s);
                if (!mag.isEmpty()) emit spectrum(mag, sampleRate());
            }
            break;
        }
        case GST_MESSAGE_ASYNC_DONE:
            if (m_pendingSeek >= 0 && GST_MESSAGE_SRC(msg) == GST_OBJECT(m_pipe)) {
                const qint64 ms = m_pendingSeek;
                m_pendingSeek = -1;
                seek(ms);
            }
            break;
        case GST_MESSAGE_EOS:
            gst_element_set_state(m_pipe, GST_STATE_NULL);
            setState(Stopped);
            emit endOfStream();
            break;
        case GST_MESSAGE_ERROR: {
            GError *err = nullptr; gchar *dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            emit errorMessage(QString::fromUtf8(err ? err->message : "unknown error"));
            if (err) g_error_free(err);
            g_free(dbg);
            gst_element_set_state(m_pipe, GST_STATE_NULL);
            setState(Stopped);
            break;
        }
        default: break;
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
}
