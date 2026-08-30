const sourceEl = document.getElementById('markdown-source');
const previewEl = document.getElementById('html-preview');
const statusContainer = document.getElementById('status-container');
const statusText = document.getElementById('status-text');
const errorBanner = document.getElementById('error-banner');
const errorMsg = document.getElementById('error-msg');
const errorSnippet = document.getElementById('error-snippet');
const errorDismiss = document.getElementById('error-dismiss');

let renderTimeout = null;
let serializeTimeout = null;
let activeEditor = null; /* Can be 'source', 'preview', or null */

/* Request/Response sequence numbers to prevent stale-response overwrites */
let renderRequestSeq = 0;
let renderResponseSeq = 0;
let serializeRequestSeq = 0;
let serializeResponseSeq = 0;


/**
 * @brief Updates the synchronization status badge in the header.
 * @param {string} state - The status state ('synced', 'pending', 'error')
 */
function setStatus(state) {
    statusContainer.className = `status-badge state-${state}`;
    statusText.textContent = state.charAt(0).toUpperCase() + state.slice(1);
}

/**
 * @brief Renders the error banner with error message and caret snippet.
 */
function showError(message, snippet = '') {
    errorMsg.textContent = message;
    if (snippet) {
        errorSnippet.textContent = snippet;
        errorSnippet.style.display = 'block';
    } else {
        errorSnippet.textContent = '';
        errorSnippet.style.display = 'none';
    }
    errorBanner.classList.remove('hidden');
    setStatus('error');
}

/**
 * @brief Hides the error banner.
 */
function hideError() {
    errorBanner.classList.add('hidden');
}

/**
 * @brief Submits the Markdown source to `/render` to retrieve HTML.
 */
function renderMarkdown() {
    const md = sourceEl.value;
    setStatus('pending');
    
    renderRequestSeq++;
    const currentSeq = renderRequestSeq;
    
    fetch('/render', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ md: md })
    })
    .then(async (res) => {
        if (currentSeq < renderResponseSeq) {
            return; /* Discard stale render response */
        }
        renderResponseSeq = currentSeq;
        
        if (res.ok) {
            const html = await res.text();
            /*
             * Contenteditable Quirk Workaround 1: Selection Reset
             * Only update innerHTML if the active editor is NOT the preview pane.
             * Replacing innerHTML on a contenteditable element while the user is typing
             * destroys all browser text nodes, immediately losing selection/focus and
             * jumping the text cursor back to the start of the paragraph.
             */
            if (activeEditor !== 'preview') {
                previewEl.innerHTML = html;
                renderPostProcessing();
            }
            hideError();
            setStatus('synced');
        } else {
            const errData = await res.json();
            showError(errData.error || 'Failed to render Markdown', errData.snippet);
        }
    })
    .catch((err) => {
        if (currentSeq >= renderResponseSeq) {
            showError('Network error connecting to local companion server');
        }
        console.error(err);
    });
}

/**
 * @brief Renders KaTeX math formulas and Mermaid diagrams in the preview pane.
 */
