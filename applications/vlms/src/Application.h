#pragma once

#include <QApplication>
#include <QString>
#include <QTranslator>

#include <memory>

#include "ui/presentation/QtCatalogLookups.h"
#include "ui/services/OcrService.h"
#include "ui/workflows/DataEvents.h"
#include "ui/workflows/DialogFactory.h"
#include "ui/workflows/ErrorPresenter.h"
#include "ui/workflows/QtDialogFactory.h"
#include "ui/workflows/QtErrorPresenter.h"

namespace VLMS::Database {
class Connection;
}  // namespace VLMS::Database

namespace VLMS::Repositories {
class CatalogRepository;
class MemberRepository;
class CirculationRepository;
class MetricsRepository;
}  // namespace VLMS::Repositories

class Application final : public QApplication {
    Q_OBJECT

public:
    Application(int& argc, char** argv);
    ~Application() override;

    [[nodiscard]] VLMS::Database::Connection& database() const { return *m_database; }
    /// False when Connection::open() refused. main() must not build a window on
    /// top of a database that is not there.
    [[nodiscard]] bool isDatabaseReady() const { return m_databaseReady; }
    [[nodiscard]] VLMS::Repositories::CatalogRepository& catalog() const { return *m_catalogRepository; }
    [[nodiscard]] VLMS::Repositories::MemberRepository& members() const { return *m_memberRepository; }
    [[nodiscard]] VLMS::Repositories::CirculationRepository& circulation() const { return *m_circulationRepository; }
    [[nodiscard]] VLMS::Repositories::MetricsRepository& metrics() const { return *m_metricsRepository; }
    [[nodiscard]] VLMS::CatalogLookups& catalogLookups() { return *m_catalogLookups; }
    [[nodiscard]] VLMS::OcrService& ocr() { return *m_ocr; }
    [[nodiscard]] VLMS::DataEvents& dataEvents() { return *m_dataEvents; }
    [[nodiscard]] VLMS::ErrorPresenter& errors() { return *m_errors; }
    /// Null when the database is not ready (no repositories to back dialogs).
    [[nodiscard]] VLMS::DialogFactory* dialogs() { return m_dialogs.get(); }
    [[nodiscard]] QString uiLocale() const;
    [[nodiscard]] bool isDarkTheme() const;

    void setUiLocale(const QString& code);
    void setDarkTheme(bool dark);
    void toggleTheme();

signals:
    void languageChanged();
    void themeChanged();

private:
    void applyTheme();
    void applyQtCatalogue();

    QTranslator m_qtTranslator;

    VLMS::Database::Connection* m_database = nullptr;
    bool m_databaseReady = false;
    VLMS::Repositories::CatalogRepository* m_catalogRepository = nullptr;
    VLMS::Repositories::MemberRepository* m_memberRepository = nullptr;
    VLMS::Repositories::CirculationRepository* m_circulationRepository = nullptr;
    VLMS::Repositories::MetricsRepository* m_metricsRepository = nullptr;

    std::unique_ptr<VLMS::DataEvents> m_dataEvents;
    std::unique_ptr<VLMS::QtErrorPresenter> m_errors;
    std::unique_ptr<VLMS::QtCatalogLookups> m_catalogLookups;
    std::unique_ptr<VLMS::OcrService> m_ocr;
    std::unique_ptr<VLMS::QtDialogFactory> m_dialogs;
};
