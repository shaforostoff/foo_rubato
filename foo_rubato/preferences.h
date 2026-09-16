#ifndef __PREFERENCES_H__
#define __PREFERENCES_H__

#include "resource.h"
#include "globals.h"
#include "guid.h"

#include <SDK/foobar2000.h>
#ifdef _WIN32
// ATL and WTL, which the macOS build has neither of nor any use for.
#include <helpers/atl-misc.h>
#endif

typedef enum
{
	BPM_PRECISION_1 = 0,
	BPM_PRECISION_1DP,
	BPM_PRECISION_2DP
} bpm_precision_enum;

#endif