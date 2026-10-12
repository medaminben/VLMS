// MainWindow on the real Application: navigation, the member-loans jump,
// refreshes driven by DataEvents, and one full-window loan round trip.

#include "Librarian.h"
#include "TestSeed.h"

#include "ui/presentation/PagedQueryModel.h"
#include "ui/shell/Application.h"
#include "ui/shell/MainWindow.h"
#include "ui/views/ListPage.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Paths.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <QComboBox>
#include <QDate>
#include <QListWidget>
#include <QStackedWidget>
#include <QTableView>
#include <QElapsedTimer>
#include <QTest>

#include <memory>

namespace {

enum StackIndex { kCatalog = 0, kMembers = 1, kCirculation = 2, kArchive = 3, kMetrics = 4 };

Application& app()
{
    return *qobject_cast<Application*>(QApplication::instance());
}

QString isoDaysFromToday(int days)
{
    return QDate::currentDate().addDays(days).toString(Qt::ISODate);
}

/// Processes events until `done` holds or a second passes.
void waitFor(const std::function<bool()>& done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 1000) {
        QTest::qWait(20);
    }
}

bool listHas(const QListWidget* list, const QString& text)
{
    for (int i = 0; list != nullptr && i < list->count(); ++i) {
        if (list->item(i)->text().contains(text)) {
            return true;
        }
    }
    return false;
}

}  // namespace

class Shell : public ::testing::Test {
protected:
    static void SetUpTestSuite() { app().setUiLocale(QStringLiteral("en")); }

    void SetUp() override
    {
        window = std::make_unique<MainWindow>(app());
        window->resize(1280, 800);
        window->QMainWindow::show();
        stack = window->findChild<QStackedWidget*>();
        ASSERT_NE(stack, nullptr);
    }

    void TearDown() override { window.reset(); }

    VLMS::ListPage* pageAt(int index) const
    {
        return qobject_cast<VLMS::ListPage*>(stack->widget(index));
    }

    /// A member and a book of their own, so tests sharing the database do not
    /// see each other's rows as theirs.
    struct Seeded {
        qint64 memberId = 0;
        QString membershipNumber;
        qint64 bookId = 0;
        qint64 copyId = 0;
    };

    Seeded seed(int index)
    {
        Seeded seeded;
        auto member = ::Test::uniqueMemberSeed(index).toInput();
        member.status = VLMS::Repositories::MemberStatus::kActive;
        seeded.memberId = app().members().createMember(member).value();
        seeded.membershipNumber = QString::fromStdString(
            app().members().getMember(seeded.memberId)->membershipNumber);
        seeded.bookId = app().catalog().createBook(::Test::uniqueBookSeed(index).toInput()).value();
        seeded.copyId = app().catalog().listCopies(seeded.bookId)->front().id;
        return seeded;
    }

    qint64 lend(const Seeded& seeded)
    {
        VLMS::Repositories::LoanInput loan;
        loan.memberId = seeded.memberId;
        loan.bookCopyId = seeded.copyId;
        loan.borrowedAt = isoDaysFromToday(-3).toStdString();
        loan.dueAt = isoDaysFromToday(11).toStdString();
        return app().circulation().createLoan(loan).value();
    }

    std::unique_ptr<MainWindow> window;
    QStackedWidget* stack = nullptr;
};

TEST_F(Shell, OpensOnTheCatalog)
{
    EXPECT_EQ(stack->currentIndex(), kCatalog);
    ASSERT_NE(pageAt(kCatalog), nullptr);
}

TEST_F(Shell, TheNavigatorSwitchesPages)
{
    window->show(VLMS::PageId::Members);
    EXPECT_EQ(stack->currentIndex(), kMembers);
    window->show(VLMS::PageId::Circulation);
    EXPECT_EQ(stack->currentIndex(), kCirculation);
    window->showArchive(VLMS::ArchiveType::Books);
    EXPECT_EQ(stack->currentIndex(), kArchive);
    window->show(VLMS::PageId::Metrics);
    EXPECT_EQ(stack->currentIndex(), kMetrics);
    window->show(VLMS::PageId::Catalog);
    EXPECT_EQ(stack->currentIndex(), kCatalog);
}

