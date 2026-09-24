// The results window, macOS side. The Windows counterpart is
// bpm_result_dialog.cpp, built on a dialog template and a list view control.
//
// What the two share is bpm_result_format.h, which settles how the generated
// columns read, and file_info_filter_bpm, which settles what reaches the
// files. A column that rounded differently between the platforms would be a
// difference in what the component reports rather than in how it draws, so
// neither of those is written twice.

#import <Cocoa/Cocoa.h>

#include <SDK/foobar2000.h>

#include <cmath>
#include <vector>

#include "../bpm_result_format.h"
#include "../bpm_tmpo_sync.h"
#include "../bpm_track_result.h"
#include "../bpm_ui.h"
#include "../file_info_filter_bpm.h"
#include "../format_bpm.h"
#include "../globals.h"
#include "../preferences.h"
#include "mac_strings.h"

namespace
{
	//! Everything the window shows, and everything the buttons change.
	//!
	//! A copy, taken when the window opens: the scan that produced it is over,
	//! and the rows are the user's to double and halve from here.
	struct results_model
	{
		metadb_handle_list            tracks;
		pfc::list_t<file_info_impl>   infos;
		//! One per row. Carries the tempo figures the double and halve
		//! buttons scale, and the `adjusted` flag those buttons set - for
		//! which file_info_filter_bpm drops the attribution tag.
		std::vector<bpm_track_result> results;
		//! What the BPM tag held before the scan, as the file carried it.
		std::vector<pfc::string8>     tag_bpms;
	};

	//! Hands the whole list to foobar2000's tag writer.
	//!
	//! Every track in the window, not the selection: the selection drives the
	//! double and halve buttons only, which is what the hint beside them says.
	void write_tags(const results_model & model)
	{
		const pfc::string8 rhythm_tag = bpm_rhythm_tag_or_empty();

		metadb_io_v2::get()->update_info_async(
			model.tracks,
			fb2k::service_new<file_info_filter_bpm>(model.tracks, bpm_tag_name(), model.results,
			                                        rhythm_tag.is_empty() ? nullptr : rhythm_tag.get_ptr()),
			core_api::get_main_window(),
			metadb_io_v2::op_flag_background | metadb_io_v2::op_flag_delay_ui,
			bpm_tmpo_sync_after(model.tracks, bpm_tag_name()));
	}

	//! Is there anything to put in the "BPM from tag" column? It is only built
	//! when at least one of the tracks arrived with a BPM tag on it.
	bool any_tag_bpm(const results_model & model)
	{
		for (const pfc::string8 & value : model.tag_bpms)
		{
			if (!value.is_empty()) return true;
		}
		return false;
	}

	//! The title to show for a row: the TITLE tag, or the filename where the
	//! track has not got one.
	pfc::string8 row_title(const results_model & model, std::size_t row)
	{
		pfc::string8 title;
		if (row < model.infos.get_size() && model.infos[row].meta_exists("TITLE"))
			title = model.infos[row].meta_get("TITLE", 0);
		else if (row < model.tracks.get_count())
			title = pfc::string_filename(model.tracks[row]->get_path());
		return title;
	}

	// Column identifiers. The tag column is only built when at least one track
	// arrived with a BPM tag on it, so the set is not fixed and the cells are
	// filled in by identifier rather than by index.
	NSString * const col_title   = @"title";
	NSString * const col_bpm     = @"bpm";
	NSString * const col_tag_bpm = @"tagbpm";
	NSString * const col_initial = @"initial";
	NSString * const col_spread  = @"spread";
	NSString * const col_rhythm  = @"rhythm";
	NSString * const col_key     = @"key";
	NSString * const col_tuning  = @"tuning";

	//! Was anything measured? With key detection switched off in the
	//! preferences there is nothing to put in those two columns, and two
	//! permanently empty ones would only push the title out of the window.
	bool any_key(const results_model & model)
	{
		for (const bpm_track_result & r : model.results)
		{
			if (r.key.ok) return true;
		}
		return false;
	}
}


@interface fooRubatoResultsWindow : NSWindowController
                                   <NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate>
- (instancetype)initWithModel:(std::shared_ptr<results_model>)model;
@end


@implementation fooRubatoResultsWindow
{
	std::shared_ptr<results_model> _model;
	NSTableView * _table;
	NSButton    * _doubleButton;
	NSButton    * _halveButton;
}

//! Open windows, so that one stays alive after the function that made it has
//! returned. An NSWindowController does not retain itself, and nothing else
//! here holds a reference; released again in windowWillClose:.
static NSMutableArray<fooRubatoResultsWindow *> * g_openWindows = nil;

// --- construction ----------------------------------------------------------

