const FOLDERS = [1, 2, 3];
const MAX_FILES = 16;
const TARGET_RATE = 32000;
const SILENCE_THRESHOLD = 0.003;

const folderState = {
  1: [],
  2: [],
  3: []
};

let audioContext = null;
let previewAudio = null;
let previewItemId = null;
let previewUrl = null;

function newItemId() {
  if (window.crypto && window.crypto.randomUUID) {
    return window.crypto.randomUUID();
  }
  return `${Date.now()}-${Math.floor(Math.random() * 1e9)}`;
}

function ensureAudioContext() {
  if (!audioContext) {
    const Ctx = window.AudioContext || window.webkitAudioContext;
    if (!Ctx) {
      throw new Error("AudioContext is not supported in this browser.");
    }
    audioContext = new Ctx();
  }
  return audioContext;
}

function setStatus(text, progress = null) {
  document.getElementById("statusText").textContent = text;
  if (progress !== null) {
    document.getElementById("progress").value = progress;
  }
}

function maxPeak(samples) {
  let peak = 0;
  for (let i = 0; i < samples.length; i++) {
    const v = Math.abs(samples[i]);
    if (v > peak) peak = v;
  }
  return peak;
}

function renderReport(rows) {
  const body = document.getElementById("reportBody");
  const summary = document.getElementById("reportSummary");
  body.innerHTML = "";

  if (!rows.length) {
    summary.textContent = "No export report yet.";
    return;
  }

  let clippedCount = 0;
  let shortCount = 0;

  rows.forEach((row) => {
    if (row.warnings.includes("clip-risk")) clippedCount++;
    if (row.warnings.includes("very-short")) shortCount++;

    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td>${row.folder}</td>
      <td>${row.slot}</td>
      <td title="${row.sourceName}">${row.sourceName}</td>
      <td>${row.duration.toFixed(3)}</td>
      <td>${row.peakBefore.toFixed(3)}</td>
      <td>${row.peakAfter.toFixed(3)}</td>
      <td>${row.warnings.length ? row.warnings.join(", ") : "-"}</td>
    `;
    body.appendChild(tr);
  });

  summary.textContent = `Exported ${rows.length} file(s). Warnings: ${clippedCount} clip-risk, ${shortCount} very-short.`;
}

function createFolderCard(folderId) {
  const card = document.createElement("article");
  card.className = "folder";
  card.innerHTML = `
    <h2>Folder ${folderId}</h2>
    <div class="folder-controls">
      <input type="file" id="input-${folderId}" multiple accept="audio/*">
    </div>
    <p class="hint">Max ${MAX_FILES} files. Drag files between folders. Exported as ${folderId}/01.wav ... ${folderId}/16.wav</p>
    <ol class="file-list" id="list-${folderId}" data-folder="${folderId}"></ol>
  `;

  const input = card.querySelector(`#input-${folderId}`);
  input.addEventListener("change", (event) => {
    addFiles(folderId, Array.from(event.target.files || []));
    input.value = "";
  });

  const list = card.querySelector(`#list-${folderId}`);
  list.addEventListener("dragover", (event) => {
    event.preventDefault();
    list.classList.add("drag-over");
  });
  list.addEventListener("dragleave", () => {
    list.classList.remove("drag-over");
  });
  list.addEventListener("drop", (event) => {
    event.preventDefault();
    list.classList.remove("drag-over");
    const payload = event.dataTransfer.getData("application/json");
    if (!payload) return;
    const data = JSON.parse(payload);
    moveItem(data.fromFolder, folderId, data.id);
  });
  return card;
}

function renderFolders() {
  const root = document.getElementById("folders");
  root.innerHTML = "";
  FOLDERS.forEach((folderId) => root.appendChild(createFolderCard(folderId)));
  renderAllLists();
}

function stopPreview() {
  if (previewAudio) {
    previewAudio.pause();
    previewAudio.currentTime = 0;
  }
  if (previewUrl) {
    URL.revokeObjectURL(previewUrl);
  }
  previewAudio = null;
  previewItemId = null;
  previewUrl = null;
}

function renderAllLists() {
  FOLDERS.forEach((folderId) => {
    const list = document.getElementById(`list-${folderId}`);
    list.innerHTML = "";

    folderState[folderId].forEach((item, idx) => {
      const li = document.createElement("li");
      li.className = "file-row";
      li.draggable = true;
      li.addEventListener("dragstart", (event) => {
        event.dataTransfer.setData("application/json", JSON.stringify({ id: item.id, fromFolder: folderId }));
        event.dataTransfer.effectAllowed = "move";
      });

      const label = document.createElement("span");
      label.className = "file-row-label";
      label.textContent = `${String(idx + 1).padStart(2, "0")}. ${item.file.name}`;

      const actions = document.createElement("div");
      actions.className = "file-actions";

      const playBtn = document.createElement("button");
      playBtn.className = "secondary";
      playBtn.textContent = "Play";
      playBtn.addEventListener("click", () => togglePreview(item.id, item.file));

      const removeBtn = document.createElement("button");
      removeBtn.className = "secondary";
      removeBtn.textContent = "Remove";
      removeBtn.addEventListener("click", () => {
        if (previewItemId === item.id) stopPreview();
        folderState[folderId].splice(idx, 1);
        renderAllLists();
      });

      actions.appendChild(playBtn);
      actions.appendChild(removeBtn);
      li.appendChild(label);
      li.appendChild(actions);
      list.appendChild(li);
    });
  });
}

async function togglePreview(itemId, file) {
  if (previewItemId === itemId && previewAudio) {
    stopPreview();
    setStatus("Preview stopped.");
    return;
  }

  stopPreview();
  previewUrl = URL.createObjectURL(file);
  previewAudio = new Audio(previewUrl);
  previewItemId = itemId;

  previewAudio.onended = () => {
    stopPreview();
  };

  try {
    await previewAudio.play();
    setStatus(`Preview: ${file.name}`);
  } catch (error) {
    stopPreview();
    setStatus(`Cannot preview file: ${error.message}`);
  }
}

function addFiles(folderId, files) {
  if (!files.length) return;
  const room = MAX_FILES - folderState[folderId].length;
  if (room <= 0) {
    setStatus(`Folder ${folderId} is already full (${MAX_FILES} files).`);
    return;
  }

  const accepted = files.slice(0, room).map((file) => ({ id: newItemId(), file }));
  folderState[folderId].push(...accepted);
  renderAllLists();

  if (files.length > room) {
    setStatus(`Added ${room} files to folder ${folderId}. Extra files were skipped.`);
  } else {
    setStatus(`Added ${accepted.length} file(s) to folder ${folderId}.`);
  }
}

function moveItem(fromFolder, toFolder, itemId) {
  if (fromFolder === toFolder) return;
  const src = folderState[fromFolder];
  const dst = folderState[toFolder];
  const idx = src.findIndex((x) => x.id === itemId);
  if (idx < 0) return;
  if (dst.length >= MAX_FILES) {
    setStatus(`Folder ${toFolder} is full (${MAX_FILES} files).`);
    return;
  }
  const [item] = src.splice(idx, 1);
  dst.push(item);
  renderAllLists();
  setStatus(`Moved "${item.file.name}" to folder ${toFolder}.`);
}

function mixToMono(audioBuffer) {
  const channels = audioBuffer.numberOfChannels;
  const length = audioBuffer.length;
  const mono = new Float32Array(length);

  if (channels === 1) {
    mono.set(audioBuffer.getChannelData(0));
    return mono;
  }

  for (let ch = 0; ch < channels; ch++) {
    const src = audioBuffer.getChannelData(ch);
    for (let i = 0; i < length; i++) {
      mono[i] += src[i];
    }
  }

  const scale = 1 / channels;
  for (let i = 0; i < length; i++) mono[i] *= scale;
  return mono;
}

function trimSilence(samples, threshold) {
  let start = 0;
  let end = samples.length - 1;
  while (start < samples.length && Math.abs(samples[start]) < threshold) start++;
  while (end > start && Math.abs(samples[end]) < threshold) end--;
  if (start >= end) return samples;
  const pad = 64;
  const finalStart = Math.max(0, start - pad);
  const finalEnd = Math.min(samples.length - 1, end + pad);
  return samples.slice(finalStart, finalEnd + 1);
}

function normalize(samples, targetPeak = 0.95) {
  let maxAbs = 0;
  for (let i = 0; i < samples.length; i++) {
    const v = Math.abs(samples[i]);
    if (v > maxAbs) maxAbs = v;
  }
  if (maxAbs <= 0.000001) return samples;
  const gain = targetPeak / maxAbs;
  const out = new Float32Array(samples.length);
  for (let i = 0; i < samples.length; i++) out[i] = samples[i] * gain;
  return out;
}

function resampleLinear(samples, sourceRate, targetRate) {
  if (sourceRate === targetRate) return samples;
  const outLength = Math.max(1, Math.round(samples.length * targetRate / sourceRate));
  const out = new Float32Array(outLength);
  const ratio = sourceRate / targetRate;

  for (let i = 0; i < outLength; i++) {
    const pos = i * ratio;
    const idx = Math.floor(pos);
    const frac = pos - idx;
    const a = samples[idx] || 0;
    const b = samples[idx + 1] || a;
    out[i] = a + ((b - a) * frac);
  }
  return out;
}

function floatTo16BitPCM(value) {
  if (value > 1) value = 1;
  if (value < -1) value = -1;
  return value < 0 ? Math.round(value * 32768) : Math.round(value * 32767);
}

function encodeWav16Mono(samples, sampleRate) {
  const bytesPerSample = 2;
  const dataSize = samples.length * bytesPerSample;
  const buffer = new ArrayBuffer(44 + dataSize);
  const view = new DataView(buffer);

  function writeString(offset, text) {
    for (let i = 0; i < text.length; i++) view.setUint8(offset + i, text.charCodeAt(i));
  }

  writeString(0, "RIFF");
  view.setUint32(4, 36 + dataSize, true);
  writeString(8, "WAVE");
  writeString(12, "fmt ");
  view.setUint32(16, 16, true);
  view.setUint16(20, 1, true);
  view.setUint16(22, 1, true);
  view.setUint32(24, sampleRate, true);
  view.setUint32(28, sampleRate * bytesPerSample, true);
  view.setUint16(32, bytesPerSample, true);
  view.setUint16(34, 16, true);
  writeString(36, "data");
  view.setUint32(40, dataSize, true);

  let offset = 44;
  for (let i = 0; i < samples.length; i++) {
    view.setInt16(offset, floatTo16BitPCM(samples[i]), true);
    offset += 2;
  }
  return buffer;
}

async function convertAudioFile(file, options) {
  const ctx = ensureAudioContext();
  const arrayBuffer = await file.arrayBuffer();
  const decoded = await ctx.decodeAudioData(arrayBuffer.slice(0));
  let mono = mixToMono(decoded);
  const peakBefore = maxPeak(mono);

  if (options.trimSilence) mono = trimSilence(mono, SILENCE_THRESHOLD);
  if (options.normalize) mono = normalize(mono, 0.95);

  const resampled = resampleLinear(mono, decoded.sampleRate, TARGET_RATE);
  const peakAfter = maxPeak(resampled);
  const duration = resampled.length / TARGET_RATE;
  const warnings = [];
  if (peakAfter >= 0.999) warnings.push("clip-risk");
  if (duration < 0.03) warnings.push("very-short");

  return {
    wavBuffer: encodeWav16Mono(resampled, TARGET_RATE),
    report: {
      sourceName: file.name,
      duration,
      peakBefore,
      peakAfter,
      warnings
    }
  };
}

function getConversionOptions() {
  return {
    trimSilence: document.getElementById("trimToggle").checked,
    normalize: document.getElementById("normalizeToggle").checked
  };
}

async function buildZip() {
  const total = FOLDERS.reduce((acc, folderId) => acc + folderState[folderId].length, 0);
  if (total === 0) {
    setStatus("No files loaded.");
    return;
  }

  const options = getConversionOptions();
  const zip = new JSZip();
  const reportRows = [];
  let done = 0;

  for (const folderId of FOLDERS) {
    const files = folderState[folderId];
    for (let i = 0; i < files.length; i++) {
      const autoName = `${String(i + 1).padStart(2, "0")}.wav`;
      setStatus(`Converting ${folderId}/${autoName} ...`, Math.round((done / total) * 100));
      const converted = await convertAudioFile(files[i].file, options);
      zip.file(`${folderId}/${autoName}`, converted.wavBuffer);
      reportRows.push({
        folder: folderId,
        slot: autoName,
        ...converted.report
      });
      done++;
      setStatus("Converting files...", Math.round((done / total) * 100));
    }
  }

  setStatus("Packaging ZIP...", 100);
  const zipBlob = await zip.generateAsync({ type: "blob" });
  const url = URL.createObjectURL(zipBlob);
  const link = document.createElement("a");
  link.href = url;
  link.download = "radiomusic-library-32k.zip";
  document.body.appendChild(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
  renderReport(reportRows);
  setStatus("Done. ZIP downloaded.", 100);
}

function clearAll() {
  stopPreview();
  FOLDERS.forEach((folderId) => {
    folderState[folderId] = [];
  });
  renderAllLists();
  renderReport([]);
  setStatus("Cleared all folders.", 0);
}

function init() {
  renderFolders();

  document.getElementById("downloadZip").addEventListener("click", async () => {
    const button = document.getElementById("downloadZip");
    button.disabled = true;
    try {
      await buildZip();
    } catch (error) {
      console.error(error);
      setStatus(`Conversion failed: ${error.message}`, 0);
    } finally {
      button.disabled = false;
    }
  });

  document.getElementById("clearAll").addEventListener("click", clearAll);
}

init();
