// The Cocoa windows, on the stand-in for DeaDBeeF with a synthetic scan behind
// them. Not a test: the way to look at the windows without the player, as
// rubato_gtk_preview is for the GTK ones.
//
//   rubato_cocoa_preview          scan eight made-up tracks and use the
//                                 windows as they are; close them all to quit
//   rubato_cocoa_preview <dir>    draw the progress, results and tapping
//                                 windows to PNGs in <dir> and exit

#import <Cocoa/Cocoa.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "fake_host.h"
#include "preview_tracks.h"
#include "rubato_results.h"
#include "rubato_ui.h"

using namespace fake;

namespace
{

DB_plugin_t fake_cocoaui;
std::string out_dir;

NSWindow * find_window(NSString * title)
{
	for (NSWindow * w in NSApp.windows)
		if (w.visible && [w.title isEqualToString:title]) return w;
	return nil;
}

template <class T> T * find_view(NSView * root, BOOL (^match)(T *))
{
	if ([root isKindOfClass:[T class]] && match((T *) root)) return (T *) root;
	for (NSView * v in root.subviews)
		if (T * found = find_view<T>(v, match)) return found;
	return nil;
}

//! The whole window, title bar and all, drawn into a bitmap rather than read
//! off the screen, which would want the screen recording permission.
void snapshot(NSWindow * window, const char * name)
{
	if (window == nil) return;
	NSView * frame = window.contentView.superview;
	NSBitmapImageRep * rep = [frame bitmapImageRepForCachingDisplayInRect:frame.bounds];
	[frame cacheDisplayInRect:frame.bounds toBitmapImageRep:rep];
	NSData * png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
	const std::string path = out_dir + "/" + name + ".png";
	[png writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES];
	std::printf("wrote %s (%.0fx%.0f)\n", path.c_str(), frame.bounds.size.width, frame.bounds.size.height);
}

void quit()
{
	[NSApp stop:nil];
	// stop: takes effect after the next event, so there has to be one.
	[NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
	                                modifierFlags:0 timestamp:0 windowNumber:0 context:nil
	                                      subtype:0 data1:0 data2:0]
	         atStart:NO];
}

void after(double seconds, void (^block)(void))
{
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (seconds * NSEC_PER_SEC)),
	               dispatch_get_main_queue(), block);
}

//! Waits for each window in turn, and draws it once it has been laid out.
void watch()
{
	static bool saw_progress = false;
	static int progress_ticks = 0;
	if (!saw_progress)
		if (NSWindow * p = find_window(@"Analysing BPMs"))
			if (++progress_ticks > 4)
			{
				saw_progress = true;
				snapshot(p, "progress");
			}

	NSWindow * results = find_window(@"Rubato BPM Analysis");
	if (results == nil)
	{
		after(0.05, ^{ watch(); });
		return;
	}

	// Double the second row, so the picture shows a scaled one too, with the
	// row still selected.
	NSTableView * table = find_view<NSTableView>(results.contentView, ^BOOL(NSTableView *) { return YES; });
	[table selectRowIndexes:[NSIndexSet indexSetWithIndex:1] byExtendingSelection:NO];
	NSButton * twice = find_view<NSButton>(results.contentView, ^BOOL(NSButton * b) {
		return [b.title isEqualToString:@"Double BPM"];
	});
	[twice performClick:nil];

	after(1.0, ^{
		snapshot(find_window(@"Rubato BPM Analysis"), "results");
		rubato::ui::show_tap();
		after(1.0, ^{
			snapshot(find_window(@"Tap BPM"), "tap");
			quit();
		});
	});
}

//! Until the last window is closed. The results window goes up only once
//! the scan is done, so nothing on screen for a few seconds running is the
//! end.
void watch_until_closed()
{
	static int idle = 0;
	bool any = false;
	for (NSWindow * w in NSApp.windows) any = any || w.visible;
	idle = any ? 0 : idle + 1;
	if (idle >= 6) quit();
	else after(0.5, ^{ watch_until_closed(); });
}

}   // namespace

int main(int argc, char ** argv)
{
	@autoreleasepool
	{
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		if (argc > 1) out_dir = argv[1];
		quiet = !out_dir.empty();
		init();

		add_preview_tracks();

		std::memset(&fake_cocoaui, 0, sizeof(fake_cocoaui));
		fake_cocoaui.type = DB_PLUGIN_GUI;
		fake_cocoaui.id = "cocoaui";
		ui_plugin = &fake_cocoaui;

		DB_functions_t api = make_api();
		DB_plugin_t * p = ddb_rubato_load(&api);
		p->start();
		p->connect();

		DB_plugin_action_t * analyse = find_action(p, "rubato_analyse");
		analyse->callback2(analyse, DDB_ACTION_CTX_SELECTION);

		if (!out_dir.empty()) after(0.05, ^{ watch(); });
		else after(0.5, ^{ watch_until_closed(); });

		[NSApp activateIgnoringOtherApps:YES];
		[NSApp run];
		p->stop();
	}
	return 0;
}
