#pragma once

// Objective-C has one flat namespace for class names across every bundle the
// process has loaded, so two components that both define a class called, say,
// PreferencesView would find that whichever loaded second silently lost. The
// SDK's mitigation is this header: any of its macOS helpers a component
// compiles get their class names suffixed with the value below.
//
// foo_rubato's own classes are named rather than macro-built - they are
// already prefixed fooRubato and will not collide - so this exists for the
// helpers in foobar2000/helpers-mac, which #error without it.
#define FOOBAR2000_MAC_CLASS_SUFFIX _foo_rubato
