#include "stdafx.h"

#include "bpm_preferences_page.h"

#include "preferences.h"

void bpm_preferences_page::reset()
{
	// Tagging
	CComboBox bpm_precision_box = GetDlgItem(ID_CONFIG_BPM_PRECISION);
	bpm_precision_box.SetCurSel(BPM_PRECISION_1);
	SetDlgItemText(ID_CONFIG_BPM_TAG, _T("BPM"));
	CheckDlgButton(ID_CONFIG_AUTO_WRITE_TAG, BST_UNCHECKED);
	CheckDlgButton(ID_CONFIG_WRITE_INITIAL_BPM, BST_CHECKED);
	CheckDlgButton(ID_CONFIG_WRITE_BPM_ALGORITHM, BST_CHECKED);

	// Manual
	SetDlgItemInt(ID_CONFIG_TAPS_TO_AVERAGE, 30, true);
	SetDlgItemInt(ID_CONFIG_SECONDS_TO_RESET_AVERAGE, 5, true);

	// Diagnostics
	CheckDlgButton(ID_CONFIG_OUTPUT_DEBUG, BST_UNCHECKED);
}

BOOL bpm_preferences_page::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	wchar_t w_bpm_tag[256];
	const pfc::string8 bpm_tag = bpm_tag_name();

	// Tagging. The list order must match bpm_precision_enum.
	CComboBox bpm_precision_box = GetDlgItem(ID_CONFIG_BPM_PRECISION);
	bpm_precision_box.AddString(_T("Nearest 1"));
	bpm_precision_box.AddString(_T("1 Decimal"));
	bpm_precision_box.AddString(_T("2 Decimals"));
	bpm_precision_box.SetCurSel(bpm_config_bpm_precision);
	pfc::stringcvt::convert_ansi_to_wide(w_bpm_tag, 256, bpm_tag.get_ptr(), bpm_tag.length());
	SetDlgItemText(ID_CONFIG_BPM_TAG, w_bpm_tag);
	CheckDlgButton(ID_CONFIG_AUTO_WRITE_TAG, bpm_config_auto_write_tag ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(ID_CONFIG_WRITE_INITIAL_BPM, bpm_config_write_initial_bpm ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(ID_CONFIG_WRITE_BPM_ALGORITHM, bpm_config_write_bpm_algorithm ? BST_CHECKED : BST_UNCHECKED);

	// Manual
	SetDlgItemInt(ID_CONFIG_TAPS_TO_AVERAGE, bpm_config_taps_to_average, true);
	SetDlgItemInt(ID_CONFIG_SECONDS_TO_RESET_AVERAGE, bpm_config_seconds_to_reset_average, true);

	// Diagnostics
	CheckDlgButton(ID_CONFIG_OUTPUT_DEBUG, bpm_config_output_debug ? BST_CHECKED : BST_UNCHECKED);

	return TRUE;
}

void bpm_preferences_page::OnEditControlChange(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	switch (nID)
	{
		case ID_CONFIG_BPM_TAG:
			break;
		case ID_CONFIG_TAPS_TO_AVERAGE:
			if (GetDlgItemInt(ID_CONFIG_TAPS_TO_AVERAGE, NULL, false) < 1)
			{
				SetDlgItemInt(ID_CONFIG_TAPS_TO_AVERAGE, 1, true);
			}
			break;
		case ID_CONFIG_SECONDS_TO_RESET_AVERAGE:
			if (GetDlgItemInt(ID_CONFIG_SECONDS_TO_RESET_AVERAGE, NULL, false) < 1)
			{
				SetDlgItemInt(ID_CONFIG_SECONDS_TO_RESET_AVERAGE, 1, true);
			}
			break;
		default:
			break;
	}

	OnChanged();
}

void bpm_preferences_page::OnComboBoxChange(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	switch (nID)
	{
		case ID_CONFIG_BPM_PRECISION:
			break;
		default:
			break;
	}

	OnChanged();
}

void bpm_preferences_page::OnBnClicked(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	switch (nID)
	{
		case ID_CONFIG_OUTPUT_DEBUG:
			break;
		case ID_CONFIG_AUTO_WRITE_TAG:
			break;
		case ID_CONFIG_WRITE_INITIAL_BPM:
			break;
		case ID_CONFIG_WRITE_BPM_ALGORITHM:
			break;
		default:
			break;
	}

	OnChanged();
}

void bpm_preferences_page::apply()
{
	pfc::string8 bpm_tag;
	uGetDlgItemText(*this, ID_CONFIG_BPM_TAG, bpm_tag);
	bpm_config_bpm_tag = bpm_tag.get_ptr();

	bpm_config_taps_to_average = GetDlgItemInt(ID_CONFIG_TAPS_TO_AVERAGE, NULL, false);
	bpm_config_seconds_to_reset_average = GetDlgItemInt(ID_CONFIG_SECONDS_TO_RESET_AVERAGE, NULL, false);

	CComboBox bpm_precision_box = GetDlgItem(ID_CONFIG_BPM_PRECISION);
	bpm_config_bpm_precision = bpm_precision_box.GetCurSel();

	bpm_config_output_debug = (IsDlgButtonChecked(ID_CONFIG_OUTPUT_DEBUG) == BST_CHECKED);
	bpm_config_auto_write_tag = (IsDlgButtonChecked(ID_CONFIG_AUTO_WRITE_TAG) == BST_CHECKED);
	bpm_config_write_initial_bpm = (IsDlgButtonChecked(ID_CONFIG_WRITE_INITIAL_BPM) == BST_CHECKED);
	bpm_config_write_bpm_algorithm = (IsDlgButtonChecked(ID_CONFIG_WRITE_BPM_ALGORITHM) == BST_CHECKED);
}

t_uint32 bpm_preferences_page::get_state()
{
	t_uint32 state = preferences_state::resettable;

	if (HasChanged())
	{
		state |= preferences_state::changed;
	}

	return state;
}

bool bpm_preferences_page::HasChanged()
{
	bool changed = false;

	pfc::string8 bpm_tag;
	uGetDlgItemText(*this, ID_CONFIG_BPM_TAG, bpm_tag);
	if (strcmp(bpm_tag_name(), bpm_tag) != 0 ||
		bpm_config_taps_to_average != (int) GetDlgItemInt(ID_CONFIG_TAPS_TO_AVERAGE, NULL, false) ||
		bpm_config_seconds_to_reset_average != (int) GetDlgItemInt(ID_CONFIG_SECONDS_TO_RESET_AVERAGE, NULL, false) ||
		bpm_config_output_debug != (IsDlgButtonChecked(ID_CONFIG_OUTPUT_DEBUG) == BST_CHECKED) ||
		bpm_config_auto_write_tag != (IsDlgButtonChecked(ID_CONFIG_AUTO_WRITE_TAG) == BST_CHECKED) ||
		bpm_config_write_initial_bpm != (IsDlgButtonChecked(ID_CONFIG_WRITE_INITIAL_BPM) == BST_CHECKED) ||
		bpm_config_write_bpm_algorithm != (IsDlgButtonChecked(ID_CONFIG_WRITE_BPM_ALGORITHM) == BST_CHECKED))
	{
		changed = true;
	}

	CComboBox bpm_precision_box = GetDlgItem(ID_CONFIG_BPM_PRECISION);
	if (bpm_config_bpm_precision != bpm_precision_box.GetCurSel())
	{
		changed = true;
	}

	return changed;
}

void bpm_preferences_page::OnChanged()
{
	m_callback->on_state_changed();
}






const char * bpm_preferences_page_impl::get_name()
{
	return "Rubato BPM Analyzer";
}

GUID bpm_preferences_page_impl::get_guid()
{
	return guid_bpm_preferences;
}

GUID bpm_preferences_page_impl::get_parent_guid()
{
	return preferences_page::guid_tools;
}

static preferences_page_factory_t<bpm_preferences_page_impl> g_bpm_preferences_page_impl;
