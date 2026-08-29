# Member 2 Action Plan — Real-Time Status & Next Steps
**Zero-Dep Markdown Viewer · SoulessSages**  
**Member 2: Serializer & Correctness Lead**

**Current Status:** Hour ~36-40 (estimated)  
**Code Freeze:** Aug 31, 18:00 UTC

---

## 📊 Current Project Status Summary

### ✅ What's Already Done (Members 1 & 3)

| Component | Member | Status | Lines | Notes |
|-----------|--------|--------|-------|-------|
| **Parser** | Member 1 | ✅ Stubbed | 52 | `md_parser.c` returns dummy HTML; interface frozen & working |
| **Tokenizer** | Member 1 | ✅ Stubbed | 51 | `tokenizer.c` basic scaffold; not used yet |
| **Error Reporting** | Member 1 | ✅ PARTIAL | 147 | `error_report.c` has caret formatter skeleton |
| **HTTP Server** | Member 3 | ✅ IMPLEMENTED | 528 | Fully working raw socket server with routing |
| **File Writer** | Member 3 | ✅ IMPLEMENTED | 224 | Atomic writes, debouncing, crash-safe |
| **Platform Layer** | Member 3 | ✅ IMPLEMENTED | 202 | Cross-platform POSIX/Win32 sockets, browser launcher |
| **CLI & Main** | Member 3 | ✅ IMPLEMENTED | 102 | Argument parsing, server startup |
| **Frontend UI** | Member 3 | ✅ IMPLEMENTED | ~300 | HTML/CSS/JS two-pane editor, debouncing |
| **Fuzzer Harness** | Starter | ✅ IMPLEMENTED | 192 | Full round-trip fuzzer with mutation engine |

### 🔴 What's Pending (YOUR WORK — Member 2)

| Component | Task | Status | Priority | Est. Hours |
|-----------|------|--------|----------|-----------|
| **HTML Serializer** | Implement `html_to_md()` core logic | 🔴 TODO | **CRITICAL** | 8–12 |
| **Serializer Tests** | Write 15+ unit tests | 🔴 TODO | **HIGH** | 4–6 |
| **Round-Trip Integration** | Test bidirectional convergence | 🔴 TODO | **HIGH** | 2–4 |
| **Memory Safety** | ASan/UBSan/Valgrind auditing | 🔴 TODO | **MEDIUM** | 2–3 |
| **Edge Cases** | Malformed HTML, unicode, nested structures | 🔴 TODO | **MEDIUM** | 2–3 |
| **STDLIB.md** | Finalize documentation (already started) | 🟡 DRAFT | **LOW** | 1–2 |

---

## 🎯 Your Critical Path (Next 32 Hours Until Code Freeze)

### Hour Breakdown

**Hours 0–8 (Immediate - Next 8 Hours)**
- Implement core `html_to_md()` logic
- Write 10+ basic unit tests
- Verify parser output format (what HTML does Member 1 actually produce?)

**Hours 8–16 (Next 16 Hours)**
- Handle edge cases: nested lists, blockquotes, links
- Write remaining 5+ tests
- Run fuzzer for short bursts; identify any failures

**Hours 16–24 (Final 16 Hours)**
- Memory safety audit (ASan/UBSan/Valgrind)
- Fix any fuzz-discovered bugs
- Verify 100% convergence

**Hours 24–32 (Last 8 Hours Before Freeze)**
- Polish STDLIB.md
- Final testing pass
- Code review & cleanup

---

## 🔍 Detailed Analysis: What Needs Implementation

### 1. HTML Serializer Core Logic (`src-c/html_serializer.c`)

**Current Status:** Stub (44 lines)  
**What's Needed:** Full tag-to-Markdown walker

**Current Stub:**
```c
html_serialize_result_t html_to_md(const char *html_src, size_t html_len) {
    // Returns hardcoded "Stub Markdown output"
}
```

**What You Need to Build:**

