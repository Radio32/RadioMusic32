'use strict';

// RadioMusic32 Sample Manager. Card layout: <bank A–P>/<voice 1|2>/01.wav … 16.wav

const BANKS     = Array.from('ABCDEFGHIJKLMNOP');
const VOICES    = [
  { title: 'Sampler',   sub: 'CV 1 triggers' },
  { title: 'Chromatic', sub: 'CV 2 triggers · CV 3 V/oct' }
];
const MAX_FILES     = 16;
const TARGET_RATE   = 44100;   // firmware output rate: no rate conversion on voice 1
const SIL_THRESHOLD = 0.003;
const AUDIO_EXT     = /\.(wav|wave|aif|aiff|aifc|flac|mp3|ogg|oga|opus|m4a|aac|caf)$/i;

const state = { bank: 0, banks: emptyBanks() };
const ui    = { voice: 0, sel: new Set(), anchor: null, cursor: null };
let undoStack = [], redoStack = [], clipboard = [];
let drag = null;            // { bank, voice, ids } while dragging rows
let springTimer = null;
const metaCache = new WeakMap();   // File -> { dur, ch, rate, peaks } | { error } | 'pending'

const $    = id => document.getElementById(id);
const pad2 = n => String(n).padStart(2, '0');
const plural = (n, w) => `${n} ${w}${n === 1 ? '' : 's'}`;
const where  = (b, v) => `${BANKS[b]}/${v + 1}`;
const list   = (b, v) => state.banks[b][v];
const room   = (b, v) => MAX_FILES - list(b, v).length;
let nextId = 1;
const newItem = file => ({ id: 's' + (nextId++), file });

function emptyBanks() { return BANKS.map(() => [[], []]); }
function total() { return state.banks.reduce((n, b) => n + b[0].length + b[1].length, 0); }

function setStatus(text, kind = '', progress = null) {
  const el = $('statusText');
  el.textContent = text;
  el.className = 'status' + (kind ? ' ' + kind : '');
  if (progress !== null) $('progress').value = progress;
}

// ── edits (all undoable) ──────────────────────────────────────────────────────

function snapshot() { return state.banks.map(b => b.map(v => v.slice())); }

// fn mutates state and returns a status message. Checks must happen before calling.
function edit(fn) {
  const before = snapshot();
  const msg = fn();
  undoStack.push(before);
  if (undoStack.length > 100) undoStack.shift();
  redoStack = [];
  render();
  if (msg) setStatus(msg);
}

function undo() {
  if (!undoStack.length) return;
  redoStack.push(snapshot());
  state.banks = undoStack.pop();
  pruneSel(); render(); setStatus('Undone.');
}

function redo() {
  if (!redoStack.length) return;
  undoStack.push(snapshot());
  state.banks = redoStack.pop();
  pruneSel(); render(); setStatus('Redone.');
}

function selectItems(b, v, items) {
  if (b !== state.bank || !items.length) { ui.sel.clear(); ui.cursor = ui.anchor = null; return; }
  ui.voice = v;
  ui.sel = new Set(items.map(i => i.id));
  ui.anchor = items[0].id;
  ui.cursor = items[items.length - 1].id;
}

function pruneSel() {
  const ids = new Set(list(state.bank, ui.voice).map(i => i.id));
  for (const id of ui.sel) if (!ids.has(id)) ui.sel.delete(id);
  if (!ids.has(ui.cursor)) ui.cursor = null;
  if (!ids.has(ui.anchor)) ui.anchor = null;
}

function selectedItems() {
  return list(state.bank, ui.voice).filter(i => ui.sel.has(i.id));
}

function isAudio(f) { return AUDIO_EXT.test(f.name) || (f.type || '').startsWith('audio/'); }

function addFiles(b, v, files, at = null) {
  const audio = files.filter(isAudio);
  if (!audio.length) { setStatus('No audio files in that.', 'err'); return; }
  const free = room(b, v);
  if (free <= 0) { setStatus(`${where(b, v)} is full (16 samples).`, 'err'); return; }
  const items = audio.slice(0, free).map(newItem);
  const skipped = audio.length - items.length;
  edit(() => {
    const dst = list(b, v);
    dst.splice(at === null ? dst.length : Math.min(at, dst.length), 0, ...items);
    selectItems(b, v, items);
    return `Added ${plural(items.length, 'sample')} to ${where(b, v)}.` + (skipped ? ` ${skipped} left out: a voice holds 16.` : '');
  });
  items.forEach(i => queueMeta(i.file));
}

