#pragma once

#include "ui/presentation/LoanListSpec.h"
#include "ui/presentation/QtCatalogLookups.h"
#include "ui/presentation/QtCirculationLookups.h"
#include "ui/presentation/QtMemberLookups.h"
#include "ui/services/OcrService.h"
#include "ui/workflows/ArchiveWorkflow.h"
#include "ui/workflows/BookWorkflow.h"
#include "ui/workflows/BulkRunner.h"
#include "ui/workflows/DataEvents.h"
#include "ui/workflows/FakeDialogFactory.h"
#include "ui/workflows/FakeErrorPresenter.h"
#include "ui/workflows/LoanWorkflow.h"
#include "ui/workflows/MemberWorkflow.h"
#include "ui/workflows/Navigator.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <memory>

struct LoanWorkflowHarness {
    VLMS::FakeDialogFactory dialogs;
    VLMS::FakeErrorPresenter errors;
    VLMS::DataEvents events;
    VLMS::FakeNavigator navigator;
    std::unique_ptr<VLMS::QtCirculationLookups> lookups;
    std::unique_ptr<VLMS::BulkRunner> bulk;
    std::unique_ptr<VLMS::LoanWorkflow> loans;

    explicit LoanWorkflowHarness(VLMS::Repositories::CirculationRepository& circulation)
    {
        lookups = std::make_unique<VLMS::QtCirculationLookups>(circulation);
        bulk = std::make_unique<VLMS::BulkRunner>(dialogs, errors, events);
        loans = std::make_unique<VLMS::LoanWorkflow>(
            circulation, *lookups, dialogs, errors, events, navigator, *bulk);
    }
};

struct BookWorkflowHarness {
    VLMS::FakeDialogFactory dialogs;
    VLMS::FakeErrorPresenter errors;
    VLMS::DataEvents events;
    VLMS::FakeNavigator navigator;
    std::unique_ptr<VLMS::QtCatalogLookups> lookups;
    std::unique_ptr<VLMS::QtCirculationLookups> circulationLookups;
    std::unique_ptr<VLMS::OcrService> ocr;
    std::unique_ptr<VLMS::BulkRunner> bulk;
    std::unique_ptr<VLMS::LoanWorkflow> loans;
    std::unique_ptr<VLMS::LoanListSpec> loanHistorySpec;
    std::unique_ptr<VLMS::BookWorkflow> books;

    BookWorkflowHarness(VLMS::Repositories::CatalogRepository& catalog,
                        VLMS::Repositories::CirculationRepository& circulation,
                        VLMS::Repositories::MemberRepository& members)
    {
        lookups = std::make_unique<VLMS::QtCatalogLookups>(catalog);
        circulationLookups = std::make_unique<VLMS::QtCirculationLookups>(circulation);
        ocr = std::make_unique<VLMS::OcrService>();
        bulk = std::make_unique<VLMS::BulkRunner>(dialogs, errors, events);
        loans = std::make_unique<VLMS::LoanWorkflow>(
            circulation, *circulationLookups, dialogs, errors, events, navigator, *bulk);
        loanHistorySpec =
            std::make_unique<VLMS::LoanListSpec>(circulation, catalog, members, *loans);
        books = std::make_unique<VLMS::BookWorkflow>(
            catalog, circulation, *lookups, *ocr, dialogs, errors, events, navigator, *bulk,
            *loans, *loanHistorySpec);
    }
};

struct MemberWorkflowHarness {
    VLMS::FakeDialogFactory dialogs;
    VLMS::FakeErrorPresenter errors;
    VLMS::DataEvents events;
    VLMS::FakeNavigator navigator;
    std::unique_ptr<VLMS::QtMemberLookups> lookups;
    std::unique_ptr<VLMS::QtCirculationLookups> circulationLookups;
    std::unique_ptr<VLMS::BulkRunner> bulk;
    std::unique_ptr<VLMS::LoanWorkflow> loans;
    std::unique_ptr<VLMS::LoanListSpec> loanHistorySpec;
    std::unique_ptr<VLMS::MemberWorkflow> members;

    MemberWorkflowHarness(VLMS::Repositories::MemberRepository& memberRepo,
                          VLMS::Repositories::CirculationRepository& circulation,
                          VLMS::Repositories::CatalogRepository& catalog)
    {
        lookups = std::make_unique<VLMS::QtMemberLookups>(memberRepo);
        circulationLookups = std::make_unique<VLMS::QtCirculationLookups>(circulation);
        bulk = std::make_unique<VLMS::BulkRunner>(dialogs, errors, events);
        loans = std::make_unique<VLMS::LoanWorkflow>(
            circulation, *circulationLookups, dialogs, errors, events, navigator, *bulk);
        loanHistorySpec =
            std::make_unique<VLMS::LoanListSpec>(circulation, catalog, memberRepo, *loans);
        members = std::make_unique<VLMS::MemberWorkflow>(
            memberRepo, circulation, *lookups, dialogs, errors, events, navigator, *bulk, *loans,
            *loanHistorySpec);
    }
};

struct ArchiveWorkflowHarness {
    VLMS::FakeDialogFactory dialogs;
    VLMS::FakeErrorPresenter errors;
    VLMS::DataEvents events;
    VLMS::FakeNavigator navigator;
    std::unique_ptr<VLMS::QtCatalogLookups> lookups;
    std::unique_ptr<VLMS::QtCirculationLookups> circulationLookups;
    std::unique_ptr<VLMS::OcrService> ocr;
    std::unique_ptr<VLMS::BulkRunner> bulk;
    std::unique_ptr<VLMS::LoanWorkflow> loans;
    std::unique_ptr<VLMS::LoanListSpec> loanHistorySpec;
    std::unique_ptr<VLMS::ArchiveWorkflow> archive;

    ArchiveWorkflowHarness(VLMS::Repositories::MemberRepository& members,
                           VLMS::Repositories::CatalogRepository& catalog,
                           VLMS::Repositories::CirculationRepository& circulation)
    {
        lookups = std::make_unique<VLMS::QtCatalogLookups>(catalog);
        circulationLookups = std::make_unique<VLMS::QtCirculationLookups>(circulation);
        ocr = std::make_unique<VLMS::OcrService>();
        bulk = std::make_unique<VLMS::BulkRunner>(dialogs, errors, events);
        loans = std::make_unique<VLMS::LoanWorkflow>(
            circulation, *circulationLookups, dialogs, errors, events, navigator, *bulk);
        loanHistorySpec =
            std::make_unique<VLMS::LoanListSpec>(circulation, catalog, members, *loans);
        archive = std::make_unique<VLMS::ArchiveWorkflow>(
            members, catalog, circulation, *lookups, *ocr, dialogs, errors, events, navigator,
            *bulk, *loanHistorySpec, *loans);
    }
};
