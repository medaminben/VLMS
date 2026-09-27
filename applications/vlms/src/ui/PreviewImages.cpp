#include "ui/PreviewImages.h"

#include "QtBridge.h"
#include "ui/Theme.h"
#include "ui/UiHelpers.h"

#include <QLabel>

namespace VLMS {
namespace {

QPixmap loadScaled(const QString& path, const QSize& size)
{
    if (path.isEmpty()) {
        return {};
    }
    const QPixmap pixmap(path);
    if (pixmap.isNull()) {
        return {};
    }
    return pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

}  // namespace

QPixmap bookCoverPlaceholder(const QSize& size)
{
    static const QPixmap source(QStringLiteral(":/images/book-placeholder.png"));
    if (source.isNull()) {
        return {};
    }
    // Recoloured after scaling, on the smaller image: the artwork ships in the
    // light palette and would otherwise be a white card on a dark window.
    return themedArtwork(source.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void showBookCover(QLabel* label, const QString& path, const QSize& size)
{
    if (label == nullptr) {
        return;
    }
    applyPreviewLabelGeometry(label, size);
    const QPixmap cover = loadScaled(path, size);
    label->setText({});
    label->setPixmap(cover.isNull() ? bookCoverPlaceholder(size) : cover);
}

void showMemberPhoto(QLabel* label, const QString& path, const QSize& size)
{
    if (label == nullptr) {
        return;
    }
    applyPreviewLabelGeometry(label, size);
    const QPixmap photo = loadScaled(path, size);
    if (photo.isNull()) {
        label->setPixmap({});
        label->setText(T("members.noPhoto"));
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignCenter);
        return;
    }
    label->setText({});
    label->setPixmap(photo);
}

}  // namespace VLMS
