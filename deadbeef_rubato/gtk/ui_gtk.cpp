// The plugin's windows in GTK 3. See rubato_ui.h for what they are and
// rubato_results.h for everything they show; this file lays them out and
// passes clicks back, and decides nothing a Cocoa version would have to
// decide again.
//
// Everything here runs on GTK's thread. The five entry points may be called
// from any thread, and hand their work over with g_idle_add.

#include <gtk/gtk.h>

#include "rubato_ui.h"

#include <deadbeef/gtkui_api.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{

DB_functions_t * deadbeef = nullptr;
ddb_gtkui_t * gtkui = nullptr;
std::atomic<bool> ready(false);

//! Every window this file has open, for shutdown to close.
std::set<GtkWidget *> open_windows;

void on_gtk_thread(std::function<void()> fn)
{
	g_idle_add([](gpointer data) -> gboolean {
		std::unique_ptr<std::function<void()>> f(static_cast<std::function<void()> *>(data));
		if (ready.load()) (*f)();
		return G_SOURCE_REMOVE;
	}, new std::function<void()>(std::move(fn)));
}

GtkWindow * main_window()
{
	GtkWidget * w = gtkui != nullptr ? gtkui->get_mainwin() : nullptr;
	return w != nullptr ? GTK_WINDOW(w) : nullptr;
}

//! A top-level window over DeaDBeeF's own, closed by Escape.
GtkWidget * new_window(const char * title, int width, int height)
{
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), title);
	gtk_window_set_default_size(GTK_WINDOW(window), width, height);
	if (GtkWindow * parent = main_window())
	{
		gtk_window_set_transient_for(GTK_WINDOW(window), parent);
		gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ON_PARENT);
	}
	gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_container_set_border_width(GTK_CONTAINER(window), 12);
	open_windows.insert(window);
	g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget * w, gpointer) {
		open_windows.erase(w);
	}), nullptr);
	return window;
}

//! Escape closes. Connected separately from new_window because the tapping
//! window has a key handler of its own that has to see the other keys first.
void close_on_escape(GtkWidget * window)
{
	g_signal_connect(window, "key-press-event", G_CALLBACK(+[](GtkWidget * w, GdkEventKey * e, gpointer) -> gboolean {
		if (e->keyval != GDK_KEY_Escape) return FALSE;
		gtk_widget_destroy(w);
		return TRUE;
	}), nullptr);
}

GtkWidget * left_label(const std::string & text)
{
	GtkWidget * label = gtk_label_new(text.c_str());
	gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	// A wrapping label asks for the width of its whole text on one line
	// unless told otherwise, and a window that is not resizable takes it.
	gtk_label_set_max_width_chars(GTK_LABEL(label), 56);
	return label;
}

// --- the results window ----------------------------------------------------

struct results_window
{
	std::shared_ptr<rubato::results> model;
	GtkWidget * window = nullptr;
	GtkWidget * view = nullptr;
	GtkListStore * store = nullptr;
	GtkTreeViewColumn * title_column = nullptr;
	GtkWidget * double_button = nullptr;
	GtkWidget * halve_button = nullptr;
};

//! Every cell of one row, from the model.
void refresh_row(results_window & w, std::size_t row)
{
	GtkTreeIter iter;
	if (!gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(w.store), &iter, nullptr, static_cast<gint>(row)))
		return;
	const std::vector<rubato::column_id> & columns = w.model->columns();
	for (std::size_t c = 0; c < columns.size(); c++)
		gtk_list_store_set(w.store, &iter, static_cast<gint>(c),
		                   w.model->cell(row, columns[c]).c_str(), -1);
}

void update_scale_buttons(results_window & w)
{
	GtkTreeSelection * selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(w.view));
	const bool any = gtk_tree_selection_count_selected_rows(selection) > 0;
	gtk_widget_set_sensitive(w.double_button, any);
	gtk_widget_set_sensitive(w.halve_button, any);
}

//! The double and halve buttons act on the rows highlighted.
void scale_selection(results_window & w, double factor)
{
	GtkTreeSelection * selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(w.view));
	GList * paths = gtk_tree_selection_get_selected_rows(selection, nullptr);
	for (GList * p = paths; p != nullptr; p = p->next)
	{
		const gint * indices = gtk_tree_path_get_indices(static_cast<GtkTreePath *>(p->data));
		if (indices == nullptr) continue;
		const std::size_t row = static_cast<std::size_t>(indices[0]);
		w.model->scale(row, factor);
		refresh_row(w, row);
	}
	g_list_free_full(paths, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));
}

