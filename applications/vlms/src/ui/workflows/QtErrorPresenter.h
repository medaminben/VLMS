#pragma once

#include "ui/workflows/ErrorPresenter.h"

namespace VLMS {

class QtErrorPresenter final : public ErrorPresenter {
public:
    void show(QWidget* parent, const Core::Error& error) override;
};

}  // namespace VLMS
