// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Edward J Edmonds

package gstr

import (
	"strings"
	"testing"
)

// Test strings with various Unicode complexities
var (
	empty       = ""
	ascii       = "Hello"
	cjk         = "世界"
	mixed       = "Hello 世界"
	emoji       = "👋"
	zwj         = "👨‍👩‍👧" // Family emoji (7 codepoints, 1 grapheme)
	flag        = "🇺🇸"    // Flag (2 codepoints, 1 grapheme)
	skinTone    = "👋🏽"    // Wave with skin tone (2 codepoints, 1 grapheme)
	combining   = "e\u0301" // é as e + combining acute (2 codepoints, 1 grapheme)
	hangul      = "한글"
	arabic      = "مرحبا"
	devanagari  = "नमस्ते"
	multiEmoji  = "👨‍👩‍👧👋🎉"
	spaces      = "   hello world   "
	tabs        = "\t\thello\t\t"
	newlines    = "\n\nhello\n\n"
	mixedWS     = " \t\n hello \t\n "
)

func TestLen(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  int
	}{
		{"empty", empty, 0},
		{"ascii", ascii, 5},
		{"cjk", cjk, 2},
		{"mixed", mixed, 8},
		{"emoji", emoji, 1},
		{"zwj family", zwj, 1},
		{"flag", flag, 1},
		{"skin tone", skinTone, 1},
		{"combining", combining, 1},
		{"hangul", hangul, 2},
		{"arabic", arabic, 5},
		{"devanagari", devanagari, 3}, // Conjunct clusters
		{"multi emoji", multiEmoji, 3},
		{"spaces", spaces, 17},
		{"single char", "a", 1},
		{"single emoji", "🎉", 1},
		{"many zwj", "👨‍👩‍👧‍👦", 1}, // Family of 4
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Len(tt.input)
			if got != tt.want {
				t.Errorf("Len(%q) = %d, want %d", tt.input, got, tt.want)
			}
		})
	}
}

func TestNLen(t *testing.T) {
	tests := []struct {
		name  string
		input string
		max   int
		want  int
	}{
		{"empty", empty, 10, 0},
		{"ascii full", ascii, 10, 5},
		{"ascii limited", ascii, 3, 3},
		{"ascii exact", ascii, 5, 5},
		{"cjk limited", cjk, 1, 1},
		{"zwj limited", multiEmoji, 2, 2},
		{"zero max", ascii, 0, 0},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := NLen(tt.input, tt.max)
			if got != tt.want {
				t.Errorf("NLen(%q, %d) = %d, want %d", tt.input, tt.max, got, tt.want)
			}
		})
	}
}

func TestWidth(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  int
	}{
		{"empty", empty, 0},
		{"ascii", ascii, 5},
		{"cjk", cjk, 4},         // Each CJK = 2 columns
		{"mixed", mixed, 10},    // 5 + 1 + 4
		{"emoji", emoji, 2},     // Emoji = 2 columns
		{"zwj family", zwj, 2},  // ZWJ = 2 columns
		{"flag", flag, 2},       // Flag = 2 columns
		{"skin tone", skinTone, 2},
		{"combining", combining, 1}, // e + combining = 1 column
		{"multi emoji", multiEmoji, 6}, // 2 + 2 + 2
		{"single space", " ", 1},
		{"tab", "\t", 0}, // Control char
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Width(tt.input)
			if got != tt.want {
				t.Errorf("Width(%q) = %d, want %d", tt.input, got, tt.want)
			}
		})
	}
}

