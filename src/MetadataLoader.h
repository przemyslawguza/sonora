#pragma once
#include <QHash>
#include <QObject>
#include <QSet>
#include <QThread>
#include <QUrl>
#include <atomic>
#include <deque>
#include <utility>
#include "TrackInfo.h"

// Runs in its own (low priority) thread: reads tags/duration/format with GstDiscoverer.
class MetadataWorker : public QObject
{
    Q_OBJECT
public:
    ~MetadataWorker() override;
    std::atomic<bool> stop{false};
public slots:
    void scan(const QString &key, const QUrl &url);
signals:
    void scanned(const QString &key, const TrackInfo &info);
private:
    void *m_disc = nullptr;     // GstDiscoverer*
};

// Application-wide metadata cache (main-thread API) + background scanning.
class MetadataLoader : public QObject
{
    Q_OBJECT
public:
    static MetadataLoader &instance();

    static QString keyFor(const QUrl &url);                 // path|mtime|size, empty for non-local files
    const TrackInfo *find(const QString &key) const;        // nullptr if not scanned yet
    void request(const QString &key, const QUrl &url);

    void startScanning();                                   // called a moment after startup
    void loadCache(const QString &file);
    void shutdown(const QString &cacheFile);                // stop thread + save cache

signals:
    void updated();                                          // new data arrived
    void scanRequested(const QString &key, const QUrl &url);

private slots:
    void onScanned(const QString &key, const TrackInfo &info);

private:
    MetadataLoader();
    ~MetadataLoader() override;
    void saveCache(const QString &file) const;
    void pump();                                            // keeps a few scans in flight

    QThread m_thread;
    MetadataWorker *m_worker;
    QHash<QString, TrackInfo> m_cache;
    QSet<QString> m_pending;
    mutable QSet<QString> m_used;      // keys used this session (others are pruned on save)
    std::deque<std::pair<QString, QUrl>> m_queue;
    int m_inflight = 0;
    bool m_started = false;
    bool m_down = false;
};