#### Step 1a: Simple HTML Parser
You need a **minimal DOM tree builder** that:
- Scans HTML for opening tags: `<tag>` → create node
- Scans for closing tags: `</tag>` → close node  
- Captures text between tags → TEXT node
- Detects malformed HTML → return error

```c
typedef enum {
    NODE_TEXT,
    NODE_ELEMENT,
    NODE_COMMENT
} NodeType;

typedef struct HtmlNode {
    NodeType type;
    char *tag_name;           // e.g., "p", "strong", "a"
    char *text_content;       // For TEXT nodes
    struct HtmlNode *parent;
    struct HtmlNode *first_child;
    struct HtmlNode *next_sibling;
    // Attributes: href, class, id, etc. (simplified map or array)
} HtmlNode;

HtmlNode* parse_html_fragment(const char *html, size_t len);
```

#### Step 1b: DOM Walker (Recursive)
Walk the DOM tree and emit Markdown:

```c
typedef struct {
    char *buffer;          // Output Markdown string
    size_t pos;            // Current position
    size_t capacity;       // Allocated size
} MdBuilder;

void walk_node(HtmlNode *node, MdBuilder *out, int indent_level);
```

#### Step 1c: Tag Mapping Table
For each tag type, emit correct Markdown:

| HTML | Markdown |
|------|----------|
| `<h1>text</h1>` | `# text` |
| `<h2>text</h2>` | `## text` |
| `<strong>text</strong>` | `**text**` |
| `<em>text</em>` | `*text*` |
| `<ul><li>item</li></ul>` | `- item` |
| `<ol><li>item</li></ol>` | `1. item` |
| `<pre><code>text</code></pre>` | `` ```\ntext\n``` `` |
| `<code>text</code>` | `` `text` `` |
| `<blockquote>text</blockquote>` | `> text` |
| `<a href="url">text</a>` | `[text](url)` |
| `<p>text</p>` | `text\n\n` |

**Key Requirements:**
- [ ] Handle nested lists with proper indentation (2 spaces per level)
- [ ] Renumber `<ol>` items sequentially: `3./5./5.` → `1./2./3.`
- [ ] Escape Markdown metacharacters in text: `*`, `_`, `#`, `>`, `` ` ``
- [ ] Handle `<br>` → two spaces + newline
- [ ] Detect malformed HTML (unclosed tags) → return structured error

---

### 2. Unit Tests (`tests/test_html_serializer.c`)

**Current Status:** 1 stub test (20 lines)  
**Target:** 15+ tests covering all tag types + edge cases

**Test Categories:**

#### Basic Tags (5 tests)
```c
TEST(heading_levels)    // <h1> → #, <h2> → ##, etc.
TEST(bold_italic)       // <strong> + <em>
TEST(inline_code)       // <code> and <pre><code>
TEST(paragraph)         // <p> tags
TEST(links)             // <a href>
```

#### Nested Structures (5 tests)
```c
TEST(nested_lists)      // Lists with proper indentation
TEST(list_in_blockquote)
TEST(bold_in_link)      // <a><strong>...</strong></a>
TEST(blockquote_nesting)
TEST(ol_renumbering)    // <ol> → sequential 1. 2. 3.
```

#### Edge Cases (5 tests)
```c
TEST(empty_html)        // "" → ""
TEST(malformed_unclosed_tag)
TEST(unicode_passthrough)  // Emoji, accents, etc.
TEST(escaped_characters)   // Characters that need escaping
TEST(whitespace_handling)  // Multiple spaces, newlines
```

---

### 3. Integration with HTTP Server

**Current Status:**
- ✅ `/render` endpoint works: `POST /render {"md": "..."}` → returns HTML from Member 1's parser
- ❌ `/serialize` endpoint stubbed: needs your `html_to_md()` to work

**What Needs to Happen:**

The HTTP server (`src-c/http.c`) already has a `/serialize` endpoint stub:

```c
// In http.c (around line 350):
if (strncmp(uri, "/serialize", 10) == 0) {
    // TODO: Call html_to_md()
    html_serialize_result_t res = html_to_md(html_body, strlen(html_body));
    if (res.success) {
        // Return JSON: {"md": "..."}
    } else {
        // Return 400 error with error message
    }
}
```

**You don't need to modify http.c** — just ensure your `html_to_md()` returns the right structure, and it will work.

---

### 4. Round-Trip Fuzzer Integration

**Current Status:** ✅ Fully implemented (`tests/fuzz_roundtrip.c`)

**How It Works:**
1. Generates random/mutated Markdown
2. Calls `md_to_html()` (Member 1's parser)
3. Calls `html_to_md()` (YOUR serializer)
4. Calls `md_to_html()` again (re-parses)
5. Asserts: `HTML_1 == HTML_2` (fixed-point convergence)

**What Will Happen:**
- ✅ If your serializer works perfectly, fuzzer passes
- ❌ If there are bugs, fuzzer finds them quickly (worst case: 1-2 minutes per bug)

**Your Job:** Run fuzzer, find failures, fix them.

---

## 📋 Implementation Roadmap (Step-by-Step)

### Phase 1: HTML Parser Skeleton (2 hours)

**File:** `src-c/html_serializer.c` (currently 44 lines → ~150 lines)

```c
#include "html_serializer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// PART 1: Simple HTML Fragment Parser (Build DOM Tree)
// ============================================================================

