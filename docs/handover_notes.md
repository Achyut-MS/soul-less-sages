# Handover Notes — Round-Trip Fuzzer (current state: ~2 failing classes)

## make test: PASSING (50/50)
All unit + integration tests pass cleanly.

## make fuzz: FAILING — 2 remaining failure classes

---

### Failure Class 1: Trailing space before `<br>` stripped by md_parser.c

**Example:**
- Input MD: `#<byte> Heading \<newline>text` 
- Pass 1 HTML: `<p>#<byte> Heading <br />text</p>` (space before `<br>`)
- Pass 2 MD: `#<byte> Heading  \ntext\n\n` (two-space hard break emitted correctly)
- Pass 3 HTML: `<p>#<byte> Heading<br />text</p>` — **space gone!**

**Root Cause:** `md_parser.c` normalizes away trailing spaces before a hard line break (CommonMark spec §6.7). So `Heading  \n` → `<p>Heading<br />` without the trailing space. This is correct [...]

**Fix needed in `md_parser.c`:** When emitting text before a `<br />`, preserve the trailing space in the HTML output. OR: when `html_serializer.c` detects that the text node is followed by `<br>`[...]

**Simplest fix:** In `html_serializer.c`'s `<br>` handler (line ~1070), when `<br>` is the first child of a `<p>` (i.e., `out->len == 0` or previous char is `\n`), emit `\\\n`. Otherwise emit `  \[...]

---

### Failure Class 2: Raw inline HTML attributes with space but no value get `=""` added

**Example:**
- Input MD: `Plain text with \nin<ine >ode\`` here.`
- Pass 1 HTML: `<p>...in<ine >ode`...</p>` (space after `ine`, no attribute value)
- Pass 2 HTML: `<p>...in<ine="">ode`...</p>` — adds `=""` to the space

**Root Cause:** In `html_serializer.c`'s HTML fragment parser, when parsing `<ine >`:
- It reads tag = `ine`  
- Then sees ` ` (space), scans attr name...
- But actually the issue is that `<ine >` is being parsed as `<ine` with an attribute name being empty or `>` is being consumed wrong.

Wait — looking at the hex:
- Pass 1: `3C 69 6E 65 20 3E` = `<ine >`  
- Pass 2: `3C 69 6E 65 3E` = `<ine>` (space dropped)

Actually the space IS being dropped (not `=""`). The mismatch is that `<ine >` vs `<ine>` differs.

**Actual root cause:** When `html_to_md` serializes `<ine >` (tag with trailing space but no attrs), the raw_html_fallback emits `<ine>` without the trailing space. The original HTML had `<ine >` [...]

**Fix:** In the raw_html_fallback serialization code (`html_serializer.c` ~line 1156), if a tag has no attributes, still emit the raw tag string as-is from `raw_tag`. But `raw_tag` only stores the[...]

Actually the right fix is simpler: the HTML parser should be treating `<ine >` exactly like `<ine>` since a trailing space inside a tag is just whitespace. And the re-serialized `<ine>` is valid e[...]

Check: does `md_parser.c` handle `<ine >` vs `<ine>` identically? The answer is likely yes for unknown tags. So this may not be a real semantic difference — just a byte-level mismatch.

**Fix needed:** Either normalize `<tag >` to `<tag>` in Pass 1 output, OR preserve the trailing space in the serializer.

---

## Files Changed in This Session

- `src-c/html_serializer.c` — many changes, see git log
- `tests/test_html_serializer.c` — updated `test_html_entities` expected value to `\<tag>`

## Current git state

Commit `96c5e36` on `main`. Additional uncommitted changes:
- `html_serializer.c`: attr null fix, `#`/`>` over-escaping fix, trailing-space-before-br strip, br context detection

## What to do next

1. `git add src-c/html_serializer.c && git commit && git push`
2. Fix the two failure classes above in `md_parser.c` and/or `html_serializer.c`
3. Run `make DURATION=300 fuzz` to verify clean
