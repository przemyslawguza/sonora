#include "PlaylistModel.h"
#include "CueSheet.h"
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
    it.search = it.path + ' ' + it.cueAudio + ' ' + titleOf(it) + ' ' + it.info.artist + ' ' + it.info.album;
    if (it.info.year > 0) it.search += ' ' + QString::number(it.info.year);
}

// ---------------------------------------------------------------- CUE tracks
void PlaylistModel::mergeInfo(Item &it)
{
    TrackInfo m = it.cueInfo;                         // title, artist, album, year, genre ... from the sheet
    if (it.fileScanned) {
        const TrackInfo &f = it.fileInfo;
        m.codec = f.codec;                            // technical data come from the audio file
        m.bitrate = f.bitrate;
        m.sampleRate = f.sampleRate;
        m.channels = f.channels;
        m.depth = f.depth;
        m.fileSize = f.fileSize;
        if (m.album.isEmpty())    m.album = f.album;  // gaps in the sheet are filled from the file's tags
        if (m.artist.isEmpty())   m.artist = f.artist;
        if (m.genre.isEmpty())    m.genre = f.genre;
        if (m.composer.isEmpty()) m.composer = f.composer;
        if (m.comment.isEmpty())  m.comment = f.comment;
        if (m.year <= 0)          m.year = f.year;
    }
    if (it.endMs >= 0)
        m.durationMs = it.endMs - it.startMs;
    else if (it.fileScanned && it.fileInfo.durationMs > 0)    // last track: up to the end of the file
        m.durationMs = std::max<qint64>(0, it.fileInfo.durationMs - it.startMs);
    else
        m.durationMs = -1;
    m.valid = true;
    it.info = m;
    it.haveInfo = true;
}

bool PlaylistModel::resolveCue(Item &it)
{
    if (it.cueResolved) return !it.cueAudio.isEmpty();
    it.cueResolved = true;

    const CueSheet cs = CueSheet::load(it.path);
    const CueTrack *t = cs.track(it.cueTrack);
    if (!t || t->audioFile.isEmpty()) return false;

    it.cueAudio = t->audioFile;
    it.audioUrl = QUrl::fromLocalFile(t->audioFile);
    it.startMs = t->startMs;
    it.endMs = t->endMs;
    it.suffix = QFileInfo(t->audioFile).suffix();
    it.key = MetadataLoader::keyFor(it.audioUrl);     // all tracks of one image share one file scan

    TrackInfo c;
    c.title = t->title;
    c.artist = !t->performer.isEmpty() ? t->performer : cs.performer;
    c.albumArtist = cs.performer;
    c.album = cs.album;
    c.genre = cs.genre;
    c.composer = t->composer;
    c.comment = cs.comment;
    c.year = cs.year;
    c.track = t->number;
    it.cueInfo = c;
    mergeInfo(it);
    return true;
}

// Resolves one item: computes its metadata key (the only place that touches the disk), takes tags
// from the cache or queues a background scan.
bool PlaylistModel::resolveOne(Item &it)
{
    if (it.resolved) return false;
    it.resolved = true;
    MetadataLoader &loader = MetadataLoader::instance();
    bool changed = false;

    if (it.isCue) {
        changed = true;
        if (resolveCue(it) && !it.key.isEmpty()) {
            if (const TrackInfo *ti = loader.find(it.key)) {
                it.fileInfo = *ti;
                it.fileScanned = true;
                mergeInfo(it);
            } else {
                loader.request(it.key, it.audioUrl);
            }
        }
        rebuildSearch(it);
    } else if (it.url.isLocalFile()) {
        it.key = MetadataLoader::keyFor(it.url);
        if (!it.key.isEmpty()) {
            if (const TrackInfo *ti = loader.find(it.key)) {
                it.info = *ti;
                it.haveInfo = true;
                rebuildSearch(it);
                changed = true;
            } else {
                loader.request(it.key, it.url);
            }
        }
    }
    return changed;
}