// Move (or copy) ids from one voice to a position in another, or within the same one.
function transfer(fromB, fromV, ids, toB, toV, at = null, copy = false) {
  const idSet = new Set(ids);
  const src = list(fromB, fromV);
  const moving = src.filter(i => idSet.has(i.id));
  if (!moving.length) return;
  const same = fromB === toB && fromV === toV;
  if ((copy || !same) && room(toB, toV) < moving.length) {
    const free = room(toB, toV);
    setStatus(`${where(toB, toV)} has room for ${free ? plural(free, 'more sample') : 'no more samples'}; ${moving.length} selected.`, 'err');
    return;
  }
  edit(() => {
    let items = moving;
    if (copy) {
      items = moving.map(i => newItem(i.file));
    } else {
      if (same && at !== null) at -= src.slice(0, at).filter(i => idSet.has(i.id)).length;
      state.banks[fromB][fromV] = src.filter(i => !idSet.has(i.id));
    }
    const dst = list(toB, toV);
    dst.splice(at === null ? dst.length : Math.min(at, dst.length), 0, ...items);
    if (copy || toB === state.bank) selectItems(toB, toV, items);
    else pruneSel();
    if (same && !copy) return `Moved ${plural(items.length, 'sample')}.`;
    return `${copy ? 'Copied' : 'Moved'} ${plural(items.length, 'sample')} to ${where(toB, toV)}.`;
  });
}

function nudge(dir) {
  const b = state.bank, v = ui.voice, arr = list(b, v);
  const idx = arr.map((it, i) => ui.sel.has(it.id) ? i : -1).filter(i => i >= 0);
  if (!idx.length) return;
  if (dir < 0 && idx[0] === 0) return;
  if (dir > 0 && idx[idx.length - 1] === arr.length - 1) return;
  edit(() => {
    const a = list(b, v);
    for (const i of dir < 0 ? idx : idx.slice().reverse()) [a[i], a[i + dir]] = [a[i + dir], a[i]];
    return '';
  });
}

function removeSelected() {
  const items = selectedItems();
  if (!items.length) return;
  const b = state.bank, v = ui.voice;
  edit(() => {
    if (items.some(i => i.id === preview.id)) stopPreview();
    state.banks[b][v] = list(b, v).filter(i => !ui.sel.has(i.id));
    ui.sel.clear(); ui.cursor = ui.anchor = null;
    return `Removed ${plural(items.length, 'sample')} from ${where(b, v)}. ⌘Z brings ${items.length === 1 ? 'it' : 'them'} back.`;
  });
}

function copySelected(cut) {
  const items = selectedItems();
  if (!items.length) return;
  clipboard = items.map(i => i.file);
  if (cut) {
    const b = state.bank, v = ui.voice;
    edit(() => {
      state.banks[b][v] = list(b, v).filter(i => !ui.sel.has(i.id));
      ui.sel.clear(); ui.cursor = ui.anchor = null;
      return `Cut ${plural(items.length, 'sample')}. Pick a bank and voice, then paste (⌘V).`;
    });
  } else {
    setStatus(`Copied ${plural(items.length, 'sample')}. Paste (⌘V) in any bank.`);
  }
}

function paste() {
  if (!clipboard.length) return;
  const b = state.bank, v = ui.voice, arr = list(b, v);
  if (room(b, v) < clipboard.length) {
    setStatus(`${where(b, v)} has room for ${room(b, v)}; the clipboard holds ${clipboard.length}.`, 'err');
    return;
  }
  let at = arr.length;
  arr.forEach((it, i) => { if (ui.sel.has(it.id)) at = i + 1; });
  const items = clipboard.map(newItem);
  edit(() => {
    list(b, v).splice(at, 0, ...items);
    selectItems(b, v, items);
    return `Pasted ${plural(items.length, 'sample')} into ${where(b, v)}.`;
  });
}

function clearAll() {
  if (!total()) return;
  confirmBox(`Clear all ${plural(total(), 'sample')} from every bank?`, [
    { label: 'Clear all', cls: 'danger', run: () => edit(() => {
      stopPreview();
      state.banks = emptyBanks();
      ui.sel.clear(); ui.cursor = ui.anchor = null;
      return 'Cleared. ⌘Z brings the layout back.';
    }) },
    { label: 'Cancel' }
  ]);
}

// ── open an existing card ─────────────────────────────────────────────────────

