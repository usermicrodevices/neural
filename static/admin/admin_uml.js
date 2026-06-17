const btnTrain = document.getElementById('btnTrain');
const btnTrainUml = document.getElementById('btnTrainUml');
const btnConfig = document.getElementById('btnConfig');
const btnStop = document.getElementById('btnStop');
btnTrain.onclick = () => window.location.href = '/';
btnTrainUml.onclick = () => window.location.href = '/train_uml';
btnDbShow.onclick = () => window.location.href = '/show_db';
btnConfig.onclick = () => window.location.href = '/config';
btnStop.onclick = async () => {
    if (confirm('Stop the service?')) {
        const res = await fetch('/stop', { method: 'POST' });
        alert((await res.json()).message || 'Stopping...');
    }
};

let sourceTypes = [];
let trainedBlocks = [];
let batchBlocks = [];

const UML_EXTS = ['.puml', '.uml', '.txt'];
const SRC_EXTS = { '.cpp': 1, '.hpp': 1, '.c': 1, '.h': 1, '.py': 2, '.js': 3, '.html': 3 };

async function loadTrainedBlocks() {
    try {
        const res = await fetch('/src_types');
        sourceTypes = await res.json();
    } catch (err) {
        sourceTypes = [{id:1, name:"C++"}, {id:2, name:"Python"}, {id:3, name:"JS"}];
    }
    try {
        const res = await fetch('/list_uml');
        trainedBlocks = await res.json();
    } catch (err) {
        trainedBlocks = [];
    }
}

function getExt(name) {
    const dot = name.lastIndexOf('.');
    return dot >= 0 ? name.slice(dot).toLowerCase() : '';
}

function getBaseName(name) {
    const dot = name.lastIndexOf('.');
    return dot >= 0 ? name.slice(0, dot) : name;
}

function isUmlFile(name) {
    return UML_EXTS.includes(getExt(name));
}

function isSrcFile(name) {
    return getExt(name) in SRC_EXTS;
}

function getSrcTypeId(name) {
    return SRC_EXTS[getExt(name)] || 1;
}

function detectBlocks(fileList) {
    const groups = {};
    for (const file of fileList) {
        const path = file.webkitRelativePath || file.name;
        const parts = path.split('/');
        const fname = parts[parts.length - 1];
        if (fname.startsWith('.')) continue;

        if (isUmlFile(fname)) {
            const base = getBaseName(fname);
            if (!groups[base]) groups[base] = { name: base, uml: null, sources: [] };
            groups[base].uml = file;
        } else if (isSrcFile(fname)) {
            const base = getBaseName(fname);
            if (!groups[base]) groups[base] = { name: base, uml: null, sources: [] };
            groups[base].sources.push({ file, type_id: getSrcTypeId(fname) });
        }
    }
    return Object.values(groups).filter(g => g.uml || g.sources.length > 0);
}

function readFileText(file) {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = () => resolve(reader.result);
        reader.onerror = reject;
        reader.readAsText(file);
    });
}

function isDuplicate(block) {
    return trainedBlocks.some(t => t.name === block.name);
}

function renderBatchPreview(blocks) {
    batchBlocks = blocks;
    const container = document.getElementById('batchBlocks');
    const countEl = document.getElementById('blockCount');
    countEl.textContent = blocks.length;

    if (blocks.length === 0) {
        container.innerHTML = '<em style="font-size:12px;color:#999;">No trainable blocks found.</em>';
        document.getElementById('trainAllBtn').disabled = true;
        document.getElementById('trainNewBtn').disabled = true;
        return;
    }

    container.innerHTML = blocks.map((b, i) => {
        const dup = isDuplicate(b);
        const fileNames = [];
        if (b.uml) fileNames.push(b.uml.name.split('/').pop());
        b.sources.forEach(s => fileNames.push(s.file.name.split('/').pop()));
        return '<div class="batch-block' + (dup ? ' existing' : '') + '">' +
            '<span class="name">' + b.name + '</span>' +
            '<span class="files">' + fileNames.join(', ') + '</span>' +
            '<span class="' + (dup ? 'dup' : 'ok') + '">' + (dup ? '⚠ exists' : '✓ new') + '</span>' +
            '</div>';
    }).join('');

    const hasNew = blocks.some(b => !isDuplicate(b));
    document.getElementById('trainAllBtn').disabled = blocks.length === 0;
    document.getElementById('trainNewBtn').disabled = !hasNew;
}