func TestAt(t *testing.T) {
	tests := []struct {
		name  string
		input string
		n     int
		want  string
	}{
		{"ascii first", ascii, 0, "H"},
		{"ascii last", ascii, 4, "o"},
		{"ascii oob", ascii, 5, ""},
		{"ascii negative", ascii, -1, ""},
		{"cjk first", cjk, 0, "世"},
		{"cjk second", cjk, 1, "界"},
		{"zwj first", multiEmoji, 0, zwj},
		{"zwj second", multiEmoji, 1, "👋"},
		{"empty", empty, 0, ""},
		{"combining", combining, 0, combining},
		{"flag", flag, 0, flag},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := At(tt.input, tt.n)
			if got != tt.want {
				t.Errorf("At(%q, %d) = %q, want %q", tt.input, tt.n, got, tt.want)
			}
		})
	}
}

func TestOffset(t *testing.T) {
	tests := []struct {
		name  string
		input string
		n     int
		want  int
	}{
		{"ascii 0", ascii, 0, 0},
		{"ascii 1", ascii, 1, 1},
		{"cjk 0", cjk, 0, 0},
		{"cjk 1", cjk, 1, 3}, // First CJK is 3 bytes
		{"emoji 1", multiEmoji, 1, 18}, // ZWJ family is 18 bytes
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Offset(tt.input, tt.n)
			if got != tt.want {
				t.Errorf("Offset(%q, %d) = %d, want %d", tt.input, tt.n, got, tt.want)
			}
		})
	}
}

func TestCompare(t *testing.T) {
	tests := []struct {
		name string
		a, b string
		want int // -1, 0, or 1
	}{
		{"equal ascii", "abc", "abc", 0},
		{"less ascii", "abc", "abd", -1},
		{"greater ascii", "abd", "abc", 1},
		{"empty equal", "", "", 0},
		{"empty less", "", "a", -1},
		{"empty greater", "a", "", 1},
		{"cjk equal", cjk, cjk, 0},
		{"emoji equal", zwj, zwj, 0},
		{"length diff", "ab", "abc", -1},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Compare(tt.a, tt.b)
			// Normalize to -1, 0, 1
			if got < 0 {
				got = -1
			} else if got > 0 {
				got = 1
			}
			if got != tt.want {
				t.Errorf("Compare(%q, %q) = %d, want %d", tt.a, tt.b, got, tt.want)
			}
		})
	}
}

func TestCompareN(t *testing.T) {
	tests := []struct {
		name string
		a, b string
		n    int
		want int
	}{
		{"equal prefix", "hello world", "hello there", 5, 0},
		{"diff after n", "hello world", "hello there", 7, 1}, // 'w' (0x77) > 't' (0x74)
		{"n=0", "abc", "xyz", 0, 0},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := CompareN(tt.a, tt.b, tt.n)
			if got < 0 {
				got = -1
			} else if got > 0 {
				got = 1
			}
			if got != tt.want {
				t.Errorf("CompareN(%q, %q, %d) = %d, want %d", tt.a, tt.b, tt.n, got, tt.want)
			}
		})
	}
}

func TestCaseCompare(t *testing.T) {
	tests := []struct {
		name string
		a, b string
		want int
	}{
		{"equal", "Hello", "HELLO", 0},
		{"equal lower", "hello", "HELLO", 0},
		{"diff", "Hello", "World", -1},
		{"non-ascii differs", "Héllo", "HÉLLO", 1}, // é != É (only ASCII case folded)
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := CaseCompare(tt.a, tt.b)
			if got < 0 {
				got = -1
			} else if got > 0 {
				got = 1
			}
			if got != tt.want {
				t.Errorf("CaseCompare(%q, %q) = %d, want %d", tt.a, tt.b, got, tt.want)
			}
		})
	}
}

func TestHasPrefix(t *testing.T) {
	tests := []struct {
		name   string
		s, pre string
		want   bool
	}{
		{"has", "Hello, World!", "Hello", true},
		{"not", "Hello", "World", false},
		{"empty prefix", "Hello", "", true},
		{"empty string", "", "Hello", false},
		{"both empty", "", "", true},
		{"exact", "Hello", "Hello", true},
		{"longer prefix", "Hi", "Hello", false},
		{"zwj prefix", "👨‍👩‍👧 family", "👨‍👩‍👧", true},
		{"cjk prefix", "世界你好", "世界", true},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := HasPrefix(tt.s, tt.pre)
			if got != tt.want {
				t.Errorf("HasPrefix(%q, %q) = %v, want %v", tt.s, tt.pre, got, tt.want)
			}
		})
	}
}