function importCard(files) {
  const found = emptyBanks();
  for (const f of files) {
    const parts = (f.webkitRelativePath || f.name).split('/');
    if (parts.length < 3) continue;
    const name = parts[parts.length - 1];
    if (name.startsWith('.')) continue;          // macOS ._ resource files
    const b = BANKS.indexOf(parts[parts.length - 3].toUpperCase());
    const v = ['1', '2'].indexOf(parts[parts.length - 2]);
    if (b < 0 || v < 0 || !/\.wav$/i.test(name)) continue;
    found[b][v].push(f);
  }
  let count = 0, over = 0;
  // Same byte-wise name order the firmware sorts by
  for (const bank of found) for (let v = 0; v < 2; v++) {
    bank[v].sort((x, y) => (x.name < y.name ? -1 : x.name > y.name ? 1 : 0));
    over += Math.max(0, bank[v].length - MAX_FILES);
    bank[v] = bank[v].slice(0, MAX_FILES);
    count += bank[v].length;
  }
  if (!count) { setStatus('No A/1 … P/2 folders with .wav files in that folder. Pick the card itself (its root).', 'err'); return; }

  const apply = replace => {
    let added = 0, left = over;
    const newOnes = [];
    edit(() => {
      found.forEach((bank, b) => bank.forEach((files, v) => {
        if (replace) state.banks[b][v] = [];
        const take = files.slice(0, room(b, v));
        left += files.length - take.length;
        const items = take.map(newItem);
        state.banks[b][v].push(...items);
        newOnes.push(...items);
        added += items.length;
      }));
      if (replace) state.bank = Math.max(0, state.banks.findIndex(bk => bk[0].length || bk[1].length));
      ui.sel.clear(); ui.cursor = ui.anchor = null;
      return `Opened ${plural(added, 'sample')} from the card.` + (left ? ` ${left} left out: a voice holds 16.` : '');
    });
    newOnes.forEach(i => queueMeta(i.file));
  };

  if (!total()) apply(true);
  else confirmBox(`The card has ${plural(count, 'sample')}. Replace what's here, or add them after it?`, [
    { label: 'Replace', cls: 'primary', run: () => apply(true) },
    { label: 'Add', run: () => apply(false) },
    { label: 'Cancel' }
  ]);
}

// ── dialogs ───────────────────────────────────────────────────────────────────

function closeDialog() { const d = $('dialog'); d.hidden = true; d.innerHTML = ''; }

function confirmBox(text, buttons) {
  const d = $('dialog');
  d.innerHTML = '';
  const span = document.createElement('span');
  span.textContent = text;
  d.appendChild(span);
  buttons.forEach(b => {
    const btn = document.createElement('button');
    btn.className = 'btn' + (b.cls ? ' ' + b.cls : '');
    btn.textContent = b.label;
    btn.addEventListener('click', () => { closeDialog(); if (b.run) b.run(); });
    d.appendChild(btn);
  });
  d.hidden = false;
  d.querySelector('.btn').focus();
}

function moveDialog() {
  const items = selectedItems();
  if (!items.length) { setStatus('Select samples first.'); return; }
  const fromB = state.bank, fromV = ui.voice, ids = items.map(i => i.id);
  const d = $('dialog');
  d.innerHTML = `
    <span>${plural(items.length, 'sample')} to</span>
    <select id="dlgBank" aria-label="Bank">${BANKS.map((l, i) =>
      `<option value="${i}"${i === fromB ? ' selected' : ''}>Bank ${l} (${list(i, 0).length} · ${list(i, 1).length})</option>`).join('')}</select>
    <select id="dlgVoice" aria-label="Voice">${VOICES.map((vc, i) =>
      `<option value="${i}"${i === fromV ? ' selected' : ''}>Voice ${i + 1} · ${vc.title}</option>`).join('')}</select>
    <button class="btn primary" id="dlgMove">Move</button>
    <button class="btn" id="dlgCopy">Copy</button>
    <button class="btn" id="dlgCancel">Cancel</button>`;
  d.hidden = false;
  const go = copy => {
    const toB = +$('dlgBank').value, toV = +$('dlgVoice').value;
    closeDialog();
    transfer(fromB, fromV, ids, toB, toV, null, copy);
  };
  $('dlgMove').addEventListener('click', () => go(false));
  $('dlgCopy').addEventListener('click', () => go(true));
  $('dlgCancel').addEventListener('click', closeDialog);
  $('dlgBank').focus();
}

// ── rendering ─────────────────────────────────────────────────────────────────

function render() {
  renderTabs();
  renderPanes();
  renderSummary();
  $('undoBtn').disabled = !undoStack.length;
  $('redoBtn').disabled = !redoStack.length;
}

function switchBank(b) {
  if (b === state.bank) return;
  state.bank = b;
  ui.sel.clear(); ui.cursor = ui.anchor = null;
  renderTabs(); renderPanes();
}

