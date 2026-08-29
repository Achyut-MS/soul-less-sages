const source = document.getElementById('source');
const preview = document.getElementById('preview');
const status = document.getElementById('status');

function setStatus(text, kind) {
  status.textContent = text;
  status.className = 'status ' + kind;
}

function renderPreview() {
  const md = source.value;
  setStatus('Rendering…', 'warn');

  fetch('/render', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ md })
  })
    .then((res) => res.text())
    .then((html) => {
      preview.innerHTML = html;
      setStatus('Synced', 'ok');
    })
    .catch(() => {
      setStatus('Render error', 'bad');
    });
}

source.addEventListener('input', () => {
  clearTimeout(window.renderTimer);
  window.renderTimer = setTimeout(renderPreview, 200);
});

renderPreview();