PlaylistModel::PlayTarget PlaylistModel::playTarget(int row)
{
    PlayTarget t;
    if (row < 0 || row >= m_items.size()) return t;
    Item &it = m_items[row];
    if (!it.isCue) { t.url = it.url; return t; }
    if (resolveOne(it)) emit dataChanged(index(row, 0), index(row, ColCount - 1));
    if (!it.cueAudio.isEmpty()) {
        t.url = it.audioUrl;
        t.startMs = it.startMs;
        t.stopMs = it.endMs;
    }
    return t;
}

// ---------------------------------------------------------------- model API
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

    if (it.isCue && it.cueResolved && it.cueAudio.isEmpty()) {
        h += row(tr("Status"), tr("audio file of this cue sheet / track was not found"));
    } else if (!it.haveInfo && it.url.isLocalFile()) {
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
    if (it.isCue && !it.cueAudio.isEmpty()) {          // where the track really comes from
        h += row(tr("Cue sheet"), QFileInfo(it.path).fileName());
        h += row(tr("Audio file"), QFileInfo(it.cueAudio).fileName());
        h += row(tr("Range"), formatDuration(it.startMs) + QStringLiteral(" – ") +
                              (it.endMs >= 0 ? formatDuration(it.endMs) : tr("end of file")));
    }
    h += row(tr("Path"), it.isCue && !it.cueAudio.isEmpty() ? it.cueAudio : it.path);
    h += QStringLiteral("</table>");
    return h;
}

bool PlaylistModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || count <= 0 || row + count > m_items.size()) return false;
    beginRemoveRows(parent, row, row + count - 1);
    m_items.remove(row, count);
    endRemoveRows();
    if (m_current >= row) m_current = (m_current < row + count) ? -1 : m_current - count;
    m_resolvePos = std::min(m_resolvePos, row);
    if (m_resolvePos < m_items.size() && !m_resolveTimer.isActive()) m_resolveTimer.start(0);
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
        int track = 0;
        if (CueSheet::isCueUrl(u, &track)) {            // a track of a cue sheet: details are read later
            it.isCue = true;
            it.cueTrack = track;
            it.fileTitle = QStringLiteral("%1 #%2").arg(fi.completeBaseName()).arg(track);
        }
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
    QElapsedTimer t;
    t.start();
    int lo = INT_MAX, hi = -1;
    int r = m_resolvePos;
    for (; r < m_items.size(); ++r) {
        if (resolveOne(m_items[r])) {
            lo = std::min(lo, r);
            hi = std::max(hi, r);
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
        if (it.key.isEmpty()) continue;
        if (it.isCue) {
            if (!it.cueResolved || it.cueAudio.isEmpty() || it.fileScanned) continue;
            if (const TrackInfo *t = loader.find(it.key)) {
                it.fileInfo = *t;
                it.fileScanned = true;
                mergeInfo(it);                         // e.g. the length of the last track is now known
                rebuildSearch(it);
                lo = std::min(lo, r);
                hi = std::max(hi, r);
            }
        } else {
            if (it.haveInfo) continue;
            if (const TrackInfo *t = loader.find(it.key)) {
                it.info = *t;
                it.haveInfo = true;
                rebuildSearch(it);
                lo = std::min(lo, r);
                hi = std::max(hi, r);
            }
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

// ---------------------------------------------------------------- playlist files
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
        if (line.contains("://")) {                     // URLs, including our own file:///x.cue#N tracks
            urls << QUrl(line);
            continue;
        }
        const QString path = QDir::cleanPath(QDir::isAbsolutePath(line) ? line : base.absoluteFilePath(line));
        if (path.endsWith(".cue", Qt::CaseInsensitive)) {   // a whole cue sheet: all its tracks
            const CueSheet cs = CueSheet::load(path);
            for (const CueTrack &t : cs.tracks) urls << cs.trackUrl(t.number);
        } else {
            urls << QUrl::fromLocalFile(path);
        }
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
    for (const Item &it : m_items) {
        if (it.isCue)            out << it.url.toString(QUrl::FullyEncoded) << '\n';   // cue sheet + track number
        else if (it.url.isLocalFile()) out << it.url.toLocalFile() << '\n';
        else                     out << it.url.toString() << '\n';
    }
    out.flush();
    return f.commit();
}

// ---------------------------------------------------------------- filter
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
