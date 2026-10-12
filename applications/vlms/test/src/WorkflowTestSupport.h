#pragma once

#include "Fakes.h"
#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/presentation/LoanListSpec.h"
#include "ui/presentation/QtCatalogLookups.h"
#include "ui/presentation/QtCirculationLookups.h"
#include "ui/presentation/QtMemberLookups.h"
#include "ui/services/OcrService.h"
#include "ui/workflows/QtDialogFactory.h"
#include "ui/workflows/ArchiveWorkflow.h"
#include "ui/workflows/BookWorkflow.h"
#include "ui/workflows/BulkRunner.h"
#include "ui/workflows/DataEvents.h"
#include "ui/workflows/LoanWorkflow.h"
#include "ui/workflows/MemberWorkflow.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <QApplication>
#include <QSignalSpy>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <gtest/gtest.h>

#include <memory>
#include <string>

/// Closes any real window a workflow opens and remembers its title. Workflow
/// tests must see none: every dialog goes through FakeDialogFactory.
class RealModalGuard {
public:
    RealModalGuard()
    {
        m_timer.setInterval(20);
        QObject::connect(&m_timer, &QTimer::timeout, [this] {
            QWidget* modal = QApplication::activeModalWidget();
            if (modal != nullptr && modal->isVisible()) {
                opened.append(modal->windowTitle().isEmpty() ? modal->metaObject()->className()
                                                             : modal->windowTitle());
                modal->close();
            }
        });
        m_timer.start();
    }

    QStringList opened;

private:
    QTimer m_timer;
};

/// Lookups with nothing in them, for harnesses that only run loan flows.
class EmptyCatalogLookups final : public VLMS::CatalogLookups {
public:
    QStringList authorNames() const override { return {}; }
    QStringList publisherNames() const override { return {}; }
    std::vector<VLMS::Repositories::CategoryRecord> categories() const override { return {}; }
    std::vector<VLMS::Repositories::LanguageRecord> bookLanguages(
        VLMS::Repositories::ArchiveScope) const override
    {
        return {};
    }
    QStringList freeLocalNumbers(const QString&, int) const override { return {}; }
    void suggestCopyIdentifiers(const QString&, QString*, QString*, QString*) const override {}
    std::vector<VLMS::Repositories::BookCopyRecord> copiesForBook(std::int64_t) const override
    {
        return {};
    }
    QString resolveCoverPath(const QString& storedPath) const override { return storedPath; }
    std::vector<VLMS::Repositories::BookRecord> booksForCopySource(const QString&,
                                                                   const QString&) const override
    {
        return {};
    }
};

class EmptyMemberLookups final : public VLMS::MemberLookups {
public:
    QString suggestMembershipNumber() const override { return {}; }
    QStringList cities(VLMS::Repositories::ArchiveScope) const override { return {}; }
    QString resolveImagePath(const QString& storedPath) const override { return storedPath; }
};