TEST_F(Shell, MemberLoansJumpShowsOnlyThatMembersLoans)
{
    const Seeded mine = seed(101);
    const Seeded other = seed(102);
    lend(mine);
    lend(other);

    window->showMemberLoans(mine.membershipNumber);

    ASSERT_EQ(stack->currentIndex(), kCirculation);
    VLMS::PagedQueryModel* model = pageAt(kCirculation)->model();
    ASSERT_NE(model, nullptr);
    // Quoted: exact, so member 1 does not also bring 10, 11 or "Book 1".
    EXPECT_EQ(model->query().search, QLatin1Char('"') + mine.membershipNumber + QLatin1Char('"'));
    // Let the search debounce fire. QTRY_* does not report to GoogleTest.
    waitFor([model] { return model->rowCount() == 1; });
    EXPECT_EQ(model->rowCount(), 1);
}

TEST_F(Shell, ACategoryChangeRefreshesTheBookFacets)
{
    auto* categories = pageAt(kCatalog)->findChild<QListWidget*>(QStringLiteral("categoryFilter"));
    ASSERT_NE(categories, nullptr);
    ASSERT_FALSE(listHas(categories, QStringLiteral("Shell Category")));

    ASSERT_TRUE(app().catalog().createCategory("SHC", "Shell Category"));
    app().dataEvents().emitChanged(VLMS::Entity::Categories, {});

    EXPECT_TRUE(listHas(categories, QStringLiteral("Shell Category")));
}

TEST_F(Shell, AMemberChangeReloadsTheMembersList)
{
    window->show(VLMS::PageId::Members);
    VLMS::PagedQueryModel* model = pageAt(kMembers)->model();
    const int before = model->rowCount();

    const Seeded added = seed(103);
    app().dataEvents().emitChanged(VLMS::Entity::Members, {added.memberId});

    EXPECT_EQ(model->rowCount(), before + 1);
}

TEST_F(Shell, CheckoutExtendAndReturnFromTheCirculationPage)
{
    const Seeded seeded = seed(104);
    window->show(VLMS::PageId::Circulation);
    VLMS::ListPage* page = pageAt(kCirculation);
    auto* checkout = page->findChild<QPushButton*>(QStringLiteral("listAction_checkout"));
    auto* extend = page->findChild<QPushButton*>(QStringLiteral("listAction_extend"));
    auto* giveBack = page->findChild<QPushButton*>(QStringLiteral("listAction_return"));
    ASSERT_NE(checkout, nullptr);
    ASSERT_NE(extend, nullptr);
    ASSERT_NE(giveBack, nullptr);

    const auto pickSeeded = [&seeded](QWidget* dialog) {
        // Member combo first, copy combo second: pick this test's member and
        // the first copy that combo offers for them.
        const auto combos = dialog->findChildren<QComboBox*>();
        ASSERT_GE(combos.size(), 2);
        const int member = combos.at(0)->findData(QVariant::fromValue(seeded.memberId));
        ASSERT_GE(member, 0);
        combos.at(0)->setCurrentIndex(member);
        const int copy = combos.at(1)->findData(QVariant::fromValue(seeded.copyId));
        ASSERT_GE(copy, 0);
        combos.at(1)->setCurrentIndex(copy);
        pressOk(dialog);
    };
    {
        Librarian librarian({pickSeeded});
        checkout->click();
        EXPECT_TRUE(librarian.done()) << librarian.seen.join(", ").toStdString();
    }
    VLMS::Repositories::LoanQuery query;
    query.memberId = seeded.memberId;
    const auto loans = app().circulation().listLoans(query);
    ASSERT_TRUE(loans);
    ASSERT_EQ(loans->size(), 1U);
    const qint64 loanId = loans->front().id;
    const std::string dueBefore = loans->front().dueAt;

    page->model()->revealId(loanId);
    ASSERT_TRUE(extend->isEnabled());
    {
        Librarian librarian({pressOk});
        extend->click();
        EXPECT_TRUE(librarian.done());
    }
    EXPECT_GT(app().circulation().getLoan(loanId)->dueAt, dueBefore);

    page->model()->revealId(loanId);
    {
        Librarian librarian({pressOk});
        giveBack->click();
        EXPECT_TRUE(librarian.done());
    }
    EXPECT_FALSE(app().circulation().getLoan(loanId)->returnedAt.empty());
}
