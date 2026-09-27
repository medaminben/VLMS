#include "Capture.h"

#include "Application.h"
#include "QtBridge.h"
#include "ui/MainWindow.h"

#include <VLMS/Core/Strings.h>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTest>
#include <QTimer>

#include <cstdio>
#include <stdexcept>

using VLMS::T;

namespace ManualCapture {

namespace {

QString buttonLabel(const QAbstractButton* button)
{
    QString label = button->text();
    label.remove(QLatin1Char('&'));
    return label;
}

}  // namespace

Capture::Capture(MainWindow& window, QString outRoot, QString lang)
    : m_window(window)
    , m_outRoot(std::move(outRoot))
    , m_lang(std::move(lang))
{
}

MainWindow& Capture::window() const
{
    return m_window;
}

const QString& Capture::lang() const
{
    return m_lang;
}

void Capture::settle(int ms)
{
    QApplication::processEvents();
    QTest::qWait(ms);
}

void Capture::goTo(const char* navKey)
{
    const QString wanted = T(navKey);
    const auto buttons = m_window.findChildren<QPushButton*>(QStringLiteral("navLink"));
    for (QPushButton* nav : buttons) {
        if (buttonLabel(nav) == wanted) {
            click(nav);
            settle(400);
            return;
        }
    }
    throw std::runtime_error(std::string("no nav button ") + navKey);
}

QWidget* Capture::page() const
{
    auto* stack = m_window.findChild<QStackedWidget*>();
    if (stack == nullptr || stack->currentWidget() == nullptr) {
        throw std::runtime_error("no current page");
    }
    return stack->currentWidget();
}

QAbstractButton* Capture::button(const char* key, QWidget* root) const
{
    QWidget* base = root;
    if (base == nullptr) {
        base = QApplication::activeModalWidget();
    }
    if (base == nullptr) {
        base = page();
    }
    const QString wanted = T(key);
    const auto buttons = base->findChildren<QAbstractButton*>();
    for (QAbstractButton* candidate : buttons) {
        if (candidate->isVisible() && buttonLabel(candidate) == wanted) {
            return candidate;
        }
    }
    throw std::runtime_error(std::string("no button ") + key);
}

QTableWidget* Capture::table(QWidget* root) const
{
    QWidget* base = root != nullptr ? root : page();
    auto* found = base->findChild<QTableWidget*>();
    if (found == nullptr) {
        throw std::runtime_error("no table");
    }
    return found;
}

void Capture::click(QWidget* widget)
{
    if (widget == nullptr) {
        throw std::runtime_error("click on a null widget");
    }
    // mouseClick spins the event loop before the slot runs. Under the Arabic
    // layout that loop commits the search field and rebuilds the table, so the
    // row just selected is gone by the time Delete or Loans reads it.
    // QAbstractButton::click() emits clicked() directly.
    if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
        if (!button->isEnabled()) {
            throw std::runtime_error("button is disabled");
        }
        button->click();
    } else {
        QTest::mouseClick(widget, Qt::LeftButton, {}, widget->rect().center());
    }
    settle();
}

void Capture::typeInto(QWidget* edit, const QString& text)
{
    if (auto* line = qobject_cast<QLineEdit*>(edit)) {
        line->clear();
    } else if (auto* combo = qobject_cast<QComboBox*>(edit)) {
        if (combo->isEditable() && combo->lineEdit() != nullptr) {
            combo->lineEdit()->clear();
        }
    }
    QTest::keyClicks(edit, text);
    settle();
}

void Capture::openModal(const std::function<void()>& trigger,
                        const std::function<void(QWidget* modal)>& inside)
{
    // A repeating timer, not one blocking poll: QTest::mouseClick spins the
    // event loop before the dialog's exec() does, and a single 3 s wait posted
    // ahead of the click would give up before the modal existed.
    bool ran = false;
    QString error;
    int spins = 0;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        if (ran || !error.isEmpty()) {
            timer.stop();
            return;
        }
        QWidget* modal = QApplication::activeModalWidget();
        if (modal == nullptr) {
            if (++spins > 300) {
                timer.stop();
            }
            return;
        }
        timer.stop();
        try {
            settle(250);
            inside(modal);
            ran = true;
        } catch (const std::exception& exception) {
            error = QString::fromUtf8(exception.what());
            if (auto* dialog = qobject_cast<QDialog*>(modal)) {
                dialog->reject();
            } else if (modal != nullptr) {
                modal->close();
            }
        }
    });
    timer.start();
    trigger();
    if (!ran && error.isEmpty()) {
        QElapsedTimer clock;
        clock.start();
        while (!ran && error.isEmpty() && clock.elapsed() < 3000) {
            QTest::qWait(10);
        }
    }
    timer.stop();
    if (!error.isEmpty()) {
        throw std::runtime_error(error.toStdString());
    }
    if (!ran) {
        throw std::runtime_error("no modal appeared");
    }
}

