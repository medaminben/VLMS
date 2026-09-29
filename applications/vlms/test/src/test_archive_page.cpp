#include "ModalTest.h"
#include "TestDatabase.h"
#include "TestSeed.h"

#include "QtBridge.h"
#include "ui/Theme.h"
#include "ui/ListPage.h"
#include "ui/archive/ReuseNumberFlow.h"
#include "ui/catalog/BookFacetFilters.h"
#include "ui/members/MemberFacetFilters.h"
#include "ui/presentation/ArchivedBookSpec.h"
#include "ui/presentation/ArchivedCopySpec.h"
#include "ui/presentation/ArchivedLoanSpec.h"
#include "ui/presentation/ArchivedMemberSpec.h"
#include "WorkflowTestSupport.h"

#include <VLMS/Repositories/ArchiveTypes.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Core/Strings.h>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QHash>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>
#include <QTimer>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

using namespace VLMS;
using namespace Test;

namespace {

struct ArchiveTypeEntry {
    ArchiveType type;
    const char* code;
    const char* labelKey;
};

constexpr ArchiveTypeEntry kArchiveTypes[] = {
    {ArchiveType::Members, "members", "archive.type.members"},
    {ArchiveType::Books, "books", "archive.type.books"},
    {ArchiveType::Copies, "copies", "archive.type.copies"},
    {ArchiveType::Loans, "loans", "archive.type.loans"},
};

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

bool selectType(QListWidget* list, const QString& code)
{
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        if (item->data(Qt::UserRole).toString() == code) {
            list->setCurrentItem(item);
            item->setSelected(true);
            QApplication::processEvents();
            // Flush deleteLater from ListPage::rebuildActions after setSpec.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            return true;
        }
    }
    return false;
}

QPushButton* actionButton(QWidget* root, const QString& id)
{
    return root->findChild<QPushButton*>(QStringLiteral("listAction_%1").arg(id));
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
        typeList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        for (const ArchiveTypeEntry& entry : kArchiveTypes) {
            auto* item = new QListWidgetItem(typeList);
            item->setData(Qt::UserRole, QString::fromLatin1(entry.code));
            item->setText(T(entry.labelKey));
        }
        typeList->item(0)->setSelected(true);
        page->addFilterWidget(typeList, 0);
        sizeTypeList();

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

    void sizeTypeList()
    {
        typeList->ensurePolished();
        int rows = 0;
        for (int row = 0; row < typeList->count(); ++row) {
            rows += std::max(typeList->sizeHintForRow(row), 20);
        }
        const QMargins margins = typeList->contentsMargins();
        typeList->setFixedHeight(rows + 2 * typeList->frameWidth() + margins.top()
                                 + margins.bottom());
    }

    void applyLabels()
    {
        QEvent languageChange(QEvent::LanguageChange);
        QCoreApplication::sendEvent(page.get(), &languageChange);
        for (int row = 0; row < typeList->count(); ++row) {
            typeList->item(row)->setText(T(kArchiveTypes[row].labelKey));
        }
        sizeTypeList();
        applyFilterVisibility();
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
            const QSignalBlocker blocker(typeList);
            for (int row = 0; row < typeList->count(); ++row) {
                if (kArchiveTypes[row].type == type) {
                    typeList->item(row)->setSelected(true);
                    break;
                }
            }
            return;
        }
        type = kArchiveTypes[typeList->row(selected.first())].type;
        memberFilters->reset();
        bookFilters->reset();
        applyFilterVisibility();
        page->setSpec(specFor(type));
        applyFacets();
    }

    void setType(ArchiveType t)
    {
        for (int row = 0; row < typeList->count(); ++row) {
            if (kArchiveTypes[row].type == t) {
                const QSignalBlocker blocker(typeList);
                typeList->clearSelection();
                typeList->item(row)->setSelected(true);
                break;
            }
        }
        type = t;
        memberFilters->reset();
        bookFilters->reset();
        applyFilterVisibility();
        page->setSpec(specFor(t));
        applyFacets();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void refresh()
    {
        applyFilterVisibility();
        if (page->model() != nullptr) {
            page->model()->reload();
        }
    }

    QTableView* table() const { return page->table(); }

    int rowCount() const
    {
        return table() != nullptr && table()->model() != nullptr ? table()->model()->rowCount() : 0;
    }

    int columnCount() const
    {
        return table() != nullptr && table()->model() != nullptr ? table()->model()->columnCount()
                                                                : 0;
    }

    QString headerText(int column) const
    {
        return table()->model()->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
    }

    qint64 idAt(int row) const
    {
        return table()->model()->index(row, 0).data(Qt::UserRole).toLongLong();
    }

    QString cellText(int row, int column) const
    {
        return table()->model()->index(row, column).data(Qt::DisplayRole).toString();
    }

    int rowOf(qint64 id) const
    {
        for (int row = 0; row < rowCount(); ++row) {
            if (idAt(row) == id) {
                return row;
            }
        }
        return -1;
    }

    void selectRow(int row)
    {
        table()->selectRow(row);
        QApplication::processEvents();
    }

    void clearSelection()
    {
        if (QItemSelectionModel* selection = table()->selectionModel()) {
            selection->clear();
        }
        table()->setCurrentIndex(QModelIndex());
        QApplication::processEvents();
    }

    void tickId(qint64 id)
    {
        const int row = rowOf(id);
        ASSERT_GE(row, 0) << "row " << id << " is not on the archive page";
        ASSERT_TRUE(table()->model()->setData(table()->model()->index(row, 0), Qt::Checked,
                                              Qt::CheckStateRole));
        QApplication::processEvents();
    }
};

}  // namespace

