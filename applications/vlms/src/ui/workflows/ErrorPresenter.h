#pragma once

#include <VLMS/Core/Result.h>

class QWidget;

namespace VLMS {

class ErrorPresenter {
public:
    virtual ~ErrorPresenter() = default;
    virtual void show(QWidget* parent, const Core::Error& error) = 0;
};

}  // namespace VLMS
