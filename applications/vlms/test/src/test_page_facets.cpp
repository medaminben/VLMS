#include "TestDatabase.h"
#include "TestSeed.h"

#include "QtBridge.h"
#include "ui/services/Theme.h"
#include "ui/views/ListPage.h"
#include "ui/views/BookFacetFilters.h"
#include "ui/views/MemberFacetFilters.h"
#include "ui/presentation/ArchivedBookSpec.h"
#include "ui/presentation/ArchivedCopySpec.h"
#include "ui/presentation/ArchivedLoanSpec.h"
#include "ui/presentation/ArchivedMemberSpec.h"
#include "ui/presentation/LoanListSpec.h"
#include "ui/shell/Navigator.h"
#include "WorkflowTestSupport.h"

#include <VLMS/Repositories/ArchiveTypes.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QAbstractItemView>
#include <QHash>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <QTableWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

using namespace VLMS;
using namespace Test;

namespace {

bool pick(QListWidget* list, const QString& code)
{
    if (list == nullptr) {
        return false;
    }
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        if (item->data(Qt::UserRole).toString() == code) {
            list->setCurrentItem(item);
            item->setSelected(true);
            QApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            return true;
        }
    }
    return false;
}

template <typename T>
T* named(QWidget* root, const char* name)
{
    return root->findChild<T*>(QString::fromLatin1(name));
}

ListPage::Config circulationConfig()
{
    ListPage::Config cfg;
    cfg.titleKey = QStringLiteral("page.circulation.title");
    cfg.bodyKey = QStringLiteral("page.circulation.body");
    cfg.searchPlaceholderKey = QStringLiteral("circulation.searchPlaceholder");
    cfg.imageObjectName = QStringLiteral("bookCover");
    cfg.imageBounds = bookCoverPreviewBounds();
    cfg.secondImageObjectName = QStringLiteral("loanMemberPhoto");
    cfg.secondImageBounds = bookCoverPreviewBounds();
    return cfg;
}

ListPage::Config archiveConfig()
{
    ListPage::Config cfg;
    cfg.titleKey = QStringLiteral("page.archive.title");
    cfg.bodyKey = QStringLiteral("page.archive.body");
    cfg.searchPlaceholderKey = QStringLiteral("archive.searchPlaceholder");
    cfg.imageObjectName = QStringLiteral("archiveImage");
    cfg.imageBounds = bookCoverPreviewBounds();
    cfg.secondImageObjectName = QStringLiteral("archiveMemberPhoto");
    cfg.secondImageBounds = bookCoverPreviewBounds();
    return cfg;
}

QStringList coverFacetTokens(Repositories::CoverFilter filter)
{
    switch (filter) {
    case Repositories::CoverFilter::WithCover:
        return {QStringLiteral("with")};
    case Repositories::CoverFilter::WithoutCover:
        return {QStringLiteral("without")};
    case Repositories::CoverFilter::All:
        break;
    }
    return {QStringLiteral("all")};
}

struct ArchiveTypeEntry {
    ArchiveType type;
    const char* code;
};

constexpr ArchiveTypeEntry kArchiveTypes[] = {
    {ArchiveType::Members, "members"},
    {ArchiveType::Books, "books"},
    {ArchiveType::Copies, "copies"},
    {ArchiveType::Loans, "loans"},
};

struct CirculationListHarness {
    LoanWorkflowHarness workflows;
    std::unique_ptr<LoanListSpec> spec;
    std::unique_ptr<ListPage> page;
    MemberFacetFilters* memberFilters = nullptr;