//! The whole row's tooltip, wherever on the row the pointer rests - the
//! tuning column, on the right, is the one it is mostly for.
gboolean results_query_tooltip(GtkWidget * view, gint x, gint y, gboolean keyboard,
                               GtkTooltip * tooltip, gpointer data)
{
	results_window & w = *static_cast<results_window *>(data);
	GtkTreeModel * model = nullptr;
	GtkTreePath * path = nullptr;
	GtkTreeIter iter;
	if (!gtk_tree_view_get_tooltip_context(GTK_TREE_VIEW(view), &x, &y, keyboard,
	                                       &model, &path, &iter))
		return FALSE;
	const gint * indices = gtk_tree_path_get_indices(path);
	const std::size_t row = indices != nullptr ? static_cast<std::size_t>(indices[0]) : 0;

	// Whether the title column is drawing the title cut short: the one case
	// where repeating the title says something the reader cannot see.
	GdkRectangle cell;
	gtk_tree_view_get_cell_area(GTK_TREE_VIEW(view), path, w.title_column, &cell);
	const std::string title = w.model->cell(row, rubato::column_title);
	PangoLayout * layout = gtk_widget_create_pango_layout(view, title.c_str());
	int text_width = 0;
	pango_layout_get_pixel_size(layout, &text_width, nullptr);
	g_object_unref(layout);
	const int padding = 8;
	const bool clipped = text_width + padding > cell.width;

	const std::string text = w.model->tooltip(row, clipped);
	if (!text.empty())
	{
		gtk_tooltip_set_text(tooltip, text.c_str());
		gtk_tree_view_set_tooltip_row(GTK_TREE_VIEW(view), tooltip, path);
	}
	gtk_tree_path_free(path);
	return text.empty() ? FALSE : TRUE;
}

void open_results(std::shared_ptr<rubato::results> model)
{
	results_window * w = new results_window();
	w->model = std::move(model);
	const std::vector<rubato::column_id> & columns = w->model->columns();

	w->window = new_window("Rubato BPM Analysis", 900, 460);
	close_on_escape(w->window);
	g_signal_connect(w->window, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer data) {
		delete static_cast<results_window *>(data);
	}), w);

	GtkWidget * vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_add(GTK_CONTAINER(w->window), vbox);

	// One string per column, in the order the model lists them.
	std::vector<GType> types(columns.size(), G_TYPE_STRING);
	w->store = gtk_list_store_newv(static_cast<gint>(types.size()), types.data());
	for (std::size_t row = 0; row < w->model->size(); row++)
	{
		GtkTreeIter iter;
		gtk_list_store_append(w->store, &iter);
		refresh_row(*w, row);
	}

	w->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(w->store));
	g_object_unref(w->store);   // the view holds it now
	gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(w->view), GTK_TREE_VIEW_GRID_LINES_VERTICAL);
	for (std::size_t c = 0; c < columns.size(); c++)
	{
		GtkCellRenderer * renderer = gtk_cell_renderer_text_new();
		GtkTreeViewColumn * column = gtk_tree_view_column_new_with_attributes(
			rubato::results::heading(columns[c]), renderer, "text", static_cast<gint>(c), nullptr);
		gtk_tree_view_column_set_resizable(column, TRUE);
		if (columns[c] == rubato::column_title)
		{
			// The title takes what the others leave, and is cut short with
			// an ellipsis rather than pushing them out of the window.
			g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, nullptr);
			gtk_tree_view_column_set_expand(column, TRUE);
			gtk_tree_view_column_set_min_width(column, 160);
			w->title_column = column;
		}
		else if (rubato::results::column_numeric(columns[c]))
		{
			g_object_set(renderer, "xalign", 1.0f, nullptr);
			gtk_tree_view_column_set_alignment(column, 1.0f);
		}
		gtk_tree_view_append_column(GTK_TREE_VIEW(w->view), column);
	}
	GtkTreeSelection * selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(w->view));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);
	gtk_widget_set_has_tooltip(w->view, TRUE);
	g_signal_connect(w->view, "query-tooltip", G_CALLBACK(results_query_tooltip), w);

	GtkWidget * scroller = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
	                               GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroller), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(scroller), w->view);
	gtk_box_pack_start(GTK_BOX(vbox), scroller, TRUE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(vbox), left_label(w->model->destination()), FALSE, FALSE, 0);

	// Double and halve on the left, acting on the selection; Cancel and the
	// commit button on the right, acting on everything, and saying so.
	GtkWidget * buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_pack_start(GTK_BOX(vbox), buttons, FALSE, FALSE, 0);

	w->double_button = gtk_button_new_with_mnemonic("_Double BPM");
	w->halve_button = gtk_button_new_with_mnemonic("_Halve BPM");
	gtk_box_pack_start(GTK_BOX(buttons), w->double_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(buttons), w->halve_button, FALSE, FALSE, 0);
	g_signal_connect(w->double_button, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
		scale_selection(*static_cast<results_window *>(data), 2.0);
	}), w);
	g_signal_connect(w->halve_button, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
		scale_selection(*static_cast<results_window *>(data), 0.5);
	}), w);
	g_signal_connect(selection, "changed", G_CALLBACK(+[](GtkTreeSelection *, gpointer data) {
		update_scale_buttons(*static_cast<results_window *>(data));
	}), w);

	GtkWidget * commit = gtk_button_new_with_label(w->model->commit_label().c_str());
	GtkWidget * cancel = gtk_button_new_with_mnemonic("_Cancel");
	gtk_box_pack_end(GTK_BOX(buttons), commit, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(buttons), cancel, FALSE, FALSE, 0);
	g_signal_connect(commit, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
		results_window & w = *static_cast<results_window *>(data);
		w.model->commit();
		gtk_widget_destroy(w.window);
	}), w);
	g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_widget_destroy), w->window);

	gtk_widget_set_can_default(commit, TRUE);
	gtk_window_set_default(GTK_WINDOW(w->window), commit);
	update_scale_buttons(*w);
	gtk_widget_show_all(w->window);
	gtk_widget_grab_focus(w->view);
}