- (instancetype)initWithModel:(std::shared_ptr<results_model>)model
{
	const BOOL haveTagBPM = any_tag_bpm(*model) ? YES : NO;
	const BOOL haveKey = any_key(*model) ? YES : NO;

	NSWindow * window =
		[[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 720, 420)
		                            styleMask:NSWindowStyleMaskTitled |
		                                      NSWindowStyleMaskClosable |
		                                      NSWindowStyleMaskResizable
		                              backing:NSBackingStoreBuffered
		                                defer:NO];
	window.title = @"Rubato BPM Analyzer";
	window.releasedWhenClosed = NO;   // the array above owns it
	[window center];

	self = [super initWithWindow:window];
	if (self == nil) return nil;

	_model = model;
	[self buildContentWithTagColumn:haveTagBPM keyColumns:haveKey];
	window.delegate = self;

	return self;
}

- (NSTableColumn *)addColumn:(NSString *)identifier title:(NSString *)title width:(CGFloat)width
{
	NSTableColumn * column = [[NSTableColumn alloc] initWithIdentifier:identifier];
	column.title = title;
	column.width = width;
	column.minWidth = width;
	column.resizingMask = NSTableColumnNoResizing;
	[_table addTableColumn:column];
	return column;
}

- (void)buildContentWithTagColumn:(BOOL)haveTagBPM keyColumns:(BOOL)haveKey
{
	NSView * root = self.window.contentView;

	_table = [[NSTableView alloc] initWithFrame:NSZeroRect];
	_table.usesAlternatingRowBackgroundColors = YES;
	_table.allowsMultipleSelection = YES;
	_table.dataSource = self;
	_table.delegate = self;
	_table.rowSizeStyle = NSTableViewRowSizeStyleDefault;
	// The title is what gives when the window is resized; the generated
	// columns are sized to their contents below and stay that way.
	_table.columnAutoresizingStyle = NSTableViewFirstColumnOnlyAutoresizingStyle;

	NSTableColumn * titleColumn = [self addColumn:col_title title:@"Title" width:260];
	titleColumn.minWidth = 120;
	titleColumn.resizingMask = NSTableColumnAutoresizingMask | NSTableColumnUserResizingMask;

	// The BPM for the whole side, what the file already said, the tempo it
	// opens at and how much it moves all read as one group, so they sit
	// together, with the measurement next to the tap it can be judged by.
	[self addColumn:col_bpm title:@"BPM" width:60];
	if (haveTagBPM) [self addColumn:col_tag_bpm title:@"BPM from tag" width:100];
	[self addColumn:col_initial title:@"Initial BPM" width:90];
	[self addColumn:col_spread title:@"Fluctuation" width:90];
	[self addColumn:col_rhythm title:@"Rhythm" width:80];
	// Where the recording sits against A=440, and what key it is in. Both
	// come from the same pass and neither is a tempo measurement, so they sit
	// to the right of the group above rather than inside it.
	if (haveKey)
	{
		[self addColumn:col_key title:@"Key" width:90];
		[self addColumn:col_tuning title:@"Tuning" width:80];
	}

	NSScrollView * scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
	scroll.documentView = _table;
	scroll.hasVerticalScroller = YES;
	scroll.autohidesScrollers = YES;
	scroll.borderType = NSBezelBorder;
	scroll.translatesAutoresizingMaskIntoConstraints = NO;

	pfc::string_formatter tagLine;
	tagLine << "BPM will be written to %" << bpm_tag_name().get_ptr() << "% tag.";
	NSTextField * tagLabel = [NSTextField labelWithString:fooRubatoStr(tagLine.get_ptr())];
	tagLabel.translatesAutoresizingMaskIntoConstraints = NO;
	tagLabel.textColor = NSColor.secondaryLabelColor;

	_doubleButton = [NSButton buttonWithTitle:@"Double" target:self action:@selector(onDouble:)];
	_halveButton  = [NSButton buttonWithTitle:@"Halve"  target:self action:@selector(onHalve:)];

	NSTextField * hint = [NSTextField labelWithString:@"Select rows to double or halve them."];
	hint.textColor = NSColor.secondaryLabelColor;
	hint.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];

	NSButton * cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(onCancel:)];
	cancel.keyEquivalent = @"\033";   // Escape

	// The count is on the button because it is the answer to what the
	// selection has to do with the writing: nothing, every track is written.
	NSButton * update = [NSButton buttonWithTitle:[self updateButtonTitle]
	                                       target:self
	                                       action:@selector(onUpdate:)];
	update.keyEquivalent = @"\r";     // the default button

	NSStackView * scaleButtons = [NSStackView stackViewWithViews:@[ _doubleButton, _halveButton ]];
	scaleButtons.spacing = 8;

	NSStackView * commitButtons = [NSStackView stackViewWithViews:@[ cancel, update ]];
	commitButtons.spacing = 8;

	NSStackView * buttonRow =
		[NSStackView stackViewWithViews:@[ scaleButtons, hint, commitButtons ]];
	buttonRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
	buttonRow.alignment = NSLayoutAttributeCenterY;
	buttonRow.spacing = 12;
	buttonRow.translatesAutoresizingMaskIntoConstraints = NO;
	// Everything keeps its size and the hint in the middle takes the slack.
	[scaleButtons setContentHuggingPriority:NSLayoutPriorityDefaultHigh
	                         forOrientation:NSLayoutConstraintOrientationHorizontal];
	[commitButtons setContentHuggingPriority:NSLayoutPriorityDefaultHigh
	                          forOrientation:NSLayoutConstraintOrientationHorizontal];

	[root addSubview:scroll];
	[root addSubview:tagLabel];
	[root addSubview:buttonRow];

	[NSLayoutConstraint activateConstraints:@[
		[scroll.topAnchor      constraintEqualToAnchor:root.topAnchor      constant:20],
		[scroll.leadingAnchor  constraintEqualToAnchor:root.leadingAnchor  constant:20],
		[scroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-20],

		[tagLabel.topAnchor      constraintEqualToAnchor:scroll.bottomAnchor constant:12],
		[tagLabel.leadingAnchor  constraintEqualToAnchor:root.leadingAnchor  constant:20],
		[tagLabel.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-20],

		[buttonRow.topAnchor      constraintEqualToAnchor:tagLabel.bottomAnchor constant:12],
		[buttonRow.leadingAnchor  constraintEqualToAnchor:root.leadingAnchor    constant:20],
		[buttonRow.trailingAnchor constraintEqualToAnchor:root.trailingAnchor   constant:-20],
		[buttonRow.bottomAnchor   constraintEqualToAnchor:root.bottomAnchor     constant:-20],
	]];

	[self sizeColumnsToContents];
	[self enableScaleButtons];
}

