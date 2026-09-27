#pragma once

#include <QDialog>

class QTextBrowser;

/**
 * The licence, opened from the copyright line in the footer.
 *
 * Modal on purpose: the language selector sits in the header, unreachable
 * while this is up, so the text can never go stale underneath the reader.
 * That is why there is no retranslateUi here -- every open composes the
 * document afresh from the current Locale::code().
 */
class LicenceDialog final : public QDialog {
    Q_OBJECT

public:
    explicit LicenceDialog(QWidget* parent = nullptr);

    /// The rendered licence as plain text. For tests; nothing in the
    /// application reads it.
    [[nodiscard]] QString documentText() const;

private:
    void buildUi();

    QTextBrowser* m_body = nullptr;
};
