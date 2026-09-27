#pragma once

#include <QString>
#include <QWidget>

class QButtonGroup;

namespace VLMS {

/**
 * The three UI languages as circular flag buttons, exactly one pressed.
 *
 * Its own widget rather than a row built inside MainWindow, because MainWindow
 * reaches for qobject_cast<Application*>(qApp) and so cannot be constructed in
 * a test; this can.
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
    /// Re-reads the tooltips and repaints the flags, which a theme change
    /// needs as much as a language change does.
    void retranslateUi();

signals:
    void languageSelected(const QString& code);

private:
    void updateButtons();

    QButtonGroup* m_group = nullptr;
    QString m_current;
};

}  // namespace VLMS