class test_ui_ArchiveList : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { Core::Locale::setCode(Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members = std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());

        m_memberId = seedMember(*m_db, uniqueMemberSeed(1));
        ASSERT_TRUE(m_members->archiveMember(m_memberId));

        BookSeed book = uniqueBookSeed(1);
        book.initialCopyCount = 2;
        m_bookId = seedBook(*m_db, book);
        ASSERT_TRUE(m_catalog->archiveBook(m_bookId));
        const auto copies = copyIdsOf(*m_db, m_bookId);
        m_numberedCopyId = copies.at(0);
        m_numberlessCopyId = copies.at(1);
        ASSERT_TRUE(m_db->exec("UPDATE book_copies SET local_id = NULL, global_copy_id = NULL "
                               "WHERE id = " + std::to_string(m_numberlessCopyId)));

        const std::int64_t liveBook = seedBook(*m_db, uniqueBookSeed(2));
        const std::int64_t borrower = seedMember(*m_db, uniqueMemberSeed(2));
        m_loanId = rawInsertLoan(*m_db, borrower, copyIdsOf(*m_db, liveBook).front(),
                                 "2026-09-01", "2026-09-15", "2026-09-10");
        ASSERT_TRUE(m_circulation->archiveLoan(m_loanId));

        m_harness = std::make_unique<ArchiveListHarness>(*m_members, *m_catalog, *m_circulation);
        m_typeList = m_harness->typeList;
        m_harness->selectRow(0);
        refreshActionButtons();
        ASSERT_NE(m_typeList, nullptr);
        ASSERT_NE(m_harness->table(), nullptr);
        ASSERT_NE(m_restore, nullptr);
        ASSERT_NE(m_purge, nullptr);
    }

    void TearDown() override
    {
        m_harness.reset();
        m_circulation.reset();
        m_catalog.reset();
        m_members.reset();
        m_db.reset();
    }

    void refreshActionButtons()
    {
        QApplication::processEvents();
        m_restore = actionButton(m_harness->page.get(), QStringLiteral("restore"));
        m_reuse = actionButton(m_harness->page.get(), QStringLiteral("reuse"));
        m_purge = actionButton(m_harness->page.get(), QStringLiteral("purge"));
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<ArchiveListHarness> m_harness;
    QListWidget* m_typeList = nullptr;
    QPushButton* m_restore = nullptr;
    QPushButton* m_reuse = nullptr;
    QPushButton* m_purge = nullptr;
    std::int64_t m_memberId = 0;
    std::int64_t m_bookId = 0;
    std::int64_t m_numberedCopyId = 0;
    std::int64_t m_numberlessCopyId = 0;
    std::int64_t m_loanId = 0;
};

TEST_F(test_ui_ArchiveList, TypeFilterOffersFourTypesAndStartsOnMembers)
{
    ASSERT_EQ(m_typeList->count(), 4);
    EXPECT_EQ(m_typeList->item(0)->data(Qt::UserRole).toString(), QStringLiteral("members"));
    EXPECT_EQ(m_typeList->item(3)->data(Qt::UserRole).toString(), QStringLiteral("loans"));
    EXPECT_EQ(m_harness->type, ArchiveType::Members);
    EXPECT_EQ(m_harness->columnCount(), 6);
    EXPECT_EQ(m_harness->headerText(4), QStringLiteral("Loans"));
    EXPECT_EQ(m_harness->headerText(5), QStringLiteral("Archived"));
    ASSERT_EQ(m_harness->rowCount(), 1);
    EXPECT_EQ(m_harness->rowOf(m_memberId), 0);
}

