#include "ExportDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <algorithm>

ExportDialog::ExportDialog(const QString &name, int total, int shown, int selected, int playlists,
                           QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Export playlist"));

    m_scope = new QComboBox;
    m_scope->addItem(tr("“%1” – all %2 entries").arg(name).arg(total), int(Whole));
    m_scope->addItem(tr("“%1” – the %2 entries shown (current filter and sort order)").arg(name).arg(shown), int(Shown));
    m_scope->addItem(tr("“%1” – the %2 selected entries").arg(name).arg(selected), int(Selected));
    m_scope->addItem(tr("All %1 playlists – one file each, into a folder").arg(playlists), int(AllPlaylists));
    if (auto *model = qobject_cast<QStandardItemModel *>(m_scope->model())) {
        if (selected <= 0) model->item(int(Selected))->setEnabled(false);
        if (playlists < 2) model->item(int(AllPlaylists))->setEnabled(false);
    }

    m_format = new QComboBox;
    m_format->addItem(tr("M3U / M3U8 (UTF-8) – understood by almost every player"), int(PlaylistExporter::M3U));
    m_format->addItem(tr("PLS"), int(PlaylistExporter::PLS));
    m_format->addItem(tr("XSPF (XML)"), int(PlaylistExporter::XSPF));

    m_paths = new QComboBox;
    m_paths->addItem(tr("Absolute"), false);
    m_paths->addItem(tr("Relative to the exported file (portable)"), true);

    m_info = new QCheckBox(tr("Include track information (artist, title, length)"));
    m_cue = new QCheckBox(tr("Write a cue sheet as one entry (readable by other players)"));
    m_cue->setToolTip(tr("On: one line pointing to the .cue file.\n"
                         "Off: one \"file.cue#N\" line per track - only Sonora understands that."));

    // remembered choices
    QSettings s;
    s.beginGroup("export");
    m_format->setCurrentIndex(std::max(0, m_format->findData(s.value("format", int(PlaylistExporter::M3U)).toInt())));
    m_paths->setCurrentIndex(std::max(0, m_paths->findData(s.value("relative", false).toBool())));
    m_info->setChecked(s.value("info", true).toBool());
    m_cue->setChecked(s.value("collapseCue", true).toBool());
    s.endGroup();

    auto *form = new QFormLayout;
    form->addRow(tr("What:"), m_scope);
    form->addRow(tr("Format:"), m_format);
    form->addRow(tr("Paths:"), m_paths);
    form->addRow(QString(), m_info);
    form->addRow(QString(), m_cue);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText(tr("Export…"));
    connect(box, &QDialogButtonBox::accepted, this, [this] {
        QSettings st;
        st.beginGroup("export");
        st.setValue("format", m_format->currentData().toInt());
        st.setValue("relative", m_paths->currentData().toBool());
        st.setValue("info", m_info->isChecked());
        st.setValue("collapseCue", m_cue->isChecked());
        st.endGroup();
        accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(box);
    setMinimumWidth(520);
}

PlaylistExporter::Options ExportDialog::options() const
{
    PlaylistExporter::Options o;
    o.format = PlaylistExporter::Format(m_format->currentData().toInt());
    o.relativePaths = m_paths->currentData().toBool();
    o.includeInfo = m_info->isChecked();
    o.collapseCue = m_cue->isChecked();
    return o;
}

ExportDialog::Scope ExportDialog::scope() const
{
    return Scope(m_scope->currentData().toInt());
}