    explicit CirculationListHarness(Repositories::CirculationRepository& circulation,
                                    Repositories::CatalogRepository& catalog,
                                    Repositories::MemberRepository& members)
        : workflows(circulation)
    {
        spec = std::make_unique<LoanListSpec>(circulation, catalog, members, *workflows.loans);
        page = std::make_unique<ListPage>(spec.get(), workflows.events, circulationConfig());
        memberFilters = new MemberFacetFilters(members, Repositories::ArchiveScope::Any,
                                               page->filterColumn());
        memberFilters->useLoanYears([&circulation]() {
            return circulation.listLoanYears(Repositories::ArchiveScope::Live);
        });
        memberFilters->refresh();
        page->addFilterWidget(memberFilters, 1);
        QObject::connect(memberFilters, &MemberFacetFilters::changed, page.get(), [this]() {
            const Repositories::MemberFacets facets = memberFilters->facets();
            QHash<QString, QStringList> mapped;
            mapped.insert(QStringLiteral("status"), qsl(facets.statuses));
            mapped.insert(QStringLiteral("sex"), qsl(facets.sexes));
            mapped.insert(QStringLiteral("year"), memberFilters->loanYears());
            mapped.insert(QStringLiteral("ageGroup"), qsl(facets.ageGroups));
            mapped.insert(QStringLiteral("city"), qsl(facets.cities));
            page->setFacets(mapped);
        });
    }
};

struct ArchiveListHarness {
    ArchiveWorkflowHarness workflows;
    Repositories::CirculationRepository& circulation;
    std::unique_ptr<ArchivedMemberSpec> members;
    std::unique_ptr<ArchivedBookSpec> books;
    std::unique_ptr<ArchivedCopySpec> copies;
    std::unique_ptr<ArchivedLoanSpec> loans;
    std::unique_ptr<ListPage> page;
    QListWidget* typeList = nullptr;
    MemberFacetFilters* memberFilters = nullptr;
    BookFacetFilters* bookFilters = nullptr;
    ArchiveType type = ArchiveType::Members;

    ArchiveListHarness(Repositories::MemberRepository& memberRepo,
                       Repositories::CatalogRepository& catalog,
                       Repositories::CirculationRepository& circulationRepo)
        : workflows(memberRepo, catalog, circulationRepo),
          circulation(circulationRepo)
    {
        members = std::make_unique<ArchivedMemberSpec>(memberRepo, *workflows.archive);
        books = std::make_unique<ArchivedBookSpec>(catalog, circulationRepo, *workflows.archive);
        copies = std::make_unique<ArchivedCopySpec>(catalog, *workflows.archive);
        loans = std::make_unique<ArchivedLoanSpec>(circulationRepo, catalog, memberRepo,
                                                   *workflows.archive);
        page = std::make_unique<ListPage>(members.get(), workflows.events, archiveConfig());

        typeList = new QListWidget(page->filterColumn());
        typeList->setObjectName(QStringLiteral("archiveType"));
        typeList->setSelectionMode(QAbstractItemView::SingleSelection);
        for (const ArchiveTypeEntry& entry : kArchiveTypes) {
            auto* item = new QListWidgetItem(typeList);
            item->setData(Qt::UserRole, QString::fromLatin1(entry.code));
        }
        typeList->item(0)->setSelected(true);
        page->addFilterWidget(typeList, 0);

        memberFilters = new MemberFacetFilters(memberRepo, Repositories::ArchiveScope::Archived,
                                               page->filterColumn());
        page->addFilterWidget(memberFilters, 1);
        bookFilters = new BookFacetFilters(catalog, Repositories::ArchiveScope::Archived,
                                           page->filterColumn());
        page->addFilterWidget(bookFilters, 1);

        QObject::connect(typeList, &QListWidget::itemSelectionChanged, page.get(), [this]() {
            onTypeChanged();
        });
        QObject::connect(memberFilters, &MemberFacetFilters::changed, page.get(), [this]() {
            applyFacets();
        });
        QObject::connect(bookFilters, &BookFacetFilters::changed, page.get(), [this]() {
            applyFacets();
        });
        applyFilterVisibility();
        applyFacets();
    }

    ListSpec* specFor(ArchiveType t) const
    {
        switch (t) {
        case ArchiveType::Members:
            return members.get();
        case ArchiveType::Books:
            return books.get();
        case ArchiveType::Copies:
            return copies.get();
        case ArchiveType::Loans:
            return loans.get();
        }
        return members.get();
    }