typedef struct HtmlNode {
    enum { NODE_TEXT, NODE_ELEMENT, NODE_COMMENT } type;
    char *tag_name;
    char *text_content;
    struct HtmlNode *parent;
    struct HtmlNode *first_child;
    struct HtmlNode *next_sibling;
} HtmlNode;

static HtmlNode* html_node_new(void) {
    HtmlNode *node = malloc(sizeof(HtmlNode));
    if (!node) return NULL;
    memset(node, 0, sizeof(HtmlNode));
    return node;
}

static void html_node_free(HtmlNode *node) {
    if (!node) return;
    if (node->tag_name) free(node->tag_name);
    if (node->text_content) free(node->text_content);
    // Recursively free children
    HtmlNode *child = node->first_child;
    while (child) {
        HtmlNode *next = child->next_sibling;
        html_node_free(child);
        child = next;
    }
    free(node);
}

static HtmlNode* parse_html_fragment(const char *html, size_t len) {
    if (!html || len == 0) {
        return NULL;  // Empty HTML
    }
    
    HtmlNode *root = html_node_new();
    if (!root) return NULL;
    
    // TODO: Implement simple tag scanner
    // For now, just return root
    
    return root;
}

// ============================================================================
// PART 2: Markdown Builder (Dynamic String)
// ============================================================================

typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} MdBuilder;

static MdBuilder* md_builder_new(void) {
    MdBuilder *b = malloc(sizeof(MdBuilder));
    if (!b) return NULL;
    b->data = malloc(4096);
    if (!b->data) {
        free(b);
        return NULL;
    }
    b->len = 0;
    b->capacity = 4096;
    return b;
}

static void md_builder_append(MdBuilder *b, const char *str) {
    if (!b || !str) return;
    size_t needed = strlen(str);
    if (b->len + needed >= b->capacity) {
        b->capacity *= 2;
        char *new_data = realloc(b->data, b->capacity);
        if (!new_data) return;
        b->data = new_data;
    }
    memcpy(b->data + b->len, str, needed);
    b->len += needed;
    b->data[b->len] = '\0';
}

static char* md_builder_take(MdBuilder *b) {
    if (!b) return NULL;
    char *result = b->data;
    free(b);
    return result;
}

// ============================================================================
// PART 3: DOM Walker (Emit Markdown)
// ============================================================================

