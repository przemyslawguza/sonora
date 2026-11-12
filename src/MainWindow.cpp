#include "MainWindow.h"
#include "AudioOutput.h"
#include "CueSheet.h"
#include "LevelMeter.h"
#include "MetadataLoader.h"
#include "Mpris.h"
#include "PlaylistModel.h"
#include "PlaylistWidget.h"
#include "PreferencesDialog.h"
#include "SpectrumAnalyzer.h"
#include "TransportIcons.h"
#include "VizDelay.h"

#include <QAction>
#include <QApplication>
#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSet>
#include <QSaveFile>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <climits>
#include <cmath>

namespace {
const QStringList kAudioSuffixes = {
    "mp3", "flac", "ogg", "oga", "opus", "wav", "m4a", "aac", "wma", "ape", "wv",
    "mka", "mpc", "aif", "aiff", "ac3", "dts"};
const QStringList kPlaylistSuffixes = {"m3u", "m3u8", "pls"};

QPalette darkPalette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(45, 45, 48));
    p.setColor(QPalette::WindowText, QColor(220, 220, 220));
    p.setColor(QPalette::Base, QColor(30, 30, 32));
    p.setColor(QPalette::AlternateBase, QColor(40, 40, 43));
    p.setColor(QPalette::ToolTipBase, QColor(60, 60, 64));
    p.setColor(QPalette::ToolTipText, QColor(230, 230, 230));
    p.setColor(QPalette::Text, QColor(220, 220, 220));
    p.setColor(QPalette::Button, QColor(58, 58, 63));
    p.setColor(QPalette::ButtonText, QColor(228, 228, 228));
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(80, 160, 255));
    p.setColor(QPalette::Highlight, QColor(42, 130, 218));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(135, 135, 135));
    const QColor dis(120, 120, 120);
    p.setColor(QPalette::Disabled, QPalette::WindowText, dis);
    p.setColor(QPalette::Disabled, QPalette::Text, dis);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, dis);
    return p;
}
}

// ================================================================== construction
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    QSettings s;
    m_cfg.load(s);
    m_headerState = s.value("playlistHeader").toByteArray();
    MetadataLoader::instance().loadCache(dataDir() + "/metadata.cache");

    m_player = new Player(this);
    m_player->setSpectrumBands(m_cfg.spectrum.fftBands);
    m_player->setTruePeak(m_cfg.meter.truePeak);
    buildUi();
    setAcceptDrops(true);

    // player -> shared delay line -> level meters and spectrum analyser (same delay for both)
    m_viz = new VizDelay(this);
    m_viz->setDelayMs(m_cfg.vizDelayMs);
    connect(m_player, &Player::levels, m_viz, &VizDelay::pushLevels);
    connect(m_player, &Player::spectrum, m_viz, &VizDelay::pushSpectrum);
    connect(m_viz, &VizDelay::levels, m_meter, &LevelMeter::setLevels);
    connect(m_viz, &VizDelay::spectrum, m_spectrum, &SpectrumAnalyzer::setSpectrum);
    connect(m_player, &Player::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_player, &Player::outputProblem, this, [this](const QString &m) {
        statusBar()->showMessage(m, 12000);
    });
    connect(m_player, &Player::errorMessage, this, [this](const QString &m) {
        statusBar()->showMessage(m, 8000);
        // skip the unplayable item, but never spin forever on a list of broken files
        if (m_playing && ++m_errorStreak < m_playing->proxy()->rowCount())
            QMetaObject::invokeMethod(this, [this] { step(+1); }, Qt::QueuedConnection);
    });
    connect(m_player, &Player::endOfStream, this, [this] {
        QMetaObject::invokeMethod(this, [this] { step(+1); }, Qt::QueuedConnection);
    });

    // ---- desktop integration: dedicated media keys, "now playing" (MPRIS2 over D-Bus)
    m_mpris = new MprisService(m_player, this);
    connect(m_mpris, &MprisService::nextRequested,      this, [this] { step(+1); });
    connect(m_mpris, &MprisService::previousRequested,  this, [this] { step(-1); });
    connect(m_mpris, &MprisService::playPauseRequested, this, &MainWindow::togglePlayPause);
    connect(m_mpris, &MprisService::pauseRequested,     this, &MainWindow::pauseOnly);
    connect(m_mpris, &MprisService::stopRequested,      this, &MainWindow::stopPlayback);
    connect(m_mpris, &MprisService::playRequested,      this, [this] {
        if (m_player->state() == Player::Paused) m_player->resume();
        else if (m_player->state() == Player::Stopped) playClicked();
    });
    connect(m_mpris, &MprisService::seekRequested, this, [this](qint64 off) {
        m_player->seek(std::max<qint64>(0, m_player->position() + off / 1000));
    });
    connect(m_mpris, &MprisService::positionRequested, this, [this](qint64 us) { m_player->seek(us / 1000); });
    connect(m_mpris, &MprisService::volumeRequested, this, [this](double v) {
        m_vol->setValue(int(std::lround(std::clamp(v, 0.0, 1.0) * 100.0)));
    });
    connect(m_mpris, &MprisService::raiseRequested, this, [this] {
        showNormal(); raise(); activateWindow();
    });
    connect(m_mpris, &MprisService::quitRequested, this, &QWidget::close);

    auto *tick = new QTimer(this);
    connect(tick, &QTimer::timeout, this, &MainWindow::onTick);
    tick->start(200);

    restoreGeometry(s.value("geometry").toByteArray());
    restoreState(s.value("windowState").toByteArray());
    m_vol->setValue(s.value("volume", 80).toInt());
    m_repeat->setChecked(s.value("repeat", false).toBool());
    m_shuffle->setChecked(s.value("shuffle", false).toBool());
    m_spectrumAct->setChecked(s.value("spectrum", true).toBool());      // visible by default
    m_remainAct->setChecked(s.value("remaining", false).toBool());
    m_darkAct->setChecked(s.value("darkMode", false).toBool());

    m_player->setOutput(m_cfg.audio);            // only stored; applied when the first track starts
    updateOutputLabel();
    loadPlaylists();
    QTimer::singleShot(1500, this, [] { MetadataLoader::instance().startScanning(); });
}

