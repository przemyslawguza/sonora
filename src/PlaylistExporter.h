#pragma once
#include <QList>
#include <QString>
#include "PlaylistModel.h"

// Writes playlists in formats other players understand.
namespace PlaylistExporter {

enum Format { M3U, PLS, XSPF };

struct Options {
    Format format = M3U;
    bool relativePaths = false;     // relative to the exported file (portable) instead of absolute
    bool includeInfo = true;        // #EXTINF / Title / Length / artist / album
    bool collapseCue = true;        // a cue sheet becomes ONE entry (the .cue file) - readable by other players;
                                    // false: Sonora-only "file.cue#N" track entries
};

QString suffix(Format f);           // default file suffix without the dot ("m3u8", "pls", "xspf")
QString fileFilter(Format f);       // filter string for QFileDialog

bool write(const QString &file, const QList<PlaylistModel::ExportItem> &items,
           const Options &opt, QString *error = nullptr);
}
