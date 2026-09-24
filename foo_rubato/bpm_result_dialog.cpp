#include "stdafx.h"

#include <string>

#include "bpm_result_dialog.h"
#include "preferences.h"
#include "format_bpm.h"
#include "file_info_filter_bpm.h"
#include "bpm_result_format.h"
#include "bpm_ui.h"
#include "bpm_tmpo_sync.h"

using std::string;

bpm_result_dialog::bpm_result_dialog(metadb_handle_list_cref p_tracks,
                                     const pfc::list_t<file_info_impl> &p_infos,
                                     const std::vector<bpm_track_result> &p_results):
	m_tracks(p_tracks),
	m_infos(p_infos),
	m_results(p_results)
{
	// Read before anything here can write to m_infos. Kept as the string the
	// file carried rather than a parsed number: it is being shown for
	// comparison, and rounding someone's tap on the way to the screen would
	// defeat the point.
	const pfc::string8 bpm_tag = bpm_tag_name();
	m_tag_bpms.resize(p_infos.get_size());
	for (t_size i = 0; i < p_infos.get_size(); i++)
	{
		const char * value = p_infos[i].meta_get(bpm_tag, 0);
		if (value != NULL) m_tag_bpms[i] = value;
	}
}

LRESULT bpm_result_dialog::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	if (bpm_config_auto_write_tag)
	{
		const pfc::string8 rhythm_tag = bpm_rhythm_tag_or_empty();
		metadb_io_v2::get()->update_info_async(
			m_tracks,
			fb2k::service_new<file_info_filter_bpm>(m_tracks, bpm_tag_name(), m_results,
			                                        rhythm_tag.is_empty() ? nullptr : rhythm_tag.get_ptr()),
			core_api::get_main_window(),
			metadb_io_v2::op_flag_background | metadb_io_v2::op_flag_delay_ui,
			bpm_tmpo_sync_after(m_tracks, bpm_tag_name()));

		DestroyWindow();
	}
	else
	{
		CListViewCtrl result_list = GetDlgItem(ID_BPM_RESULT_LIST);

		// Built in sequence rather than at fixed indices: the tag column is
		// only present when there is something to put in it.
		const bool have_tag_bpm =
			std::any_of(m_tag_bpms.begin(), m_tag_bpms.end(),
			            [](const pfc::string8 & v) { return !v.is_empty(); });

		unsigned col = 0;
		listview_helper::insert_column(result_list, col++, "Title", 270);
		// TODO: Remember status of scan result (ie. success, ambiguous, double, half)
	//	 listview_helper::insert_column(result_list, col++, "Status", 60);
		// The BPM for the whole side, what the file already said, the tempo it
		// opens at and how much it moves all read as one group, so they sit
		// together, with the measurement next to the tap it can be judged by.
		m_col_bpm = static_cast<int>(col);
		listview_helper::insert_column(result_list, col++, "BPM", 50);
		if (have_tag_bpm)
		{
			m_col_tag_bpm = static_cast<int>(col);
			listview_helper::insert_column(result_list, col++, "BPM from tag", 60);
		}
		m_col_initial = static_cast<int>(col);
		listview_helper::insert_column(result_list, col++, "Initial BPM", 60);
		m_col_spread = static_cast<int>(col);
		listview_helper::insert_column(result_list, col++, "Fluctuation", 70);
		m_col_rhythm = static_cast<int>(col);
		listview_helper::insert_column(result_list, col++, "Rhythm", 70);
		// Key and tuning only when something was measured. With detection
		// switched off in the preferences there is nothing to put in them,
		// and two permanently empty columns would push the title out of the
		// window for no return.
		const bool have_key =
			std::any_of(m_results.begin(), m_results.end(),
			            [](const bpm_track_result & r) { return r.key.ok; });
		if (have_key)
		{
			m_col_key = static_cast<int>(col);
			listview_helper::insert_column(result_list, col++, "Key", 70);
			m_col_tuning = static_cast<int>(col);
			listview_helper::insert_column(result_list, col++, "Tuning", 60);
		}
		// TODO: Allow selection of an alternate BPM
	//	 listview_helper::insert_column(result_list, col++, "BPM (Alt)", 50);

		result_list.SetExtendedListViewStyle(LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT);// | LVS_EX_CHECKBOXES);
		CreateRowTooltip(result_list);

		string title_column;

		for (t_size index = 0; index < m_infos.get_size(); index++)
		{
			if (m_infos[index].meta_exists("TITLE"))
				title_column = m_infos[index].meta_get("TITLE", 0);
			else
				title_column = pfc::string_filename(m_tracks[index]->get_path());

			// listview_helper indexes rows as unsigned; on a 64 bit build the
			// loop counter is wider than that, so narrow it explicitly.
			const unsigned row = pfc::downcast_guarded<unsigned>(index);

			listview_helper::insert_item(result_list, row, title_column.c_str(), 0);

			const bpm_track_result & r = m_results[index];
			format_bpm bpm_value(r.bpm);

			listview_helper::set_item_text(result_list, row, m_col_bpm, bpm_value);
			if (m_col_tag_bpm >= 0 && index < m_tag_bpms.size())
				listview_helper::set_item_text(result_list, row, m_col_tag_bpm,
				                               bpm_format_tag_bpm(m_tag_bpms[index]));
			listview_helper::set_item_text(result_list, row, m_col_initial,
			                               bpm_format_initial(r.initial_bpm));
			listview_helper::set_item_text(result_list, row, m_col_spread,
			                               bpm_format_spread(r.spread));
			listview_helper::set_item_text(result_list, row, m_col_rhythm, r.rhythm);
			if (m_col_key >= 0)
			{
				listview_helper::set_item_text(result_list, row, m_col_key,
				                               bpm_format_key_column(r.key));
				listview_helper::set_item_text(result_list, row, m_col_tuning,
				                               bpm_format_tuning_column(r.key));
			}
		}

		SizeColumnsToContents();

		pfc::string_formatter bpm_tag_label;
		bpm_tag_label << "BPM will be written to %" << bpm_tag_name().get_ptr() << "% tag.";
		uSetDlgItemText(m_hWnd, ID_RESULT_BPM_TAG, bpm_tag_label);

		EnableScaleBPMButtons();
		LabelUpdateButton();
	}

	return 0;
}

