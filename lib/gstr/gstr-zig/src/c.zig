//! Raw C bindings for gstr.h
//!
//! This module provides direct access to the C API for zero-overhead calls.
//! For most use cases, prefer the idiomatic Zig API in the parent module.

pub const c = @cImport({
    @cInclude("gstr.h");
});

// Re-export all C types and functions
pub const unicode_range = c.struct_unicode_range;
pub const gcb_range = c.struct_gcb_range;
pub const gcb_property = c.enum_gcb_property;

// Constants
pub const UTF8_REPLACEMENT_CHAR = c.UTF8_REPLACEMENT_CHAR;
pub const UTF8_MAX_BYTES = c.UTF8_MAX_BYTES;
pub const GRAPHEME_MAX_BACKTRACK = c.GRAPHEME_MAX_BACKTRACK;

// GCB property values
pub const GCB_OTHER = c.GCB_OTHER;
pub const GCB_CR = c.GCB_CR;
pub const GCB_LF = c.GCB_LF;
pub const GCB_CONTROL = c.GCB_CONTROL;
pub const GCB_EXTEND = c.GCB_EXTEND;
pub const GCB_ZWJ = c.GCB_ZWJ;
pub const GCB_REGIONAL_INDICATOR = c.GCB_REGIONAL_INDICATOR;
pub const GCB_PREPEND = c.GCB_PREPEND;
pub const GCB_SPACING_MARK = c.GCB_SPACING_MARK;
pub const GCB_L = c.GCB_L;
pub const GCB_V = c.GCB_V;
pub const GCB_T = c.GCB_T;
pub const GCB_LV = c.GCB_LV;
pub const GCB_LVT = c.GCB_LVT;

// UTF-8 Layer functions
pub const utf8_decode = c.utf8_decode;
pub const utf8_encode = c.utf8_encode;
pub const utf8_cpwidth = c.utf8_cpwidth;
pub const utf8_charwidth = c.utf8_charwidth;
pub const utf8_is_zerowidth = c.utf8_is_zerowidth;
pub const utf8_is_wide = c.utf8_is_wide;
pub const utf8_next = c.utf8_next;
pub const utf8_prev = c.utf8_prev;
pub const utf8_next_grapheme = c.utf8_next_grapheme;
pub const utf8_prev_grapheme = c.utf8_prev_grapheme;
pub const utf8_valid = c.utf8_valid;
pub const utf8_cpcount = c.utf8_cpcount;
pub const utf8_truncate = c.utf8_truncate;

// Grapheme string layer functions
pub const gstrlen = c.gstrlen;
pub const gstrnlen = c.gstrnlen;
pub const gstrwidth = c.gstrwidth;
pub const gstroff = c.gstroff;
pub const gstrat = c.gstrat;
pub const gstrcmp = c.gstrcmp;
pub const gstrncmp = c.gstrncmp;
pub const gstrcasecmp = c.gstrcasecmp;
pub const gstrncasecmp = c.gstrncasecmp;
pub const gstrstartswith = c.gstrstartswith;
pub const gstrendswith = c.gstrendswith;
pub const gstrchr = c.gstrchr;
pub const gstrrchr = c.gstrrchr;
pub const gstrstr = c.gstrstr;
pub const gstrrstr = c.gstrrstr;
pub const gstrcasestr = c.gstrcasestr;
pub const gstrcount = c.gstrcount;
pub const gstrspn = c.gstrspn;
pub const gstrcspn = c.gstrcspn;
pub const gstrpbrk = c.gstrpbrk;
pub const gstrsub = c.gstrsub;
pub const gstrcpy = c.gstrcpy;
pub const gstrncpy = c.gstrncpy;
pub const gstrcat = c.gstrcat;
pub const gstrncat = c.gstrncat;
pub const gstrdup = c.gstrdup;
pub const gstrndup = c.gstrndup;
pub const gstrsep = c.gstrsep;
pub const gstrltrim = c.gstrltrim;
pub const gstrrtrim = c.gstrrtrim;
pub const gstrtrim = c.gstrtrim;
pub const gstrrev = c.gstrrev;
pub const gstrreplace = c.gstrreplace;
pub const gstrlower = c.gstrlower;
pub const gstrupper = c.gstrupper;
pub const gstrellipsis = c.gstrellipsis;
pub const gstrfill = c.gstrfill;
pub const gstrlpad = c.gstrlpad;
pub const gstrrpad = c.gstrrpad;
pub const gstrpad = c.gstrpad;
pub const gstrwtrunc = c.gstrwtrunc;
pub const gstrwlpad = c.gstrwlpad;
pub const gstrwrpad = c.gstrwrpad;
pub const gstrwpad = c.gstrwpad;
