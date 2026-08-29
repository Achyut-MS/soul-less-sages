# Quick Reference: Member 2 Sprint to Code Freeze

**Current Time:** ~Hour 36-40 (estimated)  
**Code Freeze:** Aug 31, 18:00 UTC  
**Time Remaining:** ~32-36 hours

---

## 🎯 Your ONE Job

Implement `html_to_md()` in `src-c/html_serializer.c` that converts HTML back to Markdown such that:
1. All tag types work (headings, lists, bold, italic, code, links, blockquotes)
2. Bidirectional round-trip succeeds: `HTML(MD(x)) ≈ HTML(MD(HTML(MD(x))))`
3. Memory is safe: 0 leaks, 0 crashes, 0 sanitizer errors

---

## ⏱️ Time Breakdown (32 Hours)

### Hours 0–4 (Next 4 Hours) — Setup
- [ ] Read both action plans (this file + detailed guide)
- [ ] Copy Phase 1 skeleton code into `html_serializer.c`
- [ ] Write 3-4 basic tests (headings, bold, paragraph)
- [ ] Get `make test` compiling and running

### Hours 4–10 (6 Hours) — Core Implementation
- [ ] Implement HTML parser (`parse_html_fragment()`)
- [ ] Implement tag handlers (h1-h6, strong, em, ul/ol, code, blockquote, p)
- [ ] Write 10+ unit tests
- [ ] Run `make test` — target: 10+ passing tests

### Hours 10–16 (6 Hours) — Integration & Fuzzing
- [ ] Run fuzzer: `make fuzz DURATION=60` (short 1-min run)
- [ ] Fix any failures (usually missing tag handlers or escaping)
- [ ] Add edge case tests (empty HTML, unicode, nested)
- [ ] Verify HTTP `/serialize` endpoint works with curl

### Hours 16–24 (8 Hours) — Memory Safety & Polish
- [ ] Build and test with ASan: `make asan && make test`
- [ ] Fix any memory errors (leaks, use-after-free)
- [ ] Run Valgrind: `valgrind ./test_html_serializer`
- [ ] Compile with strict flags: 0 warnings

### Hours 24–32 (8 Hours) — Final Validation
- [ ] Run full 5-min fuzzer: `make fuzz`
- [ ] Run full test suite: `make test`
- [ ] Update STDLIB.md with any new substitutions found
- [ ] Code review & cleanup
- [ ] Final commit and push

---

## 📝 Implementation Checklist

### Critical Path (Must Complete)

```
[ ] Phase 1: Skeleton (2h)
  [ ] Copy MdBuilder struct + functions into html_serializer.c
  [ ] Copy HtmlNode struct + parse_html_fragment() stub
  [ ] Copy walk_node() skeleton
  [ ] Verify compiles with `make clean && make`

[ ] Phase 2: HTML Parser (3h)
  [ ] Implement simple tag scanner (find < > pairs)
  [ ] Build DOM tree from HTML
  [ ] Handle text nodes
  [ ] Test: `<p>Hello</p>` parses correctly

[ ] Phase 3: Tag Handlers (4h)
  [ ] H1–H6 → #, ##, etc.
  [ ] <strong> → **text**
  [ ] <em> → *text*
  [ ] <ul><li> → - item (with indentation)
  [ ] <ol><li> → 1. item (renumber sequentially)
  [ ] <code> → `text`
  [ ] <pre><code> → ```\ncode\n```
  [ ] <blockquote> → > text
  [ ] <a href> → [text](url)
  [ ] <p> → text\n\n
  [ ] Unknown tags → strip, walk children

[ ] Phase 4: Testing (3h)
  [ ] Write 15+ unit tests
  [ ] Run `make test` — all pass
  [ ] Run fuzzer briefly — see cycles processed

[ ] Phase 5: Memory Safety (2h)
  [ ] ASan build: `make asan && make test` — 0 errors
  [ ] Valgrind: 0 leaks
  [ ] Compiler: 0 warnings

[ ] Phase 6: Final Push (2h)
  [ ] 5-min fuzzer: `make fuzz` — all pass
  [ ] Update STDLIB.md
  [ ] Final code review
  [ ] Git commit + push
```

### High Priority (Should Complete)

```
[ ] Escaping: escape *, _, `, [, ], # in text
[ ] Nested lists: proper indentation
[ ] Link URL extraction from href attribute
[ ] Blockquote nesting (up to 3 levels)
[ ] Error handling: malformed HTML → structured error (no crash)
```

### Nice-to-Have (If Time Permits)

```
[ ] Code coverage report (gcov/lcov)
[ ] Detailed STDLIB.md for this component
[ ] Comments in code
[ ] Performance optimization
```

---

## 📊 Current Status

| Component | Status | Done? |
|-----------|--------|-------|
| HTTP Server | ✅ Working | YES |
| MD Parser | ✅ Stubbed (returns dummy HTML) | PARTIAL |
| HTML Serializer | 🔴 Stubbed (returns dummy MD) | **NO** ← YOUR JOB |
| File Writer | ✅ Working | YES |
| CLI/Main | ✅ Working | YES |
| Frontend UI | ✅ Working | YES |
| Fuzzer | ✅ Implemented | YES |
| Tests Pass | ✅ (with stubs) | YES |

**You are unblocking:** `/serialize` endpoint, round-trip fuzzer, bidirectional sync

---

## 🔧 Key Commands

```bash
# Build
make clean && make