    void applyFilterVisibility()
    {
        const bool memberType = type == ArchiveType::Members || type == ArchiveType::Loans;
        memberFilters->setVisible(memberType);
        bookFilters->setVisible(!memberType);
        memberFilters->setValueScope(type == ArchiveType::Loans ? Repositories::ArchiveScope::Any
                                                                : Repositories::ArchiveScope::Archived);
        if (type == ArchiveType::Loans) {
            memberFilters->useLoanYears([this]() {
                return circulation.listLoanYears(Repositories::ArchiveScope::Archived);
            });
        } else {
            memberFilters->useLoanYears({});
        }
        bookFilters->setScope(type == ArchiveType::Copies ? Repositories::ArchiveScope::Any
                                                          : Repositories::ArchiveScope::Archived);
        if (memberType) {
            memberFilters->refresh();
        } else {
            bookFilters->refresh();
        }
    }

    void applyFacets()
    {
        QHash<QString, QStringList> mapped;
        const bool memberType = type == ArchiveType::Members || type == ArchiveType::Loans;
        if (memberType) {
            const Repositories::MemberFacets facets = memberFilters->facets();
            mapped.insert(QStringLiteral("status"), qsl(facets.statuses));
            mapped.insert(QStringLiteral("sex"), qsl(facets.sexes));
            mapped.insert(QStringLiteral("year"),
                          type == ArchiveType::Loans ? memberFilters->loanYears()
                                                     : qsl(facets.inscriptionYears));
            mapped.insert(QStringLiteral("ageGroup"), qsl(facets.ageGroups));
            mapped.insert(QStringLiteral("city"), qsl(facets.cities));
        } else {
            mapped.insert(QStringLiteral("language"), bookFilters->languages());
            mapped.insert(QStringLiteral("category"), bookFilters->categoryCodes());
            mapped.insert(QStringLiteral("cover"), coverFacetTokens(bookFilters->coverFilter()));
        }
        page->setFacets(mapped);
    }

    void onTypeChanged()
    {
        const auto selected = typeList->selectedItems();
        if (selected.isEmpty()) {
            return;
        }
        type = kArchiveTypes[typeList->row(selected.first())].type;
        memberFilters->reset();
        bookFilters->reset();
        applyFilterVisibility();
        page->setSpec(specFor(type));
        applyFacets();
    }

    int rowCount() const
    {
        return page->table() != nullptr && page->table()->model() != nullptr
            ? page->table()->model()->rowCount()
            : 0;
    }

    qint64 idAt(int row) const
    {
        return page->table()->model()->index(row, 0).data(Qt::UserRole).toLongLong();
    }
};

}  // namespace

/// The Members page's filters on every page that lists members or loans, the
/// Catalogue's on the Archive's titles and copies, and the loan viewers' two
/// images.
class test_ui_PageFacets : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members = std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());

        MemberSeed male = uniqueMemberSeed(1);
        male.sex = Repositories::MemberSex::kMale;
        m_maleId = seedMember(*m_db, male);
        MemberSeed female = uniqueMemberSeed(2);
        female.sex = Repositories::MemberSex::kFemale;
        m_femaleId = seedMember(*m_db, female);
        ASSERT_GT(m_maleId, 0);
        ASSERT_GT(m_femaleId, 0);

        BookSeed arabic = uniqueBookSeed(1);
        arabic.language = "ar";
        m_arabicBookId = seedBook(*m_db, arabic);
        BookSeed french = uniqueBookSeed(2);
        french.language = "fr";
        m_frenchBookId = seedBook(*m_db, french);
        ASSERT_GT(m_arabicBookId, 0);
        ASSERT_GT(m_frenchBookId, 0);

        m_maleLoan = rawInsertLoan(*m_db, m_maleId, copyIdsOf(*m_db, m_arabicBookId).front(),
                                   "2026-09-01", "2026-09-15", "2026-09-10");
        m_femaleLoan = rawInsertLoan(*m_db, m_femaleId, copyIdsOf(*m_db, m_frenchBookId).front(),
                                     "2026-09-02", "2026-09-16", "2026-09-11");
        ASSERT_GT(m_maleLoan, 0);
        ASSERT_GT(m_femaleLoan, 0);
    }

    void TearDown() override
    {
        m_circulation.reset();
        m_catalog.reset();
        m_members.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::int64_t m_maleId = 0;
    std::int64_t m_femaleId = 0;
    std::int64_t m_arabicBookId = 0;
    std::int64_t m_frenchBookId = 0;
    std::int64_t m_maleLoan = 0;
    std::int64_t m_femaleLoan = 0;
};

