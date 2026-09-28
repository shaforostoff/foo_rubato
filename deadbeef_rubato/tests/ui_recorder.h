#ifndef RUBATO_UI_RECORDER_H
#define RUBATO_UI_RECORDER_H

// What the plugin asked the window system to show, for the host test.

#include <memory>
#include <vector>

#include "rubato_ui.h"

namespace recorder
{
	//! What ui::available() answers.
	extern bool available;
	extern int progress_shown;
	extern int tap_shown;
	extern std::shared_ptr<rubato::scan_progress> last_progress;
	//! Every results window asked for, in order. Clear it to give the tracks
	//! back.
	extern std::vector<std::shared_ptr<rubato::results>> results;
}

#endif // RUBATO_UI_RECORDER_H
