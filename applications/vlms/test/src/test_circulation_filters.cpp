#include "TestDatabase.h"
#include "TestSeed.h"

#include "QtBridge.h"
#include "ui/views/ListPage.h"
#include "ui/views/MemberFacetFilters.h"
#include "ui/presentation/LoanListSpec.h"
#include "WorkflowTestSupport.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/ArchiveTypes.h>
#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/LoanTypes.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QHash>
#include <QHeaderView>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableView>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

namespace {

bool isAllLoanFilter(const QString& code)
{
    return code.isEmpty() || code == QLatin1String(Repositories::LoanFilter::kAll);
}

bool selectFilterCode(QListWidget* list, const QString& code)
{
    if (list == nullptr) {
        return false;
    }
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        const QString itemCode = item->data(Qt::UserRole).toString();
        const bool matchesAll = isAllLoanFilter(code) && isAllLoanFilter(itemCode);
        if (itemCode == code || matchesAll) {
            list->setCurrentItem(item);
            item->setSelected(true);
            QApplication::processEvents();
            return true;
        }
    }
    return false;
}

QStringList filterCodesOnList(QListWidget* list)
{
    QStringList codes;
    if (list == nullptr) {
        return codes;
    }
    for (int row = 0; row < list->count(); ++row) {
        codes.append(list->item(row)->data(Qt::UserRole).toString());
    }
    return codes;
}