// ================================================================== UI
void MainWindow::buildUi()
{
    setWindowTitle("Sonora");
    resize(1180, 700);

    auto act = [this](QMenu *m, const QString &text, const QKeySequence &ks, auto slot) {
        QAction *a = m->addAction(text);
        if (!ks.isEmpty()) a->setShortcut(ks);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    auto actKeys = [this](QMenu *m, const QString &text, const QList<QKeySequence> &keys, auto slot) {
        QAction *a = m->addAction(text);
        a->setShortcuts(keys);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };

    // ---- File
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *aFiles  = act(fileMenu, tr("Add &Files…"), QKeySequence::Open, &MainWindow::addFiles);
    QAction *aFolder = act(fileMenu, tr("Add F&older…"), QKeySequence("Ctrl+Shift+O"), &MainWindow::addFolder);
    QAction *aUrl    = act(fileMenu, tr("Add &URL…"), QKeySequence("Ctrl+U"), &MainWindow::addUrl);
    fileMenu->addSeparator();
    QAction *aImpNew = act(fileMenu, tr("&Import Playlist as New…"), QKeySequence("Ctrl+I"), &MainWindow::importPlaylist);
    QAction *aImpCur = act(fileMenu, tr("Import Playlist into C&urrent…"), {}, &MainWindow::importIntoCurrent);
    act(fileMenu, tr("E&xport Playlist…"), {}, &MainWindow::exportPlaylist);
    fileMenu->addSeparator();
    act(fileMenu, tr("&Preferences…"), QKeySequence("Ctrl+P"), &MainWindow::showPreferences);
    fileMenu->addSeparator();
    act(fileMenu, tr("&Quit"), QKeySequence::Quit, [] { QApplication::quit(); });

    // ---- Playlist
    auto *plMenu = menuBar()->addMenu(tr("&Playlist"));
    act(plMenu, tr("&New Playlist"), QKeySequence("Ctrl+T"), [this] { createPlaylist(); });
    act(plMenu, tr("&Rename Playlist…"), QKeySequence("F2"), [this] { renamePlaylist(m_tabs->currentIndex()); });
    act(plMenu, tr("&Close Playlist"), QKeySequence("Ctrl+W"), [this] { closePlaylist(m_tabs->currentIndex()); });
    plMenu->addSeparator();
    act(plMenu, tr("&Find / Filter"), QKeySequence::Find, [this] { current()->focusFilter(); });
    act(plMenu, tr("Re&move Selected"), {}, [this] { current()->removeSelected(); });
    act(plMenu, tr("C&lear Playlist"), {}, [this] { current()->model()->clear(); });

    // ---- Playback (Audacious keys + dedicated keyboard media keys)
    auto *pbMenu = menuBar()->addMenu(tr("Play&back"));
    actKeys(pbMenu, tr("&Play"), {QKeySequence("X"), QKeySequence(Qt::Key_MediaPlay)}, &MainWindow::playClicked);
    actKeys(pbMenu, tr("Pa&use"), {QKeySequence("C"), QKeySequence(Qt::Key_MediaPause)}, &MainWindow::pauseClicked);
    actKeys(pbMenu, tr("Play / Pause"), {QKeySequence(Qt::Key_Space), QKeySequence(Qt::Key_MediaTogglePlayPause)},
            &MainWindow::togglePlayPause);
    actKeys(pbMenu, tr("&Stop"), {QKeySequence("V"), QKeySequence(Qt::Key_MediaStop)}, &MainWindow::stopPlayback);
    actKeys(pbMenu, tr("Pre&vious"), {QKeySequence("Z"), QKeySequence(Qt::Key_MediaPrevious)}, [this] { step(-1); });
    actKeys(pbMenu, tr("&Next"), {QKeySequence("B"), QKeySequence(Qt::Key_MediaNext)}, [this] { step(+1); });
    pbMenu->addSeparator();
    act(pbMenu, tr("Seek &backward 5 s"), QKeySequence("Ctrl+Left"), [this] {
        m_player->seek(std::max<qint64>(0, m_player->position() - 5000));
    });
    act(pbMenu, tr("Seek &forward 5 s"), QKeySequence("Ctrl+Right"), [this] {
        m_player->seek(m_player->position() + 5000);
    });
    pbMenu->addSeparator();
    m_repeat = pbMenu->addAction(tr("Re&peat (within filter)"));
    m_repeat->setCheckable(true);
    m_shuffle = pbMenu->addAction(tr("Shu&ffle"));
    m_shuffle->setCheckable(true);

    // ---- View
    m_viewMenu = menuBar()->addMenu(tr("&View"));
    m_spectrumAct = m_viewMenu->addAction(tr("&Spectrum analyser"));
    m_spectrumAct->setCheckable(true);
    m_spectrumAct->setChecked(true);
    m_remainAct = m_viewMenu->addAction(tr("Show &remaining time"));
    m_remainAct->setCheckable(true);
    m_darkAct = m_viewMenu->addAction(tr("&Dark mode"));
    m_darkAct->setCheckable(true);
    connect(m_darkAct, &QAction::toggled, this, [this](bool on) { applyTheme(on); });

    act(menuBar()->addMenu(tr("&Help")), tr("&About"), {}, &MainWindow::about);

    // ---- left: LED level meter
    m_meter = new LevelMeter;
    m_meter->setConfig(m_cfg.meter);

    // ---- transport buttons, Audacious style: prev, play, pause, stop, next, add | repeat, shuffle
    auto mkBtn = [this](const QString &tip, bool checkable = false) {
        auto *b = new QToolButton;
        b->setToolTip(tip);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setAutoRaise(false);                 // keep the button frame: always clearly visible
        b->setCheckable(checkable);
        b->setFixedSize(40, 34);
        b->setIconSize(QSize(22, 22));
        b->setFocusPolicy(Qt::NoFocus);         // so Space always means play/pause
        return b;
    };
    m_btnPrev    = mkBtn(tr("Previous (within filter)"));
    m_btnPlay    = mkBtn(tr("Play"));
    m_btnPause   = mkBtn(tr("Pause / Resume"), true);
    m_btnStop    = mkBtn(tr("Stop"));
    m_btnNext    = mkBtn(tr("Next (within filter)"));
    m_btnOpen    = mkBtn(tr("Add files… (arrow: more)"));
    m_btnRepeat  = mkBtn(tr("Repeat"), true);
    m_btnShuffle = mkBtn(tr("Shuffle"), true);
    connect(m_btnPrev,  &QToolButton::clicked, this, [this] { step(-1); });
    connect(m_btnPlay,  &QToolButton::clicked, this, &MainWindow::playClicked);
    connect(m_btnPause, &QToolButton::clicked, this, &MainWindow::pauseClicked);
    connect(m_btnStop,  &QToolButton::clicked, this, &MainWindow::stopPlayback);
    connect(m_btnNext,  &QToolButton::clicked, this, [this] { step(+1); });
    connect(m_btnOpen,  &QToolButton::clicked, this, &MainWindow::addFiles);
    auto *addMenu = new QMenu(this);
    addMenu->addAction(aFiles);
    addMenu->addAction(aFolder);
    addMenu->addAction(aUrl);
    addMenu->addSeparator();
    addMenu->addAction(aImpNew);
    addMenu->addAction(aImpCur);
    m_btnOpen->setMenu(addMenu);
    m_btnOpen->setPopupMode(QToolButton::MenuButtonPopup);
    m_btnOpen->setFixedWidth(56);
    connect(m_btnRepeat,  &QToolButton::toggled, m_repeat,  &QAction::setChecked);
    connect(m_repeat,     &QAction::toggled, m_btnRepeat,  &QToolButton::setChecked);
    connect(m_btnShuffle, &QToolButton::toggled, m_shuffle, &QAction::setChecked);
    connect(m_shuffle,    &QAction::toggled, m_btnShuffle, &QToolButton::setChecked);

    m_vol = new QSlider(Qt::Horizontal);
    m_vol->setRange(0, 100);
    m_vol->setFixedWidth(120);
    m_vol->setToolTip(tr("Volume"));
    connect(m_vol, &QSlider::valueChanged, this, [this](int v) {
        m_player->setVolume(std::pow(v / 100.0, 3.0));
        if (m_mpris) m_mpris->setVolume(v / 100.0);
    });

    auto *transport = new QHBoxLayout;
    for (QToolButton *b : {m_btnPrev, m_btnPlay, m_btnPause, m_btnStop, m_btnNext, m_btnOpen})
        transport->addWidget(b);
    transport->addSpacing(12);
    transport->addWidget(m_btnRepeat);
    transport->addWidget(m_btnShuffle);
    transport->addStretch(1);
    transport->addWidget(new QLabel(tr("Vol")));
    transport->addWidget(m_vol);

    // ---- seek bar + elapsed / remaining time (click the time to switch)
    m_seek = new QSlider(Qt::Horizontal);
    m_seek->setRange(0, 0);
    m_timeBtn = new QToolButton;
    m_timeBtn->setAutoRaise(true);
    m_timeBtn->setFocusPolicy(Qt::NoFocus);
    m_timeBtn->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_timeBtn->setToolTip(tr("Click to switch between elapsed and remaining time"));
    m_timeBtn->setMinimumWidth(QFontMetrics(m_timeBtn->font()).horizontalAdvance("-00:00:00 / 00:00:00") + 16);
    m_timeBtn->setText("0:00 / 0:00");
    connect(m_timeBtn, &QToolButton::clicked, this, [this] { m_remainAct->toggle(); });
    connect(m_remainAct, &QAction::toggled, this, [this](bool on) {
        m_showRemaining = on;
        if (m_player->state() == Player::Stopped) m_timeBtn->setText("0:00 / 0:00");
        else updateTimeLabel(m_player->position(), m_player->duration());
    });
    connect(m_seek, &QSlider::sliderPressed, this, [this] { m_seeking = true; });
    connect(m_seek, &QSlider::sliderMoved, this, [this](int v) {
        updateTimeLabel(v, m_player->duration());
    });
    connect(m_seek, &QSlider::sliderReleased, this, [this] {
        m_seeking = false;
        m_player->seek(m_seek->value());
    });
    auto *seekRow = new QHBoxLayout;
    seekRow->addWidget(m_seek, 1);
    seekRow->addWidget(m_timeBtn);

    // ---- spectrum analyser (shown by default)
    m_spectrum = new SpectrumAnalyzer;
    m_spectrum->setConfig(m_cfg.spectrum);
    connect(m_spectrumAct, &QAction::toggled, m_spectrum, &QWidget::setVisible);

    // ---- playlist tabs
    m_tabs = new QTabWidget;
    m_tabs->setMovable(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setDocumentMode(true);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closePlaylist);
    connect(m_tabs->tabBar(), &QTabBar::tabBarDoubleClicked, this, [this](int i) {
        if (i >= 0) renamePlaylist(i); else createPlaylist();
    });
    m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs->tabBar(), &QWidget::customContextMenuRequested, this, &MainWindow::showTabMenu);
    auto *plus = new QToolButton;
    plus->setText("+");
    plus->setAutoRaise(true);
    plus->setToolTip(tr("New playlist (Ctrl+T)"));
    connect(plus, &QToolButton::clicked, this, [this] { createPlaylist(); });
    m_tabs->setCornerWidget(plus, Qt::TopRightCorner);

    auto *right = new QVBoxLayout;
    right->addLayout(transport);
    right->addLayout(seekRow);
    right->addWidget(m_spectrum);
    right->addWidget(m_tabs, 1);

    auto *central = new QWidget;
    auto *lay = new QHBoxLayout(central);
    lay->addWidget(m_meter);            // left side, always visible
    lay->addLayout(right, 1);
    setCentralWidget(central);

    m_outputLabel = new QLabel;
    statusBar()->addPermanentWidget(m_outputLabel);

    buildManager();
    refreshIcons();
}

