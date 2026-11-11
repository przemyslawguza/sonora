#pragma once
// Self-drawn transport icons: they do not depend on the Qt style / icon theme
// (which may be missing or invisible), and follow the palette's text colour.
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>

namespace TransportIcons {

enum Kind { Prev, Play, Pause, Stop, Next, Eject, Repeat, Shuffle };

inline QPixmap pixmap(Kind k, const QColor &c, int size, qreal dpr)
{
    const int px = int(size * dpr);
    QPixmap pm(px, px);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);          // draw on a 24x24 grid

    const QPen line(c, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    auto fill = [&] { p.setPen(Qt::NoPen); p.setBrush(c); };
    auto stroke = [&] { p.setPen(line); p.setBrush(Qt::NoBrush); };

    switch (k) {
    case Play:
        fill(); p.drawPolygon(QPolygonF{{7, 4}, {20, 12}, {7, 20}});
        break;
    case Pause:
        fill(); p.drawRect(QRectF(6, 4, 4.5, 16)); p.drawRect(QRectF(13.5, 4, 4.5, 16));
        break;
    case Stop:
        fill(); p.drawRect(QRectF(5, 5, 14, 14));
        break;
    case Prev:
        fill(); p.drawRect(QRectF(4, 4, 3, 16)); p.drawPolygon(QPolygonF{{20, 4}, {8, 12}, {20, 20}});
        break;
    case Next:
        fill(); p.drawRect(QRectF(17, 4, 3, 16)); p.drawPolygon(QPolygonF{{4, 4}, {16, 12}, {4, 20}});
        break;
    case Eject:
        fill(); p.drawPolygon(QPolygonF{{12, 4}, {21, 14}, {3, 14}}); p.drawRect(QRectF(3, 16.5, 18, 3));
        break;
    case Repeat:
        stroke(); p.drawLine(QPointF(5, 8), QPointF(16, 8)); p.drawLine(QPointF(19, 16), QPointF(8, 16));
        fill();
        p.drawPolygon(QPolygonF{{15, 3.5}, {21, 8}, {15, 12.5}});
        p.drawPolygon(QPolygonF{{9, 11.5}, {3, 16}, {9, 20.5}});
        break;
    case Shuffle:
        stroke();
        p.drawPolyline(QPolygonF{{3, 7}, {9, 7}, {15, 17}, {17, 17}});
        p.drawPolyline(QPolygonF{{3, 17}, {9, 17}, {15, 7}, {17, 7}});
        fill();
        p.drawPolygon(QPolygonF{{16.5, 3}, {22, 7}, {16.5, 11}});
        p.drawPolygon(QPolygonF{{16.5, 13}, {22, 17}, {16.5, 21}});
        break;
    }
    return pm;
}

inline QIcon make(Kind k, const QPalette &pal, int size, qreal dpr)
{
    QIcon icon;
    icon.addPixmap(pixmap(k, pal.color(QPalette::Active, QPalette::ButtonText), size, dpr), QIcon::Normal);
    icon.addPixmap(pixmap(k, pal.color(QPalette::Disabled, QPalette::ButtonText), size, dpr), QIcon::Disabled);
    return icon;
}

}  // namespace TransportIcons
