#pragma once

#include <QString>
#include <QWidget>

class QButtonGroup;
class QEvent;

namespace VLMS {

/**
 * The three UI languages as circular flag buttons, exactly one pressed.
 *
 * Its own widget rather than a row built inside MainWindow, so language
 * selection can be unit-tested without constructing the shell.
 */
class LanguageSelector final : public QWidget {
    Q_OBJECT

public:
    /// The diameter of QPushButton#themeToggle, which this row sits beside.
    static constexpr int kButtonSize = 34;
    static constexpr int kActiveIconSize = 26;
    static constexpr int kIdleIconSize = 22;

    explicit LanguageSelector(QWidget* parent = nullptr);

    QString currentLanguage() const;
    /// Moves the selection without announcing it, for following the locale.
    /// Ignores a code the row does not offer.
    void setCurrentLanguage(const QString& code);

signals:
    void languageSelected(const QString& code);

protected:
    void changeEvent(QEvent* event) override;

private:
    void applyLabels();
    void updateButtons();

    QButtonGroup* m_group = nullptr;
    QString m_current;
};

}  // namespace VLMS