// --- the progress window ---------------------------------------------------

//! Owned by its timer, which outlives the window: a Cancel closes the window
//! at once, and the timer goes on until the scan has noticed and stopped.
struct progress_window
{
	std::shared_ptr<rubato::scan_progress> progress;
	std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
	GtkWidget * window = nullptr;
	GtkWidget * count = nullptr;
	GtkWidget * items = nullptr;
	GtkWidget * bar = nullptr;
	bool dismissed = false;   //!< cancelled, or closed from outside
};

void build_progress(progress_window & p)
{
	p.window = new_window("Analysing BPMs", 460, -1);
	gtk_window_set_resizable(GTK_WINDOW(p.window), FALSE);
	g_signal_connect(p.window, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer data) {
		progress_window & p = *static_cast<progress_window *>(data);
		p.window = nullptr;
		if (!p.dismissed)
		{
			// Closed by the window manager or by shutdown: either way, the
			// scan is no longer wanted.
			p.dismissed = true;
			p.progress->cancel();
		}
	}), &p);
	close_on_escape(p.window);

	GtkWidget * vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_add(GTK_CONTAINER(p.window), vbox);
	p.count = left_label("");
	p.items = left_label("");
	gtk_label_set_ellipsize(GTK_LABEL(p.items), PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_line_wrap(GTK_LABEL(p.items), FALSE);
	p.bar = gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(vbox), p.count, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), p.bar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), p.items, FALSE, FALSE, 0);

	GtkWidget * buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget * cancel = gtk_button_new_with_mnemonic("_Cancel");
	gtk_box_pack_end(GTK_BOX(buttons), cancel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), buttons, FALSE, FALSE, 0);
	g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_widget_destroy), p.window);

	gtk_widget_show_all(p.window);
}

