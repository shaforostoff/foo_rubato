#ifndef __MAC_STRINGS_H__
#define __MAC_STRINGS_H__

#import <Cocoa/Cocoa.h>

#include <pfc/pfc.h>

// UTF-8 in and out of Cocoa.
//
// Everything foobar2000 hands a component is UTF-8 and everything it takes
// back is too, so these two are the whole of the conversion this component
// needs - the SDK's fb2k::strToPlatform does the same and is not compiled in
// for the sake of it.

//! An NSString for a UTF-8 string, never nil.
//!
//! stringWithUTF8String: returns nil on input that is not valid UTF-8, and a
//! nil where a label is expected is not a crash but an empty cell with no
//! explanation. A tag read off a file is the one thing here that arrives from
//! outside, so the fallback is worth having.
static inline NSString * fooRubatoStr(const char * utf8)
{
	if (utf8 == nullptr) return @"";
	NSString * s = [NSString stringWithUTF8String:utf8];
	return s != nil ? s : @"";
}

static inline NSString * fooRubatoStr(const pfc::string8 & utf8)
{
	return fooRubatoStr(utf8.get_ptr());
}

//! The UTF-8 behind an NSString. Empty for nil.
static inline pfc::string8 fooRubatoUTF8(NSString * s)
{
	pfc::string8 out;
	if (s != nil)
	{
		const char * utf8 = [s UTF8String];
		if (utf8 != nullptr) out = utf8;
	}
	return out;
}

#endif // __MAC_STRINGS_H__
