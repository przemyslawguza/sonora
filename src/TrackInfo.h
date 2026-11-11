#pragma once
#include <QDataStream>
#include <QMetaType>
#include <QString>
#include <algorithm>

struct TrackInfo
{
    bool valid = false;                       // true once the file was scanned (even if nothing was found)
    QString title, artist, album, albumArtist, genre, composer, comment, codec;
    int year = 0, track = 0, disc = 0;
    int bitrate = 0;                          // bits per second
    int sampleRate = 0, channels = 0, depth = 0;
    qint64 durationMs = -1;
    qint64 fileSize = 0;
};
Q_DECLARE_METATYPE(TrackInfo)

inline QDataStream &operator<<(QDataStream &s, const TrackInfo &t)
{
    return s << t.valid << t.title << t.artist << t.album << t.albumArtist << t.genre << t.composer
             << t.comment << t.codec << qint32(t.year) << qint32(t.track) << qint32(t.disc)
             << qint32(t.bitrate) << qint32(t.sampleRate) << qint32(t.channels) << qint32(t.depth)
             << t.durationMs << t.fileSize;
}

inline QDataStream &operator>>(QDataStream &s, TrackInfo &t)
{
    qint32 y, tr, d, b, sr, ch, dp;
    s >> t.valid >> t.title >> t.artist >> t.album >> t.albumArtist >> t.genre >> t.composer
      >> t.comment >> t.codec >> y >> tr >> d >> b >> sr >> ch >> dp >> t.durationMs >> t.fileSize;
    t.year = y; t.track = tr; t.disc = d; t.bitrate = b; t.sampleRate = sr; t.channels = ch; t.depth = dp;
    return s;
}

inline QString formatDuration(qint64 ms)
{
    const qint64 s = std::max<qint64>(0, ms) / 1000;
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QChar('0'))
                                        .arg(s % 60, 2, 10, QChar('0'));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}
