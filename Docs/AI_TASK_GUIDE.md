# AI Implementation Guide for the Zero-Dependency Markdown App

## 1) Goal in plain English
Build the app so it behaves like a lightweight but usable Word-like markdown editor, while still following the Zero Dependency Hackathon rules.

The app must:
- show a top toolbar like Microsoft Word for formatting
- support editing text visually in a WYSIWYG preview area
- allow opening a folder and selecting a `.md` file like an IDE
- support zoom in/out with `Ctrl +` and `Ctrl -`
- keep the project dependency-free: no npm packages, no framework install, no CDN, no external runtime libraries

This is not a full Microsoft Word clone. It is a focused zero-dependency editor that gives the user a Word-like feel inside a native desktop shell.

---

## 2) Hackathon rules to obey
This project must follow the site rules from https://zerodepshack.com/:

- The dependency manifest must stay empty
- No package manager dependencies for app runtime
- No external libraries or frameworks
- Use only standard library features and browser-native APIs
- Build a real working tool, not a mockup
- Keep the app useful and demonstrable in a short time

Important practical rule:
- The app can still use plain HTML, CSS, and JavaScript in the browser UI
- It cannot depend on Node modules, React, Vite, Electron, or third-party packages
- The C server can serve the UI locally and manage file IO, but it must not pull in external libraries

---

## 3) What the current repo already is
This repo is already a zero-dependency Markdown viewer/editor written in C with a browser UI.

Relevant parts:
- `src-c/static/index.html` → app shell
- `src-c/static/client.js` → browser logic for rendering + syncing
- `src-c/static/styles.css` → UI styling
- `src-c/http.c` and `src-c/http.h` → local HTTP server and API
- `src-c/main.c` → app entry point and desktop launch
- `src-c/md_parser.c` / `src-c/html_serializer.c` → Markdown parsing and reverse serialization

Current app is close to a markdown editor, but it does not yet have:
- Word-style toolbar buttons
- rich text formatting commands
- folder/file browser UI
- zoom controls
- file selection flow for opening projects and markdown files

---

## 4) User requirements mapped to plain English

### Requirement 1: Word-like top toolbar
The user wants a toolbar at the top of the editor with formatting buttons similar to MS Word.

Expected actions:
- Bold
- Italic
- Underline
- Strikethrough
- Heading levels
- Bullet list
- Numbered list
- Font size selector
- Font family selector
- Text color / highlight color (optional but nice)
- Alignment options (optional if time allows)

Simple interpretation:
Add a toolbar above the editor with clickable controls that change the selected text visually.

### Requirement 2: Open folder and open markdown file like an IDE
The user wants to browse a local folder and open a `.md` file, not just edit a single static file.

Simple interpretation:
Add a file explorer in the sidebar or top bar with a button like “Open Folder” and “Open File”.

Expected behavior:
- choose a folder
- list markdown files in it
- select a file to load into the editor
- save the file back to disk

### Requirement 3: Zoom in/out visually
The user wants the editor viewport to zoom like PDF/browser zoom.

Expected behavior:
- `Ctrl +` increases zoom
- `Ctrl -` decreases zoom
- the content area visibly scales without destroying layout

### Requirement 4: Keep all of it within hackathon rules
No dependency installation, no external packages, no frameworks, no CDN fonts or JS libraries.

This must be a standard-library-only or browser-native implementation.

---

## 5) Coding plan for the AI

### Step A: Update the UI shell
File: `src-c/static/index.html`

Add:
- top toolbar container
- buttons for bold, italic, underline, heading, bullet list, numbered list, font size, etc.
- file browser area
- status bar / notice area
- editor container with zoom-aware styling

Example structure:
```html
<div class="top-toolbar">
  <button data-command="bold"><b>B</b></button>
  <button data-command="italic"><i>I</i></button>
  <button data-command="underline"><u>U</u></button>
  <button data-command="insertUnorderedList">• List</button>
  <button data-command="insertOrderedList">1. List</button>
  <select id="font-size-select">...</select>
  <button id="open-file-btn">Open File</button>
  <button id="open-folder-btn">Open Folder</button>
  <button id="zoom-out-btn">-</button>
  <button id="zoom-in-btn">+</button>
</div>
```

### Step B: Add CSS for Word-like appearance
File: `src-c/static/styles.css`

Add:
- top toolbar border and spacing
- grouped formatting buttons
- viewport scaling via CSS `zoom` or `transform: scale()` carefully
- non-dependent style adjustments
- dark/light styling if desired
- file tree and preview/editor layout

Important note:
Use CSS only. Do not add a CSS framework.

### Step C: Add browser formatting logic in JavaScript
File: `src-c/static/client.js`

Add functions such as:
```js
function applyFormatting(command, value = null) {
  document.execCommand(command, false, value);
}

function updateToolbarState() {
  // check active selection state and update button highlight
}

function handleZoom(delta) {
  currentZoom = Math.max(0.6, Math.min(2.0, currentZoom + delta));
  editorRoot.style.zoom = currentZoom;
}
```

Use browser built-ins like:
- `document.execCommand()` for basic formatting
- `selection` and `Range` APIs
- `keydown` event listeners for `Ctrl+B`, `Ctrl+I`, `Ctrl+U`, `Ctrl+=`, `Ctrl+-`
- `input` events to trigger Markdown conversion

### Step D: Connect file selection flow
Current app already contains source/preview sync. Keep it.

You must add a file-open workflow using browser-native file picker and local API endpoints.

