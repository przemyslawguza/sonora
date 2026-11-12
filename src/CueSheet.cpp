#include "CueSheet.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QStringDecoder>
#include <cmath>
#include <utility>

namespace {

// Sheets made by older tools are often not UTF-8: fall back to the code page that matches the
// user's language (Central Europe / Cyrillic / Greek / Turkish / Western Europe).
const char *legacyEncoding()
{
    switch (QLocale::system().language()) {
    case QLocale::Polish: case QLocale::Czech: case QLocale::Slovak: case QLocale::Hungarian:
    case QLocale::Slovenian: case QLocale::Croatian: case QLocale::Romanian:
        return "Windows-1250";
    case QLocale::Russian: case QLocale::Ukrainian: case QLocale::Bulgarian:
    case QLocale::Serbian: case QLocale::Belarusian:
        return "Windows-1251";
    case QLocale::Greek:   return "Windows-1253";
    case QLocale::Turkish: return "Windows-1254";
    default:               return "Windows-1252";
    }
}

QString decode(QByteArray raw)
{
    if (raw.startsWith("\xEF\xBB\xBF")) raw.remove(0, 3);                 // UTF-8 BOM
    QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString text = utf8(raw);
    if (!utf8.hasError()) return text;
    QStringDecoder legacy(legacyEncoding());
    if (legacy.isValid()) return QString(legacy(raw));
    return QString::fromLatin1(raw);
}

QString unquote(const QString &s)
{
    if (s.startsWith('"')) {
        const int end = s.indexOf('"', 1);
        return end > 0 ? s.mid(1, end - 1) : s.mid(1);
    }
    return s;
}

QString fileNameOf(const QString &rest)             // FILE "name with spaces.flac" WAVE
{
    if (rest.startsWith('"')) return unquote(rest);
    const int sp = rest.lastIndexOf(' ');
    return sp > 0 ? rest.left(sp).trimmed() : rest;
}

qint64 parseTime(const QString &t)                  // mm:ss:ff, 75 frames per second
{
    const QStringList p = t.split(':');
    if (p.size() < 2) return -1;
    const int m = p[0].toInt(), s = p[1].toInt(), ff = p.size() > 2 ? p[2].toInt() : 0;
    return qint64(m) * 60000 + qint64(s) * 1000 + qint64(std::lround(ff * 1000.0 / 75.0));
}

const QStringList kAudio = {"flac", "ape", "wv", "wav", "mp3", "ogg", "opus", "m4a", "aac",
                            "tta", "wma", "mka", "mpc", "aif", "aiff"};

// Looks for fileName in d: exact name, same name in another case, or the same base name with
// another audio extension (the sheet says "album.wav" but the file on disk is album.flac).
QString findIn(const QDir &d, const QString &fileName)
{
    const QFileInfo direct(d.absoluteFilePath(fileName));
    if (direct.isFile()) return direct.absoluteFilePath();
    const QStringList entries = d.entryList(QDir::Files);
    for (const QString &e : entries)
        if (e.compare(fileName, Qt::CaseInsensitive) == 0) return d.absoluteFilePath(e);
    const QString base = QFileInfo(fileName).completeBaseName();
    for (const QString &e : entries) {
        const QFileInfo ei(e);
        if (ei.completeBaseName().compare(base, Qt::CaseInsensitive) == 0 &&
            kAudio.contains(ei.suffix(), Qt::CaseInsensitive))
            return d.absoluteFilePath(e);
    }
    return {};
}

// The name in a sheet is often not usable as written: wrong case or extension, Windows separators
// or a full Windows path ("D:\\Music\\album.flac"). Try the path relative to the sheet first, then
// just the file name next to the sheet.
QString resolveAudio(const QDir &dir, QString name)
{
    name.replace('\\', '/');
    const QFileInfo wanted(name);
    QString r = findIn(QDir(dir.absoluteFilePath(wanted.path())), wanted.fileName());
    if (r.isEmpty()) r = findIn(dir, wanted.fileName());
    return r;
}

CueSheet parseCue(const QString &absPath)
{
    CueSheet cs;
    cs.path = absPath;
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly)) return cs;
    const QString text = decode(f.readAll());
    const QDir dir = QFileInfo(absPath).absoluteDir();

    QString curFile;                                // resolved audio file for the following tracks
    bool haveCur = false, skipTrack = false;
    CueTrack cur;
    QList<CueTrack> tracks;
    auto finish = [&] {
        if (haveCur && !skipTrack && cur.startMs >= 0 && !cur.audioFile.isEmpty()) tracks.append(cur);
        haveCur = false;
    };

    const QStringList lines = text.split('\n');
    for (QString line : lines) {
        line.replace('\t', ' ');
        line = line.trimmed();
        if (line.isEmpty()) continue;
        const int sp = line.indexOf(' ');
        const QString cmd = (sp < 0 ? line : line.left(sp)).toUpper();
        const QString rest = sp < 0 ? QString() : line.mid(sp + 1).trimmed();

        if (cmd == "REM") {
            const int s2 = rest.indexOf(' ');
            const QString key = (s2 < 0 ? rest : rest.left(s2)).toUpper();
            const QString val = unquote(s2 < 0 ? QString() : rest.mid(s2 + 1).trimmed());
            if (key == "GENRE") cs.genre = val;
            else if (key == "DATE") cs.year = val.left(4).toInt();
            else if (key == "COMMENT") cs.comment = val;
        } else if (cmd == "PERFORMER") {
            (haveCur ? cur.performer : cs.performer) = unquote(rest);
        } else if (cmd == "TITLE") {
            (haveCur ? cur.title : cs.album) = unquote(rest);
        } else if (cmd == "SONGWRITER" || cmd == "COMPOSER") {
            if (haveCur) cur.composer = unquote(rest);
        } else if (cmd == "FILE") {
            finish();
            curFile = resolveAudio(dir, fileNameOf(rest));
        } else if (cmd == "TRACK") {
            finish();
            const QStringList parts = rest.split(' ', Qt::SkipEmptyParts);
            cur = CueTrack();
            cur.number = parts.value(0).toInt();
            cur.startMs = -1;
            cur.audioFile = curFile;
            skipTrack = parts.value(1).toUpper() != "AUDIO";        // data tracks are ignored
            haveCur = true;
        } else if (cmd == "INDEX" && haveCur) {
            const QStringList parts = rest.split(' ', Qt::SkipEmptyParts);
            if (parts.value(0).toInt() == 1) {                      // INDEX 01 = start of the music
                const qint64 ms = parseTime(parts.value(1));
                if (ms >= 0) cur.startMs = ms;
            }
        }
    }
    finish();

    QSet<int> seen;                                 // duplicate numbers (numbering restarted per FILE)
    bool dup = false;
    for (const CueTrack &t : tracks) {
        if (seen.contains(t.number)) dup = true;
        seen.insert(t.number);
    }
    if (dup) for (int i = 0; i < tracks.size(); ++i) tracks[i].number = i + 1;

    for (int i = 0; i < tracks.size(); ++i) {       // a track ends where the next one in the same file starts
        tracks[i].endMs = -1;
        if (i + 1 < tracks.size() && tracks[i + 1].audioFile == tracks[i].audioFile &&
            tracks[i + 1].startMs > tracks[i].startMs)
            tracks[i].endMs = tracks[i + 1].startMs;
    }
    cs.tracks = tracks;
    return cs;
}

}  // namespace

