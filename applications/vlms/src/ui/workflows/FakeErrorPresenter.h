#pragma once

#include "ui/workflows/ErrorPresenter.h"

#include <string>
#include <vector>

namespace VLMS {

class FakeErrorPresenter final : public ErrorPresenter {
public:
    struct Call {
        QWidget* parent = nullptr;
        std::string key;
    };
    std::vector<Call> calls;

    void show(QWidget* parent, const Core::Error& error) override
    {
        calls.push_back(Call{parent, error.key});
    }
};

}  // namespace VLMS