function renderTabs() {
  const nav = $('bankTabs');
  nav.innerHTML = '';
  BANKS.forEach((letter, b) => {
    const n1 = list(b, 0).length, n2 = list(b, 1).length;
    const btn = document.createElement('button');
    btn.className = 'tab' + (b === state.bank ? ' on' : '') + (n1 || n2 ? ' has' : '');
    btn.innerHTML = `<span>${letter}</span><span class="n">${n1 || n2 ? `${n1}·${n2}` : ''}</span>`;
    btn.title = `Bank ${letter}: voice 1 ${n1}, voice 2 ${n2}`;
    if (b === state.bank) btn.setAttribute('aria-current', 'true');
    btn.addEventListener('click', () => switchBank(b));

    // Drop on a tab: into that bank, same voice. Hovering a while opens the bank.
    btn.addEventListener('dragover', e => {
      if (!drag && !hasFiles(e)) return;
      e.preventDefault();
      e.dataTransfer.dropEffect = drag && !e.altKey ? 'move' : 'copy';
      btn.classList.add('over');
    });
    btn.addEventListener('dragenter', () => {
      clearTimeout(springTimer);
      springTimer = setTimeout(() => switchBank(b), 700);
    });
    btn.addEventListener('dragleave', () => { btn.classList.remove('over'); clearTimeout(springTimer); });
    btn.addEventListener('drop', e => {
      e.preventDefault();
      clearTimeout(springTimer);
      btn.classList.remove('over');
      if (hasFiles(e)) {
        addFiles(b, ui.voice, Array.from(e.dataTransfer.files));
      } else if (drag) {
        const d = drag; endDrag();
        if (d.bank === b && !e.altKey) return;
        transfer(d.bank, d.voice, d.ids, b, d.voice, null, e.altKey);
      }
    });
    nav.appendChild(btn);
  });
}

function renderSummary() {
  const n = total();
  const banks = state.banks.filter(b => b[0].length || b[1].length).length;
  let secs = 0, known = true;
  state.banks.forEach(b => b.forEach(v => v.forEach(i => {
    const m = metaCache.get(i.file);
    if (m && m.dur) secs += m.dur; else known = false;
  })));
  const mb = secs * TARGET_RATE * 2 / 1e6;
  $('cardSum').textContent = n
    ? `${plural(n, 'sample')} · ${plural(banks, 'bank')} · ${known ? '' : '≥ '}${mb.toFixed(1)} MB as WAV`
    : 'Empty card';
}

function renderPanes() {
  const host = $('panes');
  host.innerHTML = '';
  const b = state.bank;
  VOICES.forEach((vc, v) => {
    const items = list(b, v);
    const pane = document.createElement('section');
    pane.className = 'lpane' + (ui.voice === v ? ' active' : '');
    pane.dataset.voice = v;
    pane.innerHTML = `
      <div class="lpane-head">
        <div class="lpane-title">
          <span class="voice">${v + 1}</span>
          <div><h2>${vc.title}</h2><div class="lpane-sub">${vc.sub}</div></div>
        </div>
        <span class="lpane-path">${where(b, v)}/ · ${items.length}/16</span>
      </div>
      <div class="btnrow ltools">
        <label class="btn small">Add…<input type="file" multiple accept="audio/*,.wav,.aif,.aiff,.flac,.mp3,.ogg,.m4a" hidden></label>
        <button class="btn small" data-act="up" title="Move up (⌥↑)" aria-label="Move up">↑</button>
        <button class="btn small" data-act="down" title="Move down (⌥↓)" aria-label="Move down">↓</button>
        <button class="btn small" data-act="move" title="Move or copy to another bank or voice">Move to…</button>
        <button class="btn small danger" data-act="remove" title="Remove (⌫)">Remove</button>
      </div>
      <ul class="llist" tabindex="0" role="listbox" aria-multiselectable="true" aria-label="Bank ${BANKS[b]}, voice ${v + 1}"></ul>`;

    const ul = pane.querySelector('.llist');
    items.forEach((item, i) => ul.appendChild(rowFor(item, i)));
    if (!items.length) {
      ul.innerHTML = `<li class="lempty">Drop audio files or samples here, or use Add…</li>`;
    } else if (items.length < MAX_FILES) {
      const li = document.createElement('li');
      li.className = 'lfree';
      li.textContent = `${MAX_FILES - items.length} free`;
      ul.appendChild(li);
    }

    pane.querySelector('input[type=file]').addEventListener('change', e => {
      addFiles(b, v, Array.from(e.target.files || []));
      e.target.value = '';
    });
    pane.querySelector('.ltools').addEventListener('click', e => {
      const act = e.target.closest('[data-act]')?.dataset.act;
      if (!act) return;
      activate(v);
      if (act === 'up') nudge(-1);
      else if (act === 'down') nudge(1);
      else if (act === 'move') moveDialog();
      else if (act === 'remove') removeSelected();
    });
    pane.addEventListener('mousedown', () => activate(v));
    wireList(ul, b, v);
    host.appendChild(pane);
  });
  updateSelection();
  drawWaves();
}

