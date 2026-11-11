#include "Mpris.h"
#include <QDBusConnection>
#include <QDBusMessage>

namespace {
const char *kPath = "/org/mpris/MediaPlayer2";
const char *kPlayerIface = "org.mpris.MediaPlayer2.Player";
}

MprisService::MprisService(Player *player, QObject *parent) : QObject(parent), m_player(player)
{
    new MprisRootAdaptor(this);
    new MprisPlayerAdaptor(this);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && bus.registerService("org.mpris.MediaPlayer2.sonora"))
        m_registered = bus.registerObject(kPath, this);       // adaptors are exported with it
}

QString MprisService::playbackStatus() const
{
    switch (m_state) {
    case Player::Playing: return QStringLiteral("Playing");
    case Player::Paused:  return QStringLiteral("Paused");
    default:              return QStringLiteral("Stopped");
    }
}

void MprisService::notify(const QString &iface, const QVariantMap &changed)
{
    if (!m_registered) return;
    QDBusMessage sig = QDBusMessage::createSignal(kPath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    sig << iface << changed << QStringList();
    QDBusConnection::sessionBus().send(sig);
}

void MprisService::setState(Player::State s)
{
    if (m_state == s) return;
    m_state = s;
    notify(kPlayerIface, {{"PlaybackStatus", playbackStatus()}});
}

void MprisService::setTrack(const QString &title, const QStringList &artists, const QString &album,
                            const QUrl &url, qint64 lengthUs, int trackNo)
{
    const QString key = title + '|' + artists.join(',') + '|' + album + '|' + url.toString() + '|' +
                        QString::number(lengthUs) + '|' + QString::number(trackNo);
    if (key == m_lastKey) return;
    m_lastKey = key;

    m_metadata.clear();
    m_metadata["mpris:trackid"] = QVariant::fromValue(
        QDBusObjectPath(QStringLiteral("/org/sonora/track/%1").arg(qHash(url.toString()))));
    if (lengthUs > 0) m_metadata["mpris:length"] = QVariant::fromValue<qlonglong>(lengthUs);
    m_metadata["xesam:title"] = title;
    if (!artists.isEmpty()) m_metadata["xesam:artist"] = artists;
    if (!album.isEmpty()) m_metadata["xesam:album"] = album;
    m_metadata["xesam:url"] = url.toString();
    if (trackNo > 0) m_metadata["xesam:trackNumber"] = trackNo;
    notify(kPlayerIface, {{"Metadata", m_metadata}});
}

void MprisService::clearTrack()
{
    if (m_lastKey.isEmpty()) return;
    m_lastKey.clear();
    m_metadata.clear();
    m_metadata["mpris:trackid"] = QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack")));
    notify(kPlayerIface, {{"Metadata", m_metadata}});
}

void MprisService::setVolume(double v)
{
    if (qFuzzyCompare(v + 1.0, m_volume + 1.0)) return;
    m_volume = v;
    notify(kPlayerIface, {{"Volume", v}});
}