func TestHasSuffix(t *testing.T) {
	tests := []struct {
		name   string
		s, suf string
		want   bool
	}{
		{"has", "Hello, World!", "World!", true},
		{"not", "Hello", "World", false},
		{"empty suffix", "Hello", "", true},
		{"empty string", "", "Hello", false},
		{"both empty", "", "", true},
		{"exact", "Hello", "Hello", true},
		{"longer suffix", "Hi", "Hello", false},
		{"emoji suffix", "Say 👋", "👋", true},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := HasSuffix(tt.s, tt.suf)
			if got != tt.want {
				t.Errorf("HasSuffix(%q, %q) = %v, want %v", tt.s, tt.suf, got, tt.want)
			}
		})
	}
}

func TestIndex(t *testing.T) {
	tests := []struct {
		name    string
		s, g    string
		want    int
	}{
		{"found", "hello", "l", 2},
		{"not found", "hello", "x", -1},
		{"empty needle", "hello", "", -1},
		{"empty haystack", "", "a", -1},
		{"emoji", "Hi 👋 there", "👋", 3},
		{"cjk", "Hello世界", "世", 5},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Index(tt.s, tt.g)
			if got != tt.want {
				t.Errorf("Index(%q, %q) = %d, want %d", tt.s, tt.g, got, tt.want)
			}
		})
	}
}

func TestLastIndex(t *testing.T) {
	tests := []struct {
		name string
		s, g string
		want int
	}{
		{"found", "hello", "l", 3},
		{"not found", "hello", "x", -1},
		{"single", "hello", "h", 0},
		{"emoji multiple", "👋hi👋", "👋", 6}, // Second wave at byte 6
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := LastIndex(tt.s, tt.g)
			if got != tt.want {
				t.Errorf("LastIndex(%q, %q) = %d, want %d", tt.s, tt.g, got, tt.want)
			}
		})
	}
}

func TestContains(t *testing.T) {
	tests := []struct {
		name   string
		s, sub string
		want   bool
	}{
		{"has", "Hello, World!", "World", true},
		{"not", "Hello", "World", false},
		{"empty needle", "Hello", "", true},
		{"empty haystack", "", "a", false},
		{"zwj", "Meet the 👨‍👩‍👧!", zwj, true},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Contains(tt.s, tt.sub)
			if got != tt.want {
				t.Errorf("Contains(%q, %q) = %v, want %v", tt.s, tt.sub, got, tt.want)
			}
		})
	}
}

func TestIndexSubstring(t *testing.T) {
	tests := []struct {
		name   string
		s, sub string
		want   int
	}{
		{"found", "Hello, World!", "World", 7},
		{"not found", "Hello", "xyz", -1},
		{"empty needle", "Hello", "", 0},
		{"at start", "Hello", "He", 0},
		{"at end", "Hello", "lo", 3},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := IndexSubstring(tt.s, tt.sub)
			if got != tt.want {
				t.Errorf("IndexSubstring(%q, %q) = %d, want %d", tt.s, tt.sub, got, tt.want)
			}
		})
	}
}

func TestIndexFold(t *testing.T) {
	tests := []struct {
		name   string
		s, sub string
		want   int
	}{
		{"found upper", "Hello, World!", "WORLD", 7},
		{"found lower", "HELLO", "hello", 0},
		{"not found", "Hello", "xyz", -1},
		{"empty needle", "Hello", "", 0},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := IndexFold(tt.s, tt.sub)
			if got != tt.want {
				t.Errorf("IndexFold(%q, %q) = %d, want %d", tt.s, tt.sub, got, tt.want)
			}
		})
	}
}

