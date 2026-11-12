#include "PlaylistExporter.h"
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>
#include <QUrl>
#include <QXmlStreamWriter>
#include <cmath>

namespace PlaylistExporter {

namespace {

using Item = PlaylistModel::ExportItem;

// With collapseCue, consecutive tracks of the same cue sheet become one entry that points to the
// .cue file (other players expand it themselves).
QList<Item> prepare(const QList<Item> &in, bool collapseCue)
{
    QList<Item> out;
    QString lastCue;
    for (const Item &it : in) {
        if (it.isCue && collapseCue) {
            if (it.path == lastCue) continue;
            lastCue = it.path;
            Item e = it;
            e.isCue = false;
            e.url = QUrl::fromLocalFile(it.path);
            e.title = !it.album.isEmpty() ? it.album : QFileInfo(it.path).completeBaseName();
            e.durationMs = -1;
            out << e;
        } else {
            lastCue.clear();
            out << it;
        }
    }
    return out;
}

QString display(const Item &it)
{
    return it.artist.isEmpty() ? it.title : it.artist + QStringLiteral(" - ") + it.title;
}

qint64 seconds(const Item &it)
{
    return it.durationMs > 0 ? qint64(std::llround(it.durationMs / 1000.0)) : -1;
}

// Location as written into M3U / PLS.
QString textLocation(const Item &it, bool relative, const QDir &base)
{
    if (it.isCue) return it.url.toString(QUrl::FullyEncoded);        // Sonora-only: file:///x.cue#N
    if (!it.isLocal) return it.path;                                  // http:// stream etc.
    return relative ? base.relativeFilePath(it.path) : it.path;
}

// Location as written into XSPF (a URI).
QString uriLocation(const Item &it, bool relative, const QDir &base)
{
    if (it.isCue) return it.url.toString(QUrl::FullyEncoded);
    if (!it.isLocal) return it.path;
    if (relative) return QString::fromLatin1(QUrl::toPercentEncoding(base.relativeFilePath(it.path), "/"));
    return QUrl::fromLocalFile(it.path).toString(QUrl::FullyEncoded);
}

}  // namespace

QString suffix(Format f)
{
    switch (f) {
    case PLS:  return QStringLiteral("pls");
    case XSPF: return QStringLiteral("xspf");
    default:   return QStringLiteral("m3u8");
    }
}

QString fileFilter(Format f)
{
    switch (f) {
    case PLS:  return QObject::tr("PLS playlist (*.pls)");
    case XSPF: return QObject::tr("XSPF playlist (*.xspf)");
    default:   return QObject::tr("M3U playlist, UTF-8 (*.m3u8 *.m3u)");
    }
}

bool write(const QString &file, const QList<Item> &itemsIn, const Options &o, QString *error)
{
    auto fail = [&](const QString &m) { if (error) *error = m; return false; };

    const QList<Item> items = prepare(itemsIn, o.collapseCue);
    const QDir base = QFileInfo(file).absoluteDir();

    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return fail(QObject::tr("Cannot write %1: %2").arg(file, f.errorString()));

    switch (o.format) {
    case M3U: {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << "#EXTM3U\n";
        for (const Item &it : items) {
            if (o.includeInfo)
                out << "#EXTINF:" << seconds(it) << ',' << display(it) << '\n';
            out << textLocation(it, o.relativePaths, base) << '\n';
        }
        out.flush();
        break;
    }
    case PLS: {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << "[playlist]\n";
        int n = 0;
        for (const Item &it : items) {
            ++n;
            out << "File" << n << '=' << textLocation(it, o.relativePaths, base) << '\n';
            if (o.includeInfo) {
                out << "Title" << n << '=' << display(it) << '\n';
                out << "Length" << n << '=' << seconds(it) << '\n';
            }
        }
        out << "NumberOfEntries=" << n << "\nVersion=2\n";
        out.flush();
        break;
    }
    case XSPF: {
        QXmlStreamWriter xml(&f);
        xml.setAutoFormatting(true);
        xml.writeStartDocument();
        xml.writeStartElement(QStringLiteral("playlist"));
        xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1"));
        xml.writeDefaultNamespace(QStringLiteral("http://xspf.org/ns/0/"));
        xml.writeStartElement(QStringLiteral("trackList"));
        for (const Item &it : items) {
            xml.writeStartElement(QStringLiteral("track"));
            xml.writeTextElement(QStringLiteral("location"), uriLocation(it, o.relativePaths, base));
            if (o.includeInfo) {
                if (!it.title.isEmpty())  xml.writeTextElement(QStringLiteral("title"), it.title);
                if (!it.artist.isEmpty()) xml.writeTextElement(QStringLiteral("creator"), it.artist);
                if (!it.album.isEmpty())  xml.writeTextElement(QStringLiteral("album"), it.album);
                if (it.durationMs > 0)    xml.writeTextElement(QStringLiteral("duration"), QString::number(it.durationMs));
            }
            xml.writeEndElement();      // track
        }
        xml.writeEndElement();          // trackList
        xml.writeEndElement();          // playlist
        xml.writeEndDocument();
        if (xml.hasError()) return fail(QObject::tr("Cannot write %1").arg(file));
        break;
    }
    }

    if (!f.commit()) return fail(QObject::tr("Cannot write %1: %2").arg(file, f.errorString()));
    return true;
}

}  // namespace PlaylistExporter
