#pragma once

#include <QPixmap>
#include <QSize>
#include <QString>

class QLabel;

namespace VLMS {

/// The book placeholder artwork at `size`, recoloured for the current theme.
[[nodiscard]] QPixmap bookCoverPlaceholder(const QSize& size);

/// Shows the cover at `path` in `label`, or the placeholder artwork when there
/// is none or it does not load. Sizes the label to `size` first.
void showBookCover(QLabel* label, const QString& path, const QSize& size);

/// Shows the photo at `path` in `label`, or the "No photo" text when there is
/// none or it does not load. Sizes the label to `size` first.
void showMemberPhoto(QLabel* label, const QString& path, const QSize& size);

}  // namespace VLMS