TEST_F(test_ui_PageFacets, MemberFiltersCarryNoHeadings)
{
    MemberFacetFilters filters(*m_members, Repositories::ArchiveScope::Live);
    filters.refresh();
    EXPECT_TRUE(filters.findChildren<QLabel*>().isEmpty())
        << "each list's own All row names it";
}

TEST_F(test_ui_PageFacets, CirculationFiltersLoansByTheBorrowersSex)
{
    CirculationListHarness harness(*m_circulation, *m_catalog, *m_members);
    QTableView* table = harness.page->table();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 2);

    ASSERT_TRUE(pick(named<QListWidget>(harness.page.get(), "sexFilter"),
                     QString::fromLatin1(Repositories::MemberSex::kFemale)));
    ASSERT_EQ(table->model()->rowCount(), 1);
    EXPECT_EQ(table->model()->index(0, 0).data(Qt::UserRole).toLongLong(), m_femaleLoan);
}

TEST_F(test_ui_PageFacets, CirculationShowsTheCoverBesideTheBorrowersPhoto)
{
    CirculationListHarness harness(*m_circulation, *m_catalog, *m_members);
    auto* cover = named<QLabel>(harness.page.get(), "bookCover");
    auto* photo = named<QLabel>(harness.page.get(), "loanMemberPhoto");
    ASSERT_NE(cover, nullptr);
    ASSERT_NE(photo, nullptr);
    EXPECT_FALSE(photo->isHidden());
    EXPECT_FALSE(cover->pixmap().isNull()) << "the placeholder cover when a title has none";
    EXPECT_EQ(photo->text(), VLMS::T("members.noPhoto"));
    EXPECT_EQ(photo->size(), cover->size()) << "the two boxes are one size";
}

TEST_F(test_ui_PageFacets, ArchiveShowsTheFiltersOfTheTypeOnScreen)
{
    ArchiveListHarness harness(*m_members, *m_catalog, *m_circulation);
    auto* types = named<QListWidget>(harness.page.get(), "archiveType");
    auto* memberFilters = harness.page->findChild<VLMS::MemberFacetFilters*>();
    auto* bookFilters = harness.page->findChild<VLMS::BookFacetFilters*>();
    auto* photo = named<QLabel>(harness.page.get(), "archiveMemberPhoto");
    ASSERT_NE(types, nullptr);
    ASSERT_NE(memberFilters, nullptr);
    ASSERT_NE(bookFilters, nullptr);
    ASSERT_NE(photo, nullptr);

    EXPECT_FALSE(memberFilters->isHidden());
    EXPECT_TRUE(bookFilters->isHidden());
    // ListPage keeps both image slots when dual-image is configured; the
    // second stays empty for members/books/copies.
    EXPECT_FALSE(photo->isHidden());

    for (const char* code : {"books", "copies"}) {
        ASSERT_TRUE(pick(types, QString::fromLatin1(code)));
        EXPECT_TRUE(memberFilters->isHidden()) << code;
        EXPECT_FALSE(bookFilters->isHidden()) << code;
        EXPECT_FALSE(photo->isHidden()) << code;
    }

    ASSERT_TRUE(pick(types, QStringLiteral("loans")));
    EXPECT_FALSE(memberFilters->isHidden());
    EXPECT_TRUE(bookFilters->isHidden());
    EXPECT_FALSE(photo->isHidden()) << "a loan shows the cover and the borrower";
    EXPECT_EQ(photo->size(), named<QLabel>(harness.page.get(), "archiveImage")->size());
}

TEST_F(test_ui_PageFacets, ArchiveFiltersCopiesByTheirTitlesLanguage)
{
    ASSERT_TRUE(m_catalog->saveCopies(m_arabicBookId, {}));
    ASSERT_TRUE(m_catalog->saveCopies(m_frenchBookId, {}));

    ArchiveListHarness harness(*m_members, *m_catalog, *m_circulation);
    ASSERT_TRUE(pick(named<QListWidget>(harness.page.get(), "archiveType"), QStringLiteral("copies")));
    ASSERT_EQ(harness.rowCount(), 2);

    auto* bookFilters = harness.page->findChild<VLMS::BookFacetFilters*>();
    ASSERT_NE(bookFilters, nullptr);
    ASSERT_TRUE(pick(named<QListWidget>(bookFilters, "languageFilter"), QStringLiteral("fr")));
    ASSERT_EQ(harness.rowCount(), 1);
}