async function trainBlock(block) {
    if (isDuplicate(block)) return 'skip';

    const formData = new FormData();
    formData.append('uml_name', block.name);

    if (block.uml) {
        const content = await readFileText(block.uml);
        const blob = new Blob([content], { type: 'text/plain' });
        formData.append('uml_file', blob, block.uml.name.split('/').pop());
    }

    for (const src of block.sources) {
        const content = await readFileText(src.file);
        const blob = new Blob([content], { type: 'text/plain' });
        formData.append('source_files[]', blob, src.file.name.split('/').pop());
        formData.append('source_types[]', src.type_id);
    }

    try {
        const res = await fetch('/train_uml', { method: 'POST', body: formData });
        const data = await res.json();
        if (data.status === 'duplicate') return 'skip';
        return data.status === 'ok' ? 'ok' : 'err';
    } catch (e) {
        return 'err';
    }
}

async function trainBatch(blocks) {
    const log = document.getElementById('trainLog');
    const bar = document.getElementById('progressBar');
    const fill = document.getElementById('progressFill');
    const trainAllBtn = document.getElementById('trainAllBtn');
    const trainNewBtn = document.getElementById('trainNewBtn');

    trainAllBtn.disabled = true;
    trainNewBtn.disabled = true;
    bar.style.display = 'block';
    log.innerHTML = '';

    const total = blocks.length;
    let done = 0;

    for (const block of blocks) {
        const status = await trainBlock(block);
        done++;
        fill.style.width = Math.round(done / total * 100) + '%';

        const entry = document.createElement('div');
        entry.className = 'log-entry ' + status;
        entry.textContent = (status === 'ok' ? '✔ ' : status === 'skip' ? '⊘ ' : '✘ ') + block.name;
        log.appendChild(entry);
        log.scrollTop = log.scrollHeight;

        if (status === 'ok') {
            trainedBlocks.push({ name: block.name, sources: [] });
        }
    }

    const info = document.createElement('div');
    info.className = 'log-entry info';
    info.textContent = 'Done: ' + done + ' blocks processed';
    log.appendChild(info);

    trainAllBtn.disabled = false;
    trainNewBtn.disabled = false;
    renderBatchPreview(batchBlocks);
}

// Directory selection
const dropZone = document.getElementById('dropZone');
const dirInput = document.getElementById('dirInput');

dropZone.addEventListener('click', () => dirInput.click());
dropZone.addEventListener('dragover', e => { e.preventDefault(); dropZone.classList.add('active'); });
dropZone.addEventListener('dragleave', () => dropZone.classList.remove('active'));
dropZone.addEventListener('drop', e => {
    e.preventDefault();
    dropZone.classList.remove('active');
    const files = e.dataTransfer.files;
    if (files.length > 0) handleFiles(files);
});

dirInput.addEventListener('change', () => {
    if (dirInput.files.length > 0) handleFiles(dirInput.files);
    dropZone.classList.add('active');
});

function handleFiles(fileList) {
    const blocks = detectBlocks(fileList);
    document.getElementById('previewSection').style.display = 'block';
    renderBatchPreview(blocks);
}

document.getElementById('trainAllBtn').addEventListener('click', () => trainBatch(batchBlocks));
document.getElementById('trainNewBtn').addEventListener('click', () => trainBatch(batchBlocks));

