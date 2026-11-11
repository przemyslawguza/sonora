#include "MetadataLoader.h"
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

namespace {
constexpr quint32 kMagic = 0x534F4E31;   // "SON1"
constexpr qint32 kVersion = 1;

QString tagString(const GstTagList *tags, const char *tag)
{
    gchar *s = nullptr;
    QString r;
    if (gst_tag_list_get_string(tags, tag, &s) && s) {
        r = QString::fromUtf8(s).trimmed();
        g_free(s);
    }
    return r;
}
}

// ---------------------------------------------------------------- worker
MetadataWorker::~MetadataWorker()
{
    if (m_disc) g_object_unref(static_cast<GObject *>(m_disc));
}

void MetadataWorker::scan(const QString &key, const QUrl &url)
{
    if (stop.load()) return;
    if (!m_disc) {
        GError *e = nullptr;
        m_disc = gst_discoverer_new(5 * GST_SECOND, &e);
        if (e) g_error_free(e);
    }

    TrackInfo t;
    t.valid = true;                              // even a failed scan is remembered
    t.fileSize = QFileInfo(url.toLocalFile()).size();

    if (m_disc) {
        GError *err = nullptr;
        GstDiscovererInfo *info = gst_discoverer_discover_uri(
            static_cast<GstDiscoverer *>(m_disc), url.toEncoded().constData(), &err);
        if (err) g_error_free(err);
        if (info) {
            const GstClockTime d = gst_discoverer_info_get_duration(info);
            if (d != GST_CLOCK_TIME_NONE) t.durationMs = qint64(d / GST_MSECOND);

            GstTagList *all = gst_tag_list_new_empty();
            if (const GstTagList *g = gst_discoverer_info_get_tags(info))
                gst_tag_list_insert(all, g, GST_TAG_MERGE_KEEP);

            GList *streams = gst_discoverer_info_get_audio_streams(info);
            for (GList *l = streams; l; l = l->next) {
                auto *si = static_cast<GstDiscovererStreamInfo *>(l->data);
                if (const GstTagList *st = gst_discoverer_stream_info_get_tags(si))
                    gst_tag_list_insert(all, st, GST_TAG_MERGE_KEEP);
            }
            if (streams) {
                auto *si = static_cast<GstDiscovererStreamInfo *>(streams->data);
                auto *ai = GST_DISCOVERER_AUDIO_INFO(si);
                t.sampleRate = int(gst_discoverer_audio_info_get_sample_rate(ai));
                t.channels   = int(gst_discoverer_audio_info_get_channels(ai));
                t.depth      = int(gst_discoverer_audio_info_get_depth(ai));
                t.bitrate    = int(gst_discoverer_audio_info_get_bitrate(ai));
                if (GstCaps *caps = gst_discoverer_stream_info_get_caps(si)) {
                    if (gchar *desc = gst_pb_utils_get_codec_description(caps)) {
                        t.codec = QString::fromUtf8(desc);
                        g_free(desc);
                    }
                    gst_caps_unref(caps);
                }
            }

            t.title       = tagString(all, GST_TAG_TITLE);
            t.artist      = tagString(all, GST_TAG_ARTIST);
            t.album       = tagString(all, GST_TAG_ALBUM);
            t.albumArtist = tagString(all, GST_TAG_ALBUM_ARTIST);
            t.genre       = tagString(all, GST_TAG_GENRE);
            t.composer    = tagString(all, GST_TAG_COMPOSER);
            t.comment     = tagString(all, GST_TAG_COMMENT);
            const QString codecTag = tagString(all, GST_TAG_AUDIO_CODEC);
            if (!codecTag.isEmpty()) t.codec = codecTag;

            GstDateTime *dt = nullptr;
            if (gst_tag_list_get_date_time(all, GST_TAG_DATE_TIME, &dt) && dt) {
                t.year = gst_date_time_get_year(dt);
                gst_date_time_unref(dt);
            }
            if (t.year <= 0) {
                GDate *gd = nullptr;
                if (gst_tag_list_get_date(all, GST_TAG_DATE, &gd) && gd) {
                    if (g_date_valid(gd)) t.year = int(g_date_get_year(gd));
                    g_date_free(gd);
                }
            }
            guint u = 0;
            if (gst_tag_list_get_uint(all, GST_TAG_TRACK_NUMBER, &u)) t.track = int(u);
            if (gst_tag_list_get_uint(all, GST_TAG_ALBUM_VOLUME_NUMBER, &u)) t.disc = int(u);
            if (t.bitrate <= 0 && gst_tag_list_get_uint(all, GST_TAG_BITRATE, &u)) t.bitrate = int(u);
            if (t.bitrate <= 0 && gst_tag_list_get_uint(all, GST_TAG_NOMINAL_BITRATE, &u)) t.bitrate = int(u);

            gst_tag_list_unref(all);
            if (streams) gst_discoverer_stream_info_list_free(streams);
            g_object_unref(info);
        }
    }
    emit scanned(key, t);
}