// ================================================================== theme / icons
void MainWindow::applyTheme(bool dark)
{
    static bool saved = false;
    static QString style;
    static QPalette pal;
    if (!saved) {                       // remember the system look to be able to return to it
        style = QApplication::style()->name();
        pal = QApplication::palette();
        saved = true;
    }
    if (dark) {
        QApplication::setStyle(QStringLiteral("Fusion"));
        QApplication::setPalette(darkPalette());
    } else {
        if (!style.isEmpty()) QApplication::setStyle(style);
        QApplication::setPalette(pal);
    }
    if (m_btnPrev && m_tabs) refreshIcons();
}

void MainWindow::refreshIcons()
{
    using namespace TransportIcons;
    const QPalette pal = palette();
    const qreal dpr = devicePixelRatioF();
    const int sz = 22;
    m_btnPrev->setIcon(make(Prev, pal, sz, dpr));
    m_btnPlay->setIcon(make(Play, pal, sz, dpr));
    m_btnPause->setIcon(make(Pause, pal, sz, dpr));
    m_btnStop->setIcon(make(Stop, pal, sz, dpr));
    m_btnNext->setIcon(make(Next, pal, sz, dpr));
    m_btnOpen->setIcon(make(Eject, pal, sz, dpr));
    m_btnRepeat->setIcon(make(Repeat, pal, sz, dpr));
    m_btnShuffle->setIcon(make(Shuffle, pal, sz, dpr));
    updateTabIcons();
}