func TestCount(t *testing.T) {
	tests := []struct {
		name   string
		s, sub string
		want   int
	}{
		{"single char", "banana", "a", 3},
		{"non-overlapping", "aaa", "aa", 1},
		{"none", "hello", "x", 0},
		{"emoji", "👋👋👋", "👋", 3},
		{"empty needle", "abc", "", 4}, // len + 1
		{"empty string", "", "a", 0},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Count(tt.s, tt.sub)
			if got != tt.want {
				t.Errorf("Count(%q, %q) = %d, want %d", tt.s, tt.sub, got, tt.want)
			}
		})
	}
}

func TestSub(t *testing.T) {
	tests := []struct {
		name       string
		s          string
		start, cnt int
		want       string
	}{
		{"ascii mid", "Hello", 1, 3, "ell"},
		{"ascii start", "Hello", 0, 2, "He"},
		{"ascii end", "Hello", 3, 10, "lo"},
		{"cjk", "世界你好", 1, 2, "界你"},
		{"emoji", "👨‍👩‍👧👋🎉", 1, 1, "👋"},
		{"empty", "", 0, 5, ""},
		{"zero count", "Hello", 0, 0, ""},
		{"oob start", "Hello", 10, 2, ""},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Sub(tt.s, tt.start, tt.cnt)
			if got != tt.want {
				t.Errorf("Sub(%q, %d, %d) = %q, want %q", tt.s, tt.start, tt.cnt, got, tt.want)
			}
		})
	}
}

func TestReverse(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  string
	}{
		{"ascii", "Hello", "olleH"},
		{"cjk", "世界", "界世"},
		{"emoji", "👨‍👩‍👧👋", "👋👨‍👩‍👧"},
		{"empty", "", ""},
		{"single", "a", "a"},
		{"single emoji", "🎉", "🎉"},
		{"palindrome", "aba", "aba"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Reverse(tt.input)
			if got != tt.want {
				t.Errorf("Reverse(%q) = %q, want %q", tt.input, got, tt.want)
			}
		})
	}
}

func TestReplace(t *testing.T) {
	tests := []struct {
		name             string
		s, old, new_str  string
		want             string
	}{
		{"single", "Hello, World!", "World", "Go", "Hello, Go!"},
		{"multiple", "aaa", "a", "b", "bbb"},
		{"emoji", "👋🌍", "🌍", "🌎", "👋🌎"},
		{"not found", "hello", "x", "y", "hello"},
		{"empty old", "hello", "", "x", "hello"},
		{"empty new", "hello", "l", "", "heo"},
		{"empty string", "", "a", "b", ""},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Replace(tt.s, tt.old, tt.new_str)
			if got != tt.want {
				t.Errorf("Replace(%q, %q, %q) = %q, want %q", tt.s, tt.old, tt.new_str, got, tt.want)
			}
		})
	}
}

func TestToLower(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  string
	}{
		{"upper", "HELLO", "hello"},
		{"mixed", "HeLLo", "hello"},
		{"already lower", "hello", "hello"},
		{"with numbers", "HELLO123", "hello123"},
		{"empty", "", ""},
		{"non-ascii", "HELLO世界", "hello世界"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := ToLower(tt.input)
			if got != tt.want {
				t.Errorf("ToLower(%q) = %q, want %q", tt.input, got, tt.want)
			}
		})
	}
}

func TestToUpper(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  string
	}{
		{"lower", "hello", "HELLO"},
		{"mixed", "HeLLo", "HELLO"},
		{"already upper", "HELLO", "HELLO"},
		{"with numbers", "hello123", "HELLO123"},
		{"empty", "", ""},
		{"non-ascii", "hello世界", "HELLO世界"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := ToUpper(tt.input)
			if got != tt.want {
				t.Errorf("ToUpper(%q) = %q, want %q", tt.input, got, tt.want)
			}
		})
	}
}