Suggested logic:
- button `Open File` triggers a hidden `<input type="file">`
- choose `.md` file
- read file contents via `FileReader`
- load content into the editor
- send file content to `/render` if render-sync is needed

For folder browsing:
- use `<input type="file" webkitdirectory directory multiple>` where available
- or fall back to a simple “choose file” flow if directory selection is not supported in the browser environment

This is still dependency-free and works in a standard browser runtime.

### Step E: Add server endpoints for file access
Files likely involved:
- `src-c/http.c`
- `src-c/http.h`

Add endpoints such as:
- `GET /files` → list `.md` files in a chosen directory
- `GET /file?path=...` → read a markdown file
- `POST /save-file` → save content back to disk
- `POST /open-folder` → accept selected directory and return file list

The C runtime should:
- validate input paths
- restrict access to intended directories
- avoid shell injection
- always sanitize file paths before reading/writing

### Step F: Preserve all current Markdown behavior
Do not break the existing rendering flow.

Keep the current features:
- Markdown source editor
- live preview generation
- sync between source and preview
- error banner and validation

The new toolbar should layer onto that foundation, not replace it.

---

## 6) Recommended technical implementation strategy

### Option A: Rich text editing with `contenteditable`
Best fit for the UI requirement and time budget.

Use:
- a `contenteditable` editor block
- toolbar commands against the current selection
- `document.execCommand` or selection-based DOM mutation for text formatting
- conversion to Markdown only at save or sync time

Pros:
- easiest to make Word-like immediately
- no dependencies
- fits existing browser UI style

Cons:
- `execCommand` is old and somewhat limited
- formatting fidelity is not perfectly identical to Microsoft Word

### Option B: Markdown source + visual toolbar using custom formatting layer
This is more robust but takes longer.

Pros:
- better for preserving parse semantics
- easier to keep structured markdown output clean

Cons:
- more complex to implement correctly
- higher risk under sprint constraints

Recommended choice: Use `contenteditable` + toolbar + existing Markdown pipeline as the practical solution.

---

## 7) Suggested file map for the AI

### Core UI files
- `src-c/static/index.html`
- `src-c/static/styles.css`
- `src-c/static/client.js`

### Server / file system files
- `src-c/http.c`
- `src-c/http.h`
- `src-c/main.c`
- `src-c/platform.c`
- `src-c/platform.h`

### Existing Markdown logic to keep intact
- `src-c/md_parser.c`
- `src-c/html_serializer.c`
- `src-c/file_writer.c`

---

## 8) Acceptance checklist
The implementation is complete only if all are true:

- [ ] Top toolbar shows Word-like formatting controls
- [ ] Bold, italic, underline, heading, lists work visually
- [ ] Font size can be changed from toolbar or keyboard shortcuts
- [ ] `Ctrl +` and `Ctrl -` adjust zoom without external libs
- [ ] A folder can be opened and markdown files listed
- [ ] A `.md` file can be selected and loaded into the editor
- [ ] Edits are saved back to the selected file
- [ ] App still builds using the existing C project workflow
- [ ] No package manifest is added or changed
- [ ] No third-party runtime dependency is introduced
- [ ] Demo works locally without internet dependency for JS libs

---

## 9) Minimal implementation order for the AI

1. Add toolbar HTML and CSS
2. Add JS formatting commands and keyboard handlers
3. Add zoom logic with `Ctrl +` and `Ctrl -`
4. Add file selection input and load file into editor
5. Add folder listing and markdown file discovery
6. Add safe save-to-disk behavior
7. Verify local build still works
8. Verify no dependency files are added
9. Run a short demo check for the required feature flow

---

## 10) Coding-level pseudocode

```js
const state = {
  zoom: 1,
  currentFolder: '',
  currentFile: ''
};

function applyFormatting(command, value = null) {
  document.execCommand(command, false, value);
}

function handleKeyShortcut(event) {
  if (event.ctrlKey && event.key === '+') {
    event.preventDefault();
    zoomEditor(0.1);
  }

  if (event.ctrlKey && event.key === '-') {
    event.preventDefault();
    zoomEditor(-0.1);
  }

  if (event.ctrlKey && event.key.toLowerCase() === 'b') {
    event.preventDefault();
    applyFormatting('bold');
  }

  if (event.ctrlKey && event.key.toLowerCase() === 'i') {
    event.preventDefault();
    applyFormatting('italic');
  }

  if (event.ctrlKey && event.key.toLowerCase() === 'u') {
    event.preventDefault();
    applyFormatting('underline');
  }
}

function zoomEditor(delta) {
  state.zoom = Math.min(2, Math.max(0.6, state.zoom + delta));
  document.querySelector('.editor-surface').style.zoom = state.zoom;
}

function openFilePicker() {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.md,text/markdown';
  input.onchange = async () => {
    const file = input.files[0];
    const text = await file.text();
    sourceEl.value = text;
    renderMarkdown();
  };
  input.click();
}
```

---

## 11) Final AI instruction
Do not build a heavy app with third-party dependencies. Keep the solution in the existing native C + browser UI architecture.

The AI should implement the requested Word-like editing experience as a layered enhancement on top of the current app, not a rewritten app. The final result should feel like a lightweight rich-text markdown editor with file browsing and zoom controls, while keeping the project compatible with the Zero Dependency Hackathon rules.

---

## 12) Short summary for the AI
If you do only one thing: keep the app zero-dependency, build the toolbar in HTML/CSS/JS, use the contenteditable editor for WYSIWYG formatting, add file open/save logic with browser native file input, and ensure `Ctrl +` / `Ctrl -` zoom works without pulling in any library.
