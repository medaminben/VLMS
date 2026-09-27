#pragma once

#include <QLabel>

class QMouseEvent;

namespace VLMS {

/**
 * A QLabel that reports a left-click.
 *
 * Only the footer's copyright line needs this, and it could as easily have
 * been an event filter inside MainWindow -- except that MainWindow cannot be
 * constructed in a test, because it reaches for qobject_cast<Application*>
 * (qApp). Anything put there is verifiable only by hand, so the click lives
 * here instead, where it has a test.
 */
class ClickableLabel final : public QLabel {
    Q_OBJECT

public:
    explicit ClickableLabel(QWidget* parent = nullptr);

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
};

}  // namespace VLMS
