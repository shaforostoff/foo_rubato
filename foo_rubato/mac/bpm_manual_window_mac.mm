// The manual tap window, macOS side. The Windows counterpart is
// bpm_manual_dialog.cpp.
//
// The averaging is the same: keep the last N tap times, average the gaps
// between them, and report 60 over that. What differs is the clock -
// steady_clock rather than QueryPerformanceCounter - and that the tap
// registers on mouse down rather than on the click completing, which is the
// TODO the Windows dialog carries: a click held for 80ms is 80ms of error in
// a measurement whose whole content is when the button went down.

#import <Cocoa/Cocoa.h>

#include <SDK/foobar2000.h>

#include <algorithm>
#include <chrono>
#include <vector>

#include "../bpm_ui.h"
#include "../file_info_filter_bpm.h"
#include "../format_bpm.h"
#include "../globals.h"
#include "../preferences.h"
#include "mac_strings.h"

namespace
{
	typedef std::chrono::steady_clock tap_clock;

	//! How many taps to hold, from the preference, but never fewer than two.
	//!
	//! The preference's own floor is one, and one tap is no gap and so no
	//! tempo: the window would take a tap, drop the tap before it, and never
	//! have two to measure between. Two is the smallest number that answers.
	std::size_t taps_to_keep()
	{
		const long long configured = (long long) bpm_config_taps_to_average;
		return (std::size_t) std::max<long long>(2, configured);
	}
}

@interface fooRubatoTapWindow : NSWindowController <NSWindowDelegate>
@end


@implementation fooRubatoTapWindow
{
	std::vector<tap_clock::time_point> _taps;
	double _bpm;

	NSTextField * _bpmDisplay;
	NSButton    * _updateButton;
}

//! Open windows; see the same static in bpm_result_window_mac.mm for why.
static NSMutableArray<fooRubatoTapWindow *> * g_openWindows = nil;

- (instancetype)init
{
	NSWindow * window =
		[[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 320, 260)
		                            styleMask:NSWindowStyleMaskTitled |
		                                      NSWindowStyleMaskClosable
		                              backing:NSBackingStoreBuffered
		                                defer:NO];
	window.title = @"Tap BPM";
	window.releasedWhenClosed = NO;
	[window center];

	self = [super initWithWindow:window];
	if (self == nil) return nil;

	_bpm = 0.0;
	[self buildContent];
	window.delegate = self;
	[self resetBPM];

	return self;
}

- (void)buildContent
{
	NSView * root = self.window.contentView;

	_bpmDisplay = [NSTextField labelWithString:@"0"];
	_bpmDisplay.font = [NSFont monospacedDigitSystemFontOfSize:56 weight:NSFontWeightLight];
	_bpmDisplay.alignment = NSTextAlignmentCenter;

	NSTextField * unit = [NSTextField labelWithString:@"BPM"];
	unit.alignment = NSTextAlignmentCenter;
	unit.textColor = NSColor.secondaryLabelColor;

	NSButton * tap = [NSButton buttonWithTitle:@"Tap" target:self action:@selector(onTap:)];
	tap.keyEquivalent = @" ";
	tap.controlSize = NSControlSizeLarge;
	// On the way down, not on the way back up - see the note at the top.
	[tap sendActionOn:NSEventMaskLeftMouseDown];
	[tap.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;

	NSButton * reset = [NSButton buttonWithTitle:@"Reset" target:self action:@selector(onReset:)];
	_updateButton = [NSButton buttonWithTitle:@"Write to the playing track"
	                                   target:self
	                                   action:@selector(onUpdateFile:)];

	pfc::string_formatter tagLine;
	tagLine << "BPM will be written to %" << bpm_tag_name().get_ptr() << "% tag.";
	NSTextField * tagLabel = [NSTextField labelWithString:fooRubatoStr(tagLine.get_ptr())];
	tagLabel.alignment = NSTextAlignmentCenter;
	tagLabel.textColor = NSColor.secondaryLabelColor;
	tagLabel.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];

	NSStackView * stack = [NSStackView stackViewWithViews:@[
		_bpmDisplay, unit, tap, reset, _updateButton, tagLabel ]];
	stack.orientation = NSUserInterfaceLayoutOrientationVertical;
	stack.alignment = NSLayoutAttributeCenterX;
	stack.spacing = 10;
	stack.translatesAutoresizingMaskIntoConstraints = NO;
	[stack setCustomSpacing:2 afterView:_bpmDisplay];
	[stack setCustomSpacing:18 afterView:unit];

	[root addSubview:stack];
	[NSLayoutConstraint activateConstraints:@[
		[stack.leadingAnchor  constraintEqualToAnchor:root.leadingAnchor  constant:20],
		[stack.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-20],
		[stack.topAnchor      constraintEqualToAnchor:root.topAnchor      constant:20],
		[stack.bottomAnchor   constraintEqualToAnchor:root.bottomAnchor   constant:-20],
		[tap.widthAnchor      constraintEqualToAnchor:stack.widthAnchor],
	]];
}