TEST_F(test_ui_PageFacets, ArchiveFiltersLoansByTheBorrowersSex)
{
    ASSERT_TRUE(m_circulation->archiveLoan(m_maleLoan));
    ASSERT_TRUE(m_circulation->archiveLoan(m_femaleLoan));

    ArchiveListHarness harness(*m_members, *m_catalog, *m_circulation);
    ASSERT_TRUE(pick(named<QListWidget>(harness.page.get(), "archiveType"), QStringLiteral("loans")));
    ASSERT_EQ(harness.rowCount(), 2);

    ASSERT_TRUE(pick(named<QListWidget>(harness.page.get(), "sexFilter"),
                     QString::fromLatin1(Repositories::MemberSex::kMale)));
    ASSERT_EQ(harness.rowCount(), 1);
    EXPECT_EQ(harness.idAt(0), m_maleLoan);
}

TEST(test_ui_ManualButtonIcon, RightToLeftDrawsTheArabicQuestionMark)
{
    using VLMS::ThemeMode;
    const QImage ltr = VLMS::manualButtonIcon(ThemeMode::Light, 36, 1.0, Qt::LeftToRight)
                           .pixmap(36, 36).toImage();
    const QImage rtl = VLMS::manualButtonIcon(ThemeMode::Light, 36, 1.0, Qt::RightToLeft)
                           .pixmap(36, 36).toImage();
    ASSERT_FALSE(ltr.isNull());
    ASSERT_FALSE(rtl.isNull());
    EXPECT_NE(ltr, rtl) << "right to left must not draw the Latin ?";
}

TEST_F(test_ui_PageFacets, ArchivedLoanDetailsShowTheMembershipNumberAndNotes)
{
    ASSERT_TRUE(m_db->exec("UPDATE loans SET notes = 'Returned with a torn cover' WHERE id = "
                           + std::to_string(m_femaleLoan)));
    ASSERT_TRUE(m_circulation->archiveLoan(m_femaleLoan));
    const auto loan = m_circulation->getLoan(m_femaleLoan);
    ASSERT_TRUE(loan.has_value());

    ArchiveListHarness harness(*m_members, *m_catalog, *m_circulation);
    ASSERT_TRUE(pick(named<QListWidget>(harness.page.get(), "archiveType"), QStringLiteral("loans")));
    ASSERT_EQ(harness.rowCount(), 1);
    harness.page->table()->selectRow(0);
    QApplication::processEvents();

    QStringList texts;
    for (const QLabel* label : harness.page->findChildren<QLabel*>()) {
        texts.append(label->text());
    }
    const QString numberLabel = VLMS::T("member.field.number") + QStringLiteral(":");
    const QString notesLabel = VLMS::T("loan.field.notes") + QStringLiteral(":");
    EXPECT_TRUE(texts.contains(numberLabel));
    EXPECT_TRUE(texts.contains(VLMS::qs(loan->membershipNumber)));
    EXPECT_TRUE(texts.contains(notesLabel));
    int notesLabels = 0;
    for (const QString& text : texts) {
        notesLabels += text == notesLabel ? 1 : 0;
    }
    EXPECT_EQ(notesLabels, 1);
    const auto boxes = harness.page->findChildren<QPlainTextEdit*>(QStringLiteral("bookDetailValue"));
    ASSERT_EQ(boxes.size(), 1);
    EXPECT_EQ(boxes.front()->toPlainText(), QStringLiteral("Returned with a torn cover"));
}

namespace {

QPushButton* archiveLoansButton(QWidget* page)
{
    for (QPushButton* button : page->findChildren<QPushButton*>()) {
        if (button->text() == VLMS::T("archive.loans")) {
            return button;
        }
    }
    return nullptr;
}

}  // namespace