QStringList selectedFilterCodes(QListWidget* list)
{
    QStringList codes;
    if (list == nullptr) {
        return codes;
    }
    for (const QListWidgetItem* item : list->selectedItems()) {
        codes.append(item->data(Qt::UserRole).toString());
    }
    return codes;
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

qint64 tableId(QTableView* table, int row)
{
    return table->model()->index(row, 0).data(Qt::UserRole).toLongLong();
}

void sizeLoanFilter(QListWidget* list)
{
    if (list == nullptr || list->count() == 0) {
        return;
    }
    const int rowHeight = qMax(list->sizeHintForRow(0), 20);
    list->setFixedHeight(rowHeight * list->count() + 4);
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void fillLoanFilter(QListWidget* list)
{
    const QSignalBlocker blocker(list);
    list->clear();
    auto* allItem = new QListWidgetItem(T("circulation.filter.all"));
    allItem->setData(Qt::UserRole, QString::fromLatin1(Repositories::LoanFilter::kAll));
    list->addItem(allItem);
    for (const std::string& code : Repositories::CirculationRepository::filterCodes()) {
        if (code == Repositories::LoanFilter::kAll) {
            continue;
        }
        auto* item = new QListWidgetItem(T("circulation.filter." + code));
        item->setData(Qt::UserRole, qs(code));
        list->addItem(item);
    }
    allItem->setSelected(true);
    list->setCurrentItem(allItem);
    sizeLoanFilter(list);
}

}  // namespace

class test_ui_CirculationFilters : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }

    void SetUp() override
    {
        // Pinned for the whole test, not just the seeding: which loans are
        // Open and which are Overdue depends on what day the page thinks it is.
        m_clock = std::make_unique<Core::ScopedClock>(Core::Date(2026, 9, 19));
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_memberRepo =
            std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_page.reset();
        m_spec.reset();
        m_loanWorkflows.reset();
        m_circulation.reset();
        m_memberRepo.reset();
        m_catalog.reset();
        m_db.reset();
        m_clock.reset();
    }

    void seedOpenOverdueAndReturned()
    {
        MemberSeed member = uniqueMemberSeed(1);
        member.status = Repositories::MemberStatus::kActive;
        m_memberId = seedMember(*m_db, member);
        ASSERT_GT(m_memberId, 0);

        BookSeed book = uniqueBookSeed(1);
        book.initialCopyCount = 3;
        const std::int64_t bookId = seedBook(*m_db, book);
        ASSERT_GT(bookId, 0);
        const auto copies = copyIdsOf(*m_db, bookId);
        ASSERT_EQ(copies.size(), 3u);

        const Core::Date today = Core::Date(2026, 9, 19);
        m_openId = rawInsertLoan(*m_db, m_memberId, copies.at(0),
                                 today.addDays(-2).toIso(), today.addDays(12).toIso());
        m_overdueId = rawInsertLoan(*m_db, m_memberId, copies.at(1),
                                    today.addDays(-40).toIso(), today.addDays(-26).toIso());
        m_returnedId = rawInsertLoan(*m_db, m_memberId, copies.at(2),
                                     today.addDays(-60).toIso(), today.addDays(-46).toIso(),
                                     today.addDays(-50).toIso());
        ASSERT_GT(m_openId, 0);
        ASSERT_GT(m_overdueId, 0);
        ASSERT_GT(m_returnedId, 0);
    }

    void applyFacets()
    {
        QHash<QString, QStringList> mapped;
        QStringList loanFilters;
        for (const QListWidgetItem* item : m_loanFilter->selectedItems()) {
            const QString code = item->data(Qt::UserRole).toString();
            loanFilters.append(isAllLoanFilter(code)
                                   ? QString::fromLatin1(Repositories::LoanFilter::kAll)
                                   : code);
        }
        if (loanFilters.isEmpty()) {
            loanFilters.append(QString::fromLatin1(Repositories::LoanFilter::kAll));
        }
        mapped.insert(QStringLiteral("loanFilter"), loanFilters);
        const Repositories::MemberFacets facets = m_memberFilters->facets();
        mapped.insert(QStringLiteral("status"), qsl(facets.statuses));
        mapped.insert(QStringLiteral("sex"), qsl(facets.sexes));
        mapped.insert(QStringLiteral("year"), m_memberFilters->loanYears());
        mapped.insert(QStringLiteral("ageGroup"), qsl(facets.ageGroups));
        mapped.insert(QStringLiteral("city"), qsl(facets.cities));
        m_page->setFacets(mapped);
    }

    void openPage()
    {
        m_loanWorkflows = std::make_unique<LoanWorkflowHarness>(*m_circulation);
        m_spec = std::make_unique<LoanListSpec>(*m_circulation, *m_catalog, *m_memberRepo,
                                                *m_loanWorkflows->loans);
        m_page = std::make_unique<ListPage>(m_spec.get(), m_loanWorkflows->events, circulationConfig());

        m_loanFilter = new QListWidget(m_page->filterColumn());
        m_loanFilter->setObjectName(QStringLiteral("loanFilter"));
        m_loanFilter->setSelectionMode(QAbstractItemView::SingleSelection);
        fillLoanFilter(m_loanFilter);
        m_page->addFilterWidget(m_loanFilter, 0);
        QObject::connect(m_loanFilter, &QListWidget::itemSelectionChanged, m_page.get(),
                         [this]() { applyFacets(); });

        m_memberFilters = new MemberFacetFilters(*m_memberRepo, Repositories::ArchiveScope::Any,
                                                 m_page->filterColumn());
        m_memberFilters->useLoanYears([this]() {
            return m_circulation->listLoanYears(Repositories::ArchiveScope::Live);
        });
        m_memberFilters->refresh();
        m_page->addFilterWidget(m_memberFilters, 1);
        QObject::connect(m_memberFilters, &MemberFacetFilters::changed, m_page.get(),
                         [this]() { applyFacets(); });
        applyFacets();
    }

    void focusMemberLoans(const QString& membershipNumber)
    {
        {
            const QSignalBlocker blocker(m_loanFilter);
            m_loanFilter->clearSelection();
            for (int row = 0; row < m_loanFilter->count(); ++row) {
                QListWidgetItem* item = m_loanFilter->item(row);
                if (isAllLoanFilter(item->data(Qt::UserRole).toString())) {
                    item->setSelected(true);
                    m_loanFilter->setCurrentItem(item);
                    break;
                }
            }
        }
        m_memberFilters->reset();
        applyFacets();
        if (QTableView* table = m_page->table()) {
            table->setSortingEnabled(false);
            if (QHeaderView* header = table->horizontalHeader()) {
                header->setSortIndicator(-1, Qt::AscendingOrder);
            }
        }
        if (PagedQueryModel* model = m_page->model()) {
            QueryState state = model->query();
            state.sortKey.clear();
            state.ascending = true;
            model->setQuery(state);
        }
        m_page->setSearchText(membershipNumber);
        if (QTableView* table = m_page->table()) {
            table->setSortingEnabled(true);
        }
        m_page->focusSearch();
    }

    std::unique_ptr<Core::ScopedClock> m_clock;
    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::MemberRepository> m_memberRepo;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<LoanWorkflowHarness> m_loanWorkflows;
    std::unique_ptr<LoanListSpec> m_spec;
    std::unique_ptr<ListPage> m_page;
    QListWidget* m_loanFilter = nullptr;
    MemberFacetFilters* m_memberFilters = nullptr;
    std::int64_t m_memberId = 0;
    std::int64_t m_openId = 0;
    std::int64_t m_overdueId = 0;
    std::int64_t m_returnedId = 0;
};