// --- tapping ---------------------------------------------------------------

- (IBAction)onTap:(id)sender
{
	const tap_clock::time_point now = tap_clock::now();
	const std::chrono::seconds resetAfter((long long) bpm_config_seconds_to_reset_average);

	// A tap that comes long enough after the last one is the start of a fresh
	// measurement rather than a very slow beat.
	if (!_taps.empty() && (now - _taps.back()) > resetAfter)
	{
		[self resetBPM];
		return;
	}

	const std::size_t keep = taps_to_keep();
	if (_taps.size() >= keep) _taps.erase(_taps.begin());
	_taps.push_back(now);

	if (_taps.size() > 1)
	{
		// The mean gap is the total span over the number of gaps, which is the
		// same figure as averaging them one by one and is one subtraction.
		const double span =
			std::chrono::duration<double>(_taps.back() - _taps.front()).count();
		const double gaps = (double) (_taps.size() - 1);
		if (span > 0) [self setBPM:60.0 * gaps / span];
	}
}

- (IBAction)onReset:(id)sender
{
	[self resetBPM];
}

- (void)resetBPM
{
	_taps.clear();
	_taps.reserve(taps_to_keep());
	[self setBPM:0.0];
}

- (void)setBPM:(double)bpm
{
	_bpm = bpm;
	_bpmDisplay.stringValue = fooRubatoStr(format_bpm(_bpm).get_ptr());
	_updateButton.enabled = (_bpm > 0);
}

- (IBAction)onUpdateFile:(id)sender
{
	if (_bpm <= 0) return;

	metadb_handle_ptr track;
	if (core_api::assert_main_thread() && playback_control::get()->get_now_playing(track))
	{
		metadb_io_v2::get()->update_info_async(
			pfc::list_single_ref_t<metadb_handle_ptr>(track),
			fb2k::service_new<file_info_filter_bpm>(track, bpm_tag_name(), _bpm),
			core_api::get_main_window(),
			metadb_io_v2::op_flag_background | metadb_io_v2::op_flag_delay_ui,
			NULL);
	}
}

- (void)windowWillClose:(NSNotification *)notification
{
	[g_openWindows removeObject:self];
}

@end


/***** bpm_ui.h *****/

void bpm_show_manual_tap()
{
	if (g_openWindows == nil) g_openWindows = [NSMutableArray new];

	// One is enough: a second tap window would divide the taps between them
	// and measure neither.
	if (g_openWindows.count > 0)
	{
		[g_openWindows.firstObject.window makeKeyAndOrderFront:nil];
		return;
	}

	fooRubatoTapWindow * window = [fooRubatoTapWindow new];
	[g_openWindows addObject:window];
	[window showWindow:nil];
	[window.window makeKeyAndOrderFront:nil];
}
