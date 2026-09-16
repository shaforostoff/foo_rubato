// The preferences page, macOS side.
//
// The Windows page is a dialog template with an Apply button behind it, and
// preferences_page_instance to carry "has anything changed" back to the host.
// The macOS preferences API has none of that: preferences_page::instantiate()
// hands back an NSViewController and that is the whole contract, so a page
// there writes each setting as it is changed. That is also how foobar2000 for
// Mac's own pages behave, and it is why this file has no apply(), no reset()
// and no HasChanged() - there is nowhere to put them.
//
// What it does have to match is the set of settings and their meanings, which
// live in globals.h and are shared with the Windows page.

#import <Cocoa/Cocoa.h>

#include <SDK/foobar2000.h>
#include <helpers-mac/fooPreferencesCommon.h>

#include "../globals.h"
#include "../guid.h"
#include "../preferences.h"
#include "mac_strings.h"

namespace
{
	//! The smallest sensible value for the two tap settings, which is what the
	//! Windows page clamps its edit boxes to as they are typed in.
	const int min_tap_setting = 1;
}

@interface fooRubatoPreferencesView : NSViewController <NSTextFieldDelegate>
@end

@implementation fooRubatoPreferencesView
{
	NSTextField    * _bpmTagField;
	NSPopUpButton  * _precisionPopUp;
	NSButton       * _autoWriteTag;
	NSButton       * _writeInitialBPM;
	NSButton       * _writeAlgorithm;
	NSTextField    * _tapsToAverage;
	NSTextField    * _secondsToReset;
	NSButton       * _outputDebug;
}

// --- construction ----------------------------------------------------------

- (instancetype)init
{
	// No nib: the page is eight controls in two columns, and building it here
	// keeps the component a single binary with no resources to load - and no
	// bundle lookup to get wrong.
	self = [super initWithNibName:nil bundle:nil];
	return self;
}

- (NSTextField *)labelWithText:(NSString *)text
{
	NSTextField * label = [NSTextField labelWithString:text];
	label.alignment = NSTextAlignmentRight;
	return label;
}

- (NSButton *)checkboxWithTitle:(NSString *)title action:(SEL)action
{
	NSButton * box = [NSButton checkboxWithTitle:title target:self action:action];
	return box;
}

- (NSTextField *)fieldWithWidth:(CGFloat)width
{
	NSTextField * field = [NSTextField textFieldWithString:@""];
	field.delegate = self;
	[field.widthAnchor constraintEqualToConstant:width].active = YES;
	return field;
}

- (void)loadView
{
	NSView * root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 460, 300)];

	_bpmTagField    = [self fieldWithWidth:120];
	_precisionPopUp = [NSPopUpButton buttonWithTitle:@""
	                                          target:self
	                                          action:@selector(onPrecisionChanged:)];
	// The order must match bpm_precision_enum, which is what is stored.
	[_precisionPopUp addItemsWithTitles:@[ @"Nearest 1", @"1 decimal", @"2 decimals" ]];

	_autoWriteTag    = [self checkboxWithTitle:@"Write tags automatically, without showing the results"
	                                    action:@selector(onAutoWriteTagChanged:)];
	_writeInitialBPM = [self checkboxWithTitle:@"Also write the tempo the track opens at, as INITIALBPM"
	                                    action:@selector(onWriteInitialChanged:)];
	_writeAlgorithm  = [self checkboxWithTitle:@"Also write which analysis produced the BPM, as BpmAlgorithm"
	                                    action:@selector(onWriteAlgorithmChanged:)];

	_tapsToAverage  = [self fieldWithWidth:60];
	_secondsToReset = [self fieldWithWidth:60];

	_outputDebug = [self checkboxWithTitle:@"Write what each analysis measured to the console"
	                                action:@selector(onOutputDebugChanged:)];

	NSGridView * grid = [NSGridView gridViewWithViews:@[
		@[ [self labelWithText:@"Write the BPM to tag:"], _bpmTagField ],
		@[ [self labelWithText:@"Show the BPM to:"],      _precisionPopUp ],
		@[ [NSGridCell emptyContentView],                 _autoWriteTag ],
		@[ [NSGridCell emptyContentView],                 _writeInitialBPM ],
		@[ [NSGridCell emptyContentView],                 _writeAlgorithm ],
		@[ [self labelWithText:@"Taps to average:"],      _tapsToAverage ],
		@[ [self labelWithText:@"Restart the average after:"], _secondsToReset ],
		@[ [NSGridCell emptyContentView],                 _outputDebug ],
	]];
	grid.translatesAutoresizingMaskIntoConstraints = NO;
	grid.rowAlignment = NSGridRowAlignmentFirstBaseline;
	grid.columnSpacing = 8;
	grid.rowSpacing = 8;
	[grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;

	// The two groups the Windows page draws boxes around: tagging, then the
	// tap window, then diagnostics. A rule is what serves for a group heading
	// on a macOS preferences page.
	[grid rowAtIndex:5].topPadding = 12;
	[grid rowAtIndex:7].topPadding = 12;

	// "seconds", after the field rather than in the label, because the number
	// is the thing being set and the unit belongs to it.
	NSTextField * secondsUnit = [NSTextField labelWithString:@"seconds"];
	secondsUnit.translatesAutoresizingMaskIntoConstraints = NO;

	[root addSubview:grid];
	[root addSubview:secondsUnit];

	[NSLayoutConstraint activateConstraints:@[
		[grid.leadingAnchor  constraintEqualToAnchor:root.leadingAnchor  constant:20],
		[grid.topAnchor      constraintEqualToAnchor:root.topAnchor      constant:20],
		[grid.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-20],
		[grid.bottomAnchor   constraintLessThanOrEqualToAnchor:root.bottomAnchor   constant:-20],
		[secondsUnit.leadingAnchor constraintEqualToAnchor:_secondsToReset.trailingAnchor constant:6],
		[secondsUnit.firstBaselineAnchor constraintEqualToAnchor:_secondsToReset.firstBaselineAnchor],
	]];

	self.view = root;
	[self loadSettings];
}

