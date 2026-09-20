#ifndef __BPM_PREFERENCES_PAGE_H__
#define __BPM_PREFERENCES_PAGE_H__

#include <SDK/foobar2000.h>
#include <helpers/atl-misc.h>

#include "resource.h"

class bpm_preferences_page : public preferences_page_instance, public CDialogImpl<bpm_preferences_page>
{
	public:
		bpm_preferences_page(preferences_page_callback::ptr callback) : m_callback(callback) {}

		/***** CDialogImpl *****/
		enum { IDD = IDD_BPM_PREFERENCES };

		/***** preferences_page *****/
		t_uint32 get_state();
		void apply();
		void reset();

		BEGIN_MSG_MAP_EX(bpm_preferences_page)
			MSG_WM_INITDIALOG(OnInitDialog)
			// General
			COMMAND_HANDLER_EX(ID_CONFIG_BPM_PRECISION, CBN_SELCHANGE, OnComboBoxChange)
			COMMAND_HANDLER_EX(ID_CONFIG_BPM_TAG, EN_CHANGE, OnEditControlChange)
			COMMAND_HANDLER_EX(ID_CONFIG_AUTO_WRITE_TAG, BN_CLICKED, OnBnClicked)
			COMMAND_HANDLER_EX(ID_CONFIG_WRITE_INITIAL_BPM, BN_CLICKED, OnBnClicked)
			COMMAND_HANDLER_EX(ID_CONFIG_WRITE_BPM_ALGORITHM, BN_CLICKED, OnBnClicked)
			// Tuning and key
			COMMAND_HANDLER_EX(ID_CONFIG_DETECT_KEY, BN_CLICKED, OnBnClicked)
			COMMAND_HANDLER_EX(ID_CONFIG_WRITE_KEY, BN_CLICKED, OnBnClicked)
			COMMAND_HANDLER_EX(ID_CONFIG_WRITE_TUNING, BN_CLICKED, OnBnClicked)
			COMMAND_HANDLER_EX(ID_CONFIG_WRITE_RETUNE, BN_CLICKED, OnBnClicked)
			// Diagnostics
			COMMAND_HANDLER_EX(ID_CONFIG_OUTPUT_DEBUG, BN_CLICKED, OnBnClicked)
			// Manual
			COMMAND_HANDLER_EX(ID_CONFIG_TAPS_TO_AVERAGE, EN_CHANGE, OnEditControlChange)
			COMMAND_HANDLER_EX(ID_CONFIG_SECONDS_TO_RESET_AVERAGE, EN_CHANGE, OnEditControlChange)
		END_MSG_MAP()

	private:
		BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
		void OnEditControlChange(UINT uNotifyCode, int nID, CWindow wndCtl);
		void OnComboBoxChange(UINT uNotifyCode, int nID, CWindow wndCtl);
		void OnBnClicked(UINT uNotifyCode, int nID, CWindow wndCtl);
		//! Greys the three write switches while detection itself is off.
		void EnableKeyWriteButtons();
		bool HasChanged();
		void OnChanged();

		const preferences_page_callback::ptr m_callback;
};

class bpm_preferences_page_impl : public preferences_page_impl<bpm_preferences_page>
{
	// preferences_page_impl<> helper deals with instantiation of our dialog; inherits from preferences_page_v3.
	public:
		const char* get_name();
		GUID get_guid();
		GUID get_parent_guid();
};

#endif // __BPM_PREFERENCES_PAGE_H__