//! "Update 137 files", or "Update file" for one.
- (NSString *)updateButtonTitle
{
	const t_size count = _model->tracks.get_count();
	pfc::string_formatter label;
	if (count == 1) label << "Update file";
	else            label << "Update " << count << " files";
	return fooRubatoStr(label.get_ptr());
}

//! Every column but the title is sized to the widest string in it, header
//! included, because their contents are generated and there is no useful width
//! to guess for them. The title then takes whatever is left, which is what the
//! table's first-column autoresizing does with it.
- (void)sizeColumnsToContents
{
	NSDictionary * cellAttrs   = @{ NSFontAttributeName: [NSFont systemFontOfSize:NSFont.systemFontSize] };
	NSDictionary * headerAttrs = @{ NSFontAttributeName: [NSFont systemFontOfSize:NSFont.smallSystemFontSize] };
	// Room for the cell's own insets, which the measured text does not include.
	const CGFloat padding = 16;

	for (NSTableColumn * column in _table.tableColumns)
	{
		if ([column.identifier isEqualToString:col_title]) continue;

		CGFloat widest = [column.title sizeWithAttributes:headerAttrs].width;
		for (NSInteger row = 0; row < (NSInteger) _model->results.size(); row++)
		{
			NSString * text = [self textForRow:row column:column.identifier];
			widest = MAX(widest, [text sizeWithAttributes:cellAttrs].width);
		}

		const CGFloat width = ceil(widest + padding);
		column.minWidth = width;
		column.width    = width;
	}
}

// --- the cells -------------------------------------------------------------

- (NSString *)textForRow:(NSInteger)row column:(NSString *)identifier
{
	const results_model & model = *_model;
	const std::size_t index = (std::size_t) row;
	if (index >= model.results.size()) return @"";
	const bpm_track_result & r = model.results[index];

	if ([identifier isEqualToString:col_title])
		return fooRubatoStr(row_title(model, index));
	if ([identifier isEqualToString:col_bpm])
		return fooRubatoStr(format_bpm(r.bpm).get_ptr());
	if ([identifier isEqualToString:col_tag_bpm])
		return index < model.tag_bpms.size()
		     ? fooRubatoStr(bpm_format_tag_bpm(model.tag_bpms[index])) : @"";
	if ([identifier isEqualToString:col_initial])
		return fooRubatoStr(bpm_format_initial(r.initial_bpm));
	if ([identifier isEqualToString:col_spread])
		return fooRubatoStr(bpm_format_spread(r.spread));
	if ([identifier isEqualToString:col_rhythm])
		return fooRubatoStr(r.rhythm);
	if ([identifier isEqualToString:col_key])
		return fooRubatoStr(bpm_format_key_column(r.key));
	if ([identifier isEqualToString:col_tuning])
		return fooRubatoStr(bpm_format_tuning_column(r.key));

	return @"";
}

