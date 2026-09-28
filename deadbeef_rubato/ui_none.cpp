// No windows: the build without RUBATO_DDB_GTK. See rubato_ui.h.

#include "rubato_ui.h"

namespace rubato
{
namespace ui
{

void connect(DB_functions_t *) {}
void shutdown() {}
bool available() { return false; }
void show_progress(std::shared_ptr<scan_progress>) {}
void show_results(std::shared_ptr<results>) {}
void show_tap() {}

}   // namespace ui
}   // namespace rubato