LRESULT bpm_result_dialog::OnOK(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	const pfc::string8 rhythm_tag = bpm_rhythm_tag_or_empty();
	metadb_io_v2::get()->update_info_async(
		m_tracks,
		fb2k::service_new<file_info_filter_bpm>(m_tracks, bpm_tag_name(), m_results,
		                                        rhythm_tag.is_empty() ? nullptr : rhythm_tag.get_ptr()),
		core_api::get_main_window(),
		metadb_io_v2::op_flag_background | metadb_io_v2::op_flag_delay_ui,
		bpm_tmpo_sync_after(m_tracks, bpm_tag_name()));

	DestroyWindow();
	return 0;
}

LRESULT bpm_result_dialog::OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	DestroyWindow();
	return 0;
}

LRESULT bpm_result_dialog::OnDoubleBPMClicked(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	ScaleSelectionBPM(2.0);
	return 0;
}

LRESULT bpm_result_dialog::OnHalveBPMClicked(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	ScaleSelectionBPM(0.5);
	return 0;
}

LRESULT bpm_result_dialog::OnItemChanged(LPNMHDR pnmh)
{
	EnableScaleBPMButtons();

	return 0;
}

void bpm_result_dialog::OnClose()
{
	DestroyWindow();
}

void bpm_result_dialog::PostNcDestroy()
{
	delete this;
}

bool bpm_result_dialog::pretranslate_message(MSG *p_msg)
{
	if (m_hWnd != NULL)
	{
		RelayToTooltip(p_msg);

		if (IsDialogMessage(p_msg))
		{
			return true;
		}
	}

	return false;
}

//! Bare newlines, which is what bpm_key_format.h writes, into the CRLF a Win32
//! tooltip needs before it will draw a second line at all.
//!
//! The conversion lives here rather than in the formatter because the formatter
//! is shared with the Cocoa window, which wants the bare ones, and with
//! foo_rubato_test, which builds without a host at all.
static pfc::string8 bpm_tooltip_crlf(const pfc::string8 & in)
{
	pfc::string8 out;
	for (const char * p = in.get_ptr(); *p != '\0'; p++)
	{
		if (*p == '\n') out.add_string("\r\n", 2);
		else out.add_byte(*p);
	}
	return out;
}