function rowFor(item, i) {
  const li = document.createElement('li');
  li.className = 'lrow';
  li.dataset.id = item.id;
  li.draggable = true;
  li.setAttribute('role', 'option');
  li.innerHTML = `
    <span class="lno">${pad2(i + 1)}</span>
    <span class="lname"></span>
    <span class="lmeta"></span>
    <canvas class="lmini" width="120" height="32" aria-hidden="true"></canvas>
    <button class="btn small lplay" aria-label="Play">▶</button>`;
  const name = li.querySelector('.lname');
  name.textContent = item.file.name;
  name.title = item.file.name;
  fillMeta(li, item.file);
  return li;
}

function fillMeta(li, file) {
  const m = metaCache.get(file);
  const el = li.querySelector('.lmeta');
  el.classList.toggle('err', !!(m && m.error));
  if (!m || m === 'pending') { el.textContent = '…'; return; }
  if (m.error) { el.textContent = m.error; return; }
  const bits = [fmtDur(m.dur)];
  if (m.ch > 1) bits.push('st');
  if (m.rate && m.rate !== TARGET_RATE) bits.push(`${Math.round(m.rate / 100) / 10}k`);
  el.textContent = bits.join(' · ');
  el.title = `${m.dur.toFixed(2)} s, ${m.ch > 1 ? 'stereo (mixed to mono on export)' : 'mono'}` +
             (m.rate ? `, ${m.rate} Hz` : '');
}

function fmtDur(s) {
  if (s < 10) return s.toFixed(2) + 's';
  const m = Math.floor(s / 60), r = s - m * 60;
  return m ? `${m}:${r.toFixed(0).padStart(2, '0')}` : s.toFixed(1) + 's';
}

function drawWaves() {
  const css = getComputedStyle(document.documentElement);
  const fg = css.getPropertyValue('--wave').trim();
  const dpr = window.devicePixelRatio || 1;
  document.querySelectorAll('.lrow').forEach(li => {
    const item = findItem(li.dataset.id);
    const c = li.querySelector('canvas');
    const m = item && metaCache.get(item.file);
    if (!c || !m || !m.peaks || c.offsetParent === null) return;
    const w = c.clientWidth, h = c.clientHeight;
    c.width = w * dpr; c.height = h * dpr;
    const g = c.getContext('2d');
    g.scale(dpr, dpr);
    g.fillStyle = fg;
    const n = m.peaks.length, bw = w / n;
    for (let i = 0; i < n; i++) {
      const a = Math.max(1, m.peaks[i] * (h - 4));
      g.fillRect(i * bw, (h - a) / 2, Math.max(1, bw - .4), a);
    }
  });
}

function findItem(id) {
  for (const bank of state.banks) for (const v of bank) for (const i of v) if (i.id === id) return i;
  return null;
}

function updateSelection() {
  document.querySelectorAll('.lpane').forEach(pane => {
    const v = +pane.dataset.voice;
    const mine = v === ui.voice;
    pane.classList.toggle('active', mine);
    pane.querySelectorAll('.lrow').forEach(li => {
      const on = mine && ui.sel.has(li.dataset.id);
      li.classList.toggle('on', on);
      li.classList.toggle('cursor', mine && ui.cursor === li.dataset.id);
      li.setAttribute('aria-selected', on ? 'true' : 'false');
      li.querySelector('.lplay').classList.toggle('on', preview.id === li.dataset.id);
      li.querySelector('.lplay').textContent = preview.id === li.dataset.id ? '■' : '▶';
    });
    const any = mine && ui.sel.size > 0;
    pane.querySelectorAll('[data-act]').forEach(btn => { btn.disabled = !any; });
  });
}

function activate(v) {
  if (ui.voice === v) return;
  ui.voice = v;
  ui.sel.clear(); ui.cursor = ui.anchor = null;
  updateSelection();
}

// ── list interaction: select, drag, drop ──────────────────────────────────────

function hasFiles(e) { return Array.from(e.dataTransfer?.types || []).includes('Files'); }

function endDrag() {
  drag = null;
  clearTimeout(springTimer);
  document.querySelectorAll('.dragging').forEach(el => el.classList.remove('dragging'));
  clearDropMarks();
}

function clearDropMarks() {
  document.querySelectorAll('.lrow.before, .lrow.after').forEach(el => el.classList.remove('before', 'after'));
  document.querySelectorAll('.llist.over').forEach(el => el.classList.remove('over'));
}

function dropIndex(ul, y) {
  const rows = ul.querySelectorAll('.lrow');
  for (let i = 0; i < rows.length; i++) {
    const r = rows[i].getBoundingClientRect();
    if (y < r.top + r.height / 2) return i;
  }
  return rows.length;
}

