// The GTK windows, on the stand-in for DeaDBeeF with a synthetic scan behind
// them. Not a test: the way to look at the windows without the player.
//
//   rubato_gtk_preview            scan eight made-up tracks and use the
//                                 windows as they are; close them all to quit
//   rubato_gtk_preview <dir>      draw the progress, results and tapping
//                                 windows to PNGs in <dir> and exit

#include <gtk/gtk.h>

#include <cstdio>
#include <string>
#include <vector>

#include "fake_host.h"
#include "preview_tracks.h"
#include "rubato_results.h"
#include "rubato_ui.h"

// After deadbeef.h, which it builds on.
#include <deadbeef/gtkui_api.h>

using namespace fake;

namespace
{

ddb_gtkui_t fake_gtkui;
std::string out_dir;
bool saw_progress = false;

GtkWidget * fake_mainwin() { return nullptr; }

GtkWidget * find_window(const char * title)
{
	GtkWidget * found = nullptr;
	GList * all = gtk_window_list_toplevels();
	for (GList * l = all; l != nullptr; l = l->next)
	{
		GtkWindow * w = GTK_WINDOW(l->data);
		const char * t = gtk_window_get_title(w);
		if (t != nullptr && std::string(t) == title && gtk_widget_get_mapped(GTK_WIDGET(w)))
			found = GTK_WIDGET(w);
	}
	g_list_free(all);
	return found;
}

//! The window's pixels as the display has them, read back rather than
//! redrawn, so what is saved is what would be seen.
void snapshot(GtkWidget * window, const char * name)
{
	GdkWindow * gdk = gtk_widget_get_window(window);
	const int w = gdk_window_get_width(gdk);
	const int h = gdk_window_get_height(gdk);
	GdkPixbuf * pixels = gdk_pixbuf_get_from_window(gdk, 0, 0, w, h);
	const std::string path = out_dir + "/" + name + ".png";
	if (pixels != nullptr)
	{
		gdk_pixbuf_save(pixels, path.c_str(), "png", nullptr, nullptr);
		g_object_unref(pixels);
	}
	std::printf("wrote %s (%dx%d)\n", path.c_str(), w, h);
}

gboolean watch(gpointer)
{
	// A frame after it maps, once it has been laid out.
	static int progress_frames = 0;
	if (!saw_progress)
		if (GtkWidget * p = find_window("Analysing BPMs"))
			if (++progress_frames > 4)
			{
				saw_progress = true;
				snapshot(p, "progress");
			}
	GtkWidget * results = find_window("Rubato BPM Analysis");
	if (results == nullptr) return G_SOURCE_CONTINUE;

	// Double the second row, so the picture shows a scaled one too.
	GtkWidget * view = nullptr;
	std::vector<GtkWidget *> stack(1, results);
	while (!stack.empty() && view == nullptr)
	{
		GtkWidget * w = stack.back();
		stack.pop_back();
		if (GTK_IS_TREE_VIEW(w)) view = w;
		else if (GTK_IS_CONTAINER(w))
		{
			GList * kids = gtk_container_get_children(GTK_CONTAINER(w));
			for (GList * k = kids; k != nullptr; k = k->next) stack.push_back(GTK_WIDGET(k->data));
			g_list_free(kids);
		}
	}
	if (view != nullptr)
	{
		GtkTreePath * row = gtk_tree_path_new_from_indices(1, -1);
		gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), row);
		gtk_tree_path_free(row);
	}
	g_timeout_add(1500, [](gpointer) -> gboolean {
		snapshot(find_window("Rubato BPM Analysis"), "results");
		rubato::ui::show_tap();
		g_timeout_add(1500, [](gpointer) -> gboolean {
			if (GtkWidget * t = find_window("Tap BPM")) snapshot(t, "tap");
			gtk_main_quit();
			return G_SOURCE_REMOVE;
		}, nullptr);
		return G_SOURCE_REMOVE;
	}, nullptr);
	return G_SOURCE_REMOVE;
}

}   // namespace

int main(int argc, char ** argv)
{
	gtk_init(&argc, &argv);
	if (argc > 1) out_dir = argv[1];
	quiet = !out_dir.empty();
	init();

	add_preview_tracks();

	fake_gtkui.gui.plugin.id = DDB_GTKUI_PLUGIN_ID;
	fake_gtkui.gui.plugin.version_major = DDB_GTKUI_API_VERSION_MAJOR;
	fake_gtkui.get_mainwin = fake_mainwin;
	ui_plugin = &fake_gtkui.gui.plugin;

	DB_functions_t api = make_api();
	DB_plugin_t * p = ddb_rubato_load(&api);
	p->start();
	p->connect();

	DB_plugin_action_t * analyse = find_action(p, "rubato_analyse");
	analyse->callback2(analyse, DDB_ACTION_CTX_SELECTION);

	if (!out_dir.empty())
	{
		g_timeout_add(50, [](gpointer) -> gboolean { return watch(nullptr) ? TRUE : FALSE; }, nullptr);
	}
	else
	{
		// Until the last window is closed.
		g_timeout_add(500, [](gpointer) -> gboolean {
			GList * all = gtk_window_list_toplevels();
			bool any = false;
			for (GList * l = all; l != nullptr; l = l->next)
				any = any || gtk_widget_get_visible(GTK_WIDGET(l->data));
			g_list_free(all);
			// The results window goes up only once the scan is done, so
			// nothing on screen for a few seconds running is the end.
			static int idle = 0;
			idle = any ? 0 : idle + 1;
			if (idle < 6) return G_SOURCE_CONTINUE;
			gtk_main_quit();
			return G_SOURCE_REMOVE;
		}, nullptr);
	}
	gtk_main();
	p->stop();
	return 0;
}