TEST_F(test_ui_CirculationFilters, FilterListIncludesEveryStateAndDefaultsToAll)
{
    seedOpenOverdueAndReturned();
    openPage();

    auto* list = m_page->findChild<QListWidget*>(QStringLiteral("loanFilter"));
    ASSERT_NE(list, nullptr);
    const QStringList codes = filterCodesOnList(list);
    EXPECT_TRUE(codes.contains(QString::fromLatin1(Repositories::LoanFilter::kOpen)));
    EXPECT_TRUE(codes.contains(QString::fromLatin1(Repositories::LoanFilter::kOverdue)));
    EXPECT_TRUE(codes.contains(QString::fromLatin1(Repositories::LoanFilter::kReturned)));
    EXPECT_TRUE(codes.contains(QString::fromLatin1(Repositories::LoanFilter::kAll)));

    ASSERT_EQ(list->selectedItems().size(), 1);
    EXPECT_EQ(list->row(list->selectedItems().first()), 0);
    EXPECT_TRUE(isAllLoanFilter(list->selectedItems().first()->data(Qt::UserRole).toString()));

    auto* table = m_page->table();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->model()->rowCount(), 3);
}

TEST_F(test_ui_CirculationFilters, SelectingOverdueShowsOnlyLoansPastTheirDueDate)
{
    seedOpenOverdueAndReturned();
    openPage();

    auto* list = m_page->findChild<QListWidget*>(QStringLiteral("loanFilter"));
    auto* table = m_page->table();
    ASSERT_NE(list, nullptr);
    ASSERT_NE(table, nullptr);

    ASSERT_TRUE(selectFilterCode(list, QString::fromLatin1(Repositories::LoanFilter::kOverdue)));

    ASSERT_EQ(table->model()->rowCount(), 1);
    EXPECT_EQ(tableId(table, 0), m_overdueId);
}

TEST_F(test_ui_CirculationFilters, SelectingReturnedShowsOnlyReturnedLoans)
{
    seedOpenOverdueAndReturned();
    openPage();

    auto* list = m_page->findChild<QListWidget*>(QStringLiteral("loanFilter"));
    auto* table = m_page->table();
    ASSERT_NE(list, nullptr);
    ASSERT_NE(table, nullptr);

    ASSERT_TRUE(selectFilterCode(list, QString::fromLatin1(Repositories::LoanFilter::kReturned)));

    EXPECT_EQ(selectedFilterCodes(list), QStringList{QString::fromLatin1(Repositories::LoanFilter::kReturned)});
    ASSERT_EQ(table->model()->rowCount(), 1);
    EXPECT_EQ(tableId(table, 0), m_returnedId);
}