//! A tooltip belonging to this window, covering the whole list.
//!
//! The control has one of its own, and LVS_EX_INFOTIP will even route it
//! through LVN_GETINFOTIP, but in report mode the tooltip it offers belongs to
//! the item rather than to the cell, and that route showed nothing at all over
//! the tuning column. This one is filled in and shown by hand, off the mouse
//! messages pretranslate_message already sees - which also means it can be
//! watched from outside a host, and was.
//!
//! The control's own tooltip goes, rather than sitting alongside: it draws the
//! truncated title over column 0 and would pop on top of this one. What it did
//! is folded into the row text instead, which is what TitleIsClipped is for. It
//! is not destroyed here - it is a popup owned by the list view, so it goes
//! when the list does.
void bpm_result_dialog::CreateRowTooltip(CListViewCtrl & result_list)
{
	ListView_SetToolTips(result_list.m_hWnd, NULL);

	if (m_tips.Create(m_hWnd, NULL, NULL,
	                  WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP) == NULL) return;

	// TTS_NOPREFIX because titles contain ampersands and an ampersand in a
	// tooltip is otherwise eaten as an accelerator.
	//
	// The text is left to a callback rather than handed over now. A tooltip
	// stores 80 characters for a tool and these run to several hundred, so
	// what it gets is a pointer into m_tip_text, refreshed every time it asks.
	//
	// The two window handles are not the same window and the difference is the
	// whole thing: uId is what the pointer has to be over, and hwnd is who gets
	// asked for the text. Naming the list for both looks right, costs no error,
	// and silently sends TTN_GETDISPINFO to a control that does not answer it -
	// leaving the tooltip with nothing to draw and nothing drawn.
	CToolInfo tool(TTF_IDISHWND, m_hWnd,
	               reinterpret_cast<UINT_PTR>(result_list.m_hWnd), NULL,
	               LPSTR_TEXTCALLBACK);
	m_tips.AddTool(&tool);

	// A tooltip with no maximum width is a single line whatever it is given:
	// the CRLFs are drawn as spaces and nothing wraps. Asking for a width is
	// what makes it a paragraph, and 380 units is about sixty characters of
	// the dialog's own face.
	m_tips.SetMaxTipWidth(380);
	// Five seconds is the default and is not long enough to read a paragraph
	// of this length; it stays up until the pointer moves off the row.
	m_tips.SetDelayTime(TTDT_AUTOPOP, 30000);
	// Nothing to say until the pointer is over a row worth saying it for.
	m_tips.Activate(FALSE);
}

//! The tooltip's eyes. It has no hook into the message stream of its own -
//! TTF_SUBCLASS would give it one, but subclassing the list view to get it
//! would be a larger thing than this - so the mouse messages bound for the
//! list are handed over here, and the row under the pointer settled on the way
//! past.
void bpm_result_dialog::RelayToTooltip(MSG * p_msg)
{
	if (m_tips.m_hWnd == NULL) return;
	switch (p_msg->message)
	{
	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN: case WM_LBUTTONUP:
	case WM_RBUTTONDOWN: case WM_RBUTTONUP:
	case WM_MBUTTONDOWN: case WM_MBUTTONUP:
		break;
	default:
		return;
	}

	CListViewCtrl result_list = GetDlgItem(ID_BPM_RESULT_LIST);
	if (result_list.m_hWnd == NULL || p_msg->hwnd != result_list.m_hWnd) return;

	if (p_msg->message == WM_MOUSEMOVE)
	{
		// The message carries the point, which is the one the tooltip is about
		// to be shown for; the cursor may already have moved on by now.
		LVHITTESTINFO hit = {};
		hit.pt.x = static_cast<short>(LOWORD(p_msg->lParam));
		hit.pt.y = static_cast<short>(HIWORD(p_msg->lParam));
		result_list.HitTest(&hit);
		SetTooltipRow(result_list, hit.iItem);
	}
	// After the text, so that the move which changed the row is also the move
	// that starts the delay the new text will appear after.
	m_tips.RelayEvent(p_msg);
}

//! Build the text for a row, or take the tooltip away where there is none.
void bpm_result_dialog::SetTooltipRow(CListViewCtrl & result_list, int row)
{
	if (row == m_tip_row) return;
	m_tip_row = row;

	pfc::string8 text;
	if (row >= 0 && static_cast<std::size_t>(row) < m_results.size())
	{
		// Read back off the control rather than rebuilt from m_infos, so that
		// the tooltip cannot name the row differently from the row.
		TCHAR title[512] = {};
		result_list.GetItemText(row, 0, title, static_cast<int>(std::size(title)));
		const bpm_track_result & r = m_results[row];
		text = bpm_format_row_tooltip(
			pfc::stringcvt::string_utf8_from_wide(title),
			TitleIsClipped(result_list, title), r.key, r.year);
	}

	if (text.is_empty())
	{
		// Nothing to say is said by not appearing at all, rather than by an
		// empty box following the pointer down the list.
		m_tip_text.clear();
		m_tips.Activate(FALSE);
		return;
	}

	m_tip_text = pfc::stringcvt::string_wide_from_utf8(bpm_tooltip_crlf(text)).get_ptr();
	m_tips.Activate(TRUE);
	// Anything on screen at this point belongs to the row just left.
	m_tips.Pop();
}