static void walk_node(HtmlNode *node, MdBuilder *out, int indent_level) {
    if (!node) return;
    
    if (node->type == NODE_TEXT) {
        // Escape and emit text
        md_builder_append(out, node->text_content);
        return;
    }
    
    if (node->type == NODE_ELEMENT) {
        // Dispatch to tag-specific handler
        if (strcmp(node->tag_name, "h1") == 0) {
            md_builder_append(out, "# ");
        } else if (strcmp(node->tag_name, "h2") == 0) {
            md_builder_append(out, "## ");
        }
        // ... more tag handlers
        
        // Recursively walk children
        HtmlNode *child = node->first_child;
        while (child) {
            walk_node(child, out, indent_level);
            child = child->next_sibling;
        }
        
        // Closing tag
        if (strcmp(node->tag_name, "p") == 0) {
            md_builder_append(out, "\n\n");
        }
    }
}

// ============================================================================
// PART 4: Public API
// ============================================================================

html_serialize_result_t html_to_md(const char *html_src, size_t html_len) {
    html_serialize_result_t res = { .success = false, .markdown = NULL, .error_msg = NULL };
    
    // Parse HTML into DOM tree
    HtmlNode *root = parse_html_fragment(html_src, html_len);
    if (!root) {
        res.error_msg = malloc(50);
        if (res.error_msg) {
            strcpy(res.error_msg, "Failed to parse HTML");
        }
        return res;
    }
    
    // Walk DOM, emit Markdown
    MdBuilder *out = md_builder_new();
    if (!out) {
        html_node_free(root);
        res.error_msg = malloc(50);
        if (res.error_msg) strcpy(res.error_msg, "Memory allocation failed");
        return res;
    }
    
    walk_node(root, out, 0);
    
    res.markdown = md_builder_take(out);
    res.success = true;
    
    html_node_free(root);
    return res;
}