function wireList(ul, b, v) {
  ul.addEventListener('click', e => {
    const li = e.target.closest('.lrow');
    if (!li) return;
    if (e.target.closest('.lplay')) { togglePreview(findItem(li.dataset.id)); return; }
    clickRow(v, li.dataset.id, e);
  });
  ul.addEventListener('dblclick', e => {
    const li = e.target.closest('.lrow');
    if (li && !e.target.closest('.lplay')) togglePreview(findItem(li.dataset.id));
  });
  ul.addEventListener('focus', () => activate(v));

  ul.addEventListener('dragstart', e => {
    const li = e.target.closest('.lrow');
    if (!li) return;
    activate(v);
    if (!ui.sel.has(li.dataset.id)) { ui.sel = new Set([li.dataset.id]); ui.anchor = ui.cursor = li.dataset.id; updateSelection(); }
    const ids = list(b, v).filter(i => ui.sel.has(i.id)).map(i => i.id);
    drag = { bank: b, voice: v, ids };
    e.dataTransfer.effectAllowed = 'copyMove';
    e.dataTransfer.setData('text/plain', ids.map(id => findItem(id).file.name).join('\n'));
    ul.querySelectorAll('.lrow').forEach(r => { if (ui.sel.has(r.dataset.id)) r.classList.add('dragging'); });
  });
  ul.addEventListener('dragend', endDrag);

  ul.addEventListener('dragover', e => {
    if (!drag && !hasFiles(e)) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = drag && !e.altKey ? 'move' : 'copy';
    clearDropMarks();
    ul.classList.add('over');
    const at = dropIndex(ul, e.clientY);
    const rows = ul.querySelectorAll('.lrow');
    if (at < rows.length) rows[at].classList.add('before');
    else if (rows.length) rows[rows.length - 1].classList.add('after');
  });
  ul.addEventListener('dragleave', e => {
    if (!ul.contains(e.relatedTarget)) clearDropMarks();
  });
  ul.addEventListener('drop', e => {
    e.preventDefault();
    const at = dropIndex(ul, e.clientY);
    clearDropMarks();
    if (hasFiles(e)) {
      addFiles(b, v, Array.from(e.dataTransfer.files), at);
    } else if (drag) {
      const d = drag; endDrag();
      transfer(d.bank, d.voice, d.ids, b, v, at, e.altKey);
    }
  });
}

function clickRow(v, id, e) {
  activate(v);
  const arr = list(state.bank, v).map(i => i.id);
  const mod = e.metaKey || e.ctrlKey;
  if (e.shiftKey && ui.anchor && arr.includes(ui.anchor)) {
    const a = arr.indexOf(ui.anchor), z = arr.indexOf(id);
    const range = arr.slice(Math.min(a, z), Math.max(a, z) + 1);
    if (!mod) ui.sel.clear();
    range.forEach(x => ui.sel.add(x));
  } else if (mod) {
    if (ui.sel.has(id)) ui.sel.delete(id); else ui.sel.add(id);
    ui.anchor = id;
  } else {
    ui.sel = new Set([id]);
    ui.anchor = id;
  }
  ui.cursor = id;
  updateSelection();
}

function moveCursor(dir, extend) {
  const arr = list(state.bank, ui.voice).map(i => i.id);
  if (!arr.length) return;
  let i = arr.indexOf(ui.cursor);
  i = i < 0 ? (dir > 0 ? 0 : arr.length - 1) : Math.max(0, Math.min(arr.length - 1, i + dir));
  const id = arr[i];
  if (extend && ui.anchor) {
    const a = arr.indexOf(ui.anchor);
    ui.sel = new Set(arr.slice(Math.min(a, i), Math.max(a, i) + 1));
  } else {
    ui.sel = new Set([id]);
    ui.anchor = id;
  }
  ui.cursor = id;
  updateSelection();
  document.querySelector(`.lrow[data-id="${id}"]`)?.scrollIntoView({ block: 'nearest' });
}

// ── preview ───────────────────────────────────────────────────────────────────

const preview = { audio: null, url: null, id: null };

function stopPreview() {
  if (preview.audio) preview.audio.pause();
  if (preview.url) URL.revokeObjectURL(preview.url);
  preview.audio = preview.url = preview.id = null;
  updateSelection();
}

async function togglePreview(item) {
  if (!item) return;
  if (preview.id === item.id) { stopPreview(); return; }
  stopPreview();
  preview.url = URL.createObjectURL(item.file);
  preview.audio = new Audio(preview.url);
  preview.id = item.id;
  preview.audio.onended = stopPreview;
  updateSelection();
  try {
    await preview.audio.play();
  } catch (err) {
    stopPreview();
    setStatus(`Can't play ${item.file.name} in this browser.`, 'err');
  }
}

