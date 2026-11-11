#include "PlaylistWidget.h"
#include "PlaylistModel.h"

#include <QAction>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QShortcut>
#include <QTreeView>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

PlaylistWidget::PlaylistWidget(QWidget *parent) : QWidget(parent)
{
    m_model = new PlaylistModel(this);
    m_proxy = new PlaylistFilterModel(this);
    m_proxy->setSourceModel(m_model);

    m_filter = new QLineEdit;
    m_filter->setClearButtonEnabled(true);
    m_filter->setPlaceholderText(tr("Filter this playlist (title, artist, album, path…)  –  Ctrl+F; several words = all must match"));
    m_count = new QLabel;

    m_list = new QTreeView;
    m_list->setModel(m_proxy);
    m_list->setRootIsDecorated(false);
    m_list->setItemsExpandable(false);
    m_list->setUniformRowHeights(true);
    m_list->setAllColumnsShowFocus(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setAlternatingRowColors(true);
    m_list->setMouseTracking(true);                        // hover tooltips with full metadata
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);

    QHeaderView *hdr = m_list->header();
    hdr->setSectionsMovable(true);
    hdr->setSectionsClickable(true);
    hdr->setStretchLastSection(false);
    hdr->setSortIndicatorShown(false);
    hdr->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setColumnWidth(PlaylistModel::ColTitle, 280);
    m_list->setColumnWidth(PlaylistModel::ColArtist, 170);
    m_list->setColumnWidth(PlaylistModel::ColAlbum, 170);
    m_list->setColumnWidth(PlaylistModel::ColYear, 52);
    m_list->setColumnWidth(PlaylistModel::ColTime, 60);
    m_list->setColumnWidth(PlaylistModel::ColType, 54);

    connect(hdr, &QHeaderView::sectionClicked, this, &PlaylistWidget::onHeaderClicked);
    connect(hdr, &QWidget::customContextMenuRequested, this, [this, hdr](const QPoint &pos) {
        QMenu menu;                                        // choose which columns are visible
        for (int c = 0; c < PlaylistModel::ColCount; ++c) {
            QAction *a = menu.addAction(m_model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString());
            a->setCheckable(true);
            a->setChecked(!hdr->isSectionHidden(c));
            a->setEnabled(c != PlaylistModel::ColTitle);
            a->setData(c);
        }
        if (QAction *r = menu.exec(hdr->mapToGlobal(pos)))
            hdr->setSectionHidden(r->data().toInt(), !r->isChecked());
    });

    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_proxy->setFilterText(t);
        updateCount();
    });
    connect(m_filter, &QLineEdit::returnPressed, this, [this] {
        if (m_proxy->rowCount() > 0) { emit playRequested(this, 0); m_list->setFocus(); }
    });
    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), m_filter);
    esc->setContext(Qt::WidgetShortcut);
    connect(esc, &QShortcut::activated, m_filter, &QLineEdit::clear);

    connect(m_list, &QAbstractItemView::activated, this,
            [this](const QModelIndex &i) { emit playRequested(this, i.row()); });
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        emit contextMenuRequested(this, m_list->viewport()->mapToGlobal(pos));
    });

    auto *del = new QAction(this);
    del->setShortcut(QKeySequence::Delete);
    del->setShortcutContext(Qt::WidgetShortcut);
    connect(del, &QAction::triggered, this, &PlaylistWidget::removeSelected);
    m_list->addAction(del);

    connect(m_proxy, &QAbstractItemModel::rowsInserted, this, &PlaylistWidget::updateCount);
    connect(m_proxy, &QAbstractItemModel::rowsRemoved,  this, &PlaylistWidget::updateCount);
    connect(m_proxy, &QAbstractItemModel::modelReset,   this, &PlaylistWidget::updateCount);

    auto *row = new QHBoxLayout;
    row->addWidget(m_filter, 1);
    row->addWidget(m_count);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 4, 0, 0);
    lay->addLayout(row);
    lay->addWidget(m_list, 1);
    updateCount();
}

// Header click: ascending -> descending -> original order. Prev/Next/shuffle follow the shown order.
void PlaylistWidget::onHeaderClicked(int column)
{
    QHeaderView *hdr = m_list->header();
    if (column != m_sortColumn) {
        m_sortColumn = column;
        m_sortOrder = Qt::AscendingOrder;
    } else if (m_sortOrder == Qt::AscendingOrder) {
        m_sortOrder = Qt::DescendingOrder;
    } else {
        m_sortColumn = -1;
    }
    if (m_sortColumn < 0) {
        hdr->setSortIndicatorShown(false);
        m_proxy->sort(-1);
    } else {
        hdr->setSortIndicator(m_sortColumn, m_sortOrder);
        hdr->setSortIndicatorShown(true);
        m_proxy->sort(m_sortColumn, m_sortOrder);
    }
}

QByteArray PlaylistWidget::saveHeader() const { return m_list->header()->saveState(); }

void PlaylistWidget::restoreHeader(const QByteArray &state)
{
    if (!state.isEmpty()) m_list->header()->restoreState(state);
}

void PlaylistWidget::focusFilter()
{
    m_filter->setFocus();
    m_filter->selectAll();
}

QList<QUrl> PlaylistWidget::selectedUrls() const
{
    QList<QUrl> urls;
    QModelIndexList sel = m_list->selectionModel()->selectedRows();
    std::sort(sel.begin(), sel.end(), [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
    for (const QModelIndex &i : sel)
        urls << m_model->urlAt(m_proxy->mapToSource(i).row());
    return urls;
}

void PlaylistWidget::removeSelected()
{
    QList<int> rows;
    for (const QModelIndex &i : m_list->selectionModel()->selectedRows())
        rows << m_proxy->mapToSource(i).row();
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows) m_model->removeRow(r);
}

void PlaylistWidget::updateCount()
{
    m_count->setText(QStringLiteral("%1 / %2").arg(m_proxy->rowCount()).arg(m_model->rowCount()));
}
