// SPDX-License-Identifier: MIT
// Test program for gstr-zig bindings

const std = @import("std");
const gstr = @import("gstr.zig");

var passed: usize = 0;
var failed: usize = 0;

fn check(name: []const u8, condition: bool) void {
    if (condition) {
        std.debug.print("✓ {s}\n", .{name});
        passed += 1;
    } else {
        std.debug.print("✗ {s} FAILED\n", .{name});
        failed += 1;
    }
}

fn eql(a: []const u8, b: []const u8) bool {
    return std.mem.eql(u8, a, b);
}

pub fn main() !void {
    const allocator = std.heap.page_allocator;

    std.debug.print("=== gstr-zig Test Program ===\n\n", .{});

    // --- Length Tests ---
    std.debug.print("--- Length Tests ---\n", .{});
    check("Len ASCII", gstr.len("Hello") == 5);
    check("Len CJK", gstr.len("世界") == 2);
    check("Len emoji", gstr.len("👋") == 1);
    check("Len ZWJ family", gstr.len("👨‍👩‍👧") == 1);
    check("Len flag", gstr.len("🇨🇦") == 1);
    check("Len mixed", gstr.len("Hello 世界 👋") == 10);
    check("Len empty", gstr.len("") == 0);
    std.debug.print("\n", .{});

    // --- Width Tests ---
    std.debug.print("--- Width Tests ---\n", .{});
    check("Width ASCII", gstr.width("Hello") == 5);
    check("Width CJK", gstr.width("世界") == 4);
    check("Width emoji", gstr.width("👋") == 2);
    check("Width ZWJ", gstr.width("👨‍👩‍👧") == 2);
    check("Width flag", gstr.width("🇨🇦") == 2);
    check("Width mixed", gstr.width("Hi世界") == 6);
    std.debug.print("\n", .{});

    // --- Indexing Tests ---
    std.debug.print("--- Indexing Tests ---\n", .{});
    if (gstr.at("Hello", 0)) |g| {
        check("At first", eql(g, "H"));
    } else {
        check("At first", false);
    }
    if (gstr.at("Hello", 4)) |g| {
        check("At last", eql(g, "o"));
    } else {
        check("At last", false);
    }
    check("At out of bounds", gstr.at("Hello", 5) == null);
    if (gstr.at("世界", 1)) |g| {
        check("At CJK", eql(g, "界"));
    } else {
        check("At CJK", false);
    }
    if (gstr.at("👨‍👩‍👧x", 0)) |g| {
        check("At ZWJ", g.len == 18);
    } else {
        check("At ZWJ", false);
    }
    std.debug.print("\n", .{});

    // --- Comparison Tests ---
    std.debug.print("--- Comparison Tests ---\n", .{});
    check("Compare equal", gstr.cmp("hello", "hello") == .equal);
    check("Compare less", gstr.cmp("abc", "abd") == .less);
    check("Compare greater", gstr.cmp("abd", "abc") == .greater);
    check("CompareN prefix", gstr.ncmp("Hello World", "Hello There", 5) == .equal);
    check("CaseCompare", gstr.caseCmp("HELLO", "hello") == .equal);
    std.debug.print("\n", .{});

    // --- Search Tests ---
    std.debug.print("--- Search Tests ---\n", .{});
    check("Chr found", gstr.chr("Hello", "l") == 2);
    check("Chr not found", gstr.chr("Hello", "x") == null);
    check("RChr", gstr.rchr("Hello", "l") == 3);
    check("Str found", gstr.str("Hello World", "World") == 6);
    check("Str not found", gstr.str("Hello", "xyz") == null);
    check("CaseStr", gstr.caseStr("Hello World", "WORLD") == 6);
    check("Count", gstr.count("abcabc", "abc") == 2);
    std.debug.print("\n", .{});

    // --- Prefix/Suffix Tests ---
    std.debug.print("--- Prefix/Suffix Tests ---\n", .{});
    check("StartsWith true", gstr.startsWith("Hello World", "Hello"));
    check("StartsWith false", !gstr.startsWith("Hello World", "World"));
    check("EndsWith true", gstr.endsWith("Hello World", "World"));
    check("EndsWith false", !gstr.endsWith("Hello World", "Hello"));
    std.debug.print("\n", .{});

    // --- Span Tests ---
    std.debug.print("--- Span Tests ---\n", .{});
    check("Span", gstr.span("aaabbb", "a") == 3);
    check("CSpan", gstr.cspan("aaabbb", "b") == 3);
    check("PBrk found", gstr.pbrk("hello", "aeiou") == 1);
    check("PBrk not found", gstr.pbrk("xyz", "aeiou") == null);
    std.debug.print("\n", .{});

    // --- Ellipsis Tests ---
    std.debug.print("--- Ellipsis Tests ---\n", .{});
    {
        const result = try gstr.ellipsis(allocator, "Hello World", 8, "...");
        defer allocator.free(result);
        check("Ellipsis truncate", eql(result, "Hello..."));
    }
    {
        const result = try gstr.ellipsis(allocator, "Hi", 10, "...");
        defer allocator.free(result);
        check("Ellipsis no truncate", eql(result, "Hi"));
    }
    {
        const result = try gstr.ellipsis(allocator, "世界Hello", 5, "...");
        defer allocator.free(result);
        check("Ellipsis unicode", eql(result, "世界..."));
    }
    std.debug.print("\n", .{});

    // --- Padding Tests ---
    std.debug.print("--- Padding Tests ---\n", .{});
    {
        const result = try gstr.lpad(allocator, "Hi", 5, " ");
        defer allocator.free(result);
        check("LPad", eql(result, "   Hi"));
    }
    {
        const result = try gstr.rpad(allocator, "Hi", 5, " ");
        defer allocator.free(result);
        check("RPad", eql(result, "Hi   "));
    }
    {
        const result = try gstr.pad(allocator, "Hi", 6, " ");
        defer allocator.free(result);
        check("Pad center", eql(result, "  Hi  "));
    }
    {
        const result = try gstr.wlpad(allocator, "世", 5, " ");
        defer allocator.free(result);
        check("WLPad", eql(result, "   世"));
    }
    {
        const result = try gstr.wrpad(allocator, "世", 5, " ");
        defer allocator.free(result);
        check("WRPad", eql(result, "世   "));
    }
    std.debug.print("\n", .{});

    // --- UTF-8 Tests ---
    std.debug.print("--- UTF-8 Tests ---\n", .{});
    {
        const dec = gstr.decode("A");
        check("Decode ASCII", dec.codepoint == 'A' and dec.len == 1);
    }
    {
        const dec = gstr.decode("世");
        check("Decode CJK", dec.codepoint == 0x4E16 and dec.len == 3);
    }
    {
        const enc = gstr.encode(0x4E16);
        check("Encode CJK", eql(enc.bytes[0..enc.len], "世"));
    }
    check("Valid UTF-8", gstr.valid("Hello 世界 👨‍👩‍👧").valid);
    {
        const invalid = [_]u8{ 0xFF, 0xFE };
        check("Invalid UTF-8", !gstr.valid(&invalid).valid);
    }
    std.debug.print("\n", .{});

    // --- Iterator Tests ---
    std.debug.print("--- Iterator Tests ---\n", .{});
    {
        var iter = gstr.graphemes("Hi世");
        var count: usize = 0;
        var first: []const u8 = "";
        var second: []const u8 = "";
        var third: []const u8 = "";
        while (iter.next()) |g| {
            if (count == 0) first = g;
            if (count == 1) second = g;
            if (count == 2) third = g;
            count += 1;
        }
        check("Iterator count", count == 3);
        check("Iterator values", eql(first, "H") and eql(second, "i") and eql(third, "世"));
    }
    std.debug.print("\n", .{});

    // --- Reverse Tests ---
    std.debug.print("--- Reverse Tests ---\n", .{});
    {
        const result = try gstr.reverse(allocator, "Hello");
        defer allocator.free(result);
        check("Reverse ASCII", eql(result, "olleH"));
    }
    {
        const result = try gstr.reverse(allocator, "世界");
        defer allocator.free(result);
        check("Reverse CJK", eql(result, "界世"));
    }
    std.debug.print("\n", .{});

    // --- Replace Tests ---
    std.debug.print("--- Replace Tests ---\n", .{});
    {
        const result = try gstr.replace(allocator, "Hello World", "World", "Zig");
        defer allocator.free(result);
        check("Replace", eql(result, "Hello Zig"));
    }
    {
        const result = try gstr.replace(allocator, "abcabc", "abc", "x");
        defer allocator.free(result);
        check("Replace multiple", eql(result, "xx"));
    }
    std.debug.print("\n", .{});

    // --- Summary ---
    std.debug.print("=== Summary ===\n", .{});
    std.debug.print("Passed: {d}\n", .{passed});
    std.debug.print("Failed: {d}\n", .{failed});

    if (failed > 0) {
        std.debug.print("\nSome tests failed!\n", .{});
        std.process.exit(1);
    }
    std.debug.print("\nAll tests passed!\n", .{});
}