void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    if ((e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange)
        && m_btnPrev && m_tabs)
        refreshIcons();
}

void MainWindow::updateTabIcons()
{
    for (int i = 0; i < m_tabs->count(); ++i)
        m_tabs->setTabIcon(i, playlistAt(i) == m_playing
                              ? TransportIcons::make(TransportIcons::Play, palette(), 14, devicePixelRatioF())
                              : QIcon());
    scheduleManagerRefresh();
}

// ================================================================== preferences
void MainWindow::showPreferences()
{
    PreferencesDialog dlg(m_cfg, this);
    connect(&dlg, &PreferencesDialog::applied, this, &MainWindow::applyConfig);
    dlg.exec();
}

void MainWindow::applyConfig(const AppConfig &c)
{
    const bool audioChanged = !(c.audio == m_cfg.audio);
    m_cfg = c;
    QSettings s;
    m_cfg.save(s);
    m_meter->setConfig(c.meter);
    m_spectrum->setConfig(c.spectrum);
    m_viz->setDelayMs(c.vizDelayMs);
    m_player->setSpectrumBands(c.spectrum.fftBands);
    m_player->setTruePeak(c.meter.truePeak);
    if (audioChanged) m_player->setOutput(c.audio);       // keeps the playing position
    updateOutputLabel();
}

void MainWindow::updateOutputLabel()
{
    m_outputLabel->setText(tr("Output: %1").arg(AudioOutput::describe(m_cfg.audio)));
}

// ================================================================== playlists
PlaylistWidget *MainWindow::playlistAt(int i) const
{
    return static_cast<PlaylistWidget *>(m_tabs->widget(i));
}

PlaylistWidget *MainWindow::current() const
{
    return static_cast<PlaylistWidget *>(m_tabs->currentWidget());
}

PlaylistWidget *MainWindow::createPlaylist(const QString &name, bool activate)
{
    auto *w = new PlaylistWidget;
    w->restoreHeader(m_tabs->count() > 0 ? current()->saveHeader() : m_headerState);   // same columns everywhere
    connect(w, &PlaylistWidget::playRequested, this, &MainWindow::playFrom);
    connect(w, &PlaylistWidget::contextMenuRequested, this, &MainWindow::showListMenu);
    const int idx = m_tabs->addTab(w, name.isEmpty() ? tr("Playlist %1").arg(++m_nameCounter) : name);
    if (activate) m_tabs->setCurrentIndex(idx);
    for (QAbstractItemModel *m : {static_cast<QAbstractItemModel *>(w->model()),
                                  static_cast<QAbstractItemModel *>(w->proxy())}) {
        connect(m, &QAbstractItemModel::rowsInserted, this, [this] { scheduleManagerRefresh(); });
        connect(m, &QAbstractItemModel::rowsRemoved,  this, [this] { scheduleManagerRefresh(); });
        connect(m, &QAbstractItemModel::modelReset,   this, [this] { scheduleManagerRefresh(); });
    }
    connect(w->model(), &QAbstractItemModel::dataChanged, this, [this, w] {
        scheduleManagerRefresh();                       // lengths change while metadata arrives
        if (w == m_playing) updateNowPlaying();         // tags of the playing track arrived
    });
    scheduleManagerRefresh();
    return w;
}

