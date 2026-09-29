#pragma once

#include <VLMS/Repositories/MetricsRepository.h>

#include <QVector>
#include <QWidget>

class QEvent;
class QLabel;
class QPushButton;
class QTableWidget;

class MetricsPage final : public QWidget {
    Q_OBJECT

public:
    explicit MetricsPage(VLMS::Repositories::MetricsRepository& repository, QWidget* parent = nullptr);

    void refreshMetrics();

protected:
    void changeEvent(QEvent* event) override;

private:
    struct MetricCard {
        QString labelKey;
        QLabel* label = nullptr;
        QLabel* value = nullptr;
    };

    void buildUi();
    void applyLabels();
    QWidget* makeSection(const QString& titleKey, QWidget* content, bool centerTitle = false);
    QWidget* makeMetricGrid(const QStringList& labelKeys,
                            QVector<MetricCard>* cards,
                            QWidget* parent,
                            int columns);
    void setMetricValue(QLabel* label, int value);
    void refreshActivityTable();
    void refreshTopCategoriesTable();

    VLMS::Repositories::MetricsRepository& m_repository;

    QPushButton* m_refreshButton = nullptr;
    QVector<MetricCard> m_overviewCards;
    QVector<MetricCard> m_memberCards;
    QVector<MetricCard> m_circulationCards;
    QTableWidget* m_activityTable = nullptr;
    QTableWidget* m_categoriesTable = nullptr;
    QLabel* m_categoriesEmpty = nullptr;
};
