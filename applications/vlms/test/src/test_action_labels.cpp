#include "TestDatabase.h"
#include "WorkflowTestSupport.h"

#include "ui/presentation/ArchivedBookSpec.h"
#include "ui/presentation/ArchivedCopySpec.h"
#include "ui/presentation/ArchivedLoanSpec.h"
#include "ui/presentation/ArchivedMemberSpec.h"
#include "ui/presentation/BookListSpec.h"
#include "ui/presentation/LoanListSpec.h"
#include "ui/presentation/MemberListSpec.h"

#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using namespace VLMS;
using namespace Test;

// A label key missing from a table falls back to the key itself, so a typo or
// a forgotten entry shows up as "tickAll" on a button in every language. Each
// list's buttons and context menu come from actions(); every label they use
// has to exist in all three tables.
TEST(ActionLabels, EveryListActionIsWrittenInEveryLanguage)
{
    TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();
    Repositories::CatalogRepository catalog(db.session(), db.resourcesDirectory());
    Repositories::MemberRepository members(db.session(), db.resourcesDirectory());
    Repositories::CirculationRepository circulation(db.session());
    UiWorkflows workflows(circulation, &catalog, &members);

    std::vector<std::unique_ptr<ListSpec>> specs;
    specs.push_back(std::make_unique<MemberListSpec>(members, *workflows.members));
    specs.push_back(std::make_unique<BookListSpec>(catalog, *workflows.books));
    specs.push_back(std::make_unique<LoanListSpec>(circulation, catalog, members, *workflows.loans));
    specs.push_back(std::make_unique<ArchivedMemberSpec>(members, *workflows.archive));
    specs.push_back(std::make_unique<ArchivedBookSpec>(catalog, circulation, *workflows.archive));
    specs.push_back(std::make_unique<ArchivedCopySpec>(catalog, *workflows.archive));
    specs.push_back(
        std::make_unique<ArchivedLoanSpec>(circulation, catalog, members, *workflows.archive));

    for (const auto& spec : specs) {
        for (const PageAction& action : spec->actions()) {
            for (const char* locale : {"ar", "fr", "en"}) {
                SCOPED_TRACE(spec->id().toStdString() + " " + action.id.toStdString() + " "
                             + locale);
                EXPECT_FALSE(
                    Core::Strings::rawValue(locale, action.labelKey.toStdString()).empty())
                    << "missing label key " << action.labelKey.toStdString();
            }
        }
    }
}