/// The app's own wiring for widget tests: real dialogs through
/// QtDialogFactory, errors recorded instead of shown. Only the workflows the
/// given repositories allow are built.
struct UiWorkflows {
    UiWorkflows(VLMS::Repositories::CirculationRepository& circulation,
                VLMS::Repositories::CatalogRepository* catalog,
                VLMS::Repositories::MemberRepository* memberRepo)
    {
        circulationLookups = std::make_unique<VLMS::QtCirculationLookups>(circulation);
        if (catalog != nullptr) {
            catalogLookups = std::make_unique<VLMS::QtCatalogLookups>(*catalog);
        }
        if (memberRepo != nullptr) {
            memberLookups = std::make_unique<VLMS::QtMemberLookups>(*memberRepo);
        }
        ocr = std::make_unique<VLMS::OcrService>();
        dialogs = std::make_unique<VLMS::QtDialogFactory>(
            catalogLookups ? static_cast<VLMS::CatalogLookups&>(*catalogLookups) : emptyCatalog,
            memberLookups ? static_cast<VLMS::MemberLookups&>(*memberLookups) : emptyMembers,
            *circulationLookups, *ocr);
        bulk = std::make_unique<VLMS::BulkRunner>(*dialogs, events);
        loans = std::make_unique<VLMS::LoanWorkflow>(circulation, *dialogs, errors, events,
                                                     navigator, *bulk);
        if (catalog == nullptr || memberRepo == nullptr) {
            return;
        }
        loanHistorySpec =
            std::make_unique<VLMS::LoanListSpec>(circulation, *catalog, *memberRepo, *loans);
        books = std::make_unique<VLMS::BookWorkflow>(*catalog, *dialogs, errors, events, *bulk,
                                                     *loans, *loanHistorySpec);
        members = std::make_unique<VLMS::MemberWorkflow>(*memberRepo, *dialogs, errors, events,
                                                         navigator, *bulk, *loans,
                                                         *loanHistorySpec);
        archive = std::make_unique<VLMS::ArchiveWorkflow>(*memberRepo, *catalog, circulation,
                                                          *dialogs, errors, events, *bulk,
                                                          *loanHistorySpec, *loans);
    }

    EmptyCatalogLookups emptyCatalog;
    EmptyMemberLookups emptyMembers;
    std::unique_ptr<VLMS::QtCatalogLookups> catalogLookups;
    std::unique_ptr<VLMS::QtMemberLookups> memberLookups;
    std::unique_ptr<VLMS::QtCirculationLookups> circulationLookups;
    std::unique_ptr<VLMS::OcrService> ocr;
    std::unique_ptr<VLMS::QtDialogFactory> dialogs;
    VLMS::FakeErrorPresenter errors;
    VLMS::DataEvents events;
    VLMS::FakeNavigator navigator;
    std::unique_ptr<VLMS::BulkRunner> bulk;
    std::unique_ptr<VLMS::LoanWorkflow> loans;
    std::unique_ptr<VLMS::LoanListSpec> loanHistorySpec;
    std::unique_ptr<VLMS::BookWorkflow> books;
    std::unique_ptr<VLMS::MemberWorkflow> members;
    std::unique_ptr<VLMS::ArchiveWorkflow> archive;
};

struct LoanWorkflowHarness : UiWorkflows {
    explicit LoanWorkflowHarness(VLMS::Repositories::CirculationRepository& circulation)
        : UiWorkflows(circulation, nullptr, nullptr)
    {
    }
};

struct BookWorkflowHarness : UiWorkflows {
    BookWorkflowHarness(VLMS::Repositories::CatalogRepository& catalog,
                        VLMS::Repositories::CirculationRepository& circulation,
                        VLMS::Repositories::MemberRepository& memberRepo)
        : UiWorkflows(circulation, &catalog, &memberRepo)
    {
    }
};

struct MemberWorkflowHarness : UiWorkflows {
    MemberWorkflowHarness(VLMS::Repositories::MemberRepository& memberRepo,
                          VLMS::Repositories::CirculationRepository& circulation,
                          VLMS::Repositories::CatalogRepository& catalog)
        : UiWorkflows(circulation, &catalog, &memberRepo)
    {
    }
};

struct ArchiveWorkflowHarness : UiWorkflows {
    ArchiveWorkflowHarness(VLMS::Repositories::MemberRepository& memberRepo,
                           VLMS::Repositories::CatalogRepository& catalog,
                           VLMS::Repositories::CirculationRepository& circulation)
        : UiWorkflows(circulation, &catalog, &memberRepo)
    {
    }
};

/// Everything a workflow needs, on a fresh test database.
class WorkflowFixture : public ::testing::Test {
protected:
    static void SetUpTestSuite() { VLMS::Core::Locale::setCode("en"); }
    static void TearDownTestSuite()
    {
        VLMS::Core::Locale::setCode(VLMS::Core::Locale::kDefaultCode);
    }

