#include "TestDatabase.h"
#include "TestSeed.h"
#include "WorkflowTestSupport.h"

#include "ui/catalog/LocalNumberDelegate.h"
#include "ui/presentation/BookListSpec.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CatalogTypes.h>
#include <VLMS/Repositories/CirculationRepository.h>

#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

namespace {

QString writePng(const QTemporaryDir& dir, const QString& name)
{
    const QString path = dir.filePath(name);
    QImage image(64, 64, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    EXPECT_TRUE(image.save(path, "PNG"));
    return path;
}

}  // namespace

class BookListSpecTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { Core::Locale::setCode(Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        Core::Locale::setCode("en");
        ASSERT_TRUE(m_dir.isValid());
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(),
                                                                      m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_harness = std::make_unique<BookWorkflowHarness>(*m_catalog, *m_circulation);
        m_spec = std::make_unique<BookListSpec>(*m_catalog, *m_harness->books);
    }

    void TearDown() override
    {
        m_spec.reset();
        m_harness.reset();
        m_circulation.reset();
        m_catalog.reset();
        m_db.reset();
        Core::Locale::setCode("en");
    }

    QTemporaryDir m_dir;
    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<BookWorkflowHarness> m_harness;
    std::unique_ptr<BookListSpec> m_spec;
};

TEST_F(BookListSpecTest, MapsFacetsSortAndPagingIntoBookQuery)
{
    const std::int64_t fictionId = seedCategory(*m_db, "FIC", "Fiction");
    ASSERT_GT(fictionId, 0);
    const std::int64_t historyId = seedCategory(*m_db, "HIS", "History");
    ASSERT_GT(historyId, 0);

    BookSeed arabic = uniqueBookSeed(1);
    arabic.title = "Zebra";
    arabic.language = "ar";
    arabic.categoryId = fictionId;
    ASSERT_GT(seedBook(*m_db, arabic), 0);

    BookSeed french = uniqueBookSeed(2);
    french.title = "Apple";
    french.language = "fr";
    french.categoryId = historyId;
    ASSERT_GT(seedBook(*m_db, french), 0);

    BookSeed english = uniqueBookSeed(3);
    english.title = "Mango";
    english.language = "en";
    english.categoryId = historyId;
    ASSERT_GT(seedBook(*m_db, english), 0);

    QueryState state;
    state.facets.insert(QStringLiteral("language"), {QStringLiteral("fr")});
    state.facets.insert(QStringLiteral("category"), {QStringLiteral("HIS")});
    state.sortKey = QString::fromLatin1(Repositories::BookSort::kTitle);
    state.ascending = true;
    state.pageSize = 10;
    state.page = 1;

    ASSERT_TRUE(m_spec->load(state));
    EXPECT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->totalCount(), 1);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::DisplayRole).toString(), QStringLiteral("Apple"));

    state = QueryState{};
    state.facets.insert(QStringLiteral("cover"), {QStringLiteral("without")});
    state.sortKey = QString::fromLatin1(Repositories::BookSort::kTitle);
    state.ascending = true;
    state.pageSize = 1;
    state.page = 2;
    ASSERT_TRUE(m_spec->load(state));
    EXPECT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->totalCount(), 3);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::DisplayRole).toString(), QStringLiteral("Mango"));
}