const CueTrack *CueSheet::track(int number) const
{
    for (const CueTrack &t : tracks)
        if (t.number == number) return &t;
    return nullptr;
}

QUrl CueSheet::trackUrl(int number) const
{
    QUrl u = QUrl::fromLocalFile(path);
    u.setFragment(QString::number(number));
    return u;
}

QSet<QString> CueSheet::audioFiles() const
{
    QSet<QString> s;
    for (const CueTrack &t : tracks) s.insert(t.audioFile);
    return s;
}

CueSheet CueSheet::load(const QString &cuePath)
{
    static QHash<QString, std::pair<QDateTime, CueSheet>> cache;     // main thread only
    const QFileInfo fi(cuePath);
    const QString abs = fi.absoluteFilePath();
    const QDateTime mt = fi.lastModified();
    const auto it = cache.constFind(abs);
    if (it != cache.constEnd() && it->first == mt) return it->second;
    CueSheet cs = parseCue(abs);
    cache.insert(abs, {mt, cs});
    return cs;
}

bool CueSheet::isCueUrl(const QUrl &url, int *track)
{
    if (!url.isLocalFile() || url.fragment().isEmpty()) return false;
    if (!url.toLocalFile().endsWith(".cue", Qt::CaseInsensitive)) return false;
    if (track) *track = url.fragment().toInt();
    return true;
}
