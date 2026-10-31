const BANK_LETTERS  = Array.from('ABCDEFGHIJKLMNOP');
const VOICE_LABELS  = ['Voice 1 — Sampler', 'Voice 2 — Chromatic (V/oct)'];
const MAX_FILES     = 16;
const TARGET_RATE   = 44100;   // firmware output rate: no rate conversion on voice 1
const SIL_THRESHOLD = 0.003;

// state.banks[bankIndex][voiceIndex] = [{id, file}, ...]
const state = {
  currentBank: 0,
  banks: BANK_LETTERS.map(() => VOICE_LABELS.map(() => []))
};

let audioContext = null;
let previewAudio = null;
let previewItemId = null;
let previewUrl    = null;

// ── helpers ───────────────────────────────────────────────────────────────────

function newId() {
  return window.crypto?.randomUUID?.() ?? `${Date.now()}-${Math.random()}`;
}

function ensureCtx() {
  if (!audioContext) {
    const Ctx = window.AudioContext || window.webkitAudioContext;
    if (!Ctx) throw new Error('AudioContext not supported.');
    audioContext = new Ctx();
  }
  return audioContext;
}

function setStatus(text, progress = null) {
  document.getElementById('statusText').textContent = text;
  if (progress !== null) document.getElementById('progress').value = progress;
}

function bankHasFiles(b) {
  return state.banks[b].some(v => v.length > 0);
}

// ── bank tabs ─────────────────────────────────────────────────────────────────

function renderBankTabs() {
  const nav = document.getElementById('bankTabs');
  nav.innerHTML = '';
  BANK_LETTERS.forEach((letter, idx) => {
    const btn = document.createElement('button');
    btn.className = 'bank-tab' +
                    (idx === state.currentBank ? ' active' : '') +
                    (bankHasFiles(idx) ? ' has-files' : '');
    btn.textContent = letter;
    btn.title = `Bank ${letter}`;
    btn.addEventListener('click', () => {
      state.currentBank = idx;
      renderBankTabs();
      renderCurrentBank();
    });
    nav.appendChild(btn);
  });
}

// ── voice columns ─────────────────────────────────────────────────────────────

function renderCurrentBank() {
  const container = document.getElementById('bankVoices');
  container.innerHTML = '';
  const b      = state.currentBank;
  const letter = BANK_LETTERS[b];

  VOICE_LABELS.forEach((label, v) => {
    const col = document.createElement('div');
    col.className = 'voice-column';

    const inputId = `input-${b}-${v}`;
    const listId  = `list-${b}-${v}`;

    col.innerHTML = `
      <h2>${label}</h2>
      <div class="voice-controls">
        <input type="file" id="${inputId}" multiple accept="audio/*">
      </div>
      <p class="hint">Exported as ${letter}/${v + 1}/01.wav … ${letter}/${v + 1}/16.wav</p>
      <ol class="file-list" id="${listId}"></ol>`;

    // file input
    const input = col.querySelector(`#${inputId}`);
    input.addEventListener('change', e => {
      addFiles(b, v, Array.from(e.target.files || []));
      input.value = '';
    });

    // drop zone on list
    const list = col.querySelector(`#${listId}`);
    list.addEventListener('dragover', e => { e.preventDefault(); list.classList.add('drag-over'); });
    list.addEventListener('dragleave', () => list.classList.remove('drag-over'));
    list.addEventListener('drop', e => {
      e.preventDefault();
      list.classList.remove('drag-over');
      const payload = e.dataTransfer.getData('application/json');
      if (!payload) return;
      const d = JSON.parse(payload);
      moveItem(d.fromBank, d.fromVoice, b, v, d.id);
    });

    container.appendChild(col);
    renderVoiceList(b, v);
  });
}

function renderVoiceList(b, v) {
  const list = document.getElementById(`list-${b}-${v}`);
  if (!list) return;
  list.innerHTML = '';

  state.banks[b][v].forEach((item, idx) => {
    const li = document.createElement('li');
    li.className = 'file-row';
    li.draggable = true;
    li.addEventListener('dragstart', e => {
      e.dataTransfer.setData('application/json', JSON.stringify({ id: item.id, fromBank: b, fromVoice: v }));
      e.dataTransfer.effectAllowed = 'move';
    });

    const label = document.createElement('span');
    label.className = 'file-row-label';
    label.textContent = `${String(idx + 1).padStart(2, '0')}. ${item.file.name}`;

    const actions = document.createElement('div');
    actions.className = 'file-actions';

    const playBtn = document.createElement('button');
    playBtn.className = 'secondary';
    playBtn.textContent = '▶';
    playBtn.title = 'Preview';
    playBtn.addEventListener('click', () => togglePreview(item.id, item.file));

    const removeBtn = document.createElement('button');
    removeBtn.className = 'secondary';
    removeBtn.textContent = '✕';
    removeBtn.addEventListener('click', () => {
      if (previewItemId === item.id) stopPreview();
      state.banks[b][v].splice(idx, 1);
      renderVoiceList(b, v);
      renderBankTabs();
    });

    actions.appendChild(playBtn);
    actions.appendChild(removeBtn);
    li.appendChild(label);
    li.appendChild(actions);
    list.appendChild(li);
  });
}

