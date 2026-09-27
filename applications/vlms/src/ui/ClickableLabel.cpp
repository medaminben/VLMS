#include "ui/ClickableLabel.h"

#include <QMouseEvent>

namespace VLMS {

ClickableLabel::ClickableLabel(QWidget* parent)
    : QLabel(parent)
{
    setCursor(Qt::PointingHandCursor);
}

void ClickableLabel::mouseReleaseEvent(QMouseEvent* event)
{
    // Only a release that lands back on the label counts, the way a push
    // button behaves: a press that wanders off before release is a change of
    // mind, not a click.
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
        emit clicked();
    }
    QLabel::mouseReleaseEvent(event);
}

}  // namespace VLMS
