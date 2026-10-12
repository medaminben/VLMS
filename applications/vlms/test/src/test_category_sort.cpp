#include "ui/views/CategoryManagerDialog.h"
#include "ui/workflows/CategoryEditor.h"
#include <VLMS/Repositories/CatalogRepository.h>
#include "TestDatabase.h"
#include "TestSeed.h"
#include <QHeaderView>
#include <QTableWidget>
#include <gtest/gtest.h>

using namespace VLMS;

namespace {

/// Lists the repository's categories; the sort test never writes.
class ListingEditor final : public CategoryEditor {
public:
    explicit ListingEditor(Repositories::CatalogRepository& repository) : m_repository(repository) {}
    std::vector<Repositories::CategoryRecord> categories() const override
    {
        return m_repository.listAllCategories().value();
    }
    Core::Status create(QWidget*, const QString&, const QString&) override { return Core::Status::ok(); }
    Core::Status update(QWidget*, qint64, const QString&, const QString&) override
    {
        return Core::Status::ok();
    }
    Core::Status remove(QWidget*, qint64) override { return Core::Status::ok(); }

private:
    Repositories::CatalogRepository& m_repository;
};

}  // namespace

TEST(test_ui_CategorySort, HeaderClickSortsByCode)
{
    ::Test::TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();
    Repositories::CatalogRepository repository(db.session(), db.resourcesDirectory());
    ASSERT_GT(::Test::seedCategory(db, "Z9", "Zebra"), 0);
    ASSERT_GT(::Test::seedCategory(db, "A1", "Apple"), 0);

    ListingEditor editor(repository);
    CategoryManagerDialog dialog(editor);
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("A1"));
    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("Z9"));
}
