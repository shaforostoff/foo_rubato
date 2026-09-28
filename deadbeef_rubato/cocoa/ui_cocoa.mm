// The plugin's windows in Cocoa, for DeaDBeeF on macOS. See rubato_ui.h for
// what they are and rubato_results.h for everything they show; this file lays
// them out and passes clicks back, as gtk/ui_gtk.cpp does in GTK 3, and
// decides nothing that one does not.
//
// Everything here runs on the main thread, which is AppKit's. The five entry
// points may be called from any thread, and hand their work over with
// dispatch_async where the GTK windows use g_idle_add.
//
// Built with ARC. The classes carry a DdbRubato prefix: Objective-C class
// names share one namespace across every library in the process, whatever
// the linker exports, and another plugin's "ResultsWindow" would be taken
// for ours.

#import <Cocoa/Cocoa.h>

#include "rubato_ui.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{

//! DeaDBeeF's Cocoa interface, which the windows belong in.
const char * const cocoaui_id = "cocoaui";

std::atomic<bool> ready(false);

void on_main_thread(std::function<void()> fn)
{
	dispatch_async(dispatch_get_main_queue(), ^{
		if (ready.load()) fn();
	});
}

//! A tag read off a file is the one string here that arrives from outside,
//! and stringWithUTF8String: answers nil for one that is not UTF-8. An empty
//! cell is better than a nil where AppKit wants a string.
NSString * ns(const std::string & s)
{
	NSString * r = [[NSString alloc] initWithBytes:s.data() length:s.size()
	                                      encoding:NSUTF8StringEncoding];
	return r != nil ? r : @"";
}

//! Wraps at `width`: Auto Layout cannot work out on its own how tall a
//! label that wraps is going to be.
NSTextField * wrapping_label(const std::string & text, CGFloat width)
{
	NSTextField * label = [NSTextField wrappingLabelWithString:ns(text)];
	label.translatesAutoresizingMaskIntoConstraints = NO;
	label.selectable = NO;
	label.preferredMaxLayoutWidth = width;
	return label;
}

NSTextField * line_label(const std::string & text, NSLineBreakMode truncation)
{
	NSTextField * label = [NSTextField labelWithString:ns(text)];
	label.translatesAutoresizingMaskIntoConstraints = NO;
	label.lineBreakMode = truncation;
	// A truncating label gives way to the window rather than widening it.
	[label setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
	                                forOrientation:NSLayoutConstraintOrientationHorizontal];
	return label;
}

NSButton * button(NSString * title, id target, SEL action)
{
	NSButton * b = [NSButton buttonWithTitle:title target:target action:action];
	b.translatesAutoresizingMaskIntoConstraints = NO;
	return b;
}

}   // namespace

// --- every window ------------------------------------------------------------

//! Owns one window, and is kept alive by the set of open ones until it closes:
//! nothing else holds on to it. The window's delegate, for that.
@interface DdbRubatoWindow : NSObject <NSWindowDelegate>
@property (nonatomic, strong) NSWindow * window;
- (NSWindow *)makeWindowTitled:(NSString *)title
                          size:(NSSize)size
                     resizable:(BOOL)resizable
                         class:(Class)windowClass;
- (void)show;
- (void)close;
//! After the window has closed, for the subclass to let go of what it holds.
- (void)didClose;
@end

namespace
{

//! Every window this file has open, for shutdown to close.
NSMutableSet<DdbRubatoWindow *> * open_windows = nil;

}   // namespace

@implementation DdbRubatoWindow

- (NSWindow *)makeWindowTitled:(NSString *)title
                          size:(NSSize)size
                     resizable:(BOOL)resizable
                         class:(Class)windowClass
{
	NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
	if (resizable) style |= NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;
	NSWindow * w = [[windowClass alloc] initWithContentRect:NSMakeRect(0, 0, size.width, size.height)
	                                              styleMask:style
	                                                backing:NSBackingStoreBuffered
	                                                  defer:YES];
	w.title = title;
	// The set above owns the window's owner, and ARC the window.
	w.releasedWhenClosed = NO;
	w.delegate = self;
	self.window = w;
	return w;
}