TEST_F(test_ui_ArchiveList, EachTypeListsOnlyItsArchivedRowsWithItsOwnColumns)
{
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("books")));
    refreshActionButtons();
    EXPECT_EQ(m_harness->type, ArchiveType::Books);
    EXPECT_EQ(m_harness->columnCount(), 4);
    EXPECT_EQ(m_harness->headerText(0), QStringLiteral("Title"));
    ASSERT_EQ(m_harness->rowCount(), 1);
    EXPECT_EQ(m_harness->rowOf(m_bookId), 0);
    EXPECT_EQ(m_harness->cellText(0, 2), QStringLiteral("2"));

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    EXPECT_EQ(m_harness->rowCount(), 2);
    EXPECT_GE(m_harness->rowOf(m_numberedCopyId), 0);
    EXPECT_GE(m_harness->rowOf(m_numberlessCopyId), 0);

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("loans")));
    ASSERT_EQ(m_harness->rowCount(), 1);
    EXPECT_EQ(m_harness->rowOf(m_loanId), 0);
}

TEST_F(test_ui_ArchiveList, RestoreIsEnabledOnlyWithASelection)
{
    EXPECT_TRUE(m_restore->isEnabled());  // first row selected in SetUp
    m_harness->table()->clearSelection();
    m_harness->table()->setCurrentIndex(QModelIndex());
    if (m_harness->page->model() != nullptr) {
        m_harness->page->model()->clearTicks();
    }
    QTest::qWait(0);
    EXPECT_FALSE(m_restore->isEnabled());
    m_harness->selectRow(0);
    EXPECT_TRUE(m_restore->isEnabled());
}

TEST_F(test_ui_ArchiveList, ReuseShowsOnlyOnCopiesAndNeedsANumber)
{
    EXPECT_EQ(actionButton(m_harness->page.get(), QStringLiteral("reuse")), nullptr);

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    refreshActionButtons();
    ASSERT_NE(m_reuse, nullptr);
    EXPECT_FALSE(m_reuse->isHidden());
    m_harness->selectRow(m_harness->rowOf(m_numberedCopyId));
    EXPECT_TRUE(m_reuse->isEnabled());
    m_harness->selectRow(m_harness->rowOf(m_numberlessCopyId));
    EXPECT_FALSE(m_reuse->isEnabled());

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("loans")));
    refreshActionButtons();
    EXPECT_EQ(m_reuse, nullptr);
}

TEST_F(test_ui_ArchiveList, ReuseHidesOnLoansWhenTheSelectedRowIndexStaysTheSame)
{
    // Copies opens on row 0 and so does Loans: setSpec rebuilds actions so Reuse
    // must leave the pad even when the selection row index is unchanged.
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    m_harness->selectRow(0);
    refreshActionButtons();
    ASSERT_NE(m_reuse, nullptr);

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("loans")));
    refreshActionButtons();
    EXPECT_EQ(m_reuse, nullptr);
}

TEST_F(test_ui_ArchiveList, ReuseComesBeforeRestoreOnTheButtonPad)
{
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    refreshActionButtons();
    ASSERT_NE(m_reuse, nullptr);
    ASSERT_NE(m_restore, nullptr);
    ASSERT_NE(m_purge, nullptr);
    auto* pad = m_reuse->parentWidget();
    ASSERT_EQ(pad, m_restore->parentWidget());
    const int reuseAt = pad->layout()->indexOf(m_reuse);
    const int restoreAt = pad->layout()->indexOf(m_restore);
    const int purgeAt = pad->layout()->indexOf(m_purge);
    EXPECT_LT(reuseAt, restoreAt);
    EXPECT_LT(restoreAt, purgeAt);
}

TEST_F(test_ui_ArchiveList, RestoreSendsTheMemberBackToTheLiveList)
{
    QSignalSpy restored(&m_harness->workflows.events, &VLMS::DataEvents::changed);
    answerNextBoxYes();
    m_restore->click();

    EXPECT_EQ(m_harness->rowCount(), 0);
    EXPECT_TRUE(m_members->getMember(m_memberId)->archivedAt.empty());
    EXPECT_EQ(restored.count(), 1);
}