void MainWindow::renamePlaylist(int index)
{
    if (index < 0 || index >= m_tabs->count()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename playlist"), tr("Name:"),
                                               QLineEdit::Normal, m_tabs->tabText(index), &ok).trimmed();
    if (ok && !name.isEmpty()) { m_tabs->setTabText(index, name); scheduleManagerRefresh(); }
}

void MainWindow::closePlaylist(int index)
{
    if (index < 0 || index >= m_tabs->count()) return;
    PlaylistWidget *w = playlistAt(index);
    const int n = w->model()->rowCount();
    if (n > 0 && QMessageBox::question(this, tr("Close playlist"),
            tr("Close playlist “%1” (%2 items)?").arg(m_tabs->tabText(index)).arg(n)) != QMessageBox::Yes)
        return;

    if (w == m_playing) { m_player->stop(); m_playing = nullptr; }

    if (m_tabs->count() == 1) {            // always keep at least one playlist
        w->model()->clear();
        m_tabs->setTabText(0, tr("Playlist 1"));
    } else {
        m_tabs->removeTab(index);
        w->deleteLater();
    }
    updateTabIcons();
}

void MainWindow::showTabMenu(const QPoint &pos)
{
    const int i = m_tabs->tabBar()->tabAt(pos);
    QMenu menu;
    QAction *newA = menu.addAction(tr("New playlist"));
    QAction *renA = i >= 0 ? menu.addAction(tr("Rename…")) : nullptr;
    QAction *clsA = i >= 0 ? menu.addAction(tr("Close")) : nullptr;
    QAction *r = menu.exec(m_tabs->tabBar()->mapToGlobal(pos));
    if (!r) return;
    if (r == newA) createPlaylist();
    else if (r == renA) renamePlaylist(i);
    else if (r == clsA) closePlaylist(i);
}

void MainWindow::showListMenu(PlaylistWidget *w, const QPoint &globalPos)
{
    QMenu menu;
    QAction *addF = menu.addAction(tr("Add files…"));
    QAction *addD = menu.addAction(tr("Add folder…"));
    menu.addSeparator();
    QAction *play = menu.addAction(tr("Play"));
    QMenu *copy = menu.addMenu(tr("Copy selected to playlist"));
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (playlistAt(i) == w) continue;
        copy->addAction(m_tabs->tabText(i))->setData(i);
    }
    QAction *newPl = copy->addAction(tr("New playlist"));
    QAction *rem = menu.addAction(tr("Remove selected"));

    QAction *r = menu.exec(globalPos);
    if (!r) return;
    if (r == addF) { addFiles(); return; }
    if (r == addD) { addFolder(); return; }
    if (r == play) {
        const QModelIndex i = w->view()->currentIndex();
        if (i.isValid()) playFrom(w, i.row());
    } else if (r == rem) {
        w->removeSelected();
    } else if (r == newPl) {
        createPlaylist(QString(), false)->model()->addUrls(w->selectedUrls());
    } else if (r->data().isValid()) {
        playlistAt(r->data().toInt())->model()->addUrls(w->selectedUrls());
    }
}

QString MainWindow::dataDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir;
}

void MainWindow::savePlaylists()
{
    QDir d(dataDir());
    const QStringList old = d.entryList({"playlist-*.m3u"}, QDir::Files);
    for (const QString &f : old) d.remove(f);

    QJsonArray arr;
    for (int i = 0; i < m_tabs->count(); ++i) {
        const QString fn = QStringLiteral("playlist-%1.m3u").arg(i);
        playlistAt(i)->model()->saveM3U(d.filePath(fn));
        arr.append(QJsonObject{{"name", m_tabs->tabText(i)}, {"file", fn}});
    }
    QJsonObject root{{"current", m_tabs->currentIndex()}, {"lists", arr}};
    QSaveFile f(d.filePath("playlists.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson());
        f.commit();
    }
}

void MainWindow::loadPlaylists()
{
    QDir d(dataDir());
    QFile f(d.filePath("playlists.json"));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        for (const QJsonValue &v : root["lists"].toArray()) {
            const QJsonObject o = v.toObject();
            PlaylistWidget *w = createPlaylist(o["name"].toString(), false);
            w->model()->loadM3U(d.filePath(o["file"].toString()));
        }
        if (m_tabs->count() > 0)
            m_tabs->setCurrentIndex(std::clamp(root["current"].toInt(), 0, m_tabs->count() - 1));
    }
    if (m_tabs->count() == 0) createPlaylist();
}

