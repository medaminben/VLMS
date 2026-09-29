#pragma once

#include <QString>
#include <QStringList>
#include <Qt>

namespace VLMS {

/// Shared item roles and closed-cell text for catalogue local-number columns.
/// Lives in presentation so list specs can fill model data without including
/// the view-layer `LocalNumberDelegate` painter.
namespace LocalNumberCell {

/// Item role holding the row's numbers as a QStringList.
inline constexpr int kNumbersRole = Qt::UserRole + 1;

/// Item role holding the one number the cell shows, as a QString.
inline constexpr int kSelectedRole = Qt::UserRole + 2;

/// Item role holding the subset of kNumbersRole whose copy is out on loan.
inline constexpr int kOnLoanRole = Qt::UserRole + 3;

/// Shown when a book has no copies at all.
inline const QString kNoNumbers = QString::fromUtf8("—");

[[nodiscard]] inline QString leadNumber(const QStringList& numbers,
                                        const QString& selected = {})
{
    if (numbers.isEmpty()) {
        return kNoNumbers;
    }
    return numbers.contains(selected) ? selected : numbers.first();
}

[[nodiscard]] inline QString restSuffix(const QStringList& numbers)
{
    if (numbers.size() < 2) {
        return {};
    }
    return QStringLiteral(" (+%1)").arg(numbers.size() - 1);
}

[[nodiscard]] inline QString cellText(const QStringList& numbers,
                                      const QString& selected = {})
{
    return leadNumber(numbers, selected) + restSuffix(numbers);
}

}  // namespace LocalNumberCell

}  // namespace VLMS
