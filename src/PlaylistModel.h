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

    explicit PlaylistModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &idx, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    QModelIndex index(int row, int column = 0, const QModelIndex &parent = {}) const override
    { return QAbstractTableModel::index(row, column, parent); }
    bool removeRows(int row, int count, const QModelIndex &parent = {}) override;

    void addUrls(const QList<QUrl> &urls);
    void clear();
    int current() const { return m_current; }          // source row, or -1
    void setCurrent(int row);
    QUrl urlAt(int row) const;
    QString titleAt(int row) const;
    TrackInfo infoAt(int row) const;
    qint64 totalDurationMs() const;                    // sum of the durations known so far

    bool importFile(const QString &file);              // .m3u .m3u8 .pls, relative paths resolved
    bool loadM3U(const QString &file) { return importFile(file); }
    bool saveM3U(const QString &file) const;

private slots:
    void applyMetadata();
    void resolveChunk();

private:
    struct Item {
        QUrl url;
        QString path, fileTitle, suffix, key, search;
        TrackInfo info;
        bool haveInfo = false;
        bool resolved = false;      // metadata key computed / cache consulted
    };
    static QString titleOf(const Item &it);
    static void rebuildSearch(Item &it);
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