QPixmap Capture::grab(QWidget* widget) const
{
    QWidget* target = widget != nullptr ? widget : &m_window;
    QPixmap image = target->grab();
    QPainter painter(&image);
    const auto tops = QApplication::topLevelWidgets();
    for (QWidget* popup : tops) {
        if (popup == nullptr || popup == target || !popup->isVisible()) {
            continue;
        }
        if ((popup->windowFlags() & Qt::Popup) == 0) {
            continue;
        }
        const QPoint inTarget = target->mapFromGlobal(popup->mapToGlobal(QPoint(0, 0)));
        if (!target->rect().contains(inTarget)) {
            continue;
        }
        painter.drawPixmap(inTarget, popup->grab());
    }
    return image;
}

QPixmap Capture::grabRegion(const QList<QWidget*>& widgets) const
{
    QRect united;
    for (QWidget* widget : widgets) {
        if (widget == nullptr) {
            continue;
        }
        const QPoint topLeft = widget->mapTo(&m_window, QPoint(0, 0));
        united = united.united(QRect(topLeft, widget->size()));
    }
    united = united.adjusted(-12, -12, 12, 12).intersected(m_window.rect());
    return grab(&m_window).copy(united);
}

QPixmap Capture::callouts(QPixmap image, QWidget* base, const QList<QWidget*>& targets) const
{
    QList<QRect> rects;
    rects.reserve(targets.size());
    for (QWidget* target : targets) {
        if (target == nullptr || base == nullptr) {
            continue;
        }
        const QPoint topLeft = target->mapTo(base, QPoint(0, 0));
        rects.append(QRect(topLeft, target->size()));
    }
    return calloutsAt(std::move(image), rects);
}

QPixmap Capture::calloutsAt(QPixmap image, const QList<QRect>& rects) const
{
    const qreal ratio = image.devicePixelRatio() > 0 ? image.devicePixelRatio() : 1.0;
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font(QStringLiteral("Cairo"));
    font.setBold(true);
    font.setPixelSize(qRound(15 * ratio));
    painter.setFont(font);

    const bool rtl = QApplication::layoutDirection() == Qt::RightToLeft;
    const int diameter = qRound(30 * ratio);
    const int limit = qMin(rects.size(), 20);
    for (int index = 0; index < limit; ++index) {
        const QRect target = rects.at(index);
        int x = 0;
        int y = 0;
        if (rtl) {
            x = qRound((target.right() - 36) * ratio);
            y = qRound((target.top() + 6) * ratio);
        } else {
            x = qRound((target.left() + 6) * ratio);
            y = qRound((target.top() + 6) * ratio);
        }
        x = qBound(0, x, image.width() - diameter);
        y = qBound(0, y, image.height() - diameter);
        const QRect circle(x, y, diameter, diameter);
        painter.setPen(QPen(Qt::white, 2 * ratio));
        painter.setBrush(QColor(QStringLiteral("#0f766e")));
        painter.drawEllipse(circle);
        painter.setPen(Qt::white);
        painter.drawText(circle, Qt::AlignCenter, QString::number(index + 1));
    }
    return image;
}

void Capture::save(const QString& id, const QPixmap& image) const
{
    const QString directory = m_outRoot + QStringLiteral("/shots/") + m_lang;
    if (!QDir().mkpath(directory)) {
        throw std::runtime_error("could not create the shot directory");
    }
    const QString path = directory + QLatin1Char('/') + id + QStringLiteral(".png");
    if (!image.save(path, "PNG")) {
        throw std::runtime_error("could not save " + path.toStdString());
    }
    std::fprintf(stdout, "shot %s/%s %dx%d\n", qPrintable(m_lang), qPrintable(id),
                 image.width(), image.height());
}

void Capture::reset()
{
    const auto tops = QApplication::topLevelWidgets();
    for (QWidget* widget : tops) {
        if (widget == nullptr || widget == &m_window || !widget->isVisible()) {
            continue;
        }
        if (auto* dialog = qobject_cast<QDialog*>(widget)) {
            dialog->reject();
        } else {
            widget->close();
        }
    }
    if (auto* app = qobject_cast<Application*>(qApp)) {
        app->setDarkTheme(false);
    }
    goTo("nav.catalog");

    const QString placeholder = T("catalog.searchPlaceholder");
    const auto edits = page()->findChildren<QLineEdit*>();
    for (QLineEdit* edit : edits) {
        if (edit->placeholderText() == placeholder) {
            edit->clear();
        }
    }

    QTableWidget* books = table();
    for (int row = 0; row < books->rowCount(); ++row) {
        QTableWidgetItem* item = books->item(row, 0);
        if (item != nullptr && item->checkState() != Qt::Unchecked) {
            item->setCheckState(Qt::Unchecked);
        }
    }
    settle();
}

}  // namespace ManualCapture
