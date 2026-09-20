#ifndef __DPM_RESULT_DIALOG_H__
#define __DPM_RESULT_DIALOG_H__

#include <string>
#include <vector>

#include <SDK/foobar2000.h>
#include <helpers/atl-misc.h>

#include "bpm_track_result.h"
#include "resource.h"

class bpm_result_dialog : public CDialogImpl<bpm_result_dialog>, private message_filter_impl_base
{
public:
	enum { IDD = IDD_BPM_RESULT_DIALOG };

	BEGIN_MSG_MAP_EX(bpm_result_dialog)
		// TODO: Add key modifier for select all (ctrl-A)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnOK)
		COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnCancel)
		COMMAND_HANDLER_EX(ID_DOUBLE_BPM_BUTTON, BN_CLICKED, OnDoubleBPMClicked)
		COMMAND_HANDLER_EX(ID_HALVE_BPM_BUTTON, BN_CLICKED, OnHalveBPMClicked)
		NOTIFY_HANDLER_EX(ID_BPM_RESULT_LIST, LVN_ITEMCHANGED, OnItemChanged)
		NOTIFY_CODE_HANDLER_EX(TTN_GETDISPINFO, OnTipDispInfo)
		MSG_WM_CLOSE(OnClose);
	END_MSG_MAP()

	bpm_result_dialog(metadb_handle_list_cref p_tracks,
	                  const pfc::list_t<file_info_impl> &p_infos,
	                  const std::vector<bpm_track_result> &p_results);
		
private:
	LRESULT OnInitDialog(CWindow wndFocus, LPARAM lInitParam);

	LRESULT OnOK(UINT uNotifyCode, int nID, CWindow wndCtl);
	LRESULT OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl);
	LRESULT OnDoubleBPMClicked(UINT uNotifyCode, int nID, CWindow wndCtl);
	LRESULT OnHalveBPMClicked(UINT uNotifyCode, int nID, CWindow wndCtl);

	LRESULT OnItemChanged(LPNMHDR pnmh);
	//! Hands the tooltip the text for the row the pointer is on.
	LRESULT OnTipDispInfo(LPNMHDR pnmh);

	void OnClose();
	// Override the parent method for when the dialog is destroyed so we can delete its memory
	void PostNcDestroy();

	bool pretranslate_message(MSG *p_msg);

	//! The row tooltip: one of this window's own, because the list view will
	//! not offer one where it is wanted. See bpm_result_dialog.cpp.
	void CreateRowTooltip(CListViewCtrl & result_list);
	void RelayToTooltip(MSG * p_msg);
	void SetTooltipRow(CListViewCtrl & result_list, int row);
	//! Whether column 0 is drawing `title` cut short.
	bool TitleIsClipped(CListViewCtrl & result_list, const TCHAR * title);

	void EnableScaleBPMButtons();
	void ScaleSelectionBPM(double p_factor);
	//! Widths for every column but the title, from the widest text in each.
	void SizeColumnsToContents();
	//! "Update 137 files" on the commit button, and the width to draw it in.
	void LabelUpdateButton();

	//! Column indices. Not constants: the tag column is only there when at
	//! least one of the tracks arrived with a BPM tag on it, and everything to
	//! its right shifts when it is.
	int m_col_bpm = 1;
	int m_col_tag_bpm = -1;   //!< -1 when no track had a BPM tag
	int m_col_initial = 2;
	int m_col_spread = 3;
	int m_col_rhythm = 4;
	//! -1 when nothing was measured on any track, which is what happens when
	//! key detection is switched off in the preferences.
	int m_col_key = -1;
	int m_col_tuning = -1;

	CToolTipCtrl m_tips;
	//! The row m_tip_text was built for, -1 for none. The text is a member
	//! because the tooltip is given a pointer to it rather than a copy.
	int m_tip_row = -1;
	std::wstring m_tip_text;

	metadb_handle_list m_tracks;
	pfc::list_t<file_info_impl> m_infos;
	//! One entry per row, in the order shown. The tempo figures in here are
	//! the ones the double and halve buttons scale, which is why the window
	//! owns a copy rather than reading the analysis back.
	std::vector<bpm_track_result> m_results;
	//! Whatever the BPM tag held before the scan, per track, taken in the
	//! constructor because ScaleSelectionBPM writes over m_infos later. On this
	//! collection those are hand-tapped values, which is the whole reason for
	//! showing them: the measurement can be read against the tap.
	std::vector<pfc::string8> m_tag_bpms;
};

#endif // __DPM_RESULT_DIALOG_H__
