#include "ui/workflows/QtErrorPresenter.h"

#include "ui/views/UiHelpers.h"

namespace VLMS {

void QtErrorPresenter::show(QWidget* parent, const Core::Error& error)
{
    showRepoError(parent, error);
}

}  // namespace VLMS
