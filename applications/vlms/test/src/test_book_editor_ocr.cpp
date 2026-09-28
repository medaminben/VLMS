#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/catalog/BookEditorDialog.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Ocr/Ocr.h>

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMenu>
#include <QToolButton>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

using namespace VLMS;
using namespace Test;

class test_ui_BookEditorOcr : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository =
            std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
    }

    void TearDown() override
    {
        m_repository.reset();
        m_db.reset();
    }

    [[nodiscard]] static QToolButton* ocrButton(BookEditorDialog& dialog)
    {
        QToolButton* found = nullptr;
        for (QToolButton* button : dialog.findChildren<QToolButton*>()) {
            if (button->menu() != nullptr) {
                if (found != nullptr) {
                    return nullptr;
                }
                found = button;
            }
        }
        return found;
    }

    [[nodiscard]] static QStringList checkedLanguages(BookEditorDialog& dialog)
    {
        QStringList codes;
        QToolButton* button = ocrButton(dialog);
        if (button == nullptr || button->menu() == nullptr) {
            return codes;
        }
        for (QAction* action : button->menu()->actions()) {
            if (action->isChecked()) {
                codes.append(action->data().toString());
            }
        }
        codes.sort();
        return codes;
    }

    [[nodiscard]] static QComboBox* languageCombo(BookEditorDialog& dialog)
    {
        return dialog.findChild<QComboBox*>(QStringLiteral("bookLanguageCombo"));
    }

    [[nodiscard]] std::unique_ptr<BookEditorDialog> editorFor(const QString& language)
    {
        BookSeed seed = uniqueBookSeed(++m_seedIndex);
        seed.language = language.toStdString();
        const qint64 id = seedBook(*m_db, seed);
        const auto book = m_repository->getBook(id);
        if (!book) {
            return nullptr;
        }
        return std::make_unique<BookEditorDialog>(*m_repository, book.value());
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_repository;
    int m_seedIndex = 0;
};

TEST_F(test_ui_BookEditorOcr, TheOcrButtonCarriesAMenuOfEveryInstalledLanguage)
{
    auto dialog = editorFor(QStringLiteral("ar"));
    ASSERT_NE(dialog, nullptr);
    QToolButton* button = ocrButton(*dialog);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->popupMode(), QToolButton::MenuButtonPopup);

    const std::vector<std::string> installed = Ocr::availableLanguages();
    const QList<QAction*> actions = button->menu()->actions();
    EXPECT_EQ(static_cast<size_t>(actions.size()), installed.size());

    QStringList offered;
    for (QAction* action : actions) {
        EXPECT_TRUE(action->isCheckable());
        EXPECT_FALSE(action->text().isEmpty());
        offered.append(action->data().toString());
    }
    offered.sort();

    QStringList expected;
    for (const std::string& code : installed) {
        expected.append(QString::fromStdString(code));
    }
    expected.sort();
    EXPECT_EQ(offered, expected);
}

TEST_F(test_ui_BookEditorOcr, TheOcrButtonIsEnabledExactlyWhenAnEngineIsInstalled)
{
    auto dialog = editorFor(QStringLiteral("ar"));
    ASSERT_NE(dialog, nullptr);
    QToolButton* button = ocrButton(*dialog);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->isEnabled(), Ocr::isAvailable());
}

TEST_F(test_ui_BookEditorOcr, OcrPreselectsTheBooksOwnLanguage)
{
    if (!Ocr::isAvailable()) {
        GTEST_SKIP() << "no OCR languages installed";
    }
    for (const auto& [bookLanguage, expected] :
         {std::pair{QStringLiteral("ar"), std::string("ara")},
          std::pair{QStringLiteral("fr"), std::string("fra")},
          std::pair{QStringLiteral("en"), std::string("eng")}}) {
        const std::vector<std::string> installed = Ocr::availableLanguages();
        if (std::find(installed.begin(), installed.end(), expected) == installed.end()) {
            continue;
        }
        auto dialog = editorFor(bookLanguage);
        ASSERT_NE(dialog, nullptr);
        EXPECT_EQ(checkedLanguages(*dialog), QStringList{QString::fromStdString(expected)});
    }
}

TEST_F(test_ui_BookEditorOcr, ChangingTheBooksLanguageRetargetsOcr)
{
    const std::vector<std::string> installed = Ocr::availableLanguages();
    const bool haveBoth = std::find(installed.begin(), installed.end(), "ara") != installed.end()
        && std::find(installed.begin(), installed.end(), "fra") != installed.end();
    if (!haveBoth) {
        GTEST_SKIP() << "needs both ara and fra installed";
    }
    auto dialog = editorFor(QStringLiteral("ar"));
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(checkedLanguages(*dialog), QStringList{QStringLiteral("ara")});
    QComboBox* combo = languageCombo(*dialog);
    ASSERT_NE(combo, nullptr);
    const int frenchIndex = combo->findData(QStringLiteral("fr"));
    ASSERT_GE(frenchIndex, 0);
    combo->setCurrentIndex(frenchIndex);
    EXPECT_EQ(checkedLanguages(*dialog), QStringList{QStringLiteral("fra")});
}

TEST_F(test_ui_BookEditorOcr, ALibrariansChoiceOutranksTheBooksLanguage)
{
    const std::vector<std::string> installed = Ocr::availableLanguages();
    const bool haveBoth = std::find(installed.begin(), installed.end(), "ara") != installed.end()
        && std::find(installed.begin(), installed.end(), "fra") != installed.end();
    if (!haveBoth) {
        GTEST_SKIP() << "needs both ara and fra installed";
    }
    auto dialog = editorFor(QStringLiteral("ar"));
    ASSERT_NE(dialog, nullptr);
    QToolButton* button = ocrButton(*dialog);
    ASSERT_NE(button, nullptr);
    for (QAction* action : button->menu()->actions()) {
        if (action->data().toString() == QStringLiteral("fra")) {
            action->setChecked(true);
        }
    }
    EXPECT_EQ(checkedLanguages(*dialog),
              (QStringList{QStringLiteral("ara"), QStringLiteral("fra")}));

    QComboBox* combo = languageCombo(*dialog);
    ASSERT_NE(combo, nullptr);
    const int englishIndex = combo->findData(QStringLiteral("en"));
    ASSERT_GE(englishIndex, 0);
    combo->setCurrentIndex(englishIndex);
    EXPECT_EQ(checkedLanguages(*dialog),
              (QStringList{QStringLiteral("ara"), QStringLiteral("fra")}));
}

TEST_F(test_ui_BookEditorOcr, TheLastCheckedLanguageCannotBeTurnedOff)
{
    if (!Ocr::isAvailable()) {
        GTEST_SKIP() << "no OCR languages installed";
    }
    auto dialog = editorFor(QStringLiteral("ar"));
    ASSERT_NE(dialog, nullptr);
    QToolButton* button = ocrButton(*dialog);
    ASSERT_NE(button, nullptr);
    for (QAction* action : button->menu()->actions()) {
        action->setChecked(false);
    }
    EXPECT_FALSE(checkedLanguages(*dialog).isEmpty())
        << "the menu allowed every language to be switched off";
}

TEST_F(test_ui_BookEditorOcr, OpeningAndClosingTheEditorNeverBlocks)
{
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 5; ++i) {
        auto dialog = editorFor(QStringLiteral("ar"));
        ASSERT_NE(dialog, nullptr);
        dialog->show();
        QCoreApplication::processEvents();
        dialog->close();
    }
    EXPECT_LT(timer.elapsed(), 10000) << "five open/close cycles took " << timer.elapsed() << " ms";
}
