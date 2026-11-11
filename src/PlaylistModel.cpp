#include "PlaylistModel.h"
#include "MetadataLoader.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <climits>

PlaylistModel::PlaylistModel(QObject *parent) : QAbstractTableModel(parent)
{
    m_applyTimer.setSingleShot(true);
    m_applyTimer.setInterval(250);                    // batch metadata updates
    connect(&m_applyTimer, &QTimer::timeout, this, &PlaylistModel::applyMetadata);
    m_resolveTimer.setSingleShot(true);
    connect(&m_resolveTimer, &QTimer::timeout, this, &PlaylistModel::resolveChunk);
    connect(&MetadataLoader::instance(), &MetadataLoader::updated, this, [this] {
        if (!m_applyTimer.isActive()) m_applyTimer.start();
    });
}

int PlaylistModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

int PlaylistModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(ColCount);
}

QString PlaylistModel::titleOf(const Item &it)
{
    return (it.haveInfo && !it.info.title.isEmpty()) ? it.info.title : it.fileTitle;
}

void PlaylistModel::rebuildSearch(Item &it)
{
    it.search = it.path + ' ' + titleOf(it) + ' ' + it.info.artist + ' ' + it.info.album;
    if (it.info.year > 0) it.search += ' ' + QString::number(it.info.year);
}

QVariant PlaylistModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColTitle:  return tr("Title");
    case ColArtist: return tr("Artist");
    case ColAlbum:  return tr("Album");
    case ColYear:   return tr("Year");
    case ColTime:   return tr("Time");
    case ColType:   return tr("Type");
    }
    return {};
}

QVariant PlaylistModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= m_items.size()) return {};
    const Item &it = m_items.at(idx.row());
    const int col = idx.column();

    switch (role) {
    case Qt::DisplayRole:
        switch (col) {
        case ColTitle:  return titleOf(it);
        case ColArtist: return it.info.artist;
        case ColAlbum:  return it.info.album;
        case ColYear:   return it.info.year > 0 ? QString::number(it.info.year) : QString();
        case ColTime:   return it.info.durationMs > 0 ? formatDuration(it.info.durationMs) : QString();
        case ColType:   return it.suffix.toUpper();
        }
        break;
    case Qt::TextAlignmentRole:
        if (col == ColYear || col == ColTime) return int(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case Qt::FontRole:
        if (idx.row() == m_current) { QFont f; f.setBold(true); return f; }
        break;
    case Qt::ToolTipRole: return tooltipFor(it);
    case UrlRole:         return it.url;
    case SearchRole:      return it.search;
    case SortRole:
        switch (col) {
        case ColTitle:  return titleOf(it).toLower();
        case ColArtist: return it.info.artist.toLower();
        case ColAlbum:  return it.info.album.toLower();
        case ColYear:   return it.info.year;
        case ColTime:   return it.info.durationMs;
        case ColType:   return it.suffix.toLower();
        }
        break;
    }
    return {};
}