//! Whether column 0 is drawing the title cut short, which is the one case
//! where repeating it in the tooltip tells the reader something they cannot
//! already see.
bool bpm_result_dialog::TitleIsClipped(CListViewCtrl & result_list, const TCHAR * title)
{
	// The same allowance the columns are sized with: GetStringWidth measures
	// the glyphs, and the cell draws a margin either side of them.
	const int padding = 14;
	return result_list.GetStringWidth(title) + padding > result_list.GetColumnWidth(0);
}

LRESULT bpm_result_dialog::OnTipDispInfo(LPNMHDR pnmh)
{
	NMTTDISPINFO * const info = reinterpret_cast<NMTTDISPINFO *>(pnmh);
	info->szText[0] = 0;
	info->hinst = NULL;
	// Pointed at the member rather than copied into the notification's own
	// buffer, which holds 80 characters. It has to outlive this call, which is
	// the whole reason the text is built when the row changes and not here.
	info->lpszText = m_tip_text.empty() ? NULL : const_cast<LPTSTR>(m_tip_text.c_str());
	return 0;
}

void bpm_result_dialog::EnableScaleBPMButtons()
{
	CListViewCtrl listView(GetDlgItem(ID_BPM_RESULT_LIST));

	UINT selected = listView.GetSelectedCount();

	GetDlgItem(ID_DOUBLE_BPM_BUTTON).EnableWindow(selected > 0);
	GetDlgItem(ID_HALVE_BPM_BUTTON).EnableWindow(selected > 0);
}

void bpm_result_dialog::ScaleSelectionBPM(double p_factor)
{
	CWindow result_list = GetDlgItem(ID_BPM_RESULT_LIST);

	int listview_index = -1;
	while ((listview_index = ListView_GetNextItem(result_list, listview_index, LVIS_SELECTED)) != -1)
	{
		if (static_cast<std::size_t>(listview_index) >= m_results.size()) continue;
		bpm_track_result & r = m_results[listview_index];
		r.bpm *= p_factor;
		r.adjusted = true;

		format_bpm bpm_value(r.bpm);

		m_infos[listview_index].meta_set(bpm_tag_name(), bpm_value);
		listview_helper::set_item_text(result_list, listview_index, m_col_bpm, bpm_value);

		// The fluctuation and the opening tempo are both quoted in BPM at the
		// level the BPM column shows, so they follow the same factor. The key
		// and the tuning do not: they are not tempo measurements and nothing
		// the double and halve buttons do can change them.
		r.initial_bpm *= p_factor;
		listview_helper::set_item_text(result_list, listview_index, m_col_initial,
		                               bpm_format_initial(r.initial_bpm));
		r.spread *= p_factor;
		listview_helper::set_item_text(result_list, listview_index, m_col_spread,
		                               bpm_format_spread(r.spread));
	}

	// A doubled BPM can be a digit wider than the one it replaced.
	SizeColumnsToContents();
}

//! Every column but the title is sized to the widest string in it, header
//! included, because their contents are generated and there is no useful width
//! to guess for them. The title then takes whatever is left, which is the only
//! way three sized columns and a title fit a dialog of fixed width without a
//! horizontal scrollbar - the hardcoded widths already overflowed it slightly
//! before the fluctuation column was added.
//!
//! Measured with LVM_GETSTRINGWIDTH rather than left to LVSCW_AUTOSIZE: on the
//! last column LVSCW_AUTOSIZE_USEHEADER stretches to fill the control instead
//! of fitting the text, and plain LVSCW_AUTOSIZE ignores the header, so a
//! column whose header is wider than its values comes out clipped. Both are
//! the wrong answer here, where "Fluctuation" is wider than any of its cells.
void bpm_result_dialog::SizeColumnsToContents()
{
	CListViewCtrl result_list = GetDlgItem(ID_BPM_RESULT_LIST);
	if (result_list == NULL) return;

	// ATL asserts on an absent header in a debug build, so it is checked
	// rather than assumed: the list is populated before every call here, but
	// that is not a property this function can see.
	CHeaderCtrl header_ctrl = result_list.GetHeader();
	if (header_ctrl == NULL) return;
	const int count = header_ctrl.GetItemCount();
	if (count <= 0) return;
	// Room for the cell's own padding, which GETSTRINGWIDTH does not include.
	const int padding = 14;
	const int rows = result_list.GetItemCount();

	int used = 0;
	for (int col = 1; col < count; col++)
	{
		TCHAR text[256] = {};
		LVCOLUMN header = {};
		header.mask = LVCF_TEXT;
		header.pszText = text;
		header.cchTextMax = static_cast<int>(std::size(text));
		int widest = 0;
		if (result_list.GetColumn(col, &header))
			widest = result_list.GetStringWidth(text);

		for (int row = 0; row < rows; row++)
		{
			result_list.GetItemText(row, col, text, static_cast<int>(std::size(text)));
			widest = std::max(widest, result_list.GetStringWidth(text));
		}
		result_list.SetColumnWidth(col, widest + padding);
		used += result_list.GetColumnWidth(col);
	}

	// The dialog cannot be resized, so this is settled once. A vertical
	// scrollbar appears as soon as the list is longer than the window, and its
	// width comes out of the client area, so it is allowed for whether or not
	// it is showing yet - a few pixels of slack in the title beats a horizontal
	// scrollbar under a list that is only one row too long.
	CRect client;
	result_list.GetClientRect(&client);
	const int scrollbar = GetSystemMetrics(SM_CXVSCROLL);
	const int title_min = 80;
	const int left = client.Width() - used - scrollbar;
	result_list.SetColumnWidth(0, std::max(title_min, left));
}

