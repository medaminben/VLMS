#pragma once

#include <QApplication>
#include <QString>
#include <QTranslator>

class Database;

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

    [[nodiscard]] Database& database() const { return *m_database; }
    /// False when Database::open() refused. main() must not build a window on
    /// top of a database that is not there.
    [[nodiscard]] bool isDatabaseReady() const { return m_databaseReady; }
    [[nodiscard]] VLMS::Repositories::CatalogRepository& catalog() const { return *m_catalogRepository; }
    [[nodiscard]] VLMS::Repositories::MemberRepository& members() const { return *m_memberRepository; }
    [[nodiscard]] VLMS::Repositories::CirculationRepository& circulation() const { return *m_circulationRepository; }
    [[nodiscard]] VLMS::Repositories::MetricsRepository& metrics() const { return *m_metricsRepository; }
    [[nodiscard]] QString uiLocale() const;
    [[nodiscard]] bool isDarkTheme() const;

    void setUiLocale(const QString& code);
    void setDarkTheme(bool dark);
    void toggleTheme();

signals:
    void languageChanged();
    void themeChanged();

private:
    /// Palette and stylesheet for the mode Theme currently holds, in that
    /// order. Called at start-up and on every toggle.
    void applyTheme();
    /// Installs Qt's own catalogue for the current locale, so the text inside
    /// stock Qt widgets follows the language chosen in the header.
    void applyQtCatalogue();

    QTranslator m_qtTranslator;

    Database* m_database = nullptr;
    bool m_databaseReady = false;
    VLMS::Repositories::CatalogRepository* m_catalogRepository = nullptr;
    VLMS::Repositories::MemberRepository* m_memberRepository = nullptr;
    VLMS::Repositories::CirculationRepository* m_circulationRepository = nullptr;
    VLMS::Repositories::MetricsRepository* m_metricsRepository = nullptr;
};