QString PlaylistModel::tooltipFor(const Item &it) const
{
    const TrackInfo &t = it.info;
    auto row = [](const QString &k, const QString &v) {
        return v.isEmpty() ? QString()
            : QStringLiteral("<tr><td><b>%1</b>&nbsp;&nbsp;</td><td>%2</td></tr>")
                  .arg(k.toHtmlEscaped(), v.toHtmlEscaped());
    };

    QString h = QStringLiteral("<table cellspacing='2'>");
    h += QStringLiteral("<tr><td colspan='2'><big><b>%1</b></big></td></tr>").arg(titleOf(it).toHtmlEscaped());

    if (!it.haveInfo && it.url.isLocalFile()) {
        h += row(tr("Status"), tr("reading metadata…"));
    } else {
        h += row(tr("Artist"), t.artist);
        if (!t.albumArtist.isEmpty() && t.albumArtist != t.artist) h += row(tr("Album artist"), t.albumArtist);
        h += row(tr("Album"), t.album);
        if (t.year > 0) h += row(tr("Year"), QString::number(t.year));
        QString trk;
        if (t.track > 0) trk = QString::number(t.track);
        if (t.disc > 0) trk += (trk.isEmpty() ? QString() : QStringLiteral("   ")) + tr("Disc %1").arg(t.disc);
        h += row(tr("Track"), trk);
        h += row(tr("Genre"), t.genre);
        h += row(tr("Composer"), t.composer);
        if (t.durationMs > 0) h += row(tr("Length"), formatDuration(t.durationMs));

        QString fmt = it.suffix.toUpper();
        if (!t.codec.isEmpty()) fmt += fmt.isEmpty() ? t.codec : QStringLiteral(" – ") + t.codec;
        h += row(tr("Format"), fmt);
        if (t.bitrate > 0) h += row(tr("Bitrate"), tr("%1 kbps").arg(t.bitrate / 1000));
        if (t.sampleRate > 0) h += row(tr("Sample rate"), tr("%1 kHz").arg(t.sampleRate / 1000.0, 0, 'g', 4));
        if (t.channels > 0) {
            QString ch = t.channels == 1 ? tr("Mono") : t.channels == 2 ? tr("Stereo")
                                                                         : tr("%1 channels").arg(t.channels);
            if (t.depth > 0) ch += tr(", %1 bit").arg(t.depth);
            h += row(tr("Channels"), ch);
        }
        if (t.fileSize > 0) h += row(tr("File size"), QLocale().formattedDataSize(t.fileSize));
        if (!t.comment.isEmpty()) h += row(tr("Comment"), t.comment.left(200));
    }
    h += row(tr("Path"), it.path);
    h += QStringLiteral("</table>");
    return h;
}

bool PlaylistModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || count <= 0 || row + count > m_items.size()) return false;
    beginRemoveRows(parent, row, row + count - 1);
    m_items.remove(row, count);
    endRemoveRows();
    m_resolvePos = std::min(m_resolvePos, row);
    if (m_resolvePos < m_items.size() && !m_resolveTimer.isActive()) m_resolveTimer.start(0);
    if (m_current >= row) m_current = (m_current < row + count) ? -1 : m_current - count;
    return true;
}

void PlaylistModel::addUrls(const QList<QUrl> &urls)
{
    if (urls.isEmpty()) return;
    const int first = int(m_items.size());
    beginInsertRows({}, first, first + int(urls.size()) - 1);
    for (const QUrl &u : urls) {
        Item it;
        it.url = u;
        it.path = u.isLocalFile() ? u.toLocalFile() : u.toString();
        const QFileInfo fi(it.path);
        it.fileTitle = u.isLocalFile() ? fi.completeBaseName() : it.path;
        it.suffix = u.isLocalFile() ? fi.suffix() : QString();
        rebuildSearch(it);               // metadata key / cache lookup happens later, in resolveChunk()
        m_items.append(it);
    }
    endInsertRows();
    if (!m_resolveTimer.isActive()) m_resolveTimer.start(0);
}

// Looks up cached tags / queues background scans for the new items, a few milliseconds at a time,
// so adding or restoring thousands of files never blocks the user interface.
void PlaylistModel::resolveChunk()
{
    MetadataLoader &loader = MetadataLoader::instance();
    QElapsedTimer t;
    t.start();
    int lo = INT_MAX, hi = -1;
    int r = m_resolvePos;
    for (; r < m_items.size(); ++r) {
        Item &it = m_items[r];
        if (!it.resolved) {
            it.resolved = true;
            if (it.url.isLocalFile()) {
                it.key = MetadataLoader::keyFor(it.url);          // the only place that touches the disk
                if (!it.key.isEmpty()) {
                    if (const TrackInfo *ti = loader.find(it.key)) {
                        it.info = *ti;
                        it.haveInfo = true;
                        rebuildSearch(it);
                        lo = std::min(lo, r);
                        hi = std::max(hi, r);
                    } else {
                        loader.request(it.key, it.url);
                    }
                }
            }
        }
        if ((r & 15) == 15 && t.elapsed() > 8) { ++r; break; }
    }
    m_resolvePos = r;
    if (hi >= 0) emit dataChanged(index(lo, 0), index(hi, ColCount - 1));
    if (m_resolvePos < m_items.size()) m_resolveTimer.start(0);
}

