// A window system that records instead of drawing, for ddb_rubato_hosttest.
// Linked in place of ui_none.cpp or gtk/ui_gtk.cpp; see rubato_ui.h.

#include "ui_recorder.h"

namespace recorder
{
	bool available = false;
	int progress_shown = 0;
	int tap_shown = 0;
	std::shared_ptr<rubato::scan_progress> last_progress;
	std::vector<std::shared_ptr<rubato::results>> results;
}

void rubato::ui::connect(DB_functions_t *) {}
void rubato::ui::shutdown() { recorder::results.clear(); recorder::last_progress.reset(); }
bool rubato::ui::available() { return recorder::available; }

void rubato::ui::show_progress(std::shared_ptr<scan_progress> p)
{
	recorder::progress_shown++;
	recorder::last_progress = p;
}

void rubato::ui::show_results(std::shared_ptr<results> r)
{
	recorder::results.push_back(r);
}

void rubato::ui::show_tap()
{
	recorder::tap_shown++;
}