//! The whole row's explanation, on every cell of the row.
//!
//! A cell here could carry its own - AppKit has no trouble telling them apart,
//! unlike a Win32 list view - but the two windows would then say different
//! things in the same place, and there is only one thing to say.
- (NSString *)tooltipForRow:(NSInteger)row
{
	const results_model & model = *_model;
	const std::size_t index = (std::size_t) row;
	if (index >= model.results.size()) return nil;

	const bpm_track_result & r = model.results[index];
	const pfc::string8 title = [self textForRow:row column:col_title].UTF8String;
	// `clipped` is false: a table cell here truncates with an ellipsis and
	// nothing measures that, so the title rides along only where there is a
	// paragraph for it to introduce.
	const pfc::string8 text = bpm_format_row_tooltip(title, false, r.key, r.year);
	// An empty tooltip would still put an empty box under the pointer.
	return text.is_empty() ? nil : fooRubatoStr(text.get_ptr());
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
	return (NSInteger) _model->results.size();
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
	}
	cell.stringValue = [self textForRow:row column:tableColumn.identifier];
	// The tuning column carries a number the column has no room to explain -
	// which reference pitch, in which direction, by how much, and why there is
	// more than one answer. The pointer resting on the row is where that goes.
	// Cells are recycled, so this is cleared as deliberately as it is set.
	cell.toolTip = [self tooltipForRow:row];
	return cell;
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

// --- the buttons -----------------------------------------------------------

- (IBAction)onDouble:(id)sender { [self scaleSelectionBy:2.0]; }
- (IBAction)onHalve:(id)sender  { [self scaleSelectionBy:0.5]; }

- (void)scaleSelectionBy:(double)factor
{
	results_model & model = *_model;
	NSIndexSet * selected = _table.selectedRowIndexes;

	[selected enumerateIndexesUsingBlock:^(NSUInteger row, BOOL * stop) {
		if (row >= model.results.size()) return;
		bpm_track_result & r = model.results[row];

		r.bpm *= factor;
		r.adjusted = true;

		// The fluctuation and the opening tempo are both quoted in BPM at the
		// level the BPM column shows, so they follow the same factor. The key
		// and the tuning do not: they are not tempo measurements, and nothing
		// these buttons do can change them.
		r.initial_bpm *= factor;
		r.spread      *= factor;
	}];

	[_table reloadDataForRowIndexes:selected
	                  columnIndexes:[NSIndexSet indexSetWithIndexesInRange:
	                                     NSMakeRange(0, _table.numberOfColumns)]];
	// A doubled BPM can be a digit wider than the one it replaced.
	[self sizeColumnsToContents];
}

- (IBAction)onUpdate:(id)sender
{
	write_tags(*_model);
	[self close];
}

- (IBAction)onCancel:(id)sender
{
	[self close];
}

- (void)windowWillClose:(NSNotification *)notification
{
	[g_openWindows removeObject:self];
}

@end


/***** bpm_ui.h *****/

void bpm_show_results(metadb_handle_list_cref p_tracks,
                      const pfc::list_t<file_info_impl> & p_infos,
                      const std::vector<bpm_track_result> & p_results)
{
	auto model = std::make_shared<results_model>();
	model->tracks  = p_tracks;
	model->infos   = p_infos;
	model->results = p_results;

	// Read before anything can write to the copy in the model. Kept as the
	// string the file carried rather than a parsed number: it is being shown
	// for comparison, and rounding someone's tap on the way to the screen
	// would defeat the point.
	const pfc::string8 bpm_tag = bpm_tag_name();
	model->tag_bpms.resize(p_infos.get_size());
	for (t_size i = 0; i < p_infos.get_size(); i++)
	{
		const char * value = p_infos[i].meta_get(bpm_tag, 0);
		if (value != nullptr) model->tag_bpms[i] = value;
	}

	// With "write tags automatically" on the numbers go straight to the files
	// and no window is put up at all, which is what the Windows dialog does by
	// destroying itself the moment it initialises.
	if (bpm_config_auto_write_tag)
	{
		write_tags(*model);
		return;
	}

	if (g_openWindows == nil) g_openWindows = [NSMutableArray new];

	fooRubatoResultsWindow * window = [[fooRubatoResultsWindow alloc] initWithModel:model];
	[g_openWindows addObject:window];
	[window showWindow:nil];
	[window.window makeKeyAndOrderFront:nil];
}