// ── file management ───────────────────────────────────────────────────────────

function addFiles(b, v, files) {
  if (!files.length) return;
  const room = MAX_FILES - state.banks[b][v].length;
  if (room <= 0) { setStatus(`Bank ${BANK_LETTERS[b]} Voice ${v + 1} is full.`); return; }
  const accepted = files.slice(0, room).map(f => ({ id: newId(), file: f }));
  state.banks[b][v].push(...accepted);
  renderBankTabs();
  renderVoiceList(b, v);
  const skipped = files.length - accepted.length;
  setStatus(skipped > 0
    ? `Added ${accepted.length} file(s) to Bank ${BANK_LETTERS[b]} / Voice ${v + 1}. ${skipped} skipped (full).`
    : `Added ${accepted.length} file(s) to Bank ${BANK_LETTERS[b]} / Voice ${v + 1}.`);
}

function moveItem(fromBank, fromVoice, toBank, toVoice, id) {
  const src = state.banks[fromBank][fromVoice];
  const dst = state.banks[toBank][toVoice];
  const idx = src.findIndex(x => x.id === id);
  if (idx < 0) return;
  if (dst.length >= MAX_FILES) { setStatus(`Bank ${BANK_LETTERS[toBank]} Voice ${toVoice + 1} is full.`); return; }
  const [item] = src.splice(idx, 1);
  dst.push(item);
  renderBankTabs();
  renderCurrentBank();
  setStatus(`Moved "${item.file.name}" → Bank ${BANK_LETTERS[toBank]} / Voice ${toVoice + 1}.`);
}

// ── preview ───────────────────────────────────────────────────────────────────

function stopPreview() {
  if (previewAudio) { previewAudio.pause(); previewAudio.currentTime = 0; }
  if (previewUrl)   URL.revokeObjectURL(previewUrl);
  previewAudio = previewItemId = previewUrl = null;
}

async function togglePreview(itemId, file) {
  if (previewItemId === itemId) { stopPreview(); setStatus('Preview stopped.'); return; }
  stopPreview();
  previewUrl   = URL.createObjectURL(file);
  previewAudio = new Audio(previewUrl);
  previewItemId = itemId;
  previewAudio.onended = stopPreview;
  try {
    await previewAudio.play();
    setStatus(`Preview: ${file.name}`);
  } catch (err) {
    stopPreview();
    setStatus(`Preview failed: ${err.message}`);
  }
}

// ── DSP helpers ───────────────────────────────────────────────────────────────

function mixToMono(buf) {
  const ch = buf.numberOfChannels, n = buf.length;
  const out = new Float32Array(n);
  if (ch === 1) { out.set(buf.getChannelData(0)); return out; }
  for (let c = 0; c < ch; c++) {
    const src = buf.getChannelData(c);
    for (let i = 0; i < n; i++) out[i] += src[i];
  }
  const inv = 1 / ch;
  for (let i = 0; i < n; i++) out[i] *= inv;
  return out;
}

function peakOf(s) {
  let p = 0;
  for (let i = 0; i < s.length; i++) { const v = Math.abs(s[i]); if (v > p) p = v; }
  return p;
}

function trimSilence(s) {
  let lo = 0, hi = s.length - 1;
  while (lo < s.length && Math.abs(s[lo]) < SIL_THRESHOLD) lo++;
  while (hi > lo      && Math.abs(s[hi]) < SIL_THRESHOLD) hi--;
  if (lo >= hi) return s;
  return s.slice(Math.max(0, lo - 64), Math.min(s.length, hi + 65));
}

function normalize(s, target = 0.95) {
  const pk = peakOf(s);
  if (pk < 1e-6) return s;
  const g = target / pk;
  const out = new Float32Array(s.length);
  for (let i = 0; i < s.length; i++) out[i] = s[i] * g;
  return out;
}

// Band-limited resample via the browser's own resampler (anti-aliased).
async function resample(s, srcRate, dstRate) {
  if (srcRate === dstRate) return s;
  const n   = Math.max(1, Math.round(s.length * dstRate / srcRate));
  const off = new OfflineAudioContext(1, n, dstRate);
  const buf = off.createBuffer(1, s.length, srcRate);
  buf.copyToChannel(s, 0);
  const src = off.createBufferSource();
  src.buffer = buf;
  src.connect(off.destination);
  src.start();
  return (await off.startRendering()).getChannelData(0);
}

