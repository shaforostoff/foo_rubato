# The DeaDBeeF plugin with GNU Make, for DeaDBeeF's plugin builder
# (github.com/DeaDBeeF-Player/deadbeef-plugin-builder), which runs make or
# autotools and nothing else. CMakeLists.txt beside this is the build for
# everything else - the tests, the previews, Visual Studio, the release
# scripts - and this one has to be kept in step with it by hand: the sources
# and the flags that matter are the same.
#
# It builds the shipping configuration only: PFFFT at single precision, and
# the windows for the platform's interface - GTK 3 on Linux and Windows, Cocoa
# on macOS.
#
#     make                   ddb_rubato.so, .dylib or .dll, here
#     make WINDOWS=none      without the windows
#     make check             the host test, against these same objects
#     make clean
#
# Taken from the environment, as the builder sets them: CC, CXX, CFLAGS,
# CXXFLAGS, LDFLAGS, and GTK3_CFLAGS / GTK3_LIBS, which fall back to
# pkg-config where they are not given. On macOS the builder builds once per
# architecture with -arch in CFLAGS and CXXFLAGS and joins the two with lipo,
# so nothing here names an architecture.

ifeq ($(OS),Windows_NT)
PLATFORM := windows
else ifeq ($(shell uname -s),Darwin)
PLATFORM := mac
else
PLATFORM := linux
endif

ifeq ($(PLATFORM),mac)
WINDOWS ?= cocoa
PLUGIN := ddb_rubato.dylib
else ifeq ($(PLATFORM),windows)
WINDOWS ?= gtk
PLUGIN := ddb_rubato.dll
else
WINDOWS ?= gtk
PLUGIN := ddb_rubato.so
endif

ROOT := ..
OBJ := obj

# The version has one home, project(VERSION) in the top-level CMakeLists.txt.
VERSION := $(shell sed -n 's/^[[:space:]]*VERSION[[:space:]]*\([0-9][0-9.]*\).*$$/\1/p' $(ROOT)/CMakeLists.txt | head -1)
VERSION_MAJOR := $(word 1,$(subst ., ,$(VERSION)))
VERSION_MINOR := $(word 2,$(subst ., ,$(VERSION)))

# --- sources ----------------------------------------------------------------
BPMCORE_SRC := $(addprefix $(ROOT)/bpmcore/,analyse.cpp key.cpp odf.cpp real_fft.cpp \
               resample.cpp rhythm.cpp tempo.cpp)
PFFFT_SRC := $(ROOT)/pffft/pffft.c
PLUGIN_SRC := rubato_plugin.cpp rubato_results.cpp

ifeq ($(WINDOWS),gtk)
UI_SRC := gtk/ui_gtk.cpp
GTK3_CFLAGS ?= $(shell pkg-config --cflags gtk+-3.0)
GTK3_LIBS ?= $(shell pkg-config --libs gtk+-3.0)
# As system headers, so their own warnings - GTK 3.10's, which the builder
# compiles against, have several under C++ - are not reported as ours.
UI_CFLAGS := $(patsubst -I%,-isystem %,$(GTK3_CFLAGS))
UI_LIBS := $(GTK3_LIBS)
HAVE_WINDOWS := 1
else ifeq ($(WINDOWS),cocoa)
UI_SRC := cocoa/ui_cocoa.mm
UI_LIBS := -framework Cocoa
HAVE_WINDOWS := 1
else ifeq ($(WINDOWS),none)
UI_SRC := ui_none.cpp
HAVE_WINDOWS := 0
else
$(error WINDOWS must be gtk, cocoa or none, not $(WINDOWS))
endif

obj_of = $(OBJ)/$(subst ..,up,$(basename $(1))).o
BPMCORE_OBJ := $(foreach s,$(BPMCORE_SRC),$(call obj_of,$(s)))
PFFFT_OBJ := $(call obj_of,$(PFFFT_SRC))
PLUGIN_OBJ := $(foreach s,$(PLUGIN_SRC),$(call obj_of,$(s)))
UI_OBJ := $(call obj_of,$(UI_SRC))
ALL_OBJ := $(BPMCORE_OBJ) $(PFFFT_OBJ) $(PLUGIN_OBJ) $(UI_OBJ)