// ================================================================== adding / importing
void MainWindow::addPaths(const QStringList &paths)
{
    QCollator coll;
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);

    QList<QUrl> urls;
    auto flush = [&] { current()->model()->addUrls(urls); urls.clear(); };
    auto addCue = [&](const QString &cuePath) {        // a cue sheet = one playlist entry per track
        const CueSheet cs = CueSheet::load(cuePath);
        if (!cs.isValid()) {
            statusBar()->showMessage(tr("%1: no playable tracks (audio file not found?)")
                                         .arg(QFileInfo(cuePath).fileName()), 8000);
            return;
        }
        for (const CueTrack &t : cs.tracks) urls << cs.trackUrl(t.number);
    };

    for (const QString &p : paths) {
        const QFileInfo fi(p);
        const QString suffix = fi.suffix().toLower();
        if (fi.isFile() && suffix == "cue") {
            addCue(fi.absoluteFilePath());
        } else if (fi.isFile() && kPlaylistSuffixes.contains(suffix)) {
            flush();                                   // keep the order of mixed input
            current()->model()->importFile(fi.absoluteFilePath());
        } else if (fi.isDir()) {
            QStringList found;
            QDirIterator it(fi.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString f = it.next();
                const QString sfx = QFileInfo(f).suffix().toLower();
                if (sfx == "cue" || kAudioSuffixes.contains(sfx)) found << f;
            }
            std::sort(found.begin(), found.end(), [&](const QString &a, const QString &b) {
                return coll.compare(a, b) < 0;
            });
            QSet<QString> covered;                     // audio files that a cue sheet splits into tracks
            for (const QString &f : found)
                if (QFileInfo(f).suffix().compare("cue", Qt::CaseInsensitive) == 0)
                    covered |= CueSheet::load(f).audioFiles();
            for (const QString &f : found) {
                if (QFileInfo(f).suffix().compare("cue", Qt::CaseInsensitive) == 0)
                    addCue(f);
                else if (!covered.contains(QFileInfo(f).absoluteFilePath()))
                    urls << QUrl::fromLocalFile(f);    // not listed twice: as one big file AND as tracks
            }
        } else if (fi.exists()) {
            urls << QUrl::fromLocalFile(fi.absoluteFilePath());
        } else if (p.contains("://")) {
            urls << QUrl(p);
        }
    }
    flush();
}

void MainWindow::playFirst() { playFrom(current(), 0); }

void MainWindow::addFiles()
{
    QStringList globs;
    for (const QString &e : kAudioSuffixes + kPlaylistSuffixes + QStringList{"cue"}) globs << "*." + e;
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add files to playlist"), {},
        tr("Audio, cue sheet and playlist files (%1);;All files (*)").arg(globs.join(' ')));
    if (!files.isEmpty()) addPaths(files);
}

void MainWindow::addFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Add folder"));
    if (!dir.isEmpty()) addPaths({dir});
}

void MainWindow::addUrl()
{
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Add URL"),
                            tr("Stream or file URL (http://…, https://…, file://…):"),
                            QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || text.isEmpty()) return;
    const QUrl url = QUrl::fromUserInput(text);
    if (!url.isValid() || url.scheme().isEmpty()) {
        statusBar()->showMessage(tr("Invalid URL: %1").arg(text), 6000);
        return;
    }
    current()->model()->addUrls({url});
}

void MainWindow::importPlaylist()          // every chosen file becomes its own new playlist
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import playlist"), {},
                                tr("Playlists (*.m3u *.m3u8 *.pls);;All files (*)"));
    for (const QString &file : files) {
        PlaylistWidget *w = createPlaylist(QFileInfo(file).completeBaseName());
        if (!w->model()->importFile(file))
            statusBar()->showMessage(tr("Cannot read %1").arg(file), 6000);
    }
}

void MainWindow::importIntoCurrent()       // entries are appended to the visible playlist
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import playlist into current"), {},
                                tr("Playlists (*.m3u *.m3u8 *.pls);;All files (*)"));
    for (const QString &file : files) {
        const int before = current()->model()->rowCount();
        if (current()->model()->importFile(file))
            statusBar()->showMessage(tr("Imported %1 items from %2")
                .arg(current()->model()->rowCount() - before).arg(QFileInfo(file).fileName()), 6000);
        else
            statusBar()->showMessage(tr("Cannot read %1").arg(file), 6000);
    }
}

void MainWindow::exportPlaylist()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("Export playlist"),
                                                      m_tabs->tabText(m_tabs->currentIndex()) + ".m3u",
                                                      tr("M3U playlist (*.m3u)"));
    if (!file.isEmpty() && !current()->model()->saveM3U(file))
        statusBar()->showMessage(tr("Cannot write %1").arg(file), 5000);
}

// ================================================================== playback
void MainWindow::playFrom(PlaylistWidget *w, int proxyRow)
{
    if (!w) return;
    const QModelIndex pidx = w->proxy()->index(proxyRow, 0);
    const QModelIndex src = w->proxy()->mapToSource(pidx);
    if (!src.isValid()) return;

    // For a CUE track this is the audio file plus the range of the track.
    const PlaylistModel::PlayTarget target = w->model()->playTarget(src.row());
    if (!target.url.isValid()) {                       // e.g. the audio file of the cue sheet is missing
        statusBar()->showMessage(tr("Cannot find the audio file for “%1”").arg(w->model()->titleAt(src.row())), 8000);
        if (w->proxy()->rowCount() > 1 && ++m_errorStreak < w->proxy()->rowCount()) {
            m_playing = w;                             // so that Next continues after this entry
            w->model()->setCurrent(src.row());
            QMetaObject::invokeMethod(this, [this] { step(+1); }, Qt::QueuedConnection);
        }
        return;
    }

    if (m_playing && m_playing != w) m_playing->model()->setCurrent(-1);
    m_playing = w;
    w->model()->setCurrent(src.row());
    m_viz->clear();                                   // nothing of the previous track may show up late
    m_player->play(target.url, target.startMs, target.stopMs);
    w->view()->scrollTo(pidx);
    updateTabIcons();
    updateNowPlaying();
}

// Navigation happens inside the *playing* playlist and only through its *filtered*
// (and possibly sorted) view. If the playing item is not part of the filter, Next
// starts at the first match, Previous at the last.
void MainWindow::step(int delta)
{
    PlaylistWidget *w = m_playing ? m_playing.data() : current();
    const int n = w->proxy()->rowCount();
    if (n == 0) { m_player->stop(); return; }

    const QModelIndex cur = w->proxy()->mapFromSource(w->model()->index(w->model()->current()));
    int row;
    if (m_shuffle->isChecked()) {                       // random item of the filtered view
        if (n == 1) {
            if (cur.isValid() && !m_repeat->isChecked()) { m_player->stop(); return; }
            row = 0;
        } else {
            row = QRandomGenerator::global()->bounded(n - 1);
            if (cur.isValid() && row >= cur.row()) ++row;   // never the same item twice in a row
        }
    } else {
        row = cur.isValid() ? cur.row() + delta : (delta > 0 ? 0 : n - 1);
    }

    if (row < 0 || row >= n) {
        if (!m_repeat->isChecked()) { m_player->stop(); return; }
        row = (row + n) % n;
    }
    playFrom(w, row);
}

