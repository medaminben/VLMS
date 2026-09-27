#include "Application.h"
#include "ui/MainWindow.h"
#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <VLMS/Core/Strings.h>

using VLMS::T;
using VLMS::cd;
using VLMS::qd;
using VLMS::qs;
using VLMS::qsl;
using VLMS::ss;
using VLMS::svl;

int main(int argc, char* argv[]) {
    Application app(argc, argv);

    if (!app.isDatabaseReady()) {
        // Nothing in the window works without a database, and every page would
        // fail one silent query at a time. Say it once, plainly, and stop.
        VLMS::showCritical(
            nullptr,
            VLMS::T("common.error"),
            VLMS::T("app.databaseUnavailable"));
        return 1;
    }

    MainWindow window;
    window.showMaximized();

    return app.exec();
}
