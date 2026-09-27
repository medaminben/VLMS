#include "ui/ImageViewerDialog.h"

#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include <utility>

using VLMS::T;

namespace {

/// Share of the screen's available area the viewer opens at.
constexpr double kScreenShare = 0.8;
/// The window stops shrinking here; the image still fits inside it.
constexpr int kMinImageSide = 120;

}  // namespace

ImageViewerDialog::ImageViewerDialog(const QString& title,
                                     const QString& imagePath,
                                     FilePicker pickFile,
                                     QWidget* parent)
    : QDialog(parent),
      m_pickFile(std::move(pickFile)),
      m_path(imagePath),
      m_source(imagePath)
{
    buildUi(title);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    sizeToScreen();
}

QSize ImageViewerDialog::fittedSize(const QSize& image, const QSize& bounds)
{
    if (image.isEmpty() || bounds.isEmpty()) {
        return {};
    }
    return image.scaled(bounds, Qt::KeepAspectRatio);
}

QSize ImageViewerDialog::shownImageSize() const
{
    return m_image->pixmap().size();
}

void ImageViewerDialog::buildUi(const QString& title)
{
    setObjectName(QStringLiteral("imageViewer"));
    setWindowTitle(title);

    auto* layout = new QVBoxLayout(this);

    m_image = new QLabel(this);
    m_image->setObjectName(QStringLiteral("imageViewerImage"));
    m_image->setAlignment(Qt::AlignCenter);
    // The pixmap is redrawn to fit the label, so it must not also size the
    // label: a pixmap-sized minimum would stop the window from shrinking.
    m_image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_image->setMinimumSize(kMinImageSide, kMinImageSide);
    m_image->installEventFilter(this);
    layout->addWidget(m_image, 1);

    auto* change = VLMS::makeSecondaryButton(T("imageViewer.change"));
    auto* remove = VLMS::makeSecondaryButton(T("imageViewer.remove"));
    auto* close = VLMS::makePrimaryButton(T("imageViewer.close"));
    close->setDefault(true);

    connect(change, &QPushButton::clicked, this, &ImageViewerDialog::changeImage);
    connect(remove, &QPushButton::clicked, this, [this]() {
        m_outcome = Outcome::Removed;
        accept();
    });
    connect(close, &QPushButton::clicked, this, [this]() {
        if (m_outcome == Outcome::Unchanged) {
            reject();
        } else {
            accept();
        }
    });

    m_buttonRow = new QWidget(this);
    auto* buttons = new QHBoxLayout(m_buttonRow);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addWidget(change);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(close);
    layout->addWidget(m_buttonRow);
}

void ImageViewerDialog::sizeToScreen()
{
    QScreen* screen = parentWidget() != nullptr ? parentWidget()->screen()
                                                : QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return;
    }

    const QMargins margins = layout()->contentsMargins();
    const QSize chrome(margins.left() + margins.right(),
                       margins.top() + margins.bottom() + qMax(0, layout()->spacing())
                           + m_buttonRow->sizeHint().height());
    const QSize bounds = screen->availableGeometry().size() * kScreenShare - chrome;

    QSize image = fittedSize(m_source.size(), bounds);
    if (image.isEmpty()) {
        image = QSize(kMinImageSide, kMinImageSide);
    }
    resize((image + chrome).expandedTo(minimumSizeHint()));
}

bool ImageViewerDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_image && event->type() == QEvent::Resize) {
        rescale();
    }
    return QDialog::eventFilter(watched, event);
}

void ImageViewerDialog::rescale()
{
    if (m_source.isNull()) {
        m_image->clear();
        return;
    }
    // Always from the original, so repeated resizing never compounds losses.
    m_image->setPixmap(
        m_source.scaled(m_image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ImageViewerDialog::changeImage()
{
    const QString path = m_pickFile ? m_pickFile() : QString();
    if (path.isEmpty()) {
        return;
    }
    const QPixmap picked(path);
    if (picked.isNull()) {
        return;
    }
    m_path = path;
    m_source = picked;
    m_outcome = Outcome::Changed;
    rescale();
}
