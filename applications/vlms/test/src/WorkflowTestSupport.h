#pragma once

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
    std::unique_ptr<VLMS::BulkRunner> bulk;
    std::unique_ptr<VLMS::LoanWorkflow> loans;

    explicit LoanWorkflowHarness(VLMS::Repositories::CirculationRepository& circulation)
    {
        bulk = std::make_unique<VLMS::BulkRunner>(dialogs, errors, events);
        loans = std::make_unique<VLMS::LoanWorkflow>(
            circulation, dialogs, errors, events, navigator, *bulk);
    }
};

struct BookWorkflowHarness {
    VLMS::FakeDialogFactory dialogs;
    VLMS::FakeErrorPresenter errors;
    VLMS::DataEvents events;
    VLMS::FakeNavigator navigator;
    std::unique_ptr<VLMS::BulkRunner> bulk;
    std::unique_ptr<VLMS::LoanWorkflow> loans;
    std::unique_ptr<VLMS::BookWorkflow> books;

    BookWorkflowHarness(VLMS::Repositories::CatalogRepository& catalog,
                        VLMS::Repositories::CirculationRepository& circulation)
    {
        bulk = std::make_unique<VLMS::BulkRunner>(dialogs, errors, events);
        loans = std::make_unique<VLMS::LoanWorkflow>(
            circulation, dialogs, errors, events, navigator, *bulk);
        books = std::make_unique<VLMS::BookWorkflow>(
            catalog, circulation, dialogs, errors, events, navigator, *bulk, *loans);
    }
};

struct MemberWorkflowHarness {
    VLMS::FakeDialogFactory dialogs;
    VLMS::FakeErrorPresenter errors;
    VLMS::DataEvents events;
    VLMS::FakeNavigator navigator;
    std::unique_ptr<VLMS::BulkRunner> bulk;
    std::unique_ptr<VLMS::LoanWorkflow> loans;
    std::unique_ptr<VLMS::MemberWorkflow> members;

    MemberWorkflowHarness(VLMS::Repositories::MemberRepository& memberRepo,
                          VLMS::Repositories::CirculationRepository& circulation)
    {
        bulk = std::make_unique<VLMS::BulkRunner>(dialogs, errors, events);
        loans = std::make_unique<VLMS::LoanWorkflow>(
            circulation, dialogs, errors, events, navigator, *bulk);
        members = std::make_unique<VLMS::MemberWorkflow>(
            memberRepo, circulation, dialogs, errors, events, navigator, *bulk, *loans);
    }
};

struct ArchiveWorkflowHarness {
    VLMS::FakeDialogFactory dialogs;
    VLMS::FakeErrorPresenter errors;
    VLMS::DataEvents events;
    VLMS::FakeNavigator navigator;
    std::unique_ptr<VLMS::BulkRunner> bulk;
    std::unique_ptr<VLMS::ArchiveWorkflow> archive;

    ArchiveWorkflowHarness(VLMS::Repositories::MemberRepository& members,
                           VLMS::Repositories::CatalogRepository& catalog,
                           VLMS::Repositories::CirculationRepository& circulation)
    {
        bulk = std::make_unique<VLMS::BulkRunner>(dialogs, errors, events);
        archive = std::make_unique<VLMS::ArchiveWorkflow>(
            members, catalog, circulation, dialogs, errors, events, navigator, *bulk);
    }
};