// ── keyboard ──────────────────────────────────────────────────────────────────

document.addEventListener('keydown', e => {
  if (e.target.closest('input, select, textarea')) return;
  const dialogOpen = !$('dialog').hidden;
  if (e.key === 'Escape') { if (dialogOpen) closeDialog(); else { ui.sel.clear(); updateSelection(); } return; }
  if (dialogOpen) return;
  const mod = e.metaKey || e.ctrlKey;
  const k = e.key.toLowerCase();
  let handled = true;
  if (mod && k === 'z') e.shiftKey ? redo() : undo();
  else if (mod && k === 'y') redo();
  else if (mod && k === 'a') { ui.sel = new Set(list(state.bank, ui.voice).map(i => i.id)); updateSelection(); }
  else if (mod && k === 'x') copySelected(true);
  else if (mod && k === 'c') copySelected(false);
  else if (mod && k === 'v') paste();
  else if (mod) handled = false;
  else if (e.altKey && e.key === 'ArrowUp') nudge(-1);
  else if (e.altKey && e.key === 'ArrowDown') nudge(1);
  else if (e.key === 'ArrowUp') moveCursor(-1, e.shiftKey);
  else if (e.key === 'ArrowDown') moveCursor(1, e.shiftKey);
  else if (e.key === 'ArrowLeft') activate(0);
  else if (e.key === 'ArrowRight') activate(1);
  else if (e.key === 'Backspace' || e.key === 'Delete') removeSelected();
  else if (e.key === ' ' && !e.target.closest('button')) {
    const id = ui.cursor || [...ui.sel][0];
    if (id) togglePreview(findItem(id)); else if (preview.id) stopPreview();
  }
  else if (e.key === '[') switchBank((state.bank + BANKS.length - 1) % BANKS.length);
  else if (e.key === ']') switchBank((state.bank + 1) % BANKS.length);
  else handled = false;
  if (handled) e.preventDefault();
});

// ── file info (duration, waveform) ────────────────────────────────────────────

const metaQueue = [];
let metaBusy = false;
let decodeCtx = null;

function decodeAudio(ab) {
  if (!decodeCtx) {
    const C = window.OfflineAudioContext || window.webkitOfflineAudioContext;
    // Decoding into a 44.1k context uses the browser's band-limited resampler
    decodeCtx = new C(1, 1, TARGET_RATE);
  }
  return new Promise((res, rej) => {
    const p = decodeCtx.decodeAudioData(ab, res, rej);
    if (p && p.then) p.then(res, rej);
  });
}

function wavRate(ab) {
  if (ab.byteLength < 28) return null;
  const v = new DataView(ab);
  if (v.getUint32(0) !== 0x52494646 || v.getUint32(8) !== 0x57415645) return null;   // RIFF / WAVE
  let o = 12;
  while (o + 8 <= ab.byteLength) {
    const id = v.getUint32(o), size = v.getUint32(o + 4, true);
    if (id === 0x666d7420 && o + 16 <= ab.byteLength) return v.getUint32(o + 12, true);   // 'fmt '
    o += 8 + size + (size & 1);
  }
  return null;
}

function peaksOf(buf, n) {
  const peaks = new Float32Array(n);
  const len = buf.length, step = Math.max(1, Math.floor(len / n / 64));
  for (let c = 0; c < buf.numberOfChannels; c++) {
    const d = buf.getChannelData(c);
    for (let i = 0; i < n; i++) {
      const a = Math.floor(i * len / n), z = Math.floor((i + 1) * len / n);
      let p = peaks[i];
      for (let j = a; j < z; j += step) { const x = Math.abs(d[j]); if (x > p) p = x; }
      peaks[i] = p;
    }
  }
  return peaks;
}

function queueMeta(file) {
  if (metaCache.has(file)) return;
  metaCache.set(file, 'pending');
  metaQueue.push(file);
  pumpMeta();
}

async function pumpMeta() {
  if (metaBusy) return;
  metaBusy = true;
  while (metaQueue.length) {
    const file = metaQueue.shift();
    try {
      const ab = await file.arrayBuffer();
      const rate = wavRate(ab);
      const buf = await decodeAudio(ab);
      metaCache.set(file, { dur: buf.duration, ch: buf.numberOfChannels, rate, peaks: peaksOf(buf, 60) });
    } catch (err) {
      metaCache.set(file, { error: "can't decode" });
    }
    document.querySelectorAll('.lrow').forEach(li => {
      const item = findItem(li.dataset.id);
      if (item && item.file === file) fillMeta(li, file);
    });
    drawWaves();
    renderSummary();
  }
  metaBusy = false;
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
  const ab   = await file.arrayBuffer();
  const dec  = await decodeAudio(ab);
  let   mono = mixToMono(dec);
  const pkBefore = peakOf(mono);
  if (opts.trim)      mono = trimSilence(mono);
  if (opts.normalize) mono = normalize(mono, 0.95);
  const rs       = await resample(mono, dec.sampleRate, TARGET_RATE);
  const pkAfter  = peakOf(rs);
  const duration = rs.length / TARGET_RATE;
  const notes = [];
  if (dec.numberOfChannels > 1) notes.push('stereo → mono');
  if (pkAfter >= 0.999) notes.push('may clip');
  if (duration < 0.03)  notes.push('very short');
  return { wavBuffer: encodeWav(rs, TARGET_RATE), report: { sourceName: file.name, duration, pkBefore, pkAfter, notes } };
}