TEST_F(test_ui_ArchiveList, ReuseOpensTheChooserAndCancelKeepsTheNumber)
{
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    refreshActionButtons();
    ASSERT_NE(m_reuse, nullptr);
    m_harness->selectRow(m_harness->rowOf(m_numberedCopyId));
    const std::string number =
        m_db->scalar("SELECT local_id FROM book_copies WHERE id = " + std::to_string(m_numberedCopyId))
            .toString();

    bool chooserOpened = false;
    QTimer poll;
    poll.setInterval(10);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr || !dialog->isVisible()) {
            return;
        }
        poll.stop();
        chooserOpened = qobject_cast<ReuseBookChooser*>(dialog) != nullptr;
        dialog->reject();
    });
    poll.start();
    m_reuse->click();
    poll.stop();

    EXPECT_TRUE(chooserOpened);
    EXPECT_EQ(m_db->scalar("SELECT local_id FROM book_copies WHERE id = "
                           + std::to_string(m_numberedCopyId))
                  .toString(),
              number);
    EXPECT_GE(m_harness->rowOf(m_numberedCopyId), 0);
}

TEST_F(test_ui_ArchiveList, SetTypeSwitchesTheListedKind)
{
    EXPECT_EQ(m_harness->type, ArchiveType::Members);
    m_harness->setType(ArchiveType::Books);
    EXPECT_EQ(m_harness->type, ArchiveType::Books);
    m_harness->setType(ArchiveType::Copies);
    EXPECT_EQ(m_harness->type, ArchiveType::Copies);
    m_harness->setType(ArchiveType::Loans);
    EXPECT_EQ(m_harness->type, ArchiveType::Loans);
}

TEST_F(test_ui_ArchiveList, EveryTypeStaysVisibleWhateverTypeIsPicked)
{
    qApp->setStyleSheet(VLMS::applicationStylesheet());
    QFont big = m_typeList->font();
    big.setPixelSize(22);
    m_typeList->setFont(big);
    m_harness->applyLabels();
    m_harness->page->resize(1200, 800);
    m_harness->page->show();
    QApplication::processEvents();

    for (int picked = 0; picked < m_typeList->count(); ++picked) {
        ASSERT_TRUE(selectType(m_typeList, m_typeList->item(picked)->data(Qt::UserRole).toString()));
        const QRect viewport = m_typeList->viewport()->rect();
        for (int row = 0; row < m_typeList->count(); ++row) {
            EXPECT_TRUE(viewport.contains(m_typeList->visualItemRect(m_typeList->item(row))))
                << "row " << row << " hidden after picking row " << picked;
        }
    }
    qApp->setStyleSheet({});
}

TEST_F(test_ui_ArchiveList, TypeFilterSitsAtTheTopOfItsColumn)
{
    m_harness->page->resize(1200, 800);
    m_harness->page->show();
    QApplication::processEvents();

    EXPECT_EQ(m_typeList->y(), 0);
}

TEST_F(test_ui_ArchiveList, PermanentRemovalIsOfferedOnlyWhenTheRowsGateIsOpen)
{
    m_harness->selectRow(m_harness->rowOf(m_memberId));
    EXPECT_TRUE(m_purge->isEnabled());

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("books")));
    refreshActionButtons();
    m_harness->selectRow(m_harness->rowOf(m_bookId));
    EXPECT_FALSE(m_purge->isEnabled());

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    refreshActionButtons();
    m_harness->selectRow(m_harness->rowOf(m_numberedCopyId));
    EXPECT_TRUE(m_purge->isEnabled());

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("loans")));
    refreshActionButtons();
    m_harness->selectRow(m_harness->rowOf(m_loanId));
    EXPECT_TRUE(m_purge->isEnabled());
}

TEST_F(test_ui_ArchiveList, TheLoansColumnSaysWhyAMemberCannotGo)
{
    const std::int64_t borrower = seedMember(*m_db, uniqueMemberSeed(9));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(9));
    const std::int64_t loanId = rawInsertLoan(*m_db, borrower, copyIdsOf(*m_db, bookId).front(),
                                              "2026-09-01", "2026-09-15", "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_TRUE(m_circulation->archiveLoan(loanId));
    ASSERT_TRUE(m_members->archiveMember(borrower));
    m_harness->refresh();

    const int row = m_harness->rowOf(borrower);
    ASSERT_GE(row, 0);
    EXPECT_EQ(m_harness->cellText(row, 4), QStringLiteral("1"));
    m_harness->selectRow(row);
    EXPECT_FALSE(m_purge->isEnabled());

    m_harness->selectRow(m_harness->rowOf(m_memberId));
    EXPECT_EQ(m_harness->cellText(m_harness->rowOf(m_memberId), 4), QStringLiteral("0"));
    EXPECT_TRUE(m_purge->isEnabled());
}