function renderPostProcessing() {
    /* 1. Render KaTeX Math */
    if (window.katex) {
        previewEl.querySelectorAll('.math-inline').forEach((el) => {
            if (el.dataset.rendered) return;
            let tex = el.textContent.trim();
            if (tex.startsWith('$') && tex.endsWith('$') && tex.length >= 2) {
                tex = tex.substring(1, tex.length - 1).trim();
            }
            try {
                katex.render(tex, el, { throwOnError: false });
                el.dataset.rendered = 'true';
            } catch (e) {
                console.warn('KaTeX inline render error', e);
            }
        });

        previewEl.querySelectorAll('.math-block').forEach((el) => {
            if (el.dataset.rendered) return;
            let tex = el.textContent.trim();
            if (tex.startsWith('$$') && tex.endsWith('$$') && tex.length >= 4) {
                tex = tex.substring(2, tex.length - 2).trim();
            }
            try {
                katex.render(tex, el, { displayMode: true, throwOnError: false });
                el.dataset.rendered = 'true';
            } catch (e) {
                console.warn('KaTeX block render error', e);
            }
        });
    }

    /* 2. Render Mermaid Diagrams */
    if (window.mermaid) {
        let hasMermaid = false;
        previewEl.querySelectorAll('code.language-mermaid').forEach((el) => {
            const pre = el.parentElement;
            if (!pre || pre.tagName.toLowerCase() !== 'pre') return;
            const container = document.createElement('div');
            container.className = 'mermaid';
            container.textContent = el.textContent;
            pre.replaceWith(container);
            hasMermaid = true;
        });
        if (hasMermaid || previewEl.querySelector('.mermaid:not([data-processed="true"])')) {
            try {
                mermaid.run({ nodes: previewEl.querySelectorAll('.mermaid:not([data-processed="true"])') });
            } catch (e) {
                console.warn('Mermaid render error', e);
            }
        }
    }

    /* 3. Image loading & graceful error handling */
    previewEl.querySelectorAll('img').forEach((img) => {
        if (img.dataset.hasErrorHandler) return;
        img.dataset.hasErrorHandler = 'true';
        if (img.complete && img.naturalWidth === 0) {
            applyImageFallback(img);
        } else {
            img.addEventListener('error', function() {
                applyImageFallback(this);
            });
        }
    });
}

function applyImageFallback(img) {
    if (img.dataset.fallbackApplied) return;
    img.dataset.fallbackApplied = 'true';
    const altText = img.alt || img.getAttribute('src') || 'Image';
    const srcText = img.getAttribute('src') || '';
    const badge = document.createElement('div');
    badge.className = 'broken-image-placeholder';
    badge.title = `Image source: ${srcText}`;
    badge.innerHTML = `<span class="broken-img-icon">🖼️</span><div class="broken-img-meta"><span class="broken-img-alt">${escapeHtml(altText)}</span><span class="broken-img-src">${escapeHtml(srcText)}</span></div>`;
    img.style.display = 'none';
    img.after(badge);
}

function escapeHtml(str) {
    if (!str) return '';
    return str.replace(/&/g, '&amp;')
              .replace(/</g, '&lt;')
              .replace(/>/g, '&gt;')
              .replace(/"/g, '&quot;');
}

/**
 * @brief Submits the preview HTML to `/serialize` to retrieve Markdown source.
 */
function serializeHtml() {
    const html = previewEl.innerHTML;
    setStatus('pending');
    
    serializeRequestSeq++;
    const currentSeq = serializeRequestSeq;
    
    fetch('/serialize', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ html: html })
    })
    .then(async (res) => {
        if (currentSeq < serializeResponseSeq) {
            return; /* Discard stale serialize response */
        }
        serializeResponseSeq = currentSeq;
        
        if (res.ok) {
            const data = await res.json();
            /*
             * Contenteditable Quirk Workaround 2: Infinite Cycle Loop
             * Only write back to the source textarea if the active editor is NOT the source.
             * If the user is typing in the textarea, updating its value from the serialize
             * result creates an loopback echo that interrupts textarea keystrokes and resets scroll.
             */
            if (activeEditor !== 'source') {
                sourceEl.value = data.md;
            }
            hideError();
            setStatus('synced');
        } else {
            const errData = await res.json();
            showError(errData.error || 'Failed to serialize preview content');
        }
    })
    .catch((err) => {
        if (currentSeq >= serializeResponseSeq) {
            showError('Network error connecting to local companion server');
        }
        console.error(err);
    });
}

/* Event listeners for keystroke debouncing */
sourceEl.addEventListener('input', () => {
    activeEditor = 'source';
    clearTimeout(renderTimeout);
    renderTimeout = setTimeout(renderMarkdown, 200);
});