// ── export ────────────────────────────────────────────────────────────────────

function renderReport(rows) {
  const body = $('reportBody');
  body.innerHTML = '';
  $('report').hidden = !rows.length;
  if (!rows.length) return;
  rows.forEach(r => {
    const tr = document.createElement('tr');
    tr.innerHTML = `<td></td><td class="src"></td><td class="num">${r.report.duration.toFixed(2)} s</td>
      <td class="num">${r.report.pkBefore.toFixed(3)}</td><td class="num">${r.report.pkAfter.toFixed(3)}</td><td></td>`;
    tr.cells[0].textContent = r.path;
    tr.cells[1].textContent = r.report.sourceName;
    tr.cells[1].title = r.report.sourceName;
    r.report.notes.forEach(n => {
      const pill = document.createElement('span');
      pill.className = 'pill';
      pill.textContent = n;
      tr.cells[5].append(pill, ' ');
    });
    body.appendChild(tr);
  });
  const flagged = rows.filter(r => r.report.notes.some(n => n !== 'stereo → mono')).length;
  $('reportSummary').textContent = `${plural(rows.length, 'file')}${flagged ? ` · ${flagged} to check` : ''}`;
}

async function buildZip() {
  const n = total();
  if (!n) { setStatus('Add some samples first.', 'err'); return; }
  const opts = { trim: $('trimToggle').checked, normalize: $('normalizeToggle').checked };
  const zip = new JSZip();
  const report = [];
  let done = 0;
  for (let b = 0; b < BANKS.length; b++) {
    for (let v = 0; v < VOICES.length; v++) {
      const items = list(b, v);
      for (let i = 0; i < items.length; i++) {
        const path = `${where(b, v)}/${pad2(i + 1)}.wav`;
        setStatus(`Converting ${path}…`, '', Math.round(done / n * 100));
        try {
          const conv = await convertFile(items[i].file, opts);
          zip.file(path, conv.wavBuffer);
          report.push({ path, report: conv.report });
        } catch (err) {
          throw new Error(`${items[i].file.name} (${path}) can't be decoded in this browser.`);
        }
        done++;
      }
    }
  }
  setStatus('Packaging ZIP…', '', 100);
  const blob = await zip.generateAsync({ type: 'blob' });
  const url  = URL.createObjectURL(blob);
  const a    = document.createElement('a');
  a.href = url; a.download = 'radiomusic32-samples.zip';
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
  renderReport(report);
  setStatus(`Done: ${plural(n, 'sample')}. Unzip to the card's root, replacing its bank folders.`, 'ok', 100);
}

// ── init ──────────────────────────────────────────────────────────────────────

function init() {
  render();

  $('addInput').addEventListener('change', e => {
    addFiles(state.bank, ui.voice, Array.from(e.target.files || []));
    e.target.value = '';
  });
  $('cardInput').addEventListener('change', e => {
    importCard(Array.from(e.target.files || []));
    e.target.value = '';
  });
  $('undoBtn').addEventListener('click', undo);
  $('redoBtn').addEventListener('click', redo);
  $('clearAll').addEventListener('click', clearAll);
  $('downloadZip').addEventListener('click', async () => {
    const btn = $('downloadZip');
    btn.disabled = true;
    try { await buildZip(); }
    catch (err) { console.error(err); setStatus(`Error: ${err.message}`, 'err', 0); }
    finally { btn.disabled = false; }
  });

  // Dropping outside a list or tab does nothing (and doesn't open the file)
  document.addEventListener('dragover', e => { if (drag || hasFiles(e)) e.preventDefault(); });
  document.addEventListener('drop', e => { e.preventDefault(); endDrag(); });
  // A drag whose source row was re-rendered away never gets dragend
  document.addEventListener('mousedown', () => { if (drag) endDrag(); });
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener?.('change', drawWaves);
  window.addEventListener('resize', drawWaves);
  window.addEventListener('beforeunload', e => { if (total()) { e.preventDefault(); e.returnValue = ''; } });
}

init();