- (void)show
{
	NSWindow * w = self.window;
	[w layoutIfNeeded];
	// Over the middle of DeaDBeeF's own window, as GTK's centre-on-parent
	// puts it; the middle of the screen when there is none to go over.
	NSWindow * parent = NSApp.mainWindow;
	if (parent != nil && parent != w && parent.visible)
	{
		const NSRect p = parent.frame;
		const NSSize s = w.frame.size;
		[w setFrameOrigin:NSMakePoint(round(NSMidX(p) - s.width / 2),
		                              round(NSMidY(p) - s.height / 2))];
	}
	else
	{
		[w center];
	}
	if (open_windows == nil) open_windows = [NSMutableSet new];
	[open_windows addObject:self];
	[w makeKeyAndOrderFront:nil];
}

- (void)close
{
	[self.window close];
}

- (void)windowWillClose:(NSNotification *)notification
{
	self.window.delegate = nil;
	[self didClose];
	// Last: this may be the only reference left to self.
	[open_windows removeObject:self];
}

- (void)didClose {}

@end

// --- the results window ------------------------------------------------------

@interface DdbRubatoResults : DdbRubatoWindow <NSTableViewDataSource, NSTableViewDelegate>
- (instancetype)initWithModel:(std::shared_ptr<rubato::results>)model;
@end

@implementation DdbRubatoResults
{
	std::shared_ptr<rubato::results> _model;
	NSTableView * _table;
	NSTableColumn * _titleColumn;
	NSButton * _doubleButton;
	NSButton * _halveButton;
}

