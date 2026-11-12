#pragma once
#include <QList>
#include <QSet>
#include <QString>
#include <QUrl>

// One track of a CUE sheet = a part of an audio file (typically one big FLAC "image" of an album).
struct CueTrack
{
    int number = 0;
    QString title, performer, composer;
    QString audioFile;          // absolute path of the audio file ("" if it could not be found)
    qint64 startMs = 0;         // INDEX 01
    qint64 endMs = -1;          // INDEX 01 of the next track in the same file; -1 = end of the file
};

struct CueSheet
{
    QString path;               // absolute path of the .cue file
    QString album, performer, genre, comment;
    int year = 0;
    QList<CueTrack> tracks;     // only tracks whose audio file exists

    bool isValid() const { return !tracks.isEmpty(); }
    const CueTrack *track(int number) const;
    QUrl trackUrl(int number) const;            // file:///dir/album.cue#N  (identity of a track in playlists)
    QSet<QString> audioFiles() const;

    static CueSheet load(const QString &cuePath);               // parsed once per file version
    static bool isCueUrl(const QUrl &url, int *track = nullptr);
};