gboolean progress_tick(gpointer data)
{
	progress_window * p = static_cast<progress_window *>(data);
	const rubato::scan_progress::snapshot s = p->progress->read();

	if (s.finished || !ready.load())
	{
		p->dismissed = true;
		if (p->window != nullptr) gtk_widget_destroy(p->window);
		delete p;
		return G_SOURCE_REMOVE;
	}
	if (p->dismissed) return G_SOURCE_CONTINUE;

	// Not at all for a scan over before it would have been read: one track
	// is often analysed in less time than a window takes to draw.
	if (p->window == nullptr)
	{
		if (std::chrono::steady_clock::now() - p->started < std::chrono::milliseconds(600))
			return G_SOURCE_CONTINUE;
		build_progress(*p);
	}

	const std::string count = "Analysing " + std::to_string(std::min(s.done + 1, s.total))
	                        + " of " + std::to_string(s.total)
	                        + (s.total == 1 ? " track" : " tracks");
	gtk_label_set_text(GTK_LABEL(p->count), count.c_str());
	gtk_label_set_text(GTK_LABEL(p->items), s.in_flight.c_str());
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(p->bar), s.fraction);
	return G_SOURCE_CONTINUE;
}

// --- the tapping window ----------------------------------------------------

struct tap_window
{
	explicit tap_window(const rubato::settings & s) : meter(s), config(s) {}

	rubato::tap_meter meter;
	rubato::settings config;
	GtkWidget * window = nullptr;
	GtkWidget * playing = nullptr;
	GtkWidget * bpm = nullptr;
	GtkWidget * taps = nullptr;
	GtkWidget * write = nullptr;
	guint timer = 0;
	bool something_playing = false;
};

tap_window * the_tap_window = nullptr;

void tap_refresh(tap_window & t)
{
	const std::string bpm = t.meter.text();
	const std::string markup = "<span size=\"xx-large\" weight=\"bold\">"
	                         + (bpm.empty() ? std::string("\xe2\x80\x94") : bpm) + "</span>";
	gtk_label_set_markup(GTK_LABEL(t.bpm), markup.c_str());

	std::string taps;
	if (t.meter.taps() == 0)
		taps = "Tap along with the beat: click Tap, or press the space bar.";
	else
		taps = std::to_string(t.meter.taps()) + (t.meter.taps() == 1 ? " tap" : " taps")
		     + ", averaging the last " + std::to_string(t.config.taps_to_average)
		     + ". A pause of " + std::to_string(t.config.seconds_to_reset)
		     + " seconds starts again.";
	gtk_label_set_text(GTK_LABEL(t.taps), taps.c_str());
	gtk_widget_set_sensitive(t.write, t.something_playing && t.meter.bpm() > 0);
}

//! Which track the BPM will go to, kept current: it is whatever is playing
//! when Write is clicked, not when the window was opened.
gboolean tap_poll(gpointer data)
{
	tap_window & t = *static_cast<tap_window *>(data);
	const std::string title = rubato::playing_title();
	t.something_playing = !title.empty();
	gtk_label_set_text(GTK_LABEL(t.playing),
	                   title.empty() ? "Nothing is playing." : ("Playing: " + title).c_str());
	tap_refresh(t);
	return G_SOURCE_CONTINUE;
}

void tap_now(tap_window & t)
{
	t.meter.tap(rubato::tap_meter::clock::now());
	tap_refresh(t);
}

