#pragma once

#include <QString>

#include <functional>
#include <map>

namespace ManualCapture {

class Capture;

/// One screenshot. Writes <out>/shots/<lang>/<id>.png through Capture::save.
/// Must leave the window where it found it: Catalogue page, no dialog open,
/// light theme, no ticks, empty search.
using ShotFn = std::function<void(Capture&)>;

/// Keyed by shot id, so --only <id> and a stable run order come for free.
using ShotRegistry = std::map<QString, ShotFn>;

void registerGeneralShots(ShotRegistry& registry);   // Task 3
void registerScreenShots(ShotRegistry& registry);    // Task 4
void registerTaskShots(ShotRegistry& registry);      // Task 5

}  // namespace ManualCapture