TEST_F(BookListSpecTest, DisplayRowsUsePrebuiltStrings)
{
    const std::int64_t categoryId = seedCategory(*m_db, "SCI", "Science");
    ASSERT_GT(categoryId, 0);

    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Optics";
    seed.authorName = "Ibn al-Haytham";
    seed.categoryId = categoryId;
    seed.language = "ar";
    seed.initialCopyCount = 2;
    const qint64 id = seedBook(*m_db, seed);
    ASSERT_GT(id, 0);
    const auto stored = m_catalog->getBook(id);
    ASSERT_TRUE(stored);

    QueryState state;
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);

    EXPECT_EQ(m_spec->idAt(0), id);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::UserRole).toLongLong(), id);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::DisplayRole).toString(), QStringLiteral("Optics"));
    EXPECT_EQ(m_spec->cell(0, 1, Qt::DisplayRole).toString(), QStringLiteral("Ibn al-Haytham"));
    EXPECT_EQ(m_spec->cell(0, 2, Qt::DisplayRole).toString(), QStringLiteral("Science"));

    QStringList localNumbers;
    localNumbers.reserve(static_cast<int>(stored->localIds.size()));
    for (const std::string& localId : stored->localIds) {
        localNumbers.append(QString::fromStdString(localId));
    }
    EXPECT_EQ(m_spec->cell(0, 3, Qt::DisplayRole).toString(),
              LocalNumberDelegate::cellText(localNumbers));
    EXPECT_EQ(m_spec->cell(0, 3, LocalNumberDelegate::kNumbersRole).toStringList(), localNumbers);
    EXPECT_EQ(m_spec->cell(0, 4, Qt::DisplayRole).toString(),
              QString::number(stored->totalCopies));
    EXPECT_EQ(m_spec->cell(0, 5, Qt::DisplayRole).toString(),
              QString::number(stored->availableCopies));
}

TEST_F(BookListSpecTest, RetranslateDoesNotQuery)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Stable Title";
    seed.language = "ar";
    ASSERT_GT(seedBook(*m_db, seed), 0);

    QueryState state;
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);
    const QString titleBefore = m_spec->cell(0, 0, Qt::DisplayRole).toString();
    EXPECT_EQ(titleBefore, QStringLiteral("Stable Title"));

    Core::Locale::setCode("en");
    const QString arabicBefore =
        QString::fromStdString(Core::Strings::bookLanguageLabel("ar"));
    Core::Locale::setCode("fr");
    const QString arabicAfter =
        QString::fromStdString(Core::Strings::bookLanguageLabel("ar"));
    ASSERT_NE(arabicBefore, arabicAfter);

    const int rowsBefore = m_spec->rowCount();
    const int totalBefore = m_spec->totalCount();
    m_spec->retranslate();

    EXPECT_EQ(m_spec->rowCount(), rowsBefore);
    EXPECT_EQ(m_spec->totalCount(), totalBefore);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::DisplayRole).toString(), titleBefore);
}

TEST_F(BookListSpecTest, RankOfMatchesRepository)
{
    BookSeed a = uniqueBookSeed(1);
    a.title = "Aaa";
    ASSERT_GT(seedBook(*m_db, a), 0);
    BookSeed b = uniqueBookSeed(2);
    b.title = "Bbb";
    const qint64 idB = seedBook(*m_db, b);
    ASSERT_GT(idB, 0);
    BookSeed c = uniqueBookSeed(3);
    c.title = "Ccc";
    ASSERT_GT(seedBook(*m_db, c), 0);

    QueryState state;
    state.sortKey = QString::fromLatin1(Repositories::BookSort::kTitle);
    state.ascending = true;

    Repositories::BookQuery query;
    query.sortColumn = Repositories::BookSort::kTitle;
    query.sortAscending = true;
    const auto expected = m_catalog->rankOfBook(idB, query);
    ASSERT_TRUE(expected);

    const auto actual = m_spec->rankOf(idB, state);
    ASSERT_TRUE(actual);
    EXPECT_EQ(actual.value(), expected.value());
}