void html_serialize_result_free(html_serialize_result_t *res) {
    if (res) {
        if (res->markdown) {
            free(res->markdown);
            res->markdown = NULL;
        }
        if (res->error_msg) {
            free(res->error_msg);
            res->error_msg = NULL;
        }
    }
}
```

---

### Phase 2: HTML Parser Implementation (3 hours)

**Focus:** Implement `parse_html_fragment()` with robust tag scanner

```c
static HtmlNode* parse_html_fragment(const char *html, size_t len) {
    HtmlNode *root = html_node_new();
    if (!root) return NULL;
    
    HtmlNode *current = root;  // Current open node
    size_t pos = 0;
    
    while (pos < len) {
        if (html[pos] == '<') {
            // Tag found
            if (html[pos + 1] == '/') {
                // Closing tag: </tagname>
                size_t end = pos + 2;
                while (end < len && html[end] != '>') end++;
                // Close current node
                if (current->parent) {
                    current = current->parent;
                }
                pos = end + 1;
            } else {
                // Opening tag: <tagname>
                size_t end = pos + 1;
                while (end < len && html[end] != '>' && html[end] != ' ') end++;
                char tag_buf[64];
                size_t tag_len = end - pos - 1;
                if (tag_len >= sizeof(tag_buf)) tag_len = sizeof(tag_buf) - 1;
                memcpy(tag_buf, &html[pos + 1], tag_len);
                tag_buf[tag_len] = '\0';
                
                // Create new node
                HtmlNode *new_node = html_node_new();
                if (!new_node) {
                    html_node_free(root);
                    return NULL;
                }
                new_node->type = NODE_ELEMENT;
                new_node->tag_name = malloc(strlen(tag_buf) + 1);
                strcpy(new_node->tag_name, tag_buf);
                new_node->parent = current;
                
                // Attach to current node
                if (!current->first_child) {
                    current->first_child = new_node;
                } else {
                    HtmlNode *last = current->first_child;
                    while (last->next_sibling) last = last->next_sibling;
                    last->next_sibling = new_node;
                }
                
                // Skip to end of tag (handle self-closing, attributes)
                while (end < len && html[end] != '>') end++;
                
                // If not self-closing, make it current
                if (!(end > 0 && html[end - 1] == '/')) {
                    // Check if self-closing tag
                    if (strcmp(tag_buf, "br") != 0 && strcmp(tag_buf, "hr") != 0) {
                        current = new_node;
                    }
                }
                
                pos = end + 1;
            }
        } else {
            // Text content
            size_t text_start = pos;
            while (pos < len && html[pos] != '<') pos++;
            
            HtmlNode *text_node = html_node_new();
            if (!text_node) {
                html_node_free(root);
                return NULL;
            }
            text_node->type = NODE_TEXT;
            text_node->text_content = malloc(pos - text_start + 1);
            memcpy(text_node->text_content, &html[text_start], pos - text_start);
            text_node->text_content[pos - text_start] = '\0';
            text_node->parent = current;
            
            // Attach to current node
            if (!current->first_child) {
                current->first_child = text_node;
            } else {
                HtmlNode *last = current->first_child;
                while (last->next_sibling) last = last->next_sibling;
                last->next_sibling = text_node;
            }
        }
    }
    
    return root;
}
```

---

### Phase 3: Tag Handlers (4 hours)

**Focus:** Implement walk_node() with all tag mappings

```c
static void walk_node(HtmlNode *node, MdBuilder *out, int indent_level) {
    if (!node) return;
    
    if (node->type == NODE_TEXT) {
        // Escape text: *, _, `, [, ], #, >
        // TODO: Implement escaping based on context
        md_builder_append(out, node->text_content);
        return;
    }
    
    // NODE_ELEMENT
    if (!node->tag_name) return;
    
    const char *tag = node->tag_name;
    
    // ===== HEADINGS =====
    if (strcmp(tag, "h1") == 0 || strcmp(tag, "h2") == 0 || 
        strcmp(tag, "h3") == 0 || strcmp(tag, "h4") == 0 ||
        strcmp(tag, "h5") == 0 || strcmp(tag, "h6") == 0) {
        
        int level = atoi(&tag[1]);
        for (int i = 0; i < level; i++) md_builder_append(out, "#");
        md_builder_append(out, " ");
        
        HtmlNode *child = node->first_child;
        while (child) {
            walk_node(child, out, indent_level);
            child = child->next_sibling;
        }
        
        md_builder_append(out, "\n");
        return;
    }
    
    // ===== STRONG / BOLD =====
    if (strcmp(tag, "strong") == 0 || strcmp(tag, "b") == 0) {
        md_builder_append(out, "**");
        HtmlNode *child = node->first_child;
        while (child) {
            walk_node(child, out, indent_level);
            child = child->next_sibling;
        }
        md_builder_append(out, "**");
        return;
    }
    
    // ===== ITALIC / EM =====
    if (strcmp(tag, "em") == 0 || strcmp(tag, "i") == 0) {
        md_builder_append(out, "*");
        HtmlNode *child = node->first_child;
        while (child) {
            walk_node(child, out, indent_level);
            child = child->next_sibling;
        }
        md_builder_append(out, "*");
        return;
    }
    
    // ===== INLINE CODE =====
    if (strcmp(tag, "code") == 0) {
        md_builder_append(out, "`");
        HtmlNode *child = node->first_child;
        while (child) {
            // Don't walk, just grab text (code content as-is)
            if (child->type == NODE_TEXT) {
                md_builder_append(out, child->text_content);
            }
            child = child->next_sibling;
        }
        md_builder_append(out, "`");
        return;
    }
    
    // ===== CODE BLOCK =====
    if (strcmp(tag, "pre") == 0) {
        // Walk children to find <code> tag
        HtmlNode *child = node->first_child;
        while (child) {
            if (child->type == NODE_ELEMENT && strcmp(child->tag_name, "code") == 0) {
                md_builder_append(out, "```\n");
                HtmlNode *code_child = child->first_child;
                while (code_child) {
                    if (code_child->type == NODE_TEXT) {
                        md_builder_append(out, code_child->text_content);
                    }
                    code_child = code_child->next_sibling;
                }
                md_builder_append(out, "\n```\n");
                return;
            }
            child = child->next_sibling;
        }
    }
    
    // ===== LINKS =====
    if (strcmp(tag, "a") == 0) {
        md_builder_append(out, "[");
        HtmlNode *child = node->first_child;
        while (child) {
            walk_node(child, out, indent_level);
            child = child->next_sibling;
        }
        // TODO: Extract href attribute
        md_builder_append(out, "](url)");
        return;
    }
    
    // ===== UNORDERED LIST =====
    if (strcmp(tag, "ul") == 0) {
        HtmlNode *child = node->first_child;
        int item_num = 0;
        while (child) {
            if (child->type == NODE_ELEMENT && strcmp(child->tag_name, "li") == 0) {
                // Indent
                for (int i = 0; i < indent_level * 2; i++) md_builder_append(out, " ");
                md_builder_append(out, "- ");
                
                // Walk list item children
                HtmlNode *li_child = child->first_child;
                while (li_child) {
                    walk_node(li_child, out, indent_level);
                    li_child = li_child->next_sibling;
                }
                
                md_builder_append(out, "\n");
            }
            child = child->next_sibling;
        }
        return;
    }
    
    // ===== ORDERED LIST =====
    if (strcmp(tag, "ol") == 0) {
        HtmlNode *child = node->first_child;
        int item_num = 1;
        while (child) {
            if (child->type == NODE_ELEMENT && strcmp(child->tag_name, "li") == 0) {
                // Indent
                for (int i = 0; i < indent_level * 2; i++) md_builder_append(out, " ");
                
                // Renumber sequentially
                char item_prefix[16];
                snprintf(item_prefix, sizeof(item_prefix), "%d. ", item_num++);
                md_builder_append(out, item_prefix);
                
                // Walk list item children
                HtmlNode *li_child = child->first_child;
                while (li_child) {
                    walk_node(li_child, out, indent_level);
                    li_child = li_child->next_sibling;
                }
                
                md_builder_append(out, "\n");
            }
            child = child->next_sibling;
        }
        return;
    }
    
    // ===== BLOCKQUOTE =====
    if (strcmp(tag, "blockquote") == 0) {
        md_builder_append(out, "> ");
        HtmlNode *child = node->first_child;
        while (child) {
            walk_node(child, out, indent_level + 1);
            child = child->next_sibling;
        }
        return;
    }
    
    // ===== PARAGRAPH =====
    if (strcmp(tag, "p") == 0) {
        HtmlNode *child = node->first_child;
        while (child) {
            walk_node(child, out, indent_level);
            child = child->next_sibling;
        }
        md_builder_append(out, "\n\n");
        return;
    }
    
    // ===== DEFAULT: WALK CHILDREN =====
    HtmlNode *child = node->first_child;
    while (child) {
        walk_node(child, out, indent_level);
        child = child->next_sibling;
    }
}
```

---

### Phase 4: Testing & Debugging (3 hours)

**Write tests:** `tests/test_html_serializer.c`

```c
#include "test_harness.h"
#include "../src-c/html_serializer.h"
#include <string.h>