void PlaylistModel::applyMetadata()
{
    MetadataLoader &loader = MetadataLoader::instance();
    int lo = INT_MAX, hi = -1;
    for (int r = 0; r < m_items.size(); ++r) {
        Item &it = m_items[r];
        if (it.haveInfo || it.key.isEmpty()) continue;
        if (const TrackInfo *t = loader.find(it.key)) {
            it.info = *t;
            it.haveInfo = true;
            rebuildSearch(it);
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
    }
    if (hi >= 0) emit dataChanged(index(lo, 0), index(hi, ColCount - 1));
}

void PlaylistModel::clear()
{
    beginResetModel();
    m_items.clear();
    m_current = -1;
    m_resolvePos = 0;
    endResetModel();
}

void PlaylistModel::setCurrent(int row)
{
    const int old = m_current;
    m_current = row;
    for (int r : {old, row})
        if (r >= 0 && r < m_items.size()) emit dataChanged(index(r, 0), index(r, ColCount - 1), {Qt::FontRole});
}

QUrl PlaylistModel::urlAt(int row) const
{
    return (row >= 0 && row < m_items.size()) ? m_items.at(row).url : QUrl();
}

QString PlaylistModel::titleAt(int row) const
{
    return (row >= 0 && row < m_items.size()) ? titleOf(m_items.at(row)) : QString();
}

TrackInfo PlaylistModel::infoAt(int row) const
{
    return (row >= 0 && row < m_items.size()) ? m_items.at(row).info : TrackInfo();
}

qint64 PlaylistModel::totalDurationMs() const
{
    qint64 sum = 0;
    for (const Item &it : m_items)
        if (it.info.durationMs > 0) sum += it.info.durationMs;
    return sum;
}

bool PlaylistModel::importFile(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    const QDir base = QFileInfo(file).absoluteDir();
    const bool pls = file.endsWith(".pls", Qt::CaseInsensitive);
    static const QRegularExpression plsFile("^File\\d+=(.*)$", QRegularExpression::CaseInsensitiveOption);

    QList<QUrl> urls;
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith('[')) continue;
        if (pls) {
            const auto m = plsFile.match(line);
            if (!m.hasMatch()) continue;
            line = m.captured(1).trimmed();
            if (line.isEmpty()) continue;
        }
        if (line.contains("://"))
            urls << QUrl(line);
        else if (QDir::isAbsolutePath(line))
            urls << QUrl::fromLocalFile(QDir::cleanPath(line));
        else
            urls << QUrl::fromLocalFile(QDir::cleanPath(base.absoluteFilePath(line)));
    }
    addUrls(urls);
    return true;
}

bool PlaylistModel::saveM3U(const QString &file) const
{
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&f);
    out << "#EXTM3U\n";
    for (const Item &it : m_items)
        out << (it.url.isLocalFile() ? it.url.toLocalFile() : it.url.toString()) << '\n';
    out.flush();
    return f.commit();
}

PlaylistFilterModel::PlaylistFilterModel(QObject *parent) : QSortFilterProxyModel(parent)
{
    setSortRole(PlaylistModel::SortRole);
}

void PlaylistFilterModel::setFilterText(const QString &text)
{
    m_terms = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    invalidateFilter();
}

bool PlaylistFilterModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (m_terms.isEmpty()) return true;
    const QString hay = sourceModel()->index(row, 0, parent).data(PlaylistModel::SearchRole).toString();
    for (const QString &t : m_terms)
        if (!hay.contains(t, Qt::CaseInsensitive)) return false;
    return true;
}