// Manual single-block form
function addSourceEntry() {
    const container = document.getElementById('sourcesContainer');
    const div = document.createElement('div');
    div.className = 'source-entry';

    const fileInput = document.createElement('input');
    fileInput.type = 'file';
    fileInput.name = 'source_files[]';
    fileInput.accept = '.cpp,.hpp,.c,.h,.py,.js,.html';
    fileInput.required = true;

    const select = document.createElement('select');
    select.name = 'source_types[]';
    select.required = true;
    sourceTypes.forEach(type => {
        const option = document.createElement('option');
        option.value = type.id;
        option.textContent = type.name;
        select.appendChild(option);
    });

    const removeBtn = document.createElement('button');
    removeBtn.type = 'button';
    removeBtn.className = 'remove-source-btn';
    removeBtn.textContent = '✖';
    removeBtn.onclick = () => div.remove();

    div.appendChild(fileInput);
    div.appendChild(select);
    div.appendChild(removeBtn);
    container.appendChild(div);
}

document.getElementById('addSourceBtn').addEventListener('click', () => addSourceEntry());

function addEventEntry() {
    const container = document.getElementById('eventsContainer');
    const div = document.createElement('div');
    div.className = 'source-entry';

    const nameInput = document.createElement('input');
    nameInput.type = 'text';
    nameInput.placeholder = 'Event name (e.g. onClick)';
    nameInput.style.width = '120px';
    nameInput.required = true;

    const targetSelect = document.createElement('select');
    targetSelect.style.width = '150px';
    const defaultOpt = document.createElement('option');
    defaultOpt.value = '';
    defaultOpt.textContent = '-- select target --';
    targetSelect.appendChild(defaultOpt);
    trainedBlocks.forEach(b => {
        const opt = document.createElement('option');
        opt.value = b.name;
        opt.textContent = b.name;
        targetSelect.appendChild(opt);
    });

    const removeBtn = document.createElement('button');
    removeBtn.type = 'button';
    removeBtn.className = 'remove-source-btn';
    removeBtn.textContent = '✖';
    removeBtn.onclick = () => div.remove();

    div.appendChild(nameInput);
    div.appendChild(targetSelect);
    div.appendChild(removeBtn);
    container.appendChild(div);
}

document.getElementById('addEventBtn').addEventListener('click', addEventEntry);

function collectEventsJson() {
    const entries = document.querySelectorAll('#eventsContainer .source-entry');
    const events = [];
    entries.forEach(entry => {
        const nameInput = entry.querySelector('input[type="text"]');
        const targetSelect = entry.querySelector('select');
        const eventName = nameInput.value.trim();
        const targetUml = targetSelect.value;
        if (eventName && targetUml) {
            const block = trainedBlocks.find(b => b.name === targetUml);
            let targetSource = '';
            let targetSrcType = 1;
            if (block && block.sources && block.sources.length > 0) {
                targetSource = block.sources[0].content;
                const st = sourceTypes.find(s => s.name === block.sources[0].type);
                if (st) targetSrcType = st.id;
            }
            events.push({
                event_name: eventName,
                target_uml: targetUml,
                target_src_type: targetSrcType,
                target_source: targetSource
            });
        }
    });
    return JSON.stringify(events);
}

document.getElementById('umlForm').onsubmit = async (evt) => {
    evt.preventDefault();
    const formData = new FormData(evt.target);
    const sourceFiles = document.querySelectorAll('input[name="source_files[]"]');
    if (sourceFiles.length === 0) {
        alert('Please add at least one source file.');
        return;
    }
    const statusDiv = document.getElementById('status');
    statusDiv.innerHTML = 'Creating UML container...';
    const eventsJson = collectEventsJson();
    if (eventsJson !== '[]') {
        formData.append('events_json', eventsJson);
    }
    try {
        const res = await fetch('/train_uml', { method: 'POST', body: formData });
        const data = await res.json();
        if (data.status === 'ok') {
            statusDiv.innerHTML = '✔️ UML container created successfully.';
            evt.target.reset();
            document.getElementById('sourcesContainer').innerHTML = '';
            addSourceEntry();
            await loadTrainedBlocks();
        } else {
            throw new Error(data.message);
        }
    } catch (err) {
        statusDiv.innerHTML = '❌ Error: ' + err.message;
    }
};

loadTrainedBlocks().then(() => {
    addSourceEntry();
});