TEST(simple_heading) {
    const char *html = "<h1>Hello</h1>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_STREQ(res.markdown, "# Hello\n");
    html_serialize_result_free(&res);
}

TEST(bold_and_italic) {
    const char *html = "<p><strong>bold</strong> and <em>italic</em></p>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_STREQ(res.markdown, "**bold** and *italic*\n\n");
    html_serialize_result_free(&res);
}

TEST(unordered_list) {
    const char *html = "<ul><li>one</li><li>two</li></ul>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    // ASSERT_STREQ(res.markdown, "- one\n- two\n");  // or similar
    html_serialize_result_free(&res);
}

TEST(ordered_list_renumbering) {
    const char *html = "<ol><li>first</li><li>second</li><li>third</li></ol>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    // Verify: "1. first\n2. second\n3. third\n"
    html_serialize_result_free(&res);
}

TEST(blockquote) {
    const char *html = "<blockquote>quote text</blockquote>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    // ASSERT_STREQ(res.markdown, "> quote text\n");
    html_serialize_result_free(&res);
}

TEST(link) {
    const char *html = "<a href=\"http://example.com\">link text</a>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    // ASSERT_STREQ(res.markdown, "[link text](http://example.com)");
    html_serialize_result_free(&res);
}

TEST(code_block) {
    const char *html = "<pre><code>var x = 1;</code></pre>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    // ASSERT_STREQ(res.markdown, "```\nvar x = 1;\n```\n");
    html_serialize_result_free(&res);
}

// ... more tests

int main(void) {
    RUN_TEST(simple_heading);
    RUN_TEST(bold_and_italic);
    RUN_TEST(unordered_list);
    RUN_TEST(ordered_list_renumbering);
    RUN_TEST(blockquote);
    RUN_TEST(link);
    RUN_TEST(code_block);
    // ... run more tests
    
    printf("\nTest Summary: %d run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
```

Run tests:
```bash
make clean && make test
```

**Debug failures:** Use `printf()` debugging or gdb

---

### Phase 5: Round-Trip Fuzzing (2 hours)

Once tests pass, run the fuzzer:

```bash
make fuzz DURATION=300  # 5 minutes
```

**What to look for:**
- ✅ `Fuzzing run completed successfully` — you're done!
- ❌ `FUZZ FAILURE` — a bug found. The error will show:
  - Input Markdown that failed
  - Expected HTML (first pass)
  - Actual HTML (after round-trip)

**Fix any failures found.** Most will be:
- Missing tag handler
- Incorrect escaping
- Off-by-one in list numbering
- Whitespace issues

---

### Phase 6: Memory Safety Audit (2 hours)

Run tests under sanitizers:

```bash
make clean
make asan  # Builds with -fsanitize=address,undefined
make test  # Run all tests
```

Look for:
- Memory leaks: `ERROR: LeakSanitizer`
- Use-after-free: `ERROR: AddressSanitizer`
- Undefined behavior: `SUMMARY: UndefinedBehaviorSanitizer`

Fix any reported issues (usually allocation bugs or buffer overflows).

---

### Phase 7: Edge Cases (2 hours)

Test these scenarios:

```c
// Empty HTML
const char *html = "";
html_serialize_result_t res = html_to_md(html, 0);
ASSERT_TRUE(res.success);

// Malformed (unclosed tags)
const char *html = "<p>unclosed";
html_serialize_result_t res = html_to_md(html, strlen(html));
// Should either: (a) return error, or (b) gracefully close and return partial markdown
// Check what behavior makes sense with Member 1's parser

// Unicode
const char *html = "<p>Hello 世界 🌍</p>";
html_serialize_result_t res = html_to_md(html, strlen(html));
ASSERT_STREQ(res.markdown, "Hello 世界 🌍\n\n");

// Deeply nested
const char *html = "<strong><em><code>text</code></em></strong>";
// Should return "***`text`***" or similar
```

---

## ✅ Checklist: What Success Looks Like

### By Hour 12
- [ ] `html_to_md()` compiles without warnings
- [ ] 10+ basic unit tests pass
- [ ] `make test` passes

### By Hour 24
- [ ] All 15+ unit tests pass
- [ ] Fuzzer runs without failures
- [ ] Memory: 0 Valgrind leaks, 0 ASan reports

### By Hour 32 (Code Freeze)
- [ ] Edge cases handled
- [ ] STDLIB.md updated
- [ ] Final code review done
- [ ] Git: `git log --oneline` shows your work

---

## 🔗 Key Files to Work With

| File | Purpose |
|------|---------|
| `src-c/html_serializer.h` | ✅ Frozen interface (don't change) |
| `src-c/html_serializer.c` | 🔴 **YOUR MAIN FILE** — implement here |
| `tests/test_html_serializer.c` | 🔴 **WRITE TESTS HERE** |
| `tests/fuzz_roundtrip.c` | ✅ Already implemented (just run) |
| `src-c/http.c` | ✅ Already works (don't modify) |
| `src-c/md_parser.c` | ✅ Stubbed (Member 1's concern) |
| `STDLIB.md` | 🟡 **UPDATE when done** |

---

## 🚀 Quick Start (Today's First 30 Minutes)

```bash
# 1. Clone repo (already done)
cd ~/repo

# 2. Review current state
ls -la src-c/html_serializer.*
wc -l src-c/html_serializer.c

# 3. Verify current tests pass
make clean && make test

# 4. Understand parser output
./mdview ./tests/fixtures/sample.md &  # Or spin up server manually
# Inspect what HTML the parser produces

# 5. Start implementing
# Copy Phase 1 code above into html_serializer.c
# Implement parse_html_fragment() skeleton
# Write 3-4 basic tests

# 6. Run and debug
make clean && make test
# Fix any compilation errors

# 7. Iterate: add more tests, implement more tag handlers
```

---

## 📞 Questions to Ask Members 1 & 3

### For Member 1 (Parser):
- Q: "Does your parser preserve exact HTML formatting (spaces, newlines) or normalize it?"
- A: This affects round-trip convergence. If parser normalizes, serializer must match.

- Q: "How do you handle unmatched delimiters like `**unclosed bold`?"
- A: Does it error (best), or output literal `**` characters (acceptable)?

### For Member 3 (Systems):
- Q: "In `/serialize` endpoint, what's the exact JSON format for HTML input?"
- A: Is it `{"html": "<p>...</p>"}` or something else?

- Q: "Should serializer errors return 400 with `{"error": "..."}` or a different format?"
- A: Make sure JSON error response is consistent with `/render` endpoint

---

## 🎯 Success Metrics

By end of hackathon:

| Metric | Target | Your Status |
|--------|--------|-------------|
| Unit tests passing | 15+ | 🔴 TBD |
| Fuzzer convergence | 100K+ cycles no failures | 🔴 TBD |
| Memory safety | 0 leaks, 0 ASan errors | 🔴 TBD |
| Code warnings | 0 with `-Wall -Wextra -Werror` | 🔴 TBD |
| Coverage | ≥85% lines | 🔴 TBD |
| STDLIB.md | ≥10 substitutions | 🟡 DRAFT |
| Integration | Full bidirectional sync | 🔴 TBD (needs your code) |

---

## 📝 Next Immediate Actions

1. **Right now (next 30 min):**
   - [ ] Read this entire document
   - [ ] Review `src-c/html_serializer.h` interface
   - [ ] Check `tests/test_harness.h` for test macros
   - [ ] Inspect what HTML Member 1's parser actually produces (by running `/render`)

2. **Next 2 hours:**
   - [ ] Copy Phase 1 skeleton into `html_serializer.c`
   - [ ] Write 3 basic tests
   - [ ] Get basic compilation working

3. **Next 4 hours:**
   - [ ] Implement HTML parser (`parse_html_fragment()`)
   - [ ] Implement tag handlers for H1-H6, bold, italic
   - [ ] Write 10+ tests

4. **Checkpoint (Hour 4):**
   - Run `make test` — target 10+ passing
   - Run fuzzer briefly — should see many cycles processed

---

## 🎉 You've Got This!

The infrastructure is solid:
- ✅ HTTP server fully working
- ✅ Parser interface frozen
- ✅ Fuzzer ready to go
- ✅ Test harness ready

You just need to **implement the serializer** and **verify bidirectional convergence**.

This is a solid 8-12 hour task with clear acceptance criteria. Focus on:
1. **Correctness first** (all tags convert properly)
2. **Memory safety** (no leaks, no crashes)
3. **Round-trip** (fuzzer passes)

Then you're done! 🚀

---

**Questions? Check ARCHITECTURE.md, PRD.md, or WORK_SPLIT.md for more context.**

**Good luck! See you at the finish line.** 🏁