# --- flags ------------------------------------------------------------------
# Ours first, so that the builder's include path - which carries DeaDBeeF's
# headers too - cannot put another deadbeef.h in front of the one this was
# written against.
FFT_DEFS := -DBPMCORE_FFT_PFFFT -DBPMCORE_FFT_SCALAR_TYPE=float
OPT := -O3 -DNDEBUG
BASE := -fPIC -fvisibility=hidden -Wall $(OPT)
INCLUDES := -I. -Iinclude -I$(OBJ) -I$(ROOT)
CXX_ONLY := -std=c++17 -fvisibility-inlines-hidden

ALL_CFLAGS := $(BASE) -I$(ROOT)/pffft $(CFLAGS)
ALL_CXXFLAGS := $(INCLUDES) $(BASE) $(CXX_ONLY) $(FFT_DEFS) -DRUBATO_HAVE_WINDOWS=$(HAVE_WINDOWS) $(CXXFLAGS)

ifeq ($(PLATFORM),mac)
# DeaDBeeF for Mac asks for 10.13; on Apple Silicon the compiler raises it to
# 11, the first macOS there was.
MIN_MACOS := -mmacosx-version-min=10.13
ALL_CFLAGS += $(MIN_MACOS)
ALL_CXXFLAGS += $(MIN_MACOS)
# The one symbol DeaDBeeF looks up; see ddb_rubato.map for why hidden
# visibility alone does not keep the rest in.
PLUGIN_LDFLAGS := -bundle -Wl,-exported_symbol,_ddb_rubato_load
else ifeq ($(PLATFORM),windows)
# libstdc++ and libgcc go in the DLL. GTK does not: the windows must draw with
# the copy DeaDBeeF has loaded. Without GTK, winpthreads goes in too and the
# DLL depends on nothing but Windows.
PLUGIN_LDFLAGS := -shared -static-libgcc -static-libstdc++
ifeq ($(WINDOWS),none)
PLUGIN_LDFLAGS += -static
endif
else
# libstdc++ and libgcc go in and stay private, so the plugin asks for no
# particular C++ runtime; the player's API arrives as a table of pointers, so
# nothing is left to resolve against it.
PLUGIN_LDFLAGS := -shared -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL \
                  -Wl,--version-script=ddb_rubato.map -Wl,--no-undefined
endif

# --- rules ------------------------------------------------------------------
.PHONY: all clean check

all: $(PLUGIN)

$(OBJ)/rubato_version.h: rubato_version.h.in $(ROOT)/CMakeLists.txt
	@mkdir -p $(dir $@)
	sed -e 's/@RUBATO_VERSION@/$(VERSION)/' \
	    -e 's/@RUBATO_VERSION_MAJOR@/$(VERSION_MAJOR)/' \
	    -e 's/@RUBATO_VERSION_MINOR@/$(VERSION_MINOR)/' $< > $@

$(PLUGIN_OBJ) $(UI_OBJ): $(OBJ)/rubato_version.h

$(OBJ)/up/%.o: ../%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -c $< -o $@

$(OBJ)/up/%.o: ../%.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(OBJ)/gtk/%.o: gtk/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) $(UI_CFLAGS) -c $< -o $@

$(OBJ)/cocoa/%.o: cocoa/%.mm
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -fobjc-arc -c $< -o $@

$(OBJ)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -c $< -o $@

# CXXFLAGS on the link line too: on macOS that is where the builder's -arch is.
$(PLUGIN): $(ALL_OBJ) ddb_rubato.map
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(PLUGIN_LDFLAGS) $(MIN_MACOS) -o $@ $(ALL_OBJ) $(UI_LIBS) -lpthread -lm
ifeq ($(PLATFORM),mac)
	# Apple Silicon will not load unsigned code; ad hoc is enough for DeaDBeeF.
	codesign --force --sign - $@
endif

# The host test, linked against the objects the plugin is made of, with the
# recording window system in place of the real one.
TEST_SRC := tests/host_test.cpp tests/fake_host.cpp tests/ui_recorder.cpp
TEST_OBJ := $(foreach s,$(TEST_SRC),$(call obj_of,$(s)))
$(TEST_OBJ): $(OBJ)/rubato_version.h
$(OBJ)/ddb_rubato_hosttest: $(TEST_OBJ) $(BPMCORE_OBJ) $(PFFFT_OBJ) $(PLUGIN_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(MIN_MACOS) -o $@ $^ -lpthread -lm

check: $(OBJ)/ddb_rubato_hosttest
	$(OBJ)/ddb_rubato_hosttest

clean:
	rm -rf $(OBJ) ddb_rubato.so ddb_rubato.dylib ddb_rubato.dll