func TestTrimLeft(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  string
	}{
		{"spaces", "   hello", "hello"},
		{"tabs", "\t\thello", "hello"},
		{"mixed", " \t\nhello", "hello"},
		{"no ws", "hello", "hello"},
		{"empty", "", ""},
		{"all ws", "   ", ""},
		{"trailing", "hello   ", "hello   "},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := TrimLeft(tt.input)
			if got != tt.want {
				t.Errorf("TrimLeft(%q) = %q, want %q", tt.input, got, tt.want)
			}
		})
	}
}

func TestTrimRight(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  string
	}{
		{"spaces", "hello   ", "hello"},
		{"tabs", "hello\t\t", "hello"},
		{"mixed", "hello \t\n", "hello"},
		{"no ws", "hello", "hello"},
		{"empty", "", ""},
		{"all ws", "   ", ""},
		{"leading", "   hello", "   hello"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := TrimRight(tt.input)
			if got != tt.want {
				t.Errorf("TrimRight(%q) = %q, want %q", tt.input, got, tt.want)
			}
		})
	}
}

func TestTrim(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  string
	}{
		{"both", "  hello  ", "hello"},
		{"left only", "  hello", "hello"},
		{"right only", "hello  ", "hello"},
		{"mixed ws", " \t\nhello \t\n", "hello"},
		{"no ws", "hello", "hello"},
		{"empty", "", ""},
		{"all ws", "   ", ""},
		{"internal ws", "hello world", "hello world"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Trim(tt.input)
			if got != tt.want {
				t.Errorf("Trim(%q) = %q, want %q", tt.input, got, tt.want)
			}
		})
	}
}

func TestEllipsis(t *testing.T) {
	tests := []struct {
		name   string
		s      string
		max    int
		suffix string
		want   string
	}{
		{"truncated", "Hello, World!", 8, "...", "Hello..."},
		{"not truncated", "Hi", 10, "...", "Hi"},
		{"exact", "Hello", 5, "...", "Hello"},
		{"cjk", "世界你好", 3, "…", "世界…"},
		{"empty", "", 5, "...", ""},
		{"zero max", "Hello", 0, "...", ""},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Ellipsis(tt.s, tt.max, tt.suffix)
			if got != tt.want {
				t.Errorf("Ellipsis(%q, %d, %q) = %q, want %q", tt.s, tt.max, tt.suffix, got, tt.want)
			}
		})
	}
}

func TestPadLeft(t *testing.T) {
	tests := []struct {
		name  string
		s     string
		width int
		pad   string
		want  string
	}{
		{"space pad", "Hi", 5, " ", "   Hi"},
		{"zero pad", "42", 5, "0", "00042"},
		{"truncates", "Hello", 3, " ", "Hel"}, // width < string truncates
		{"cjk", "世界", 6, " ", "  世界"}, // 4 cols + 2 spaces = 6
		{"empty pad", "Hi", 5, "", "   Hi"}, // defaults to space
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := PadLeft(tt.s, tt.width, tt.pad)
			if got != tt.want {
				t.Errorf("PadLeft(%q, %d, %q) = %q, want %q", tt.s, tt.width, tt.pad, got, tt.want)
			}
		})
	}
}

func TestPadRight(t *testing.T) {
	tests := []struct {
		name  string
		s     string
		width int
		pad   string
		want  string
	}{
		{"space pad", "Hi", 5, " ", "Hi   "},
		{"dot pad", "Hi", 5, ".", "Hi..."},
		{"truncates", "Hello", 3, " ", "Hel"}, // width < string truncates
		{"cjk", "世界", 6, " ", "世界  "}, // 4 cols + 2 spaces = 6
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := PadRight(tt.s, tt.width, tt.pad)
			if got != tt.want {
				t.Errorf("PadRight(%q, %d, %q) = %q, want %q", tt.s, tt.width, tt.pad, got, tt.want)
			}
		})
	}
}