TEST_F(test_ui_CirculationFilters, SelectingOpenAgainShowsOnlyOpenLoans)
{
    seedOpenOverdueAndReturned();
    openPage();

    auto* list = m_page->findChild<QListWidget*>(QStringLiteral("loanFilter"));
    auto* table = m_page->table();
    ASSERT_NE(list, nullptr);
    ASSERT_NE(table, nullptr);

    ASSERT_TRUE(selectFilterCode(list, QString::fromLatin1(Repositories::LoanFilter::kReturned)));
    ASSERT_TRUE(selectFilterCode(list, QString::fromLatin1(Repositories::LoanFilter::kOpen)));

    EXPECT_EQ(selectedFilterCodes(list), QStringList{QString::fromLatin1(Repositories::LoanFilter::kOpen)});
    ASSERT_EQ(table->model()->rowCount(), 1);
    EXPECT_EQ(tableId(table, 0), m_openId);
}

TEST_F(test_ui_CirculationFilters, SelectingAllShowsEveryLoan)
{
    seedOpenOverdueAndReturned();
    openPage();

    auto* list = m_page->findChild<QListWidget*>(QStringLiteral("loanFilter"));
    auto* table = m_page->table();
    ASSERT_NE(list, nullptr);
    ASSERT_NE(table, nullptr);

    ASSERT_TRUE(selectFilterCode(list, QString::fromLatin1(Repositories::LoanFilter::kReturned)));
    ASSERT_TRUE(selectFilterCode(list, QString::fromLatin1(Repositories::LoanFilter::kAll)));
    EXPECT_EQ(table->model()->rowCount(), 3);
}

TEST_F(test_ui_CirculationFilters, JumpingFromMembersShowsOverdueLoansToo)
{
    // Members sends the librarian here when a member still has books out.
    // Overdue books are out as well, and are usually the ones that matter.
    seedOpenOverdueAndReturned();
    openPage();
    auto* table = m_page->table();
    ASSERT_NE(table, nullptr);

    const std::string number =
        m_db->scalar("SELECT membership_number FROM members WHERE id = " + std::to_string(m_memberId))
            .toString();
    focusMemberLoans(QString::fromStdString(number));

    QList<qint64> listed;
    for (int row = 0; row < table->model()->rowCount(); ++row) {
        listed.append(tableId(table, row));
    }
    EXPECT_TRUE(listed.contains(m_openId));
    EXPECT_TRUE(listed.contains(m_overdueId));
    ASSERT_GE(listed.size(), 2);
    // Still out comes first; history (returned) after.
    EXPECT_NE(listed.at(0), m_returnedId);
    EXPECT_NE(listed.at(1), m_returnedId);
}

TEST_F(test_ui_CirculationFilters, FilterSitsAtTheTopOfItsColumn)
{
    seedOpenOverdueAndReturned();
    openPage();
    m_page->resize(1200, 800);
    m_page->show();
    QApplication::processEvents();

    auto* list = m_page->findChild<QListWidget*>(QStringLiteral("loanFilter"));
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->y(), 0);
}

TEST_F(test_ui_CirculationFilters, ReturnSitsBetweenTheSearchAndCheckout)
{
    openPage();
    QPushButton* returnButton = nullptr;
    QPushButton* checkoutButton = nullptr;
    for (QPushButton* button : m_page->findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Return")) {
            returnButton = button;
        } else if (button->text() == QStringLiteral("Checkout")) {
            checkoutButton = button;
        }
    }
    ASSERT_NE(returnButton, nullptr);
    ASSERT_NE(checkoutButton, nullptr);
    QLayout* pad = returnButton->parentWidget()->layout();
    ASSERT_EQ(checkoutButton->parentWidget()->layout(), pad);
    auto* search = m_page->findChild<QLineEdit*>(QStringLiteral("listSearch"));
    ASSERT_NE(search, nullptr);
    EXPECT_EQ(pad->indexOf(search), 0);
    EXPECT_EQ(pad->indexOf(returnButton), 1);
    EXPECT_EQ(pad->indexOf(checkoutButton), 2);
}