- (instancetype)initWithModel:(std::shared_ptr<rubato::results>)model
{
	self = [super init];
	if (self == nil) return nil;
	_model = std::move(model);

	NSWindow * w = [self makeWindowTitled:@"Rubato BPM Analysis" size:NSMakeSize(900, 460)
	                            resizable:YES class:[NSWindow class]];
	w.contentMinSize = NSMakeSize(560, 240);
	NSView * root = w.contentView;

	_table = [[NSTableView alloc] initWithFrame:NSZeroRect];
	_table.usesAlternatingRowBackgroundColors = YES;
	_table.allowsMultipleSelection = YES;
	_table.gridStyleMask = NSTableViewSolidVerticalGridLineMask;
	// The title takes what the others leave, and is cut short rather than
	// pushing them out of the window.
	_table.columnAutoresizingStyle = NSTableViewFirstColumnOnlyAutoresizingStyle;

	const std::vector<rubato::column_id> & columns = _model->columns();
	for (std::size_t c = 0; c < columns.size(); c++)
	{
		// By position in the model's list, which is what the cells are asked
		// for by.
		NSTableColumn * column = [[NSTableColumn alloc]
			initWithIdentifier:[NSString stringWithFormat:@"%zu", c]];
		column.title = ns(rubato::results::heading(columns[c]));
		if (columns[c] == rubato::column_title)
		{
			column.minWidth = 160;
			column.width = 300;
			column.resizingMask = NSTableColumnAutoresizingMask | NSTableColumnUserResizingMask;
			_titleColumn = column;
		}
		else
		{
			column.resizingMask = NSTableColumnUserResizingMask;
			if (rubato::results::column_numeric(columns[c]))
				column.headerCell.alignment = NSTextAlignmentRight;
		}
		[_table addTableColumn:column];
	}
	_table.dataSource = self;
	_table.delegate = self;

	NSScrollView * scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
	scroll.documentView = _table;
	scroll.hasVerticalScroller = YES;
	scroll.hasHorizontalScroller = YES;
	scroll.autohidesScrollers = YES;
	scroll.borderType = NSBezelBorder;
	scroll.translatesAutoresizingMaskIntoConstraints = NO;

	NSTextField * destination = line_label(_model->destination(), NSLineBreakByTruncatingTail);

	// Double and halve on the left, acting on the selection; Cancel and the
	// commit button on the right, acting on everything, and saying so.
	_doubleButton = button(@"Double BPM", self, @selector(onDouble:));
	_halveButton = button(@"Halve BPM", self, @selector(onHalve:));
	NSButton * cancel = button(@"Cancel", self, @selector(onCancel:));
	cancel.keyEquivalent = @"\033";
	NSButton * commit = button(ns(_model->commit_label()), self, @selector(onCommit:));
	commit.keyEquivalent = @"\r";

	for (NSView * v in @[ scroll, destination, _doubleButton, _halveButton, cancel, commit ])
		[root addSubview:v];

	const CGFloat m = 20;
	[NSLayoutConstraint activateConstraints:@[
		[scroll.topAnchor constraintEqualToAnchor:root.topAnchor constant:m],
		[scroll.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[scroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],

		[destination.topAnchor constraintEqualToAnchor:scroll.bottomAnchor constant:10],
		[destination.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[destination.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],

		[_doubleButton.topAnchor constraintEqualToAnchor:destination.bottomAnchor constant:12],
		[_doubleButton.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[_doubleButton.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-m],
		[_halveButton.leadingAnchor constraintEqualToAnchor:_doubleButton.trailingAnchor constant:8],
		[_halveButton.firstBaselineAnchor constraintEqualToAnchor:_doubleButton.firstBaselineAnchor],

		[commit.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[commit.firstBaselineAnchor constraintEqualToAnchor:_doubleButton.firstBaselineAnchor],
		[cancel.trailingAnchor constraintEqualToAnchor:commit.leadingAnchor constant:-8],
		[cancel.firstBaselineAnchor constraintEqualToAnchor:_doubleButton.firstBaselineAnchor],
		[cancel.leadingAnchor constraintGreaterThanOrEqualToAnchor:_halveButton.trailingAnchor constant:m],
	]];

	[self sizeColumnsToContents];
	[self enableScaleButtons];
	[[NSNotificationCenter defaultCenter] addObserver:self
	                                         selector:@selector(columnDidResize:)
	                                             name:NSTableViewColumnDidResizeNotification
	                                           object:_table];
	[w makeFirstResponder:_table];
	return self;
}

- (void)didClose
{
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	_table.dataSource = nil;
	_table.delegate = nil;
}

- (rubato::column_id)columnFor:(NSTableColumn *)column
{
	const std::size_t c = static_cast<std::size_t>(column.identifier.integerValue);
	const std::vector<rubato::column_id> & columns = _model->columns();
	return c < columns.size() ? columns[c] : rubato::column_title;
}

//! Every column but the title is as wide as the widest thing in it, header
//! included; the title takes whatever is left.
- (void)sizeColumnsToContents
{
	NSDictionary * cellFont = @{ NSFontAttributeName: [NSFont systemFontOfSize:NSFont.systemFontSize] };
	NSDictionary * headerFont = @{ NSFontAttributeName: [NSFont systemFontOfSize:NSFont.smallSystemFontSize] };
	// The cell's and the header's own insets, which the text does not include.
	const CGFloat padding = 16;
	for (NSTableColumn * column in _table.tableColumns)
	{
		if (column == _titleColumn) continue;
		const rubato::column_id id = [self columnFor:column];
		CGFloat widest = [column.title sizeWithAttributes:headerFont].width;
		for (std::size_t row = 0; row < _model->size(); row++)
			widest = std::max(widest, [ns(_model->cell(row, id)) sizeWithAttributes:cellFont].width);
		const CGFloat width = ceil(widest + padding);
		column.minWidth = width;
		column.width = width;
	}
	[_table sizeToFit];
}

// --- the cells ---------------------------------------------------------------

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
	return static_cast<NSInteger>(_model->size());
}

- (NSView *)tableView:(NSTableView *)tableView
   viewForTableColumn:(NSTableColumn *)tableColumn
                  row:(NSInteger)row
{
	NSTextField * cell = [tableView makeViewWithIdentifier:tableColumn.identifier owner:self];
	if (cell == nil)
	{
		cell = [NSTextField labelWithString:@""];
		cell.identifier = tableColumn.identifier;
		cell.lineBreakMode = NSLineBreakByTruncatingTail;
		if (rubato::results::column_numeric([self columnFor:tableColumn]))
			cell.alignment = NSTextAlignmentRight;
	}
	const std::size_t index = static_cast<std::size_t>(row);
	cell.stringValue = ns(_model->cell(index, [self columnFor:tableColumn]));
	// The whole row's tooltip, wherever on the row the pointer rests - the
	// tuning column, on the right, is the one it is mostly for. Cells are
	// reused, so it is cleared as deliberately as it is set.
	const std::string tip = _model->tooltip(index, [self titleClippedInRow:index]);
	cell.toolTip = tip.empty() ? nil : ns(tip);
	return cell;
}

//! Whether the title column is drawing this row's title cut short: the one
//! case where repeating the title says something the reader cannot see.
- (BOOL)titleClippedInRow:(std::size_t)row
{
	NSDictionary * font = @{ NSFontAttributeName: [NSFont systemFontOfSize:NSFont.systemFontSize] };
	const CGFloat width = [ns(_model->cell(row, rubato::column_title)) sizeWithAttributes:font].width;
	const CGFloat padding = 8;
	return width + padding > _titleColumn.width;
}

//! A title cut short by a narrower column now wants its tooltip, and one no
//! longer cut short does not.
- (void)columnDidResize:(NSNotification *)notification
{
	if (notification.userInfo[@"NSTableColumn"] != _titleColumn) return;
	const NSRange visible = [_table rowsInRect:_table.visibleRect];
	if (visible.length == 0) return;
	[_table reloadDataForRowIndexes:[NSIndexSet indexSetWithIndexesInRange:visible]
	                  columnIndexes:[NSIndexSet indexSetWithIndexesInRange:
	                                    NSMakeRange(0, static_cast<NSUInteger>(_table.numberOfColumns))]];
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
	[self enableScaleButtons];
}

- (void)enableScaleButtons
{
	const BOOL any = _table.numberOfSelectedRows > 0;
	_doubleButton.enabled = any;
	_halveButton.enabled = any;
}

// --- the buttons -------------------------------------------------------------

- (void)onDouble:(id)sender { [self scaleSelectionBy:2.0]; }
- (void)onHalve:(id)sender { [self scaleSelectionBy:0.5]; }

//! The double and halve buttons act on the rows highlighted.
- (void)scaleSelectionBy:(double)factor
{
	NSIndexSet * selected = _table.selectedRowIndexes;
	rubato::results & model = *_model;
	[selected enumerateIndexesUsingBlock:^(NSUInteger row, BOOL *) {
		if (row < model.size()) model.scale(row, factor);
	}];
	[_table reloadDataForRowIndexes:selected
	                  columnIndexes:[NSIndexSet indexSetWithIndexesInRange:
	                                    NSMakeRange(0, static_cast<NSUInteger>(_table.numberOfColumns))]];
	// A doubled BPM can be a digit wider than the one it replaced.
	[self sizeColumnsToContents];
}

- (void)onCommit:(id)sender
{
	_model->commit();
	[self close];
}

- (void)onCancel:(id)sender
{
	[self close];
}

@end

// --- the progress window -----------------------------------------------------

//! Owned by its timer, which outlives the window: a Cancel closes the window
//! at once, and the timer goes on until the scan has noticed and stopped.
@interface DdbRubatoProgress : DdbRubatoWindow
- (instancetype)initWithProgress:(std::shared_ptr<rubato::scan_progress>)progress;
@end

@implementation DdbRubatoProgress
{
	std::shared_ptr<rubato::scan_progress> _progress;
	std::chrono::steady_clock::time_point _started;
	NSTimer * _timer;
	NSTextField * _count;
	NSTextField * _items;
	NSProgressIndicator * _bar;
	BOOL _dismissed;   //!< cancelled, or closed from outside
}

- (instancetype)initWithProgress:(std::shared_ptr<rubato::scan_progress>)progress
{
	self = [super init];
	if (self == nil) return nil;
	_progress = std::move(progress);
	_started = std::chrono::steady_clock::now();
	// The timer holds on to self until it is invalidated. In the common modes,
	// so that it goes on ticking while a menu is open.
	_timer = [NSTimer timerWithTimeInterval:0.1 target:self selector:@selector(tick:)
	                               userInfo:nil repeats:YES];
	[[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
	return self;
}

- (void)build
{
	NSWindow * w = [self makeWindowTitled:@"Analysing BPMs" size:NSMakeSize(460, 120)
	                            resizable:NO class:[NSWindow class]];
	NSView * root = w.contentView;

	_count = line_label("", NSLineBreakByTruncatingTail);
	_items = line_label("", NSLineBreakByTruncatingMiddle);
	_items.textColor = NSColor.secondaryLabelColor;
	_bar = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
	_bar.style = NSProgressIndicatorStyleBar;
	_bar.indeterminate = NO;
	_bar.minValue = 0;
	_bar.maxValue = 1;
	_bar.translatesAutoresizingMaskIntoConstraints = NO;
	NSButton * cancel = button(@"Cancel", self, @selector(onCancel:));
	cancel.keyEquivalent = @"\033";

	for (NSView * v in @[ _count, _bar, _items, cancel ]) [root addSubview:v];
	const CGFloat m = 20;
	[NSLayoutConstraint activateConstraints:@[
		[root.widthAnchor constraintEqualToConstant:460],
		[_count.topAnchor constraintEqualToAnchor:root.topAnchor constant:m],
		[_count.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[_count.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[_bar.topAnchor constraintEqualToAnchor:_count.bottomAnchor constant:8],
		[_bar.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[_bar.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[_items.topAnchor constraintEqualToAnchor:_bar.bottomAnchor constant:8],
		[_items.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[_items.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[cancel.topAnchor constraintEqualToAnchor:_items.bottomAnchor constant:12],
		[cancel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[cancel.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-m],
	]];
	[self show];
}

- (void)tick:(NSTimer *)timer
{
	const rubato::scan_progress::snapshot s = _progress->read();

	if (s.finished || !ready.load())
	{
		_dismissed = YES;
		[_timer invalidate];   // the last reference to self, but for the stack's
		_timer = nil;
		[self close];
		return;
	}
	if (_dismissed) return;

	// Not at all for a scan over before it would have been read: one track
	// is often analysed in less time than a window takes to draw.
	if (self.window == nil)
	{
		if (std::chrono::steady_clock::now() - _started < std::chrono::milliseconds(600)) return;
		[self build];
	}

	const std::string count = "Analysing " + std::to_string(std::min(s.done + 1, s.total))
	                        + " of " + std::to_string(s.total)
	                        + (s.total == 1 ? " track" : " tracks");
	_count.stringValue = ns(count);
	_items.stringValue = ns(s.in_flight);
	_bar.doubleValue = s.fraction;
}

- (void)onCancel:(id)sender
{
	[self close];
}

- (void)didClose
{
	if (!_dismissed)
	{
		// Cancel, Escape, the close button or shutdown: whichever, the scan
		// is no longer wanted.
		_dismissed = YES;
		_progress->cancel();
	}
}

@end

// --- the tapping window ------------------------------------------------------

//! Takes the space bar and Escape before any control does: space taps
//! wherever the focus is, rather than pressing whichever button last had it -
//! pressing Reset by accident halfway through a count would lose it.
@interface DdbRubatoTapWindow : NSWindow
@property (nonatomic, copy) void (^onSpace)(void);
@end

@implementation DdbRubatoTapWindow

- (void)sendEvent:(NSEvent *)event
{
	if (event.type == NSEventTypeKeyDown)
	{
		NSString * key = event.charactersIgnoringModifiers;
		if ([key isEqualToString:@"\033"])
		{
			[self close];
			return;
		}
		if ([key isEqualToString:@" "])
		{
			// On key down, and once however long it is held.
			if (!event.ARepeat && self.onSpace != nil) self.onSpace();
			return;
		}
	}
	[super sendEvent:event];
}

@end

@interface DdbRubatoTap : DdbRubatoWindow
@end

namespace
{

//! There is one: a second would divide the taps between them.
DdbRubatoTap * the_tap_window = nil;

}   // namespace

@implementation DdbRubatoTap
{
	std::unique_ptr<rubato::tap_meter> _meter;
	rubato::settings _config;
	NSTextField * _playing;
	NSTextField * _bpm;
	NSTextField * _taps;
	NSButton * _write;
	NSTimer * _timer;
	BOOL _somethingPlaying;
}

- (instancetype)init
{
	self = [super init];
	if (self == nil) return nil;
	_config = rubato::read_settings();
	_meter.reset(new rubato::tap_meter(_config));

	DdbRubatoTapWindow * w = (DdbRubatoTapWindow *)
		[self makeWindowTitled:@"Tap BPM" size:NSMakeSize(420, 300) resizable:NO
		                 class:[DdbRubatoTapWindow class]];
	__weak DdbRubatoTap * weakSelf = self;
	w.onSpace = ^{ [weakSelf tap]; };
	NSView * root = w.contentView;

	_playing = line_label("", NSLineBreakByTruncatingTail);

	_bpm = [NSTextField labelWithString:@""];
	_bpm.font = [NSFont monospacedDigitSystemFontOfSize:40 weight:NSFontWeightBold];
	_bpm.alignment = NSTextAlignmentCenter;
	_bpm.translatesAutoresizingMaskIntoConstraints = NO;

	// On the press, not the click: a click lands when the button comes back
	// up, which adds however long it was held to every interval.
	NSButton * tap = button(@"Tap", self, @selector(onTap:));
	tap.bezelStyle = NSBezelStyleRegularSquare;
	tap.font = [NSFont systemFontOfSize:NSFont.systemFontSize + 4];
	[tap sendActionOn:NSEventMaskLeftMouseDown];

	const CGFloat m = 20;
	const CGFloat width = 420;
	_taps = wrapping_label("", width - 2 * m);
	_taps.textColor = NSColor.secondaryLabelColor;

	NSTextField * destination = wrapping_label(rubato::bpm_destination(_config)
		+ " A tapped BPM replaces the measured one and removes INITIALBPM and BpmAlgorithm.",
		width - 2 * m);
	destination.textColor = NSColor.secondaryLabelColor;

	NSButton * reset = button(@"Reset", self, @selector(onReset:));
	NSButton * close = button(@"Close", self, @selector(onClose:));
	_write = button(@"Write to playing track", self, @selector(onWrite:));
	// The space bar is the tapping window's; see DdbRubatoTapWindow.
	for (NSButton * b in @[ tap, reset, close, _write ]) b.refusesFirstResponder = YES;

	for (NSView * v in @[ _playing, _bpm, tap, _taps, destination, reset, close, _write ])
		[root addSubview:v];
	[NSLayoutConstraint activateConstraints:@[
		[root.widthAnchor constraintEqualToConstant:width],
		[_playing.topAnchor constraintEqualToAnchor:root.topAnchor constant:m],
		[_playing.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[_playing.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[_bpm.topAnchor constraintEqualToAnchor:_playing.bottomAnchor constant:12],
		[_bpm.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[_bpm.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[tap.topAnchor constraintEqualToAnchor:_bpm.bottomAnchor constant:12],
		[tap.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[tap.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[tap.heightAnchor constraintEqualToConstant:64],
		[_taps.topAnchor constraintEqualToAnchor:tap.bottomAnchor constant:12],
		[_taps.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[_taps.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[destination.topAnchor constraintEqualToAnchor:_taps.bottomAnchor constant:10],
		[destination.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[destination.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[reset.topAnchor constraintEqualToAnchor:destination.bottomAnchor constant:16],
		[reset.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[reset.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-m],
		[_write.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[_write.firstBaselineAnchor constraintEqualToAnchor:reset.firstBaselineAnchor],
		[close.trailingAnchor constraintEqualToAnchor:_write.leadingAnchor constant:-8],
		[close.firstBaselineAnchor constraintEqualToAnchor:reset.firstBaselineAnchor],
	]];

	// Which track the BPM will go to, kept current: it is whatever is playing
	// when Write is clicked, not when the window was opened. The timer holds
	// on to self until the window closes.
	_timer = [NSTimer timerWithTimeInterval:0.5 target:self selector:@selector(poll:)
	                               userInfo:nil repeats:YES];
	[[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
	[self poll:nil];
	return self;
}

- (void)poll:(NSTimer *)timer
{
	const std::string title = rubato::playing_title();
	_somethingPlaying = !title.empty();
	_playing.stringValue = title.empty() ? @"Nothing is playing." : ns("Playing: " + title);
	[self refresh];
}

- (void)refresh
{
	const std::string bpm = _meter->text();
	_bpm.stringValue = bpm.empty() ? @"—" : ns(bpm);

	std::string taps;
	if (_meter->taps() == 0)
		taps = "Tap along with the beat: click Tap, or press the space bar.";
	else
		taps = std::to_string(_meter->taps()) + (_meter->taps() == 1 ? " tap" : " taps")
		     + ", averaging the last " + std::to_string(_config.taps_to_average)
		     + ". A pause of " + std::to_string(_config.seconds_to_reset)
		     + " seconds starts again.";
	_taps.stringValue = ns(taps);
	_write.enabled = _somethingPlaying && _meter->bpm() > 0;
}

- (void)tap
{
	_meter->tap(rubato::tap_meter::clock::now());
	[self refresh];
}

- (void)onTap:(id)sender { [self tap]; }

- (void)onReset:(id)sender
{
	_meter->reset();
	[self refresh];
}

- (void)onClose:(id)sender
{
	[self close];
}

- (void)onWrite:(id)sender
{
	if (rubato::write_tapped_bpm(_meter->bpm()))
	{
		_meter->reset();
		[self refresh];
	}
}

- (void)didClose
{
	[_timer invalidate];
	_timer = nil;
	if (the_tap_window == self) the_tap_window = nil;
}

@end

// --- rubato_ui.h -------------------------------------------------------------

void rubato::ui::connect(DB_functions_t * api)
{
	// These windows are Cocoa, and belong only in DeaDBeeF's Cocoa interface,
	// which is the only one DeaDBeeF for Mac has - but a player built from
	// source with another, or with none, goes on without them.
	ready = api->plug_get_for_id(cocoaui_id) != nullptr;
}

void rubato::ui::shutdown()
{
	if (!ready.exchange(false)) return;
	// Closing a window gives back its tracks. DeaDBeeF for Mac stops its
	// plugins on the main thread, as the application quits; anywhere else,
	// the windows are closed as soon as the main thread gets to them, and
	// hold nothing that cannot wait until then.
	void (^close_all)(void) = ^{
		// A copy, because each window takes itself out of the set as it goes.
		for (DdbRubatoWindow * w in [open_windows allObjects]) [w close];
	};
	if ([NSThread isMainThread]) close_all();
	else dispatch_async(dispatch_get_main_queue(), close_all);
}

bool rubato::ui::available()
{
	return ready.load();
}

void rubato::ui::show_progress(std::shared_ptr<scan_progress> progress)
{
	on_main_thread([progress]() {
		// Kept alive by its timer.
		(void) [[DdbRubatoProgress alloc] initWithProgress:progress];
	});
}

void rubato::ui::show_results(std::shared_ptr<results> r)
{
	on_main_thread([r]() {
		[[[DdbRubatoResults alloc] initWithModel:r] show];
	});
}

void rubato::ui::show_tap()
{
	on_main_thread([]() {
		if (the_tap_window == nil)
		{
			the_tap_window = [DdbRubatoTap new];
			[the_tap_window show];
		}
		else
		{
			[the_tap_window.window makeKeyAndOrderFront:nil];
		}
	});
}