TEST_F(test_ui_PageFacets, ArchiveLoansButtonFollowsTheTypeAndTheHistory)
{
    ASSERT_TRUE(m_members->archiveMember(m_maleId));
    // A title nobody has borrowed, archived with its one copy.
    const std::int64_t unread = seedBook(*m_db, uniqueBookSeed(3));
    ASSERT_TRUE(m_catalog->saveCopies(unread, {}));

    ArchiveListHarness harness(*m_members, *m_catalog, *m_circulation);
    auto* types = named<QListWidget>(harness.page.get(), "archiveType");
    harness.page->table()->selectRow(0);
    QApplication::processEvents();
    QPushButton* loans = archiveLoansButton(harness.page.get());
    ASSERT_NE(loans, nullptr);

    EXPECT_FALSE(loans->isHidden());
    EXPECT_TRUE(loans->isEnabled()) << "the archived member borrowed once";

    ASSERT_TRUE(pick(types, QStringLiteral("copies")));
    harness.page->table()->selectRow(0);
    QApplication::processEvents();
    loans = archiveLoansButton(harness.page.get());
    ASSERT_NE(loans, nullptr);
    EXPECT_FALSE(loans->isHidden());
    EXPECT_FALSE(loans->isEnabled()) << "that copy was never lent";

    ASSERT_TRUE(pick(types, QStringLiteral("loans")));
    loans = archiveLoansButton(harness.page.get());
    EXPECT_EQ(loans, nullptr) << "a loan is its own history";
}

TEST_F(test_ui_PageFacets, ArchiveLoansButtonOpensTheMembersHistory)
{
    ASSERT_TRUE(m_members->archiveMember(m_maleId));
    ArchiveListHarness harness(*m_members, *m_catalog, *m_circulation);
    harness.page->table()->selectRow(0);
    QApplication::processEvents();
    QPushButton* loans = archiveLoansButton(harness.page.get());
    ASSERT_NE(loans, nullptr);
    ASSERT_TRUE(loans->isEnabled());

    int rows = -1;
    QString title;
    QTimer::singleShot(0, [&rows, &title]() {
        QWidget* modal = nullptr;
        for (int attempt = 0; attempt < 100 && modal == nullptr; ++attempt) {
            modal = QApplication::activeModalWidget();
            if (modal == nullptr) {
                QTest::qWait(10);
            }
        }
        if (modal == nullptr) {
            return;
        }
        title = modal->windowTitle();
        if (auto* table = modal->findChild<QTableView*>(QStringLiteral("loanHistoryTable"))) {
            rows = table->model()->rowCount();
        }
        modal->close();
    });
    loans->click();

    EXPECT_EQ(rows, 1) << "the one loan the member ever had";
    EXPECT_FALSE(title.isEmpty());
}

TEST_F(test_ui_PageFacets, CirculationYearIsTheYearOfTheLoan)
{
    const std::int64_t older = rawInsertLoan(*m_db, m_femaleId,
                                             copyIdsOf(*m_db, m_arabicBookId).front(),
                                             "2024-05-01", "2024-05-15", "2024-05-10");
    ASSERT_GT(older, 0);

    CirculationListHarness harness(*m_circulation, *m_catalog, *m_members);
    QTableView* table = harness.page->table();
    auto* years = named<QListWidget>(harness.page.get(), "yearFilter");
    ASSERT_NE(years, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 3);
    EXPECT_EQ(years->item(0)->text(), VLMS::T("circulation.allLoanYears"));

    // Nobody registered in 2024; a loan was made then.
    ASSERT_TRUE(pick(years, QStringLiteral("2024")));
    ASSERT_EQ(table->model()->rowCount(), 1);
    EXPECT_EQ(table->model()->index(0, 0).data(Qt::UserRole).toLongLong(), older);
}

TEST_F(test_ui_PageFacets, MembersYearIsStillTheYearOfRegistration)
{
    MemberFacetFilters filters(*m_members, Repositories::ArchiveScope::Live);
    filters.refresh();
    auto* years = named<QListWidget>(&filters, "yearFilter");
    ASSERT_NE(years, nullptr);
    EXPECT_EQ(years->item(0)->text(), VLMS::T("members.allYears"));
}