func TestPadCenter(t *testing.T) {
	tests := []struct {
		name  string
		s     string
		width int
		pad   string
		want  string
	}{
		{"even", "Hi", 6, " ", "  Hi  "},
		{"odd", "Hi", 5, " ", " Hi  "},
		{"truncates", "Hello", 3, " ", "Hel"}, // width < string truncates
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := PadCenter(tt.s, tt.width, tt.pad)
			if got != tt.want {
				t.Errorf("PadCenter(%q, %d, %q) = %q, want %q", tt.s, tt.width, tt.pad, got, tt.want)
			}
		})
	}
}

func TestTruncate(t *testing.T) {
	tests := []struct {
		name     string
		s        string
		maxWidth int
		want     string
	}{
		{"ascii", "Hello", 3, "Hel"},
		{"no trunc", "Hi", 5, "Hi"},
		{"cjk partial", "世界", 3, "世"}, // Can't fit 界 (needs 2 cols)
		{"cjk exact", "世界", 4, "世界"},
		{"empty", "", 5, ""},
		{"zero width", "Hello", 0, ""},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Truncate(tt.s, tt.maxWidth)
			if got != tt.want {
				t.Errorf("Truncate(%q, %d) = %q, want %q", tt.s, tt.maxWidth, got, tt.want)
			}
		})
	}
}

func TestValid(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  bool
	}{
		{"empty", "", true},
		{"ascii", "Hello", true},
		{"utf8", "Hello, 世界! 👋", true},
		{"zwj", zwj, true},
		// Invalid UTF-8 - hard to construct in Go string literals
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Valid(tt.input)
			if got != tt.want {
				t.Errorf("Valid(%q) = %v, want %v", tt.input, got, tt.want)
			}
		})
	}
}

func TestValidIndex(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  int
	}{
		{"valid", "Hello", -1},
		{"empty", "", -1},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := ValidIndex(tt.input)
			if got != tt.want {
				t.Errorf("ValidIndex(%q) = %d, want %d", tt.input, got, tt.want)
			}
		})
	}
}

func TestGraphemeIterator(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  []string
	}{
		{"ascii", "abc", []string{"a", "b", "c"}},
		{"emoji", "Hi👨‍👩‍👧!", []string{"H", "i", "👨‍👩‍👧", "!"}},
		{"cjk", "世界", []string{"世", "界"}},
		{"empty", "", nil},
		{"flag", "🇺🇸", []string{"🇺🇸"}},
		{"combining", combining, []string{combining}},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			iter := Graphemes(tt.input)
			var got []string
			for g := iter.Next(); g != ""; g = iter.Next() {
				got = append(got, g)
			}

			if len(got) != len(tt.want) {
				t.Errorf("Graphemes(%q) got %d items %v, want %d items %v",
					tt.input, len(got), got, len(tt.want), tt.want)
				return
			}

			for i := range tt.want {
				if got[i] != tt.want[i] {
					t.Errorf("Graphemes(%q)[%d] = %q, want %q", tt.input, i, got[i], tt.want[i])
				}
			}
		})
	}
}

func TestGraphemeIteratorReset(t *testing.T) {
	iter := Graphemes("abc")
	
	// Consume all
	for iter.Next() != "" {}
	
	// Reset and verify
	iter.Reset()
	if g := iter.Next(); g != "a" {
		t.Errorf("After Reset(), Next() = %q, want %q", g, "a")
	}
}

// Benchmark tests
func BenchmarkLen(b *testing.B) {
	s := strings.Repeat("Hello 世界 👋 ", 100)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		Len(s)
	}
}

func BenchmarkWidth(b *testing.B) {
	s := strings.Repeat("Hello 世界 👋 ", 100)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		Width(s)
	}
}