// ---------------------------------------------------------------- loader
MetadataLoader &MetadataLoader::instance()
{
    static MetadataLoader inst;
    return inst;
}

MetadataLoader::MetadataLoader()
{
    qRegisterMetaType<TrackInfo>("TrackInfo");
    m_worker = new MetadataWorker;
    m_worker->moveToThread(&m_thread);
    connect(this, &MetadataLoader::scanRequested, m_worker, &MetadataWorker::scan);
    connect(m_worker, &MetadataWorker::scanned, this, &MetadataLoader::onScanned);
    m_thread.start(QThread::LowPriority);
}

MetadataLoader::~MetadataLoader()
{
    if (!m_down) {
        m_worker->stop = true;
        m_thread.quit();
        m_thread.wait(8000);
        delete m_worker;
    }
}

QString MetadataLoader::keyFor(const QUrl &url)
{
    if (!url.isLocalFile()) return {};
    const QFileInfo fi(url.toLocalFile());
    if (!fi.exists()) return {};
    return fi.absoluteFilePath() + '|' + QString::number(fi.lastModified().toMSecsSinceEpoch()) +
           '|' + QString::number(fi.size());
}

const TrackInfo *MetadataLoader::find(const QString &key) const
{
    const auto it = m_cache.constFind(key);
    if (it == m_cache.constEnd()) return nullptr;
    m_used.insert(key);
    return &it.value();
}

void MetadataLoader::request(const QString &key, const QUrl &url)
{
    if (m_down || key.isEmpty() || m_cache.contains(key) || m_pending.contains(key)) return;
    m_pending.insert(key);
    m_used.insert(key);
    m_queue.emplace_back(key, url);
    pump();
}

void MetadataLoader::startScanning()
{
    m_started = true;
    pump();
}

// At most a few scans are handed to the worker at a time: startup stays light, and the
// disk is not hammered by thousands of simultaneous requests.
void MetadataLoader::pump()
{
    constexpr int kMaxInflight = 4;
    while (m_started && !m_down && m_inflight < kMaxInflight && !m_queue.empty()) {
        auto job = std::move(m_queue.front());
        m_queue.pop_front();
        ++m_inflight;
        emit scanRequested(job.first, job.second);
    }
}

void MetadataLoader::onScanned(const QString &key, const TrackInfo &info)
{
    m_cache.insert(key, info);
    m_pending.remove(key);
    if (m_inflight > 0) --m_inflight;
    emit updated();
    pump();
}

void MetadataLoader::loadCache(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return;
    QDataStream in(&f);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0; qint32 ver = 0; qint32 n = 0;
    in >> magic >> ver >> n;
    if (magic != kMagic || ver != kVersion || n < 0) return;
    for (qint32 i = 0; i < n && in.status() == QDataStream::Ok; ++i) {
        QString key; TrackInfo t;
        in >> key >> t;
        if (in.status() == QDataStream::Ok) m_cache.insert(key, t);
    }
}

void MetadataLoader::saveCache(const QString &file) const
{
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return;
    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_6_0);
    qint32 n = 0;
    for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it)
        if (m_used.contains(it.key())) ++n;
    out << kMagic << kVersion << n;
    for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it)
        if (m_used.contains(it.key())) out << it.key() << it.value();
    f.commit();
}

void MetadataLoader::shutdown(const QString &cacheFile)
{
    if (m_down) return;
    m_down = true;
    m_queue.clear();
    m_worker->stop = true;
    m_thread.quit();
    m_thread.wait(8000);
    delete m_worker;
    m_worker = nullptr;
    saveCache(cacheFile);
}
