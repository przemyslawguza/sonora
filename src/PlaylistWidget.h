#pragma once
#include <QWidget>
#include <QUrl>

class QTreeView;
class QLineEdit;
class QLabel;
class PlaylistModel;
class PlaylistFilterModel;

// One playlist tab: model + filter proxy + column view (title, artist, album,
// year, time, type) + its own filter box.
class PlaylistWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlaylistWidget(QWidget *parent = nullptr);

    PlaylistModel *model() const { return m_model; }
    PlaylistFilterModel *proxy() const { return m_proxy; }
    QTreeView *view() const { return m_list; }

    void focusFilter();
    QList<QUrl> selectedUrls() const;
    void removeSelected();
    QByteArray saveHeader() const;
    void restoreHeader(const QByteArray &state);

signals:
    void playRequested(PlaylistWidget *self, int proxyRow);
    void contextMenuRequested(PlaylistWidget *self, const QPoint &globalPos);

private:
    void updateCount();
    void onHeaderClicked(int column);

    PlaylistModel *m_model;
    PlaylistFilterModel *m_proxy;
    QTreeView *m_list;
    QLineEdit *m_filter;
    QLabel *m_count;
    int m_sortColumn = -1;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};
