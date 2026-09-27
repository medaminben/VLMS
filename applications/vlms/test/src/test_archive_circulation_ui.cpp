#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/circulation/CirculationPage.h"

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/MemberRepository.h>
#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/Locale.h>

#include <QAbstractButton>
#include <QApplication>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>

#include <gtest/gtest.h>

#include <memory>

using VLMS::Locale;
using namespace VLMS::Test;

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

bool selectFilterCode(QListWidget* list, const QString& code)
{
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        const QString itemCode = item->data(Qt::UserRole).toString();
        // All is stored as an empty UserRole (SingleSelection filter list).
        const bool matchesAll = code == QLatin1String("all") && itemCode.isEmpty();
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

}  // namespace

class test_ui_ArchiveCirculation : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }
    static void TearDownTestSuite() { Locale::setCode(Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_memberRepo =
            std::make_unique<MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());

        const std::int64_t member = seedMember(*m_db, uniqueMemberSeed(1));
        const std::int64_t returnedBook = seedBook(*m_db, uniqueBookSeed(1));
        const std::int64_t openBook = seedBook(*m_db, uniqueBookSeed(2));
        m_returnedLoan = rawInsertLoan(*m_db, member, copyIdsOf(*m_db, returnedBook).front(),
                                       "2026-09-01", "2026-09-15", "2026-09-10");
        m_openLoan = rawInsertLoan(*m_db, member, copyIdsOf(*m_db, openBook).front(),
                                   "2026-09-01", "2026-09-15");

        m_page = std::make_unique<CirculationPage>(*m_circulation, *m_catalog, *m_memberRepo);
        ASSERT_TRUE(selectFilterCode(m_page->findChild<QListWidget*>(QStringLiteral("loanFilter")),
                                     QStringLiteral("all")));
        m_table = m_page->findChild<QTableWidget*>();
        m_delete = buttonWithText(m_page.get(), QStringLiteral("Delete"));
        ASSERT_NE(m_table, nullptr);
        ASSERT_NE(m_delete, nullptr);
    }

    void TearDown() override
    {
        m_page.reset();
        m_circulation.reset();
        m_memberRepo.reset();
        m_catalog.reset();
        m_db.reset();
    }

    void selectLoan(qint64 id)
    {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (m_table->item(row, 0)->data(Qt::UserRole).toLongLong() == id) {
                m_table->selectRow(row);
                return;
            }
        }
        FAIL() << "loan " << id << " not listed";
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CatalogRepository> m_catalog;
    std::unique_ptr<MemberRepository> m_memberRepo;
    std::unique_ptr<CirculationRepository> m_circulation;
    std::unique_ptr<CirculationPage> m_page;
    QTableWidget* m_table = nullptr;
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
    ASSERT_EQ(m_table->rowCount(), 2);
    selectLoan(m_returnedLoan);
    answerNextBoxYes();
    m_delete->click();

    EXPECT_FALSE(m_circulation->getLoan(m_returnedLoan)->archivedAt.empty());
    EXPECT_EQ(m_table->rowCount(), 1);
}