TEST_F(BookListSpecTest, PreviewLoadsCoverPathAndFields)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Covered";
    seed.authorName = "Author";
    seed.description = "Keep me scrollable";
    seed.language = "fr";
    const qint64 id = seedBook(*m_db, seed);
    ASSERT_GT(id, 0);

    const QString coverPath = writePng(m_dir, QStringLiteral("cover.png"));
    ASSERT_TRUE(m_catalog->setCoverImage(id, coverPath.toStdString()));

    const PreviewContent preview = m_spec->preview(id);
    EXPECT_FALSE(preview.imagePath.isEmpty());
    EXPECT_TRUE(QFile::exists(preview.imagePath));
    EXPECT_TRUE(preview.imagePath.contains(QStringLiteral("cover")));

    ASSERT_FALSE(preview.fields.isEmpty());
    EXPECT_EQ(preview.fields.first().labelKey, QStringLiteral("book.field.description"));
    EXPECT_TRUE(preview.fields.first().scrollable);
    EXPECT_EQ(preview.fields.first().value, QStringLiteral("Keep me scrollable"));

    bool sawTitle = false;
    bool sawLanguage = false;
    for (const PreviewField& field : preview.fields) {
        if (field.labelKey == QStringLiteral("book.field.title")) {
            sawTitle = true;
            EXPECT_EQ(field.value, QStringLiteral("Covered"));
        }
        if (field.labelKey == QStringLiteral("book.field.language")) {
            sawLanguage = true;
            EXPECT_EQ(field.value,
                      QString::fromStdString(Core::Strings::bookLanguageLabel("fr")));
        }
    }
    EXPECT_TRUE(sawTitle);
    EXPECT_TRUE(sawLanguage);
}

TEST_F(BookListSpecTest, DependsOnBooksAndCopies)
{
    EXPECT_EQ(m_spec->id(), QStringLiteral("books"));
    EXPECT_EQ(m_spec->dependsOn(), Entity::Books | Entity::Copies);

    const QList<ColumnSpec> columns = m_spec->columns();
    ASSERT_EQ(columns.size(), 6);
    EXPECT_EQ(columns.at(0).key, QStringLiteral("title"));
    EXPECT_EQ(columns.at(0).titleKey, QStringLiteral("catalog.col.title"));
    EXPECT_EQ(columns.at(0).width, 320);
    EXPECT_EQ(columns.at(0).sortKey, QString::fromLatin1(Repositories::BookSort::kTitle));
    EXPECT_EQ(columns.at(1).key, QStringLiteral("author"));
    EXPECT_EQ(columns.at(1).width, 200);
    EXPECT_EQ(columns.at(1).sortKey, QString::fromLatin1(Repositories::BookSort::kAuthor));
    EXPECT_EQ(columns.at(2).key, QStringLiteral("category"));
    EXPECT_EQ(columns.at(2).width, 130);
    EXPECT_EQ(columns.at(2).sortKey, QString::fromLatin1(Repositories::BookSort::kCategory));
    EXPECT_EQ(columns.at(3).key, QStringLiteral("localNumber"));
    EXPECT_EQ(columns.at(3).width, 110);
    EXPECT_EQ(columns.at(3).sortKey, QString::fromLatin1(Repositories::BookSort::kLocalNumber));
    EXPECT_EQ(columns.at(4).key, QStringLiteral("copies"));
    EXPECT_EQ(columns.at(4).width, 70);
    EXPECT_EQ(columns.at(4).sortKey, QString::fromLatin1(Repositories::BookSort::kCopies));
    EXPECT_EQ(columns.at(5).key, QStringLiteral("available"));
    EXPECT_EQ(columns.at(5).width, 70);
    EXPECT_EQ(columns.at(5).sortKey, QString::fromLatin1(Repositories::BookSort::kAvailable));

    const QList<FacetSpec> facets = m_spec->facets();
    QStringList keys;
    for (const FacetSpec& facet : facets) {
        keys.append(facet.key);
    }
    EXPECT_TRUE(keys.contains(QStringLiteral("language")));
    EXPECT_TRUE(keys.contains(QStringLiteral("category")));
    EXPECT_TRUE(keys.contains(QStringLiteral("cover")));

    const QList<PageAction> actions = m_spec->actions();
    QStringList actionIds;
    for (const PageAction& action : actions) {
        actionIds.append(action.id);
    }
    EXPECT_TRUE(actionIds.contains(QStringLiteral("loans")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("add")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("edit")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("archive")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("tickAll")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("refresh")));
    EXPECT_FALSE(actionIds.contains(QStringLiteral("categories")));
}
