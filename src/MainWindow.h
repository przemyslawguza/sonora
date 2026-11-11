#pragma once
#include <QMainWindow>
#include <QPointer>
#include "Config.h"
#include "Player.h"

class QTabWidget;
class QLabel;
class QSlider;
class QToolButton;
class QAction;
class QDockWidget;
class QTreeWidget;
class QMenu;
class QTimer;
class LevelMeter;
class SpectrumAnalyzer;
class PlaylistWidget;
class MprisService;
class VizDelay;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void addPaths(const QStringList &paths);   // files, folders, playlists -> current playlist
    void playFirst();

protected:
    void closeEvent(QCloseEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void changeEvent(QEvent *e) override;

private slots:
    void addFiles();
    void addFolder();
    void addUrl();
    void importPlaylist();
    void importIntoCurrent();
    void exportPlaylist();
    void showPreferences();
    void applyConfig(const AppConfig &cfg);
    void playClicked();
    void pauseClicked();
    void pauseOnly();
    void togglePlayPause();
    void stopPlayback();
    void playFrom(PlaylistWidget *w, int proxyRow);
    void step(int delta);
    void onStateChanged(Player::State s);
    void onTick();
    void renamePlaylist(int index);
    void closePlaylist(int index);
    void showListMenu(PlaylistWidget *w, const QPoint &globalPos);
    void showTabMenu(const QPoint &pos);
    void refreshManager();
    void playPlaylistAt(int index);
    void about();

private:
    void buildUi();
    void buildManager();
    void scheduleManagerRefresh();
    PlaylistWidget *createPlaylist(const QString &name = {}, bool activate = true);
    PlaylistWidget *playlistAt(int index) const;
    PlaylistWidget *current() const;
    void updateTabIcons();
    void refreshIcons();
    void updateNowPlaying();
    void updateTimeLabel(qint64 posMs, qint64 durMs);
    void updateOutputLabel();
    void applyTheme(bool dark);
    QString dataDir() const;
    void savePlaylists();
    void loadPlaylists();

    Player *m_player;
    MprisService *m_mpris = nullptr;
    VizDelay *m_viz = nullptr;              // shared delay of meters + spectrum
    LevelMeter *m_meter;
    SpectrumAnalyzer *m_spectrum;
    QTabWidget *m_tabs = nullptr;
    QPointer<PlaylistWidget> m_playing;     // playlist that owns the playing track
    QToolButton *m_timeBtn;
    QSlider *m_seek, *m_vol;
    QToolButton *m_btnPrev = nullptr, *m_btnPlay = nullptr, *m_btnPause = nullptr,
                *m_btnStop = nullptr, *m_btnNext = nullptr, *m_btnOpen = nullptr,
                *m_btnRepeat = nullptr, *m_btnShuffle = nullptr;
    QAction *m_repeat, *m_shuffle, *m_spectrumAct, *m_darkAct, *m_remainAct;
    QLabel *m_outputLabel = nullptr;
    QMenu *m_viewMenu = nullptr;

    QDockWidget *m_dock = nullptr;          // playlist manager
    QTreeWidget *m_mgr = nullptr;
    QLabel *m_mgrInfo = nullptr;
    QTimer *m_mgrTimer = nullptr;
    bool m_mgrBusy = false;

    AppConfig m_cfg;
    QByteArray m_headerState;               // playlist column layout
    bool m_seeking = false;
    bool m_showRemaining = false;
    int m_nameCounter = 0;
    int m_errorStreak = 0;
};