previewEl.addEventListener('input', () => {
    activeEditor = 'preview';
    clearTimeout(serializeTimeout);
    serializeTimeout = setTimeout(serializeHtml, 200);
});

/* Tab key interception in Markdown source textarea */
sourceEl.addEventListener('keydown', (e) => {
    if (e.key === 'Tab') {
        e.preventDefault();
        const start = sourceEl.selectionStart;
        const end = sourceEl.selectionEnd;
        const val = sourceEl.value;
        
        /* Insert 2 spaces at caret position */
        sourceEl.value = val.substring(0, start) + '  ' + val.substring(end);
        sourceEl.selectionStart = sourceEl.selectionEnd = start + 2;
        
        /* Manually dispatch input event to trigger debounced render */
        sourceEl.dispatchEvent(new Event('input'));
    }
});

/* Manual error banner dismiss */
errorDismiss.addEventListener('click', () => {
    hideError();
    setStatus('synced');
});

/* Initialize editor view on load */
window.addEventListener('DOMContentLoaded', async () => {
    try {
        const res = await fetch('/file');
        if (res.ok) {
            const data = await res.json();
            if (data.content && data.content.length > 0) {
                sourceEl.value = data.content;
            }
            if (data.filename && data.filename.length > 0) {
                const headerTitle = document.querySelector('.pane.source-pane .pane-header');
                if (headerTitle) {
                    headerTitle.textContent = `Markdown Source (${data.filename})`;
                }
            }
        }
    } catch (e) {
        console.error('Could not load initial file', e);
    }
    activeEditor = 'source';
    renderMarkdown();
});

/* ============================================================
 * Word-Style Formatting Toolbar (contenteditable preview)
 * ============================================================ */
const toolbarEl = document.getElementById('format-toolbar');
const blockSelect = document.getElementById('block-select');
const fontSizeSelect = document.getElementById('font-size-select');
const fontFamilySelect = document.getElementById('font-family-select');
const textColorInput = document.getElementById('text-color-input');
const zoomLevelLabel = document.getElementById('zoom-level');

function applyFormatting(command, value = null) {
    previewEl.focus();
    document.execCommand(command, false, value);
    /* Fire input so the debounced serialize -> source sync runs */
    previewEl.dispatchEvent(new Event('input', { bubbles: true }));
    updateToolbarState();
}

/* Reflect current selection state onto toolbar buttons */
function updateToolbarState() {
    if (document.activeElement !== previewEl) return;
    toolbarEl.querySelectorAll('.toolbar-btn[data-command]').forEach((btn) => {
        const cmd = btn.dataset.command;
        try {
            btn.classList.toggle('active', document.queryCommandState(cmd));
        } catch (e) {
            /* Some commands are unsupported in some engines */
        }
    });

    const blockTag = (() => {
        const sel = window.getSelection();
        if (!sel || sel.rangeCount === 0) return null;
        let node = sel.getRangeAt(0).startContainer;
        while (node && node !== previewEl) {
            if (node.nodeType === Node.ELEMENT_NODE) {
                const tag = node.tagName.toLowerCase();
                if (/^h[1-6]$/.test(tag)) return tag;
                if (tag === 'p') return 'p';
            }
            node = node.parentNode;
        }
        return null;
    })();
    blockSelect.value = blockTag || 'p';
}

/* Heading / paragraph blocks via execCommand formatBlock */
blockSelect.addEventListener('change', () => {
    const val = blockSelect.value;
    applyFormatting('formatBlock', val === 'p' ? '<p>' : `<${val}>`);
});

fontSizeSelect.addEventListener('change', () => {
    applyFormatting('fontSize', fontSizeSelect.value);
});

fontFamilySelect.addEventListener('change', () => {
    applyFormatting('fontName', fontFamilySelect.value);
});

textColorInput.addEventListener('input', () => {
    applyFormatting('foreColor', textColorInput.value);
});

