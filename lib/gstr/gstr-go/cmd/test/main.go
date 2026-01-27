// SPDX-License-Identifier: MIT
// Test program for gstr-go bindings

package main

import (
	"fmt"
	"os"

	"github.com/edwardedmonds/gstr-go"
)

func main() {
	fmt.Println("=== gstr-go Test Program ===")
	fmt.Println()

	passed := 0
	failed := 0

	// Test helper
	check := func(name string, condition bool) {
		if condition {
			fmt.Printf("✓ %s\n", name)
			passed++
		} else {
			fmt.Printf("✗ %s FAILED\n", name)
			failed++
		}
	}

	// --- Length Tests ---
	fmt.Println("--- Length Tests ---")
	check("Len ASCII", gstr.Len("Hello") == 5)
	check("Len CJK", gstr.Len("世界") == 2)
	check("Len emoji", gstr.Len("👋") == 1)
	check("Len ZWJ family", gstr.Len("👨‍👩‍👧") == 1)
	check("Len flag", gstr.Len("🇨🇦") == 1)
	check("Len mixed", gstr.Len("Hello 世界 👋") == 10)
	check("Len empty", gstr.Len("") == 0)
	fmt.Println()

	// --- Width Tests ---
	fmt.Println("--- Width Tests ---")
	check("Width ASCII", gstr.Width("Hello") == 5)
	check("Width CJK", gstr.Width("世界") == 4)
	check("Width emoji", gstr.Width("👋") == 2)
	check("Width ZWJ", gstr.Width("👨‍👩‍👧") == 2)
	check("Width flag", gstr.Width("🇨🇦") == 2)
	check("Width mixed", gstr.Width("Hi世界") == 6)
	fmt.Println()

	// --- Indexing Tests ---
	fmt.Println("--- Indexing Tests ---")
	check("At first", gstr.At("Hello", 0) == "H")
	check("At last", gstr.At("Hello", 4) == "o")
	check("At out of bounds", gstr.At("Hello", 5) == "")
	check("At CJK", gstr.At("世界", 1) == "界")
	g := gstr.At("👨‍👩‍👧x", 0)
	check("At ZWJ", len(g) == 18)
	fmt.Println()

	// --- Comparison Tests ---
	fmt.Println("--- Comparison Tests ---")
	check("Compare equal", gstr.Compare("hello", "hello") == 0)
	check("Compare less", gstr.Compare("abc", "abd") < 0)
	check("Compare greater", gstr.Compare("abd", "abc") > 0)
	check("CompareN prefix", gstr.CompareN("Hello World", "Hello There", 5) == 0)
	check("CaseCompare", gstr.CaseCompare("HELLO", "hello") == 0)
	fmt.Println()

	// --- Search Tests ---
	fmt.Println("--- Search Tests ---")
	check("Index found", gstr.Index("Hello", "l") == 2)
	check("Index not found", gstr.Index("Hello", "x") == -1)
	check("LastIndex", gstr.LastIndex("Hello", "l") == 3)
	check("IndexSubstring found", gstr.IndexSubstring("Hello World", "World") == 6)
	check("IndexSubstring not found", gstr.IndexSubstring("Hello", "xyz") == -1)
	check("IndexFold", gstr.IndexFold("Hello World", "WORLD") == 6)
	check("Count", gstr.Count("abcabc", "abc") == 2)
	check("Contains true", gstr.Contains("Hello World", "World"))
	check("Contains false", !gstr.Contains("Hello", "xyz"))
	fmt.Println()

	// --- Prefix/Suffix Tests ---
	fmt.Println("--- Prefix/Suffix Tests ---")
	check("HasPrefix true", gstr.HasPrefix("Hello World", "Hello"))
	check("HasPrefix false", !gstr.HasPrefix("Hello World", "World"))
	check("HasSuffix true", gstr.HasSuffix("Hello World", "World"))
	check("HasSuffix false", !gstr.HasSuffix("Hello World", "Hello"))
	fmt.Println()

	// --- Ellipsis Tests ---
	fmt.Println("--- Ellipsis Tests ---")
	check("Ellipsis truncate", gstr.Ellipsis("Hello World", 8, "...") == "Hello...")
	check("Ellipsis no truncate", gstr.Ellipsis("Hi", 10, "...") == "Hi")
	check("Ellipsis unicode", gstr.Ellipsis("世界Hello", 5, "...") == "世界...")
	fmt.Println()

	// --- Padding Tests ---
	fmt.Println("--- Padding Tests ---")
	check("PadLeft", gstr.PadLeft("Hi", 5, " ") == "   Hi")
	check("PadRight", gstr.PadRight("Hi", 5, " ") == "Hi   ")
	check("PadCenter", gstr.PadCenter("Hi", 6, " ") == "  Hi  ")
	fmt.Println()

	// --- Transform Tests ---
	fmt.Println("--- Transform Tests ---")
	check("Reverse", gstr.Reverse("Hello") == "olleH")
	check("Reverse CJK", gstr.Reverse("世界") == "界世")
	check("ToLower", gstr.ToLower("HELLO") == "hello")
	check("ToUpper", gstr.ToUpper("hello") == "HELLO")
	check("Replace", gstr.Replace("Hello World", "World", "Go") == "Hello Go")
	fmt.Println()

	// --- Trim Tests ---
	fmt.Println("--- Trim Tests ---")
	check("Trim", gstr.Trim("  hello  ") == "hello")
	check("TrimLeft", gstr.TrimLeft("  hello") == "hello")
	check("TrimRight", gstr.TrimRight("hello  ") == "hello")
	fmt.Println()

	// --- Sub Tests ---
	fmt.Println("--- Sub Tests ---")
	check("Sub", gstr.Sub("Hello World", 6, 5) == "World")
	check("Sub CJK", gstr.Sub("Hello世界", 5, 2) == "世界")
	fmt.Println()

	// --- UTF-8 Validation Tests ---
	fmt.Println("--- UTF-8 Validation Tests ---")
	check("Valid UTF-8", gstr.Valid("Hello 世界 👨‍👩‍👧"))
	check("Invalid UTF-8", !gstr.Valid(string([]byte{0xFF, 0xFE})))
	check("ValidIndex valid", gstr.ValidIndex("Hello") == -1)
	check("ValidIndex invalid", gstr.ValidIndex(string([]byte{0xFF, 0xFE})) == 0)
	fmt.Println()

	// --- Iterator Tests ---
	fmt.Println("--- Iterator Tests ---")
	iter := gstr.Graphemes("Hi世")
	gs := []string{}
	for {
		g := iter.Next()
		if g == "" {
			break
		}
		gs = append(gs, g)
	}
	check("Iterator count", len(gs) == 3)
	check("Iterator values", gs[0] == "H" && gs[1] == "i" && gs[2] == "世")

	// Test reset
	iter.Reset()
	first := iter.Next()
	check("Iterator reset", first == "H")
	fmt.Println()

	// --- Summary ---
	fmt.Println("=== Summary ===")
	fmt.Printf("Passed: %d\n", passed)
	fmt.Printf("Failed: %d\n", failed)

	if failed > 0 {
		os.Exit(1)
	}
	fmt.Println("\nAll tests passed!")
}
