#pragma once

#include <QLabel>

class QMouseEvent;

namespace VLMS {

/**
 * A QLabel that reports a left-click.
 *
 * Only the footer's copyright line needs this. Kept as its own widget so the
 * click path has a unit test without constructing MainWindow.
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
