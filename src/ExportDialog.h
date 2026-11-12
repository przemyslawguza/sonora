#pragma once
#include <QDialog>
#include "PlaylistExporter.h"

class QCheckBox;
class QComboBox;

// Asks what to export (scope), in which format and how paths are written.
class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    enum Scope { Whole, Shown, Selected, AllPlaylists };

    ExportDialog(const QString &playlistName, int total, int shown, int selected, int playlistCount,
                 QWidget *parent = nullptr);

    PlaylistExporter::Options options() const;
    Scope scope() const;

private:
    QComboBox *m_format, *m_scope, *m_paths;
    QCheckBox *m_info, *m_cue;
};
