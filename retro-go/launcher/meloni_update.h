#pragma once

#include <stdbool.h>

// Shows the update dialogs and downloads new or changed Meloni games from the release.
// Returns true when files on the SD card changed (the tab should be rescanned).
bool meloni_update_run(void);
