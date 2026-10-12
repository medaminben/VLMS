#pragma once

// Answers real modal windows in tests: a scripted librarian.

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStringList>
#include <QTimer>

#include <gtest/gtest.h>

#include <deque>
#include <functional>

/// Answers the next real windows in order. Each step gets the modal window
/// and does what the librarian would; unanswered windows are closed and
/// recorded so the test fails loudly instead of hanging.
class Librarian {
public:
    using Step = std::function<void(QWidget*)>;

    explicit Librarian(std::deque<Step> steps) : m_steps(std::move(steps))
    {
        m_timer.setInterval(20);
        QObject::connect(&m_timer, &QTimer::timeout, [this] { answer(); });
        m_timer.start();
    }

    QStringList unexpected;
    QStringList seen;

    [[nodiscard]] bool done() const { return m_steps.empty(); }

private:
    void answer()
    {
        QWidget* modal = QApplication::activeModalWidget();
        if (modal == nullptr || !modal->isVisible() || modal == m_last) {
            return;
        }
        m_last = modal;
        seen.append(modal->metaObject()->className());
        if (m_steps.empty()) {
            unexpected.append(modal->metaObject()->className());
            modal->close();
            return;
        }
        const Step step = m_steps.front();
        m_steps.pop_front();
        step(modal);
    }

    std::deque<Step> m_steps;
    QTimer m_timer;
    QPointer<QWidget> m_last;
};

inline void pressOk(QWidget* dialog)
{
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    ASSERT_NE(buttons, nullptr) << dialog->metaObject()->className();
    buttons->button(QDialogButtonBox::Ok)->click();
}

inline void pressYes(QWidget* modal)
{
    auto* box = qobject_cast<QMessageBox*>(modal);
    ASSERT_NE(box, nullptr) << modal->metaObject()->className();
    for (QAbstractButton* button : box->buttons()) {
        if (box->buttonRole(button) == QMessageBox::YesRole
            || box->standardButton(button) == QMessageBox::Yes) {
            button->click();
            return;
        }
    }
    FAIL() << "no Yes button";
}

inline void closeWindow(QWidget* modal)
{
    modal->close();
}

inline Librarian::Step titleThenOk(const QString& title)
{
    return [title](QWidget* dialog) {
        auto* edit = dialog->findChild<QLineEdit*>(QStringLiteral("bookTitleEdit"));
        ASSERT_NE(edit, nullptr);
        edit->setText(title);
        pressOk(dialog);
    };
}

