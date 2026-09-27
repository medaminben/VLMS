#include "ui/FlagIcons.h"

#include <QImage>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QRectF>

namespace VLMS {
namespace {

// What a language you are not using is worth: present enough to aim at, quiet
// enough that the picked flag is the only one that reads at a glance.
constexpr qreal kDimmedOpacity = 0.6;

// Flag_of_France.svg (900x600). Scaled to the square rather than centre
// cropped: a crop of a 3:2 tricolour shows the white band at double width.
void paintFrance(QPainter& painter, qreal side)
{
    const qreal band = side / 3.0;
    painter.fillRect(QRectF(0.0, 0.0, band, side), QColor(0x00, 0x26, 0x54));
    painter.fillRect(QRectF(band, 0.0, band, side), QColor(Qt::white));
    painter.fillRect(QRectF(band * 2.0, 0.0, side - band * 2.0, side),
                     QColor(0xCE, 0x11, 0x26));
}

// Flag_of_the_United_Kingdom_(3-5).svg, viewBox "0 0 50 30". Scaled
// non-uniformly to the square so the whole design survives: cropping a 3:5
// flag to a circle throws away the diagonals and leaves a fat cross.
void paintUnitedKingdom(QPainter& painter, qreal side)
{
    const QColor red(0xC8, 0x10, 0x2E);

    painter.save();
    painter.scale(side / 50.0, side / 30.0);
    painter.fillRect(QRectF(0.0, 0.0, 50.0, 30.0), QColor(0x01, 0x21, 0x69));

    const QLineF saltire[2] = {QLineF(0.0, 0.0, 50.0, 30.0),
                               QLineF(50.0, 0.0, 0.0, 30.0)};

    QPen white(QColor(Qt::white));
    white.setWidthF(6.0);
    painter.setPen(white);
    painter.drawLines(saltire, 2);

    // The red saltire is counterchanged -- offset within each arm rather than
    // centred on it. The source does that by clipping the same lines to four
    // triangles: "M25,15h25v15zv15h-25zh-25v-15zv-15h25z".
    QPainterPath counterchange;
    const QPointF centre(25.0, 15.0);
    const QPointF corners[4][2] = {
        {QPointF(50.0, 15.0), QPointF(50.0, 30.0)},
        {QPointF(25.0, 30.0), QPointF(0.0, 30.0)},
        {QPointF(0.0, 15.0), QPointF(0.0, 0.0)},
        {QPointF(25.0, 0.0), QPointF(50.0, 0.0)},
    };
    for (const auto& triangle : corners) {
        counterchange.moveTo(centre);
        counterchange.lineTo(triangle[0]);
        counterchange.lineTo(triangle[1]);
        counterchange.closeSubpath();
    }

    painter.save();
    painter.setClipPath(counterchange);
    QPen redPen(red);
    redPen.setWidthF(4.0);
    painter.setPen(redPen);
    painter.drawLines(saltire, 2);
    painter.restore();

    // St George's cross, one path filled red and stroked white:
    // "M-1 11h22v-12h8v12h22v8h-22v12h-8v-12h-22z".
    QPainterPath cross;
    cross.moveTo(-1.0, 11.0);
    cross.lineTo(21.0, 11.0);
    cross.lineTo(21.0, -1.0);
    cross.lineTo(29.0, -1.0);
    cross.lineTo(29.0, 11.0);
    cross.lineTo(51.0, 11.0);
    cross.lineTo(51.0, 19.0);
    cross.lineTo(29.0, 19.0);
    cross.lineTo(29.0, 31.0);
    cross.lineTo(21.0, 31.0);
    cross.lineTo(21.0, 19.0);
    cross.lineTo(-1.0, 19.0);
    cross.closeSubpath();

    QPen crossPen(QColor(Qt::white));
    crossPen.setWidthF(2.0);
    painter.setPen(crossPen);
    painter.setBrush(red);
    painter.drawPath(cross);
    painter.restore();
}

// Flag_of_Tunisia.svg, viewBox "-60 -40 120 80". Uniform scale -- a stretch
// would turn the disc into an ellipse -- zoomed 1.5x about the centre, which
// grows the disc from half the circle to three quarters of it and leaves the
// red margin half its original width.
void paintTunisia(QPainter& painter, qreal side)
{
    static constexpr qreal kZoom = 1.5;
    const QColor red(0xE7, 0x00, 0x13);

    painter.save();
    painter.translate(side / 2.0, side / 2.0);
    painter.scale(side * kZoom / 80.0, side * kZoom / 80.0);

    painter.setPen(Qt::NoPen);
    painter.fillRect(QRectF(-60.0, -40.0, 120.0, 80.0), red);

    // The crescent is cut, not stroked: a white disc, a red disc inside it,
    // and a white disc pushed right, which leaves the horns sharp.
    painter.setBrush(QColor(Qt::white));
    painter.drawEllipse(QPointF(0.0, 0.0), 20.0, 20.0);
    painter.setBrush(red);
    painter.drawEllipse(QPointF(0.0, 0.0), 15.0, 15.0);
    painter.setBrush(QColor(Qt::white));
    painter.drawEllipse(QPointF(4.0, 0.0), 12.0, 12.0);

    // "M-5 0l16.281-5.29L1.22 8.56V-8.56L11.28 5.29z" -- a pentagram, so it
    // needs the winding fill rule; Qt's default would hollow out the middle.
    QPainterPath star;
    star.setFillRule(Qt::WindingFill);
    star.moveTo(-5.0, 0.0);
    star.lineTo(11.281, -5.29);
    star.lineTo(1.22, 8.56);
    star.lineTo(1.22, -8.56);
    star.lineTo(11.28, 5.29);
    star.closeSubpath();
    painter.setBrush(red);
    painter.drawPath(star);

    painter.restore();
}

}  // namespace

QIcon languageFlagIcon(const QString& code, int pixelSize, qreal ratio, bool dimmed)
{
    void (*paint)(QPainter&, qreal) = nullptr;
    if (code == QLatin1String("ar")) {
        paint = &paintTunisia;
    } else if (code == QLatin1String("fr")) {
        paint = &paintFrance;
    } else if (code == QLatin1String("en")) {
        paint = &paintUnitedKingdom;
    } else {
        return {};
    }

    const int side = qMax(1, qRound(pixelSize * ratio));
    QPixmap canvas(side, side);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paint(painter, static_cast<qreal>(side));

    // The circle is cut with a mask rather than a clip path: QPainter clipping
    // is aliased, and a stair-stepped edge is the one thing that would give
    // away that these are painted rather than drawn artwork. Dimming rides
    // along on the same mask -- painting the flag itself at reduced opacity
    // would composite each of its own overlapping strokes over one another
    // (Tunisia's stacked discs, the UK's saltires) and leave something
    // darker and off-colour rather than a uniformly faded flag.
    QImage mask(side, side, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter maskPainter(&mask);
        maskPainter.setRenderHint(QPainter::Antialiasing, true);
        maskPainter.setPen(Qt::NoPen);
        maskPainter.setBrush(QColor(0, 0, 0, dimmed ? qRound(kDimmedOpacity * 255) : 255));
        maskPainter.drawEllipse(QRectF(0.0, 0.0, side, side));
    }
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawImage(0, 0, mask);
    painter.end();

    canvas.setDevicePixelRatio(ratio);
    return QIcon(canvas);
}

}  // namespace VLMS
