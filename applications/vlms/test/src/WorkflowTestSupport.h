#pragma once

#include "ui/workflows/BookWorkflow.h"
#include "ui/workflows/BulkRunner.h"
#include "ui/workflows/DataEvents.h"
#include "ui/workflows/FakeDialogFactory.h"
#include "ui/workflows/FakeErrorPresenter.h"
#include "ui/workflows/Navigator.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>

#include <memory>

struct BookWorkflowHarness {
    VLMS::FakeDialogFactory dialogs;
    VLMS::FakeErrorPresenter errors;
    VLMS::DataEvents events;
    VLMS::FakeNavigator navigator;
    std::unique_ptr<VLMS::BulkRunner> bulk;
    std::unique_ptr<VLMS::BookWorkflow> books;

    BookWorkflowHarness(VLMS::Repositories::CatalogRepository& catalog,
                        VLMS::Repositories::CirculationRepository& circulation)
    {
        bulk = std::make_unique<VLMS::BulkRunner>(dialogs, errors, events);
        books = std::make_unique<VLMS::BookWorkflow>(
            catalog, circulation, dialogs, errors, events, navigator, *bulk);
    }
};
