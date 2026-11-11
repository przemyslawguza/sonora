#pragma once
// MPRIS2 D-Bus interface: this is how desktop environments (GNOME, KDE, ...) deliver the
// dedicated keyboard media keys (play/pause, stop, next, previous) to a player - even when
// the window does not have the focus - and how they show "now playing" and offer controls.
#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include "Player.h"

class MprisService : public QObject
{
    Q_OBJECT
public:
    explicit MprisService(Player *player, QObject *parent = nullptr);

    bool isRegistered() const { return m_registered; }
    void setState(Player::State s);
    void setTrack(const QString &title, const QStringList &artists, const QString &album,
                  const QUrl &url, qint64 lengthUs, int trackNo);
    void clearTrack();
    void setVolume(double v);                 // 0..1

    QString playbackStatus() const;
    QVariantMap metadata() const { return m_metadata; }
    qlonglong position() const { return qlonglong(m_player->position()) * 1000; }
    double volume() const { return m_volume; }

signals:
    void nextRequested();
    void previousRequested();
    void playPauseRequested();
    void playRequested();
    void pauseRequested();
    void stopRequested();
    void raiseRequested();
    void quitRequested();
    void seekRequested(qint64 offsetUs);
    void positionRequested(qint64 us);
    void volumeRequested(double v);

private:
    void notify(const QString &iface, const QVariantMap &changed);

    Player *m_player;
    QVariantMap m_metadata;
    QString m_lastKey;
    Player::State m_state = Player::Stopped;
    double m_volume = 0.8;
    bool m_registered = false;
};

class MprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ yes)
    Q_PROPERTY(bool CanRaise READ yes)
    Q_PROPERTY(bool HasTrackList READ no)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ schemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ mimeTypes)
public:
    explicit MprisRootAdaptor(MprisService *s) : QDBusAbstractAdaptor(s) {}
    bool yes() const { return true; }
    bool no() const { return false; }
    QString identity() const { return QStringLiteral("Sonora"); }
    QString desktopEntry() const { return QStringLiteral("sonora"); }
    QStringList schemes() const { return {"file", "http", "https"}; }
    QStringList mimeTypes() const {
        return {"audio/mpeg", "audio/flac", "audio/ogg", "audio/x-wav", "audio/mp4", "audio/aac", "audio/opus"};
    }
public slots:
    void Raise() { svc()->raiseRequested(); }
    void Quit()  { svc()->quitRequested(); }
private:
    MprisService *svc() const { return static_cast<MprisService *>(parent()); }
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus)
    Q_PROPERTY(double Rate READ rate)
    Q_PROPERTY(bool Shuffle READ no)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ yes)
    Q_PROPERTY(bool CanGoPrevious READ yes)
    Q_PROPERTY(bool CanPlay READ yes)
    Q_PROPERTY(bool CanPause READ yes)
    Q_PROPERTY(bool CanSeek READ yes)
    Q_PROPERTY(bool CanControl READ yes)
public:
    explicit MprisPlayerAdaptor(MprisService *s) : QDBusAbstractAdaptor(s) {}
    bool yes() const { return true; }
    bool no() const { return false; }
    double rate() const { return 1.0; }
    QString loopStatus() const { return QStringLiteral("None"); }
    QString playbackStatus() const { return svc()->playbackStatus(); }
    QVariantMap metadata() const { return svc()->metadata(); }
    double volume() const { return svc()->volume(); }
    void setVolume(double v) { emit svc()->volumeRequested(v); }
    qlonglong position() const { return svc()->position(); }
public slots:
    void Next()      { emit svc()->nextRequested(); }
    void Previous()  { emit svc()->previousRequested(); }
    void Pause()     { emit svc()->pauseRequested(); }
    void PlayPause() { emit svc()->playPauseRequested(); }
    void Stop()      { emit svc()->stopRequested(); }
    void Play()      { emit svc()->playRequested(); }
    void Seek(qlonglong offsetUs) { emit svc()->seekRequested(offsetUs); }
    void SetPosition(const QDBusObjectPath &, qlonglong us) { emit svc()->positionRequested(us); }
    void OpenUri(const QString &) {}
private:
    MprisService *svc() const { return static_cast<MprisService *>(parent()); }
};
