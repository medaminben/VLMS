#pragma once

#include <QWidget>

class QComboBox;

class BirthDateEdit final : public QWidget {
    Q_OBJECT

public:
    explicit BirthDateEdit(QWidget* parent = nullptr);

    [[nodiscard]] QComboBox* dayCombo() const;
    [[nodiscard]] QComboBox* monthCombo() const;
    [[nodiscard]] QComboBox* yearCombo() const;

    void setIsoDate(const QString& iso);
    [[nodiscard]] QString isoDate() const;

private:
    class Combo;

    void fillCanonical();
    void selectOrInsert(QComboBox* box, const QString& text);
    [[nodiscard]] static bool allDigits(const QString& text);

    QComboBox* m_day = nullptr;
    QComboBox* m_month = nullptr;
    QComboBox* m_year = nullptr;
};