//! How many files the button is about to write, said on the button. It writes
//! every track in the list and always has - the selection drives the double and
//! halve buttons only - so the count is the whole list, and saying it is the
//! answer to what the selection has to do with it.
//!
//! Sized to the label as well, because the count is generated: "Update 99 files"
//! already needs more than the 50 units the fixed label sat in. 74 units holds
//! five digits with room to spare - scripts\render_dialogs.ps1 was pointed at a
//! template saying "Update 99999 files" to see that - so the growth here is not
//! for the count. It is for a host drawing the page in a face wider than the
//! template's own, which is the one thing that can make a fitted label overflow
//! and which nothing in the build can check. It grows from the right edge, so
//! the button keeps its place beside Cancel, and stops at the hint to its left.
void bpm_result_dialog::LabelUpdateButton()
{
	CWindow button = GetDlgItem(IDOK);
	if (button == NULL) return;

	const t_size count = m_tracks.get_count();
	pfc::string_formatter drawn;
	if (count == 1) drawn << "Update file";
	else drawn << "Update " << count << " files";

	// The accelerator prefix goes on after the measurement: it selects the
	// mnemonic rather than being drawn, so measuring it would ask for a
	// character's worth of width the label never uses.
	pfc::string_formatter label;
	label << "&" << drawn;
	uSetDlgItemText(m_hWnd, IDOK, label);

	const pfc::stringcvt::string_wide_from_utf8 wide(drawn);
	CWindowDC dc(button);
	CSize text;
	HFONT font = button.GetFont();
	HFONT previous = (font != NULL) ? dc.SelectFont(font) : NULL;
	const BOOL measured = dc.GetTextExtent(wide.get_ptr(),
	                                       pfc::downcast_guarded<int>(wide.length()), &text);
	if (previous != NULL) dc.SelectFont(previous);
	if (!measured) return;

	CRect rect;
	button.GetWindowRect(&rect);
	ScreenToClient(&rect);

	// Room for the margins the button draws either side of its text, which the
	// extent does not include - the same figure the columns above use, for the
	// same reason.
	const int padding = 14;
	int target = rect.right - std::max<int>(rect.Width(), text.cx + padding);

	CWindow hint = GetDlgItem(ID_RESULT_SELECT_HINT);
	if (hint != NULL)
	{
		CRect hint_rect;
		hint.GetWindowRect(&hint_rect);
		ScreenToClient(&hint_rect);
		// A count absurd enough to reach this would clip, but so would any
		// other answer once the row is full.
		const int gap = 8;
		target = std::max<int>(target, hint_rect.right + gap);
	}

	// Only ever wider: a one file label is narrower than the template, and a
	// button that changes size with the count draws the eye to the wrong thing.
	if (target < rect.left)
	{
		button.SetWindowPos(NULL, target, rect.top, rect.right - target, rect.Height(),
		                    SWP_NOZORDER | SWP_NOACTIVATE);
	}
}


/***** bpm_ui.h *****/

void bpm_show_results(metadb_handle_list_cref p_tracks,
                      const pfc::list_t<file_info_impl> & p_infos,
                      const std::vector<bpm_track_result> & p_results)
{
	// Deletes itself in PostNcDestroy, so it is not held onto here.
	bpm_result_dialog * dialog = new bpm_result_dialog(p_tracks, p_infos, p_results);

	dialog->Create(core_api::get_main_window(), NULL);
	if (dialog->IsWindow())
	{
		dialog->ShowWindow(SW_SHOWNORMAL);
	}
}
