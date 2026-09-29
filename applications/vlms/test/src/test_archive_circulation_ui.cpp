#include "TestDatabase.h"
#include "TestSeed.h"

#include "QtBridge.h"
#include "ui/ListPage.h"
#include "ui/members/MemberFacetFilters.h"
#include "ui/presentation/LoanListSpec.h"
#include "WorkflowTestSupport.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/ArchiveTypes.h>
#include <VLMS/Repositories/LoanTypes.h>
#include <VLMS/Core/Locale.h>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QHash>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableView>
#include <QTimer>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

namespace {

QPushButton* buttonWithText(QWidget* root, const QString& text)
{
    for (QPushButton* button : root->findChildren<QPushButton*>()) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

bool isAllLoanFilter(const QString& code)
{
    return code.isEmpty() || code == QLatin1String(Repositories::LoanFilter::kAll);
}

bool selectFilterCode(QListWidget* list, const QString& code)
{
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        const QString itemCode = item->data(Qt::UserRole).toString();
        const bool matchesAll = isAllLoanFilter(code) && isAllLoanFilter(itemCode);
        if (itemCode == code || matchesAll) {
            list->clearSelection();
            list->setCurrentItem(item);
            item->setSelected(true);
            QApplication::processEvents();
            return true;
        }
    }
    return false;
}

void answerNextBoxYes()
{
    auto* poll = new QTimer;
    poll->setInterval(10);
    QObject::connect(poll, &QTimer::timeout, [poll] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (box == nullptr || !box->isVisible()) {
            return;
        }
        poll->stop();
        poll->deleteLater();
        if (QAbstractButton* yes = box->button(QMessageBox::Yes)) {
            yes->click();
        } else {
            box->done(0);
        }
    });
    poll->start();
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
}

}  // namespace

class test_ui_ArchiveCirculation : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { Core::Locale::setCode(Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_memberRepo =
            std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());

        const std::int64_t member = seedMember(*m_db, uniqueMemberSeed(1));
        const std::int64_t returnedBook = seedBook(*m_db, uniqueBookSeed(1));
        const std::int64_t openBook = seedBook(*m_db, uniqueBookSeed(2));
        m_returnedLoan = rawInsertLoan(*m_db, member, copyIdsOf(*m_db, returnedBook).front(),
                                       "2026-09-01", "2026-09-15", "2026-09-10");
        m_openLoan = rawInsertLoan(*m_db, member, copyIdsOf(*m_db, openBook).front(),
                                   "2026-09-01", "2026-09-15");

        m_loanWorkflows = std::make_unique<LoanWorkflowHarness>(*m_circulation);
        m_spec = std::make_unique<LoanListSpec>(*m_circulation, *m_catalog, *m_memberRepo,
                                                *m_loanWorkflows->loans);
        m_page = std::make_unique<ListPage>(m_spec.get(), m_loanWorkflows->events, circulationConfig());

        m_loanFilter = new QListWidget(m_page->filterColumn());
        m_loanFilter->setObjectName(QStringLiteral("loanFilter"));
        m_loanFilter->setSelectionMode(QAbstractItemView::SingleSelection);
        fillLoanFilter(m_loanFilter);
        m_page->addFilterWidget(m_loanFilter, 0);
        QObject::connect(m_loanFilter, &QListWidget::itemSelectionChanged, m_page.get(), [this]() {
            QHash<QString, QStringList> mapped;
            QStringList loanFilters;
            for (const QListWidgetItem* item : m_loanFilter->selectedItems()) {
                const QString code = item->data(Qt::UserRole).toString();
                loanFilters.append(isAllLoanFilter(code)
                                       ? QString::fromLatin1(Repositories::LoanFilter::kAll)
                                       : code);
            }
            mapped.insert(QStringLiteral("loanFilter"), loanFilters);
            m_page->setFacets(mapped);
        });
        ASSERT_TRUE(selectFilterCode(m_loanFilter, QStringLiteral("all")));
        m_table = m_page->table();
        m_delete = m_page->findChild<QPushButton*>(QStringLiteral("listAction_archive"));
        if (m_delete == nullptr) {
            m_delete = buttonWithText(m_page.get(), QStringLiteral("Delete"));
        }
        ASSERT_NE(m_table, nullptr);
        ASSERT_NE(m_delete, nullptr);
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
    }

    void selectLoan(qint64 id)
    {
        for (int row = 0; row < m_table->model()->rowCount(); ++row) {
            if (m_table->model()->index(row, 0).data(Qt::UserRole).toLongLong() == id) {
                m_table->selectRow(row);
                QApplication::processEvents();
                return;
            }
        }
        FAIL() << "loan " << id << " not listed";
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::MemberRepository> m_memberRepo;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<LoanWorkflowHarness> m_loanWorkflows;
    std::unique_ptr<LoanListSpec> m_spec;
    std::unique_ptr<ListPage> m_page;
    QListWidget* m_loanFilter = nullptr;
    QTableView* m_table = nullptr;
    QPushButton* m_delete = nullptr;
    std::int64_t m_returnedLoan = 0;
    std::int64_t m_openLoan = 0;
};

TEST_F(test_ui_ArchiveCirculation, DeleteIsOnlyOfferedForAReturnedLoan)
{
    selectLoan(m_openLoan);
    EXPECT_FALSE(m_delete->isEnabled());
    selectLoan(m_returnedLoan);
    EXPECT_TRUE(m_delete->isEnabled());
}

TEST_F(test_ui_ArchiveCirculation, DeleteMovesTheReturnedLoanToTheArchive)
{
    ASSERT_EQ(m_table->model()->rowCount(), 2);
    selectLoan(m_returnedLoan);
    answerNextBoxYes();
    m_delete->click();

    EXPECT_FALSE(m_circulation->getLoan(m_returnedLoan)->archivedAt.empty());
    EXPECT_EQ(m_table->model()->rowCount(), 1);
}