void MainWindow::playClicked()
{
    switch (m_player->state()) {
    case Player::Paused:  m_player->resume(); break;
    case Player::Playing: m_player->seek(0); break;          // like Audacious: restart the track
    case Player::Stopped: {
        PlaylistWidget *w = current();       // play from the playlist you are looking at
        const QModelIndex cur = w->proxy()->mapFromSource(w->model()->index(w->model()->current()));
        const QModelIndex sel = w->view()->currentIndex();
        playFrom(w, cur.isValid() ? cur.row() : sel.isValid() ? sel.row() : 0);
        break;
    }
    }
}

void MainWindow::pauseClicked()
{
    if (m_player->state() == Player::Playing) m_player->pause();
    else if (m_player->state() == Player::Paused) m_player->resume();
    m_btnPause->setChecked(m_player->state() == Player::Paused);
}

void MainWindow::pauseOnly()
{
    if (m_player->state() == Player::Playing) m_player->pause();
    m_btnPause->setChecked(m_player->state() == Player::Paused);
}

// Space and the keyboard's Play/Pause key: pause/resume, or start playing when stopped.
void MainWindow::togglePlayPause()
{
    if (m_player->state() == Player::Stopped) playClicked();
    else pauseClicked();
}

void MainWindow::stopPlayback() { m_player->stop(); }

void MainWindow::onStateChanged(Player::State s)
{
    m_btnPause->setChecked(s == Player::Paused);
    if (m_mpris) m_mpris->setState(s);
    if (s == Player::Stopped) {
        m_viz->clear();
        m_meter->reset();
        m_spectrum->reset();
        m_seek->setRange(0, 0);
        m_timeBtn->setText("0:00 / 0:00");
    }
    updateNowPlaying();
}

void MainWindow::updateNowPlaying()
{
    if (!m_mpris) return;
    if (!m_playing || m_player->state() == Player::Stopped || m_playing->model()->current() < 0) {
        setWindowTitle("Sonora");
        m_mpris->clearTrack();
        return;
    }
    PlaylistModel *m = m_playing->model();
    const int row = m->current();
    const TrackInfo ti = m->infoAt(row);
    const QString title = m->titleAt(row);
    setWindowTitle((ti.artist.isEmpty() ? title : ti.artist + QStringLiteral(" – ") + title) + " — Sonora");
    m_mpris->setTrack(title, ti.artist.isEmpty() ? QStringList() : QStringList{ti.artist}, ti.album,
                      m->urlAt(row), ti.durationMs > 0 ? ti.durationMs * 1000 : 0, ti.track);
}

void MainWindow::updateTimeLabel(qint64 pos, qint64 dur)
{
    QString a = (m_showRemaining && dur > 0)
        ? QStringLiteral("-") + formatDuration(std::max<qint64>(0, dur - pos))
        : formatDuration(pos);
    m_timeBtn->setText(dur > 0 ? a + QStringLiteral(" / ") + formatDuration(dur) : a);
}

void MainWindow::onTick()
{
    if (m_player->state() == Player::Stopped || m_seeking) return;
    const qint64 dur = m_player->duration();
    const qint64 pos = m_player->position();
    if (m_player->state() == Player::Playing && pos > 500) m_errorStreak = 0;
    m_seek->setRange(0, int(std::min<qint64>(dur, INT_MAX)));
    m_seek->setValue(int(std::min<qint64>(pos, INT_MAX)));
    updateTimeLabel(pos, dur);
}

