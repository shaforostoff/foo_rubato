#ifndef RUBATO_PLUGIN_H
#define RUBATO_PLUGIN_H

// The one symbol DeaDBeeF looks for, and a hook for the test harness.
//
// DeaDBeeF finds a plugin's entry point from its file name: it strips the
// extension from ddb_rubato.so / .dll / .dylib and appends "_load". Renaming
// the library without renaming this function means it is silently skipped.

#define DDB_API_LEVEL 10   // DeaDBeeF 1.8.0 and later
#include <deadbeef/deadbeef.h>

#if defined(_WIN32)
#define RUBATO_EXPORT __declspec(dllexport)
#else
#define RUBATO_EXPORT __attribute__((visibility("default")))
#endif

extern "C" RUBATO_EXPORT DB_plugin_t * ddb_rubato_load(DB_functions_t * api);

namespace rubato
{
	//! Blocks until every queued track has been analysed and written. The
	//! player never needs this; ddb_rubato_hosttest does.
	void wait_until_idle();
}

#endif // RUBATO_PLUGIN_H