function encodeWav(s, rate) {
  const bps = 2, data = s.length * bps;
  const buf = new ArrayBuffer(44 + data);
  const v   = new DataView(buf);
  const w   = (o, t) => { for (let i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); };
  w(0, 'RIFF'); v.setUint32(4, 36 + data, true);
  w(8, 'WAVE'); w(12, 'fmt ');
  v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, rate, true); v.setUint32(28, rate * bps, true);
  v.setUint16(32, bps, true); v.setUint16(34, 16, true);
  w(36, 'data'); v.setUint32(40, data, true);
  let o = 44;
  for (let i = 0; i < s.length; i++) {
    const x = Math.max(-1, Math.min(1, s[i]));
    v.setInt16(o, x < 0 ? Math.round(x * 32768) : Math.round(x * 32767), true);
    o += 2;
  }
  return buf;
}

async function convertFile(file, opts) {
  const ctx  = ensureCtx();
  const ab   = await file.arrayBuffer();
  const dec  = await ctx.decodeAudioData(ab.slice(0));
  let   mono = mixToMono(dec);
  const pkBefore = peakOf(mono);
  if (opts.trim)      mono = trimSilence(mono);
  if (opts.normalize) mono = normalize(mono, 0.95);
  const rs       = await resample(mono, dec.sampleRate, TARGET_RATE);
  const pkAfter  = peakOf(rs);
  const duration = rs.length / TARGET_RATE;
  const warnings = [];
  if (pkAfter >= 0.999) warnings.push('clip-risk');
  if (duration < 0.03)  warnings.push('very-short');
  return { wavBuffer: encodeWav(rs, TARGET_RATE), report: { sourceName: file.name, duration, pkBefore, pkAfter, warnings } };
}

function getOpts() {
  return {
    trim:      document.getElementById('trimToggle').checked,
    normalize: document.getElementById('normalizeToggle').checked
  };
}

// ── report ────────────────────────────────────────────────────────────────────

function renderReport(rows) {
  const body    = document.getElementById('reportBody');
  const summary = document.getElementById('reportSummary');
  body.innerHTML = '';
  if (!rows.length) { summary.textContent = 'No export report yet.'; return; }
  let clips = 0, shorts = 0;
  rows.forEach(r => {
    if (r.report.warnings.includes('clip-risk'))  clips++;
    if (r.report.warnings.includes('very-short')) shorts++;
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${r.path}</td>
      <td title="${r.report.sourceName}">${r.report.sourceName}</td>
      <td>${r.report.duration.toFixed(3)}</td>
      <td>${r.report.pkBefore.toFixed(3)}</td>
      <td>${r.report.pkAfter.toFixed(3)}</td>
      <td>${r.report.warnings.length ? r.report.warnings.join(', ') : '—'}</td>`;
    body.appendChild(tr);
  });
  summary.textContent = `Exported ${rows.length} file(s). Warnings: ${clips} clip-risk, ${shorts} very-short.`;
}

// ── ZIP builder ───────────────────────────────────────────────────────────────

async function buildZip() {
  let total = 0;
  for (const bank of state.banks) for (const voice of bank) total += voice.length;
  if (total === 0) { setStatus('No files loaded.'); return; }

  const opts    = getOpts();
  const zip     = new JSZip();
  const report  = [];
  let done = 0;

  for (let b = 0; b < BANK_LETTERS.length; b++) {
    for (let v = 0; v < VOICE_LABELS.length; v++) {
      const files = state.banks[b][v];
      for (let i = 0; i < files.length; i++) {
        const name    = `${String(i + 1).padStart(2, '0')}.wav`;
        const zipPath = `${BANK_LETTERS[b]}/${v + 1}/${name}`;
        setStatus(`Converting ${zipPath}…`, Math.round(done / total * 100));
        const conv = await convertFile(files[i].file, opts);
        zip.file(zipPath, conv.wavBuffer);
        report.push({ path: zipPath, report: conv.report });
        done++;
      }
    }
  }

  setStatus('Packaging ZIP…', 100);
  const blob = await zip.generateAsync({ type: 'blob' });
  const url  = URL.createObjectURL(blob);
  const a    = document.createElement('a');
  a.href = url; a.download = 'radiomusic32-library.zip';
  document.body.appendChild(a); a.click(); a.remove();
  URL.revokeObjectURL(url);
  renderReport(report);
  setStatus('Done. ZIP downloaded.', 100);
}

// ── clear ─────────────────────────────────────────────────────────────────────

function clearAll() {
  stopPreview();
  for (let b = 0; b < BANK_LETTERS.length; b++) {
    for (let v = 0; v < VOICE_LABELS.length; v++) {
      state.banks[b][v] = [];
    }
  }
  renderBankTabs();
  renderCurrentBank();
  renderReport([]);
  setStatus('Cleared all banks.', 0);
}

// ── init ──────────────────────────────────────────────────────────────────────

function init() {
  renderBankTabs();
  renderCurrentBank();

  document.getElementById('downloadZip').addEventListener('click', async () => {
    const btn = document.getElementById('downloadZip');
    btn.disabled = true;
    try { await buildZip(); }
    catch (err) { console.error(err); setStatus(`Error: ${err.message}`, 0); }
    finally { btn.disabled = false; }
  });

  document.getElementById('clearAll').addEventListener('click', clearAll);
}

init();