toolbarEl.querySelectorAll('.toolbar-btn[data-command]').forEach((btn) => {
    btn.addEventListener('mousedown', (e) => {
        /* preventDefault keeps the text selection alive while clicking buttons */
        e.preventDefault();
    });
    btn.addEventListener('click', () => {
        applyFormatting(btn.dataset.command);
    });
});

previewEl.addEventListener('keyup', updateToolbarState);
previewEl.addEventListener('mouseup', updateToolbarState);

/* ============================================================
 * Zoom (Ctrl + / Ctrl -), applied to the editor container
 * ============================================================ */
const editorContainer = document.querySelector('.editor-container');
let currentZoom = 1.0;

function setZoom(z) {
    currentZoom = Math.min(2.0, Math.max(0.6, z));
    /* CSS zoom scales layout natively; transform fallback for Firefox */
    if ('zoom' in editorContainer.style) {
        editorContainer.style.zoom = currentZoom;
        editorContainer.style.transform = '';
        editorContainer.style.transformOrigin = '';
        editorContainer.style.width = '';
    } else {
        editorContainer.style.transformOrigin = '0 0';
        editorContainer.style.transform = `scale(${currentZoom})`;
        editorContainer.style.width = `${100 / currentZoom}%`;
    }
    zoomLevelLabel.textContent = `${Math.round(currentZoom * 100)}%`;
}

function zoomEditor(delta) {
    setZoom(currentZoom + delta);
}

document.getElementById('zoom-in-btn').addEventListener('click', () => zoomEditor(0.1));
document.getElementById('zoom-out-btn').addEventListener('click', () => zoomEditor(-0.1));
document.getElementById('zoom-reset-btn').addEventListener('click', () => setZoom(1.0));

/* ============================================================
 * Keyboard Shortcuts
 * ============================================================ */
document.addEventListener('keydown', (e) => {
    const isCtrl = e.ctrlKey || e.metaKey;

    /* Zoom: Ctrl+/Ctrl- (and Ctrl+= which many keyboards send for +) */
    if (isCtrl && (e.key === '+' || e.key === '=')) {
        e.preventDefault();
        zoomEditor(0.1);
        return;
    }
    if (isCtrl && (e.key === '-' || e.key === '_')) {
        e.preventDefault();
        zoomEditor(-0.1);
        return;
    }
    if (isCtrl && e.key === '0') {
        e.preventDefault();
        setZoom(1.0);
        return;
    }

    /* Formatting shortcuts only when editing the rich preview */
    if (document.activeElement !== previewEl) return;
    const k = e.key.toLowerCase();
    if (isCtrl && k === 'b') {
        e.preventDefault();
        applyFormatting('bold');
    } else if (isCtrl && k === 'i') {
        e.preventDefault();
        applyFormatting('italic');
    } else if (isCtrl && k === 'u') {
        e.preventDefault();
        applyFormatting('underline');
    }
});

/* ============================================================
 * File Explorer (Open Folder -> list -> open -> autosave)
 * ============================================================ */
const fileSidebar = document.getElementById('file-sidebar');
const sidebarDivider = document.getElementById('sidebar-divider');
const fileListEl = document.getElementById('file-list');
const openFolderBtn = document.getElementById('open-folder-btn');
let currentFileName = '';

function setActiveFileLabel(name) {
    currentFileName = name;
    const headerTitle = document.querySelector('.pane.source-pane .pane-header');
    if (headerTitle) {
        headerTitle.textContent = name ? `Markdown Source (${name})` : 'Markdown Source';
    }
}