// ================================================================== playlist manager
// Audacious-like overview of ALL playlists: name, entry count, total length.
void MainWindow::buildManager()
{
    m_mgr = new QTreeWidget;
    m_mgr->setColumnCount(3);
    m_mgr->setHeaderLabels({tr("Playlist"), tr("Entries"), tr("Length")});
    m_mgr->setRootIsDecorated(false);
    m_mgr->setUniformRowHeights(true);
    m_mgr->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mgr->setContextMenuPolicy(Qt::CustomContextMenu);
    m_mgr->header()->setStretchLastSection(false);
    m_mgr->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_mgr->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_mgr->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_mgr->setToolTip(tr("Click: show playlist.  Double-click: play it."));

    auto *bNew    = new QPushButton(tr("New"));
    auto *bImport = new QPushButton(tr("Import…"));
    auto *bClose  = new QPushButton(tr("Close"));
    for (QPushButton *b : {bNew, bImport, bClose}) b->setFocusPolicy(Qt::NoFocus);
    bNew->setToolTip(tr("New playlist (Ctrl+T)"));
    bImport->setToolTip(tr("Import playlist files as new playlists (Ctrl+I)"));
    bClose->setToolTip(tr("Close selected playlist"));
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(bNew);
    buttons->addWidget(bImport);
    buttons->addWidget(bClose);

    m_mgrInfo = new QLabel;
    auto *panel = new QWidget;
    panel->setMinimumWidth(240);
    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->addWidget(m_mgr, 1);
    lay->addWidget(m_mgrInfo);
    lay->addLayout(buttons);

    m_dock = new QDockWidget(tr("Playlists"), this);
    m_dock->setObjectName("playlistManager");
    m_dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, m_dock);            // visible by default
    QAction *toggle = m_dock->toggleViewAction();
    toggle->setText(tr("&Playlist manager"));
    toggle->setShortcut(QKeySequence("Ctrl+Y"));
    m_viewMenu->addAction(toggle);

    m_mgrTimer = new QTimer(this);
    m_mgrTimer->setSingleShot(true);
    m_mgrTimer->setInterval(80);
    connect(m_mgrTimer, &QTimer::timeout, this, &MainWindow::refreshManager);

    connect(m_mgr, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *cur) {
        if (!cur || m_mgrBusy) return;
        const int i = cur->data(0, Qt::UserRole).toInt();
        if (i != m_tabs->currentIndex()) m_tabs->setCurrentIndex(i);
    });
    connect(m_mgr, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        playPlaylistAt(it->data(0, Qt::UserRole).toInt());
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) {
        for (int r = 0; r < m_mgr->topLevelItemCount(); ++r) {
            QTreeWidgetItem *it = m_mgr->topLevelItem(r);
            if (it->data(0, Qt::UserRole).toInt() == i) {
                m_mgrBusy = true; m_mgr->setCurrentItem(it); m_mgrBusy = false;
                break;
            }
        }
    });
    connect(m_tabs->tabBar(), &QTabBar::tabMoved, this, [this] { scheduleManagerRefresh(); });

    connect(bNew, &QPushButton::clicked, this, [this] { createPlaylist(); });
    connect(bImport, &QPushButton::clicked, this, &MainWindow::importPlaylist);
    connect(bClose, &QPushButton::clicked, this, [this] { closePlaylist(m_tabs->currentIndex()); });

    connect(m_mgr, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *it = m_mgr->itemAt(pos);
        const int i = it ? it->data(0, Qt::UserRole).toInt() : -1;
        QMenu menu;
        QAction *play = i >= 0 ? menu.addAction(tr("Play")) : nullptr;
        QAction *ren  = i >= 0 ? menu.addAction(tr("Rename…")) : nullptr;
        QAction *cls  = i >= 0 ? menu.addAction(tr("Close")) : nullptr;
        if (i >= 0) menu.addSeparator();
        QAction *nw   = menu.addAction(tr("New playlist"));
        QAction *imp  = menu.addAction(tr("Import playlist…"));
        QAction *r = menu.exec(m_mgr->viewport()->mapToGlobal(pos));
        if (!r) return;
        if (r == play) playPlaylistAt(i);
        else if (r == ren) renamePlaylist(i);
        else if (r == cls) closePlaylist(i);
        else if (r == nw) createPlaylist();
        else if (r == imp) importPlaylist();
    });

    refreshManager();
}

void MainWindow::scheduleManagerRefresh()
{
    if (m_mgrTimer && !m_mgrTimer->isActive()) m_mgrTimer->start();
}

void MainWindow::refreshManager()
{
    if (!m_mgr) return;
    m_mgrBusy = true;
    m_mgr->clear();

    int totalEntries = 0;
    QTreeWidgetItem *currentItem = nullptr;
    for (int i = 0; i < m_tabs->count(); ++i) {
        PlaylistWidget *w = playlistAt(i);
        const int total = w->model()->rowCount();
        const int shown = w->proxy()->rowCount();
        const qint64 len = w->model()->totalDurationMs();
        totalEntries += total;

        auto *it = new QTreeWidgetItem(m_mgr);
        it->setText(0, m_tabs->tabText(i));
        it->setText(1, shown == total ? QString::number(total)
                                      : QStringLiteral("%1 / %2").arg(shown).arg(total));   // filter active
        it->setText(2, len > 0 ? formatDuration(len) : QString());
        it->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        it->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        it->setData(0, Qt::UserRole, i);
        if (w == m_playing) {
            it->setIcon(0, TransportIcons::make(TransportIcons::Play, palette(), 14, devicePixelRatioF()));
            QFont f = it->font(0); f.setBold(true); it->setFont(0, f);
        }
        if (i == m_tabs->currentIndex()) currentItem = it;
    }
    if (currentItem) m_mgr->setCurrentItem(currentItem);
    m_mgrInfo->setText(tr("%n playlist(s), %1 entries", nullptr, m_tabs->count()).arg(totalEntries));
    m_mgrBusy = false;
}

void MainWindow::playPlaylistAt(int index)
{
    PlaylistWidget *w = playlistAt(index);
    if (!w) return;
    m_tabs->setCurrentIndex(index);
    const QModelIndex cur = w->proxy()->mapFromSource(w->model()->index(w->model()->current()));
    playFrom(w, cur.isValid() ? cur.row() : 0);
}

// ================================================================== misc
void MainWindow::closeEvent(QCloseEvent *e)
{
    QSettings s;
    s.setValue("geometry", saveGeometry());
    s.setValue("windowState", saveState());
    s.setValue("volume", m_vol->value());
    s.setValue("repeat", m_repeat->isChecked());
    s.setValue("shuffle", m_shuffle->isChecked());
    s.setValue("spectrum", m_spectrumAct->isChecked());
    s.setValue("remaining", m_remainAct->isChecked());
    s.setValue("darkMode", m_darkAct->isChecked());
    s.setValue("playlistHeader", current()->saveHeader());
    savePlaylists();
    m_player->stop();
    MetadataLoader::instance().shutdown(dataDir() + "/metadata.cache");
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    addPaths(paths);
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About Sonora"),
        tr("<b>Sonora %1</b><br>An Audacious-style player: multiple playlists with metadata columns, "
           "LED level meters, spectrum analyser, selectable sound service, dark mode and media-key "
           "support.<br>Qt 6 + GStreamer.").arg(QApplication::applicationVersion()));
}
