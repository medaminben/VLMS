#pragma once

#include <QApplication>
#include <QString>
#include <QTranslator>

class Database;
class CatalogRepository;
class MemberRepository;
class CirculationRepository;
class MetricsRepository;

class Application final : public QApplication {
    Q_OBJECT

public:
    Application(int& argc, char** argv);
    ~Application() override;

    [[nodiscard]] Database& database() const { return *m_database; }
    /// False when Database::open() refused. main() must not build a window on
    /// top of a database that is not there.
    [[nodiscard]] bool isDatabaseReady() const { return m_databaseReady; }
    [[nodiscard]] CatalogRepository& catalog() const { return *m_catalogRepository; }
    [[nodiscard]] MemberRepository& members() const { return *m_memberRepository; }
    [[nodiscard]] CirculationRepository& circulation() const { return *m_circulationRepository; }
    [[nodiscard]] MetricsRepository& metrics() const { return *m_metricsRepository; }
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
    CatalogRepository* m_catalogRepository = nullptr;
    MemberRepository* m_memberRepository = nullptr;
    CirculationRepository* m_circulationRepository = nullptr;
    MetricsRepository* m_metricsRepository = nullptr;
};