void open_tap()
{
	if (the_tap_window != nullptr)
	{
		gtk_window_present(GTK_WINDOW(the_tap_window->window));
		return;
	}

	tap_window * t = new tap_window(rubato::read_settings());
	the_tap_window = t;
	t->window = new_window("Tap BPM", 420, -1);
	gtk_window_set_resizable(GTK_WINDOW(t->window), FALSE);
	g_signal_connect(t->window, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer data) {
		tap_window * t = static_cast<tap_window *>(data);
		g_source_remove(t->timer);
		the_tap_window = nullptr;
		delete t;
	}), t);

	// The space bar taps wherever the focus is, rather than activating
	// whichever button last had it - pressing Reset by accident halfway
	// through a count would lose it. Taps land on key down, not up.
	g_signal_connect(t->window, "key-press-event", G_CALLBACK(+[](GtkWidget * w, GdkEventKey * e, gpointer data) -> gboolean {
		if (e->keyval == GDK_KEY_Escape) { gtk_widget_destroy(w); return TRUE; }
		if (e->keyval != GDK_KEY_space) return FALSE;
		tap_now(*static_cast<tap_window *>(data));
		return TRUE;
	}), t);

	GtkWidget * vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_add(GTK_CONTAINER(t->window), vbox);

	t->playing = left_label("");
	gtk_label_set_line_wrap(GTK_LABEL(t->playing), FALSE);
	gtk_label_set_ellipsize(GTK_LABEL(t->playing), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start(GTK_BOX(vbox), t->playing, FALSE, FALSE, 0);

	t->bpm = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(vbox), t->bpm, FALSE, FALSE, 4);

	// On the press, not the click: a click lands when the button comes back
	// up, which adds however long it was held to every interval.
	GtkWidget * tap = gtk_button_new_with_label("Tap");
	gtk_widget_set_size_request(tap, -1, 64);
	gtk_widget_set_can_focus(tap, FALSE);
	gtk_box_pack_start(GTK_BOX(vbox), tap, FALSE, FALSE, 0);
	g_signal_connect(tap, "button-press-event", G_CALLBACK(+[](GtkWidget *, GdkEventButton * e, gpointer data) -> gboolean {
		if (e->type == GDK_BUTTON_PRESS && e->button == 1) tap_now(*static_cast<tap_window *>(data));
		return FALSE;
	}), t);

	t->taps = left_label("");
	gtk_box_pack_start(GTK_BOX(vbox), t->taps, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(vbox), left_label(rubato::bpm_destination(t->config)
		+ " A tapped BPM replaces the measured one and removes INITIALBPM and BpmAlgorithm."),
		FALSE, FALSE, 0);

	GtkWidget * buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_pack_start(GTK_BOX(vbox), buttons, FALSE, FALSE, 0);
	GtkWidget * reset = gtk_button_new_with_mnemonic("_Reset");
	gtk_widget_set_can_focus(reset, FALSE);
	gtk_box_pack_start(GTK_BOX(buttons), reset, FALSE, FALSE, 0);
	g_signal_connect(reset, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
		tap_window & t = *static_cast<tap_window *>(data);
		t.meter.reset();
		tap_refresh(t);
	}), t);

	GtkWidget * close = gtk_button_new_with_mnemonic("_Close");
	t->write = gtk_button_new_with_mnemonic("_Write to playing track");
	gtk_widget_set_can_focus(close, FALSE);
	gtk_widget_set_can_focus(t->write, FALSE);
	gtk_box_pack_end(GTK_BOX(buttons), t->write, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(buttons), close, FALSE, FALSE, 0);
	g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_destroy), t->window);
	g_signal_connect(t->write, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
		tap_window & t = *static_cast<tap_window *>(data);
		if (rubato::write_tapped_bpm(t.meter.bpm()))
		{
			t.meter.reset();
			tap_refresh(t);
		}
	}), t);

	t->timer = g_timeout_add(500, tap_poll, t);
	tap_poll(t);
	gtk_widget_show_all(t->window);
}

}   // namespace

// --- rubato_ui.h -----------------------------------------------------------

void rubato::ui::connect(DB_functions_t * api)
{
	deadbeef = api;
	// These windows are GTK 3, and belong only in DeaDBeeF's GTK 3 interface.
	// Under any other - GTK 2, or none - the plugin goes on without them.
	gtkui = reinterpret_cast<ddb_gtkui_t *>(deadbeef->plug_get_for_id(DDB_GTKUI_PLUGIN_ID));
	ready = gtkui != nullptr && gtkui->gui.plugin.version_major == DDB_GTKUI_API_VERSION_MAJOR;
}

void rubato::ui::shutdown()
{
	if (!ready.exchange(false)) return;
	// Closing a window gives back its tracks. A copy, because each window
	// takes itself out of the set as it goes.
	const std::set<GtkWidget *> windows = open_windows;
	for (GtkWidget * w : windows) gtk_widget_destroy(w);
}

bool rubato::ui::available()
{
	return ready.load();
}

void rubato::ui::show_progress(std::shared_ptr<scan_progress> progress)
{
	on_gtk_thread([progress]() {
		progress_window * p = new progress_window();
		p->progress = progress;
		g_timeout_add(100, progress_tick, p);
	});
}

void rubato::ui::show_results(std::shared_ptr<results> r)
{
	on_gtk_thread([r]() { open_results(r); });
}

void rubato::ui::show_tap()
{
	on_gtk_thread([]() { open_tap(); });
}