function renderFileList(files) {
    fileListEl.innerHTML = '';
    if (!files || files.length === 0) {
        const li = document.createElement('li');
        li.className = 'file-empty';
        li.textContent = 'No .md files found in this folder';
        fileListEl.appendChild(li);
        return;
    }
    files.forEach((f) => {
        const li = document.createElement('li');
        li.textContent = f.name;
        li.title = `${f.name} — ${f.size} bytes`;
        if (f.name === currentFileName) li.classList.add('active-file');
        li.addEventListener('click', () => openFileFromList(f.name, li));
        fileListEl.appendChild(li);
    });
}

async function openFileFromList(name, liEl) {
    try {
        const res = await fetch(`/file?path=${encodeURIComponent(name)}`);
        if (!res.ok) {
            const err = await res.json().catch(() => ({}));
            showError(err.error || `Could not open ${name}`);
            return;
        }
        const data = await res.json();
        /* Tell the server to retarget autosave to this file */
        const openRes = await fetch('/open-file', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ path: name })
        });
        if (!openRes.ok) {
            const err = await openRes.json().catch(() => ({}));
            showError(err.error || `Could not switch active file to ${name}`);
            return;
        }

        sourceEl.value = data.content || '';
        setActiveFileLabel(name);
        fileListEl.querySelectorAll('li').forEach((li) => li.classList.remove('active-file'));
        if (liEl) liEl.classList.add('active-file');

        /* Re-render preview from the freshly loaded source */
        activeEditor = 'source';
        renderMarkdown();
    } catch (e) {
        console.error('Failed to open file', e);
        showError(`Could not open ${name}`);
    }
}

async function toggleFolderBrowser() {
    const isHidden = fileSidebar.classList.contains('hidden');
    if (isHidden) {
        try {
            const res = await fetch('/files');
            if (res.ok) {
                const files = await res.json();
                renderFileList(files);
            } else {
                renderFileList([]);
            }
        } catch (e) {
            console.error('Failed to list files', e);
            renderFileList([]);
        }
        fileSidebar.classList.remove('hidden');
        sidebarDivider.style.display = '';
    } else {
        fileSidebar.classList.add('hidden');
        sidebarDivider.style.display = 'none';
    }
}

openFolderBtn.addEventListener('click', toggleFolderBrowser);
document.getElementById('sidebar-close').addEventListener('click', () => {
    fileSidebar.classList.add('hidden');
    sidebarDivider.style.display = 'none';
});

/* Track the file opened at launch (from /file) for sidebar highlighting */
setActiveFileLabel('');
window.addEventListener('DOMContentLoaded', async () => {
    try {
        const res = await fetch('/file');
        if (res.ok) {
            const data = await res.json();
            if (data.filename && data.filename.length > 0) {
                setActiveFileLabel(data.filename);
            }
        }
    } catch (e) { /* non-fatal */ }
});

/* ============================================================
 * Upload File — browse the local computer, upload a .md file.
 * The file is saved server-side into the launch folder and made
 * the active (autosaved) document.
 * ============================================================ */
const uploadBtn = document.getElementById('upload-file-btn');
const uploadInput = document.getElementById('upload-file-input');

uploadBtn.addEventListener('click', () => uploadInput.click());

uploadInput.addEventListener('change', async () => {
    const file = uploadInput.files[0];
    uploadInput.value = ''; /* allow re-selecting the same file later */
    if (!file) return;

    const name = file.name;
    if (!/\.(md|markdown)$/i.test(name)) {
        showError('Only .md or .markdown files can be uploaded');
        return;
    }

    try {
        const text = await file.text();
        const res = await fetch('/upload-file', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name: name, content: text })
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            showError(data.error || `Could not upload ${name}`);
            return;
        }

        /* Load into the editor and make it the active autosave file */
        sourceEl.value = text;
        setActiveFileLabel(name);
        if (!fileSidebar.classList.contains('hidden')) {
            toggleFolderBrowser(); /* refresh list */
            toggleFolderBrowser();
        }
        activeEditor = 'source';
        renderMarkdown();
    } catch (e) {
        console.error('Upload failed', e);
        showError(`Could not upload ${name}`);
    }
});