// --- the settings ----------------------------------------------------------

- (void)loadSettings
{
	_bpmTagField.stringValue = fooRubatoStr(bpm_tag_name());

	const long precision = (long) bpm_config_bpm_precision;
	if (precision >= 0 && precision < _precisionPopUp.numberOfItems)
		[_precisionPopUp selectItemAtIndex:precision];

	_autoWriteTag.state    = bpm_config_auto_write_tag       ? NSControlStateValueOn : NSControlStateValueOff;
	_writeInitialBPM.state = bpm_config_write_initial_bpm    ? NSControlStateValueOn : NSControlStateValueOff;
	_writeAlgorithm.state  = bpm_config_write_bpm_algorithm  ? NSControlStateValueOn : NSControlStateValueOff;
	_outputDebug.state     = bpm_config_output_debug         ? NSControlStateValueOn : NSControlStateValueOff;

	_tapsToAverage.intValue  = (int) bpm_config_taps_to_average;
	_secondsToReset.intValue = (int) bpm_config_seconds_to_reset_average;
}

- (IBAction)onPrecisionChanged:(id)sender
{
	bpm_config_bpm_precision = _precisionPopUp.indexOfSelectedItem;
}

- (IBAction)onAutoWriteTagChanged:(id)sender
{
	bpm_config_auto_write_tag = (_autoWriteTag.state == NSControlStateValueOn);
}

- (IBAction)onWriteInitialChanged:(id)sender
{
	bpm_config_write_initial_bpm = (_writeInitialBPM.state == NSControlStateValueOn);
}

- (IBAction)onWriteAlgorithmChanged:(id)sender
{
	bpm_config_write_bpm_algorithm = (_writeAlgorithm.state == NSControlStateValueOn);
}

- (IBAction)onOutputDebugChanged:(id)sender
{
	bpm_config_output_debug = (_outputDebug.state == NSControlStateValueOn);
}

//! Text fields are stored as they are typed rather than when they lose focus.
//!
//! There is no Apply button to press and nothing promises the page is asked
//! anything before it goes away, so a value only written on end-of-editing is
//! a value that can be typed and lost. What that costs is that a half-typed
//! entry is briefly the setting, so a value that cannot mean anything - an
//! empty tag name, a zero tap count - is not stored at all rather than stored
//! and corrected; the field keeps what the user is typing either way.
- (void)controlTextDidChange:(NSNotification *)notification
{
	if (notification.object == _bpmTagField)
	{
		const pfc::string8 tag = fooRubatoUTF8(_bpmTagField.stringValue);
		if (!tag.is_empty()) bpm_config_bpm_tag = tag.get_ptr();
	}
	else if (notification.object == _tapsToAverage)
	{
		const int taps = _tapsToAverage.intValue;
		if (taps >= min_tap_setting) bpm_config_taps_to_average = taps;
	}
	else if (notification.object == _secondsToReset)
	{
		const int seconds = _secondsToReset.intValue;
		if (seconds >= min_tap_setting) bpm_config_seconds_to_reset_average = seconds;
	}
}

//! Whatever was refused above is put right when the field is left, so the page
//! never shows a number the component is not using.
- (void)controlTextDidEndEditing:(NSNotification *)notification
{
	[self loadSettings];
}

@end


namespace
{
	//! preferences_mac_common does the instantiate() half - it is the macOS
	//! counterpart of preferences_page_impl<>, which the Windows page uses.
	class bpm_preferences_page_mac : public preferences_mac_common<fooRubatoPreferencesView>
	{
	public:
		const char * get_name() override { return "Rubato BPM Analyzer"; }
		GUID get_guid() override { return guid_bpm_preferences; }
		GUID get_parent_guid() override { return preferences_page::guid_tools; }
	};

	preferences_page_factory_t<bpm_preferences_page_mac> g_bpm_preferences_page_mac;
}
