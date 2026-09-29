#include "ui/workflows/QtErrorPresenter.h"

#include "ui/UiHelpers.h"

namespace VLMS {

void QtErrorPresenter::show(QWidget* parent, const Core::Error& error)
{
    showRepoError(parent, error);
}

}  // namespace VLMS