TEST_F(test_ui_ArchiveList, PermanentlyRemovingACopyTakesItOffTheListAndLeavesTheOther)
{
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    refreshActionButtons();
    ASSERT_EQ(m_harness->rowCount(), 2);
    m_harness->selectRow(m_harness->rowOf(m_numberedCopyId));

    answerNextBoxYes();
    m_purge->click();
    QApplication::processEvents();

    EXPECT_EQ(m_harness->rowCount(), 1);
    EXPECT_EQ(m_harness->rowOf(m_numberedCopyId), -1);
    EXPECT_GE(m_harness->rowOf(m_numberlessCopyId), 0);
}

TEST_F(test_ui_ArchiveList, TwoTickedMembersAreRestoredAndTheSignalFiresOnce)
{
    const std::int64_t second = seedMember(*m_db, uniqueMemberSeed(9));
    ASSERT_TRUE(m_members->archiveMember(second));
    m_harness->refresh();
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("members")));
    refreshActionButtons();
    ASSERT_GE(m_harness->rowOf(m_memberId), 0);
    ASSERT_GE(m_harness->rowOf(second), 0);
    m_harness->tickId(m_memberId);
    m_harness->tickId(second);

    QSignalSpy spy(&m_harness->workflows.events, &VLMS::DataEvents::changed);
    const ModalOutcome outcome =
        runAndAnswerModal([this]() { m_restore->click(); }, qs(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, QStringLiteral("Restore 2 members?"));
    EXPECT_EQ(spy.count(), 1);
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(m_memberId))
                    .isNull());
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(second))
                    .isNull());
}

TEST_F(test_ui_ArchiveList, NothingTickedRestoresOnlyTheHighlightedMember)
{
    const std::int64_t second = seedMember(*m_db, uniqueMemberSeed(8));
    ASSERT_TRUE(m_members->archiveMember(second));
    m_harness->refresh();
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("members")));
    refreshActionButtons();
    m_harness->selectRow(m_harness->rowOf(m_memberId));

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { m_restore->click(); }, qs(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(Core::Strings::t("archive.restoreConfirm")));
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(m_memberId))
                    .isNull());
    EXPECT_FALSE(m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(second))
                     .isNull());
}

TEST_F(test_ui_ArchiveList, TwoTickedLoansArePurgedAndTheThirdStays)
{
    const std::int64_t book = seedBook(*m_db, uniqueBookSeed(8));
    const std::int64_t member = seedMember(*m_db, uniqueMemberSeed(7));
    const std::int64_t second =
        rawInsertLoan(*m_db, member, copyIdsOf(*m_db, book).front(), "2026-08-01", "2026-08-15",
                      "2026-08-10");
    ASSERT_TRUE(m_circulation->archiveLoan(second));
    const std::int64_t book3 = seedBook(*m_db, uniqueBookSeed(9));
    const std::int64_t third =
        rawInsertLoan(*m_db, member, copyIdsOf(*m_db, book3).front(), "2026-07-01", "2026-07-15",
                      "2026-07-10");
    ASSERT_TRUE(m_circulation->archiveLoan(third));
    m_harness->refresh();
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("loans")));
    refreshActionButtons();
    m_harness->tickId(m_loanId);
    m_harness->tickId(second);
    m_harness->selectRow(m_harness->rowOf(third));

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { m_purge->click(); }, qs(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM loans WHERE id = " + std::to_string(m_loanId)).toInt(),
              0);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM loans WHERE id = " + std::to_string(second)).toInt(), 0);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM loans WHERE id = " + std::to_string(third)).toInt(), 1);
}

TEST_F(test_ui_ArchiveList, ATickedBookWithCopiesIsRefusedAndALoanFreeCopyIsPurged)
{
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("books")));
    refreshActionButtons();
    m_harness->tickId(m_bookId);

    const ModalOutcome blocked =
        runAndAnswerModal([this]() { m_purge->click(); }, qs(Core::Strings::t("common.ok")));
    ASSERT_TRUE(blocked.appeared);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(m_bookId)).toInt(),
              1);

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    refreshActionButtons();
    m_harness->tickId(m_numberedCopyId);
    const ModalOutcome purged =
        runAndAnswerModal([this]() { m_purge->click(); }, qs(Core::Strings::t("common.yes")));
    ASSERT_TRUE(purged.appeared);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE id = "
                           + std::to_string(m_numberedCopyId))
                  .toInt(),
              0);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE id = "
                           + std::to_string(m_numberlessCopyId))
                  .toInt(),
              1);
}
