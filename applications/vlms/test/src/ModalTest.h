#pragma once

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QTimer>
#include <QWidget>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>

struct ModalOutcome {
    bool appeared = false;
    QString text;
    QStringList buttonLabels;
    bool hasCheckBox = false;
    QString checkBoxLabel;
};

struct ModalAnswer {
    QString button;
    std::optional<bool> checkBox;
    QString screenshotPath;
};

inline QList<ModalOutcome> runAndAnswerModals(const std::function<void()>& action,
                                              const QList<ModalAnswer>& answers)
{
    QList<ModalOutcome> outcomes;
    outcomes.resize(answers.size());

    auto scheduleAnswer = std::make_shared<std::function<void(int)>>();
    *scheduleAnswer = [&outcomes, &answers, scheduleAnswer](int index) {
        if (index >= answers.size()) {
            return;
        }
        QTimer::singleShot(0, [&outcomes, &answers, scheduleAnswer, index]() {
            QWidget* modal = nullptr;
            for (int attempt = 0; attempt < 100 && modal == nullptr; ++attempt) {
                modal = QApplication::activeModalWidget();
                if (modal == nullptr) {
                    QTest::qWait(10);
                }
            }

            auto* box = qobject_cast<QMessageBox*>(modal);
            if (box == nullptr) {
                if (modal != nullptr) {
                    modal->close();
                }
                return;
            }

            const ModalAnswer& answer = answers.at(index);
            ModalOutcome& outcome = outcomes[index];
            outcome.appeared = true;
            outcome.text = box->text();
            if (box->checkBox() != nullptr) {
                outcome.hasCheckBox = true;
                outcome.checkBoxLabel = box->checkBox()->text();
                if (answer.checkBox.has_value()) {
                    box->checkBox()->setChecked(*answer.checkBox);
                }
            }

            const auto buttons = box->buttons();
            for (QAbstractButton* button : buttons) {
                outcome.buttonLabels.append(button->text());
            }

            if (!answer.screenshotPath.isEmpty()) {
                box->grab().save(answer.screenshotPath);
            }

            (*scheduleAnswer)(index + 1);

            for (QAbstractButton* button : buttons) {
                if (QString(button->text()).remove(QLatin1Char('&')) == answer.button) {
                    button->click();
                    return;
                }
            }
            box->close();
        });
    };

    (*scheduleAnswer)(0);
    action();
    *scheduleAnswer = nullptr;
    return outcomes;
}

inline ModalOutcome runAndAnswerModal(const std::function<void()>& action,
                                      const QString& chooseButton,
                                      const std::optional<bool>& tickCheckBox = std::nullopt,
                                      const QString& screenshotPath = {})
{
    return runAndAnswerModals(action, {ModalAnswer{chooseButton, tickCheckBox, screenshotPath}})
        .front();
}

/// Clicks the first button on `root` whose label is exactly `text`. Fails the
/// test if there is none -- a renamed button should not read as a silent pass.
inline void clickButtonWithText(QWidget* root, const QString& text)
{
    for (QPushButton* button : root->findChildren<QPushButton*>()) {
        if (button->text() == text) {
            button->click();
            return;
        }
    }
    FAIL() << "no button labelled \"" << text.toStdString() << "\"";
}
