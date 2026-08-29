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
