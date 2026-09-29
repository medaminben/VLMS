#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/catalog/BookEditorDialog.h"
#include "ui/presentation/QtCatalogLookups.h"
#include "ui/services/OcrService.h"

#include <VLMS/Ocr/Ocr.h>
#include <VLMS/Repositories/CatalogRepository.h>

#include <QAbstractItemModel>
#include <QCompleter>
#include <QLineEdit>
#include <QMenu>
#include <QStringListModel>
#include <QToolButton>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace VLMS;
using namespace Test;

namespace {

struct BookEditorEnv {
    BookEditorEnv()
    {
        db = std::make_unique<TestDatabase>();
        if (!db->isValid()) {
            return;
        }
        catalog = std::make_unique<Repositories::CatalogRepository>(db->session(),
                                                                    db->resourcesDirectory());
        lookups = std::make_unique<QtCatalogLookups>(*catalog);
        ocr = std::make_unique<OcrService>();
    }

    [[nodiscard]] bool ok() const { return db && db->isValid() && lookups && ocr; }

    std::unique_ptr<TestDatabase> db;
    std::unique_ptr<Repositories::CatalogRepository> catalog;
    std::unique_ptr<QtCatalogLookups> lookups;
    std::unique_ptr<OcrService> ocr;
};

QToolButton* ocrMenuButton(BookEditorDialog& dialog)
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

bool completerModelContains(QCompleter* completer, const QString& needle)
{
    if (completer == nullptr || completer->model() == nullptr) {
        return false;
    }
    if (auto* list = qobject_cast<QStringListModel*>(completer->model())) {
        return list->stringList().contains(needle);
    }
    const QAbstractItemModel* model = completer->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->index(row, 0).data().toString() == needle) {
            return true;
        }
    }
    return false;
}

QLineEdit* lineEditWhoseCompleterContains(BookEditorDialog& dialog, const QString& needle)
{
    for (QLineEdit* edit : dialog.findChildren<QLineEdit*>()) {
        if (completerModelContains(edit->completer(), needle)) {
            return edit;
        }
    }
    return nullptr;
}

}  // namespace

// Checklist row 4 — BookEditorDialog.Ocr*
TEST(BookEditorDialog, OcrButtonOffersInstalledLanguageMenu)
{
    BookEditorEnv env;
    ASSERT_TRUE(env.ok()) << (env.db ? env.db->lastError() : std::string("no db"));

    BookEditorDialog dialog(*env.lookups, *env.ocr);
    QToolButton* button = ocrMenuButton(dialog);
    ASSERT_NE(button, nullptr);
    ASSERT_NE(button->menu(), nullptr);
    EXPECT_EQ(button->popupMode(), QToolButton::MenuButtonPopup);
    EXPECT_EQ(button->isEnabled(), Ocr::isAvailable());

    const std::vector<std::string> installed = Ocr::availableLanguages();
    EXPECT_EQ(static_cast<size_t>(button->menu()->actions().size()), installed.size());
}

// Checklist row 5 — BookEditorDialog.Lookups*
TEST(BookEditorDialog, LookupsPopulateAuthorCompleter)
{
    BookEditorEnv env;
    ASSERT_TRUE(env.ok()) << (env.db ? env.db->lastError() : std::string("no db"));

    BookSeed seed = uniqueBookSeed(1);
    seed.authorName = "Checklist Author";
    ASSERT_GT(seedBook(*env.db, seed), 0);

    BookEditorDialog dialog(*env.lookups, *env.ocr);
    ASSERT_NE(lineEditWhoseCompleterContains(dialog, QStringLiteral("Checklist Author")), nullptr)
        << "author field must complete from CatalogLookups";
}

TEST(BookEditorDialog, LookupsPopulatePublisherCompleter)
{
    BookEditorEnv env;
    ASSERT_TRUE(env.ok()) << (env.db ? env.db->lastError() : std::string("no db"));

    BookSeed seed = uniqueBookSeed(2);
    seed.publisherName = "Checklist Publisher";
    ASSERT_GT(seedBook(*env.db, seed), 0);

    BookEditorDialog dialog(*env.lookups, *env.ocr);
    ASSERT_NE(lineEditWhoseCompleterContains(dialog, QStringLiteral("Checklist Publisher")), nullptr)
        << "publisher field must complete from CatalogLookups";
}
