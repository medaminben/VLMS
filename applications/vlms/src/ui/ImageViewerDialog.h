#pragma once

#include <QDialog>
#include <QPixmap>
#include <QSize>
#include <QString>

#include <functional>

class QLabel;

/**
 * One image, shown large, with Change…, Remove and Close.
 *
 * Opened by MemberEditorDialog when a librarian clicks a photo or ID image
 * box that already shows an image. It knows nothing about members: it is
 * given a path and a way to pick another file, and reports what happened.
 * It writes nothing; the caller decides what the outcome means.
 *
 * No retranslateUi: it is modal over a modal dialog, so the language
 * selector cannot be reached while it is open. LicenceDialog is the same.
 */
class ImageViewerDialog final : public QDialog {
    Q_OBJECT

public:
    /// Returns the chosen file, or an empty string when the picker was cancelled.
    using FilePicker = std::function<QString()>;

    enum class Outcome { Unchanged, Changed, Removed };

    ImageViewerDialog(const QString& title,
                      const QString& imagePath,
                      FilePicker pickFile,
                      QWidget* parent = nullptr);

    [[nodiscard]] Outcome outcome() const { return m_outcome; }
    /// The image on screen: the one it opened with, or the last one Change picked.
    [[nodiscard]] QString imagePath() const { return m_path; }
    /// The size of the scaled pixmap on screen. For tests.
    [[nodiscard]] QSize shownImageSize() const;

    /// The largest size `image` scales to inside `bounds`, up or down,
    /// keeping its aspect ratio. Empty when either size is empty.
    [[nodiscard]] static QSize fittedSize(const QSize& image, const QSize& bounds);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi(const QString& title);
    void sizeToScreen();
    void rescale();
    void changeImage();

    FilePicker m_pickFile;
    QString m_path;
    QPixmap m_source;
    Outcome m_outcome = Outcome::Unchanged;
    QLabel* m_image = nullptr;
    QWidget* m_buttonRow = nullptr;
};
