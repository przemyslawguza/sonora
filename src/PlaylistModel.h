#pragma once
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include "TrackInfo.h"

class PlaylistModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColTitle, ColArtist, ColAlbum, ColYear, ColTime, ColType, ColCount };
    enum Roles { UrlRole = Qt::UserRole + 1, SearchRole, SortRole };

    // What the player has to open: for a CUE track the audio file plus the range of the track.
    struct PlayTarget {
        QUrl url;                   // invalid = cannot be played (e.g. audio file of a cue sheet is missing)
        qint64 startMs = -1;        // < 0: whole file
        qint64 stopMs = -1;         // < 0: until the end of the file
    };

    // One entry as the exporter needs it (see PlaylistExporter).
    struct ExportItem {
        QUrl url;                   // as stored; CUE tracks: file:///dir/x.cue#N
        QString path;               // local path (CUE tracks: the .cue file) or the text of a remote URL
        bool isLocal = true;
        bool isCue = false;
        QString title, artist, album;
        qint64 durationMs = -1;
    };

    explicit PlaylistModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &idx, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    QModelIndex index(int row, int column = 0, const QModelIndex &parent = {}) const override
    { return QAbstractTableModel::index(row, column, parent); }
    bool removeRows(int row, int count, const QModelIndex &parent = {}) override;

    // Plain files, remote URLs, and CUE tracks (file:///dir/album.cue#N, see CueSheet::trackUrl()).
    void addUrls(const QList<QUrl> &urls);
    void clear();
    int current() const { return m_current; }          // source row, or -1
    void setCurrent(int row);
    QUrl urlAt(int row) const;
    PlayTarget playTarget(int row);
    QList<ExportItem> exportItems(const QList<int> &sourceRows) const;
    QString titleAt(int row) const;
    TrackInfo infoAt(int row) const;
    qint64 totalDurationMs() const;                    // sum of the durations known so far

    bool importFile(const QString &file);              // .m3u .m3u8 .pls, relative paths resolved, .cue expanded
    bool loadM3U(const QString &file) { return importFile(file); }
    bool saveM3U(const QString &file) const;

private slots:
    void applyMetadata();
    void resolveChunk();

private:
    struct Item {
        QUrl url;
        QString path, fileTitle, suffix, key, search;
        TrackInfo info;                 // what is shown (for CUE tracks: cue tags + file technical data)
        bool haveInfo = false;
        bool resolved = false;          // metadata key computed / cache consulted
        // --- CUE tracks only
        bool isCue = false;
        bool cueResolved = false;
        bool fileScanned = false;       // fileInfo valid
        int cueTrack = 0;
        QString cueAudio;               // absolute path of the audio file
        QUrl audioUrl;
        qint64 startMs = 0, endMs = -1;
        TrackInfo cueInfo;              // tags from the cue sheet
        TrackInfo fileInfo;             // scan of the audio file (duration, codec, bitrate ...)
    };
    static QString titleOf(const Item &it);
    static void rebuildSearch(Item &it);
    static void mergeInfo(Item &it);    // cueInfo + fileInfo -> info
    static bool resolveCue(Item &it);   // true if the audio file of the track was found
    static bool resolveOne(Item &it);   // true if the displayed data changed
    QString tooltipFor(const Item &it) const;

    QVector<Item> m_items;
    int m_current = -1;
    QTimer m_applyTimer, m_resolveTimer;
    int m_resolvePos = 0;
};

// Case-insensitive multi-term AND filter over path + title + artist + album + year.
class PlaylistFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit PlaylistFilterModel(QObject *parent = nullptr);
    void setFilterText(const QString &text);
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
private:
    QStringList m_terms;
};