    void SetUp() override
    {
        db = std::make_unique<::Test::TestDatabase>();
        ASSERT_TRUE(db->isValid()) << db->lastError();
        memberRepo = std::make_unique<VLMS::Repositories::MemberRepository>(
            db->session(), db->resourcesDirectory());
        catalog = std::make_unique<VLMS::Repositories::CatalogRepository>(
            db->session(), db->resourcesDirectory());
        circulation = std::make_unique<VLMS::Repositories::CirculationRepository>(db->session());

        bulk = std::make_unique<VLMS::BulkRunner>(dialogs, events);
        loans = std::make_unique<VLMS::LoanWorkflow>(*circulation, dialogs, errors, events,
                                                     navigator, *bulk);
        loanHistorySpec =
            std::make_unique<VLMS::LoanListSpec>(*circulation, *catalog, *memberRepo, *loans);
        books = std::make_unique<VLMS::BookWorkflow>(*catalog, dialogs, errors, events, *bulk,
                                                     *loans, *loanHistorySpec);
        members = std::make_unique<VLMS::MemberWorkflow>(*memberRepo, dialogs, errors, events,
                                                         navigator, *bulk, *loans,
                                                         *loanHistorySpec);
        archive = std::make_unique<VLMS::ArchiveWorkflow>(*memberRepo, *catalog, *circulation,
                                                          dialogs, errors, events, *bulk,
                                                          *loanHistorySpec, *loans);
        changed = std::make_unique<QSignalSpy>(&events, &VLMS::DataEvents::changed);
        guard = std::make_unique<RealModalGuard>();
    }

    void TearDown() override
    {
        EXPECT_TRUE(guard->opened.isEmpty())
            << "a workflow opened a real window: " << guard->opened.join(", ").toStdString();
        guard.reset();
        changed.reset();
        archive.reset();
        members.reset();
        books.reset();
        loanHistorySpec.reset();
        loans.reset();
        bulk.reset();
        circulation.reset();
        catalog.reset();
        memberRepo.reset();
        db.reset();
    }

    [[nodiscard]] std::string scalarText(const std::string& sql) const
    {
        return db->scalar(sql).toString();
    }
    [[nodiscard]] bool scalarIsNull(const std::string& sql) const { return db->scalar(sql).isNull(); }

    [[nodiscard]] VLMS::EntitySet changedWhat(int i) const
    {
        return changed->at(i).at(0).value<VLMS::EntitySet>();
    }
    [[nodiscard]] QList<qint64> changedIds(int i) const
    {
        return changed->at(i).at(1).value<QList<qint64>>();
    }
    [[nodiscard]] std::vector<std::string> errorKeys() const
    {
        std::vector<std::string> keys;
        for (const auto& call : errors.calls) {
            keys.push_back(call.key);
        }
        return keys;
    }

    std::unique_ptr<::Test::TestDatabase> db;
    std::unique_ptr<VLMS::Repositories::MemberRepository> memberRepo;
    std::unique_ptr<VLMS::Repositories::CatalogRepository> catalog;
    std::unique_ptr<VLMS::Repositories::CirculationRepository> circulation;

    VLMS::FakeDialogFactory dialogs;
    VLMS::FakeErrorPresenter errors;
    VLMS::DataEvents events;
    VLMS::FakeNavigator navigator;
    std::unique_ptr<VLMS::BulkRunner> bulk;
    std::unique_ptr<VLMS::LoanWorkflow> loans;
    std::unique_ptr<VLMS::LoanListSpec> loanHistorySpec;
    std::unique_ptr<VLMS::BookWorkflow> books;
    std::unique_ptr<VLMS::MemberWorkflow> members;
    std::unique_ptr<VLMS::ArchiveWorkflow> archive;

    std::unique_ptr<QSignalSpy> changed;
    std::unique_ptr<RealModalGuard> guard;
    QWidget parent;
};