# Test
make test              # Run all unit tests
make fuzz              # Run 5-min fuzzer
make fuzz DURATION=60  # Run 1-min fuzzer (for quick testing)

# Memory Safety
make asan              # Build with AddressSanitizer
make test              # Run tests (will catch ASan errors)

# Specific Test
gcc -Wall -Wextra -Werror -std=c2x -O2 \
  tests/test_html_serializer.c src-c/html_serializer.c \
  -o test_html_serializer
./test_html_serializer

# Manual Testing
./mdview ./tests/fixtures/sample.md &  # Start server
curl -X POST -d '{"html":"<h1>Hi</h1>"}' http://localhost:8080/serialize
# Should return: {"md":"# Hi\n"}
```

---

## 🐛 Common Issues & Fixes

### Issue: Compilation error `undefined reference to 'walk_node'`
**Fix:** Make sure `walk_node()` is declared before `html_to_md()` uses it, or declare forward.

### Issue: Fuzzer reports `round-trip invariant violated`
**Fix:** Your serializer is producing different markdown than expected. Check:
- List renumbering (must always be 1. 2. 3. ...)
- Whitespace (extra newlines?)
- Escaping (missing escape of special chars?)

### Issue: ASan reports `heap-buffer-overflow`
**Fix:** Buffer overflow in string copying. Check:
- String length calculations
- strcpy → use strncpy or manual memcpy with size check
- Buffer sizes in MdBuilder

### Issue: Valgrind reports `LEAK_SUMMARY: ... definitely lost`
**Fix:** Memory leak. Check:
- All malloc'd strings/nodes are freed
- Every recursion path frees its allocations
- No circular references

---

## 💡 Pro Tips

1. **Test incrementally:** Don't wait until phase 3 to test. Write a test as soon as you implement each tag handler.

2. **Use printf debugging:** Don't need a debugger for this.
   ```c
   printf("DEBUG: Parsed tag='%s'\n", node->tag_name);
   ```

3. **Start simple:** Get `<p>`, `<strong>`, `<h1>` working first. Others are variations.

4. **Escaping matters:** After you get basic tags working, focus on escaping. This is where most round-trip bugs hide.

5. **Run fuzzer early:** Once basic tests pass, run fuzzer for 30 seconds. It will find edge cases fast.

6. **Save often:** You're racing the clock. Commit frequently to GitHub.
   ```bash
   git add -A
   git commit -m "Implement h1-h6 tag handlers + tests"
   git push
   ```

---

## 🚨 Red Flags (If You See These, Ask for Help)

- Fuzzer crashes with SIGSEGV → null pointer deref or buffer overflow
- Fuzzer reports invariant violations that don't make sense → parser/serializer disagreement on HTML format
- ASan reports are increasing (not decreasing) → something is leaking worse
- More than 3 tests failing → implementation strategy might be wrong; consider reset

---

## ✅ Definition of Done

Your work is done when:

1. [ ] `make test` passes all unit tests (no failures)
2. [ ] `make fuzz DURATION=300` completes with `Failures: 0`
3. [ ] `make asan && make test` runs with 0 AddressSanitizer errors
4. [ ] Manual curl test works: `POST /serialize` returns correct Markdown
5. [ ] Code compiles with 0 warnings: `gcc -Wall -Wextra -Werror ...`
6. [ ] Valgrind reports 0 definite leaks: `valgrind ./test_html_serializer`

---

## 🚀 Go! ☑️ START NOW

1. Open `src-c/html_serializer.c`
2. Scroll to line 20 (after the `#include` statements)
3. Copy **Phase 1 skeleton code** from `MEMBER_2_ACTION_PLAN.md`
4. Compile: `make clean && make`
5. Write first test
6. Run: `make test`
7. Debug & iterate

**You've got 32 hours. This is a 12-hour job. Plenty of buffer. No stress. Just code.**

🎯 **See you at the finish line!**

---

## 📚 Reference Documents

- **MEMBER_2_ACTION_PLAN.md** — Detailed 40-page implementation guide with full code
- **PROJECT_COMPLETION_GUIDE.md** — High-level phases and architecture
- **WORK_SPLIT.md** (in repo) — Original hackathon task breakdown
- **ARCHITECTURE.md** (in repo) — System design
- **Makefile** (in repo) — Build targets and test commands

---

## ⏰ Milestones (Self-Check Every 4 Hours)

**Hour 4:** ✅ Skeleton compiles, 3 tests written  
**Hour 8:** ✅ HTML parser works, 10 tests written  
**Hour 12:** ✅ All tag handlers done, 15 tests pass, fuzzer runs  
**Hour 16:** ✅ No ASan/Valgrind errors  
**Hour 20:** ✅ 5-min fuzzer passes  
**Hour 24:** ✅ Code review done  
**Hour 32:** ✅ **DONE. PUSH TO GITHUB.** 🎉

---

**Status: Ready to start? 👇**

```bash
cd ~/repo
git status
# Make sure you're on main/develop branch
# Start coding! 🚀
```

Good luck! ⭐
