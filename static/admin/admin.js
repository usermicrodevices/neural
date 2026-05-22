const form = document.getElementById('uploadForm');
const fileInput = document.getElementById('fileInput');
const submitBtn = document.getElementById('submitBtn');
const serializeCheckbox = document.getElementById('serializeCheckbox');
const serializeNowBtn = document.getElementById('serializeNowBtn');
const progressContainer = document.getElementById('progressContainer');
const progressBar = document.getElementById('progressBar');
const statusDiv = document.getElementById('status');
const memoryDisplay = document.getElementById('memoryDisplay');
const fileListContainer = document.getElementById('fileListContainer');

let pollInterval = null;
let currentFileIndex = 0;
let filesToProcess = [];
let fileStatuses = [];

function setControlsDisabled(disabled) {
    submitBtn.disabled = disabled;
    fileInput.disabled = disabled;
    serializeCheckbox.disabled = disabled;
    serializeNowBtn.disabled = disabled;
}

function updateMemoryDisplay(memBytes) {
    if (memBytes !== undefined) {
        const memMB = (memBytes / (1024 * 1024)).toFixed(2);
        memoryDisplay.innerHTML = `🧠 Memory used: ${memMB} MB`;
    }
}

async function fetchProgress() {
    try {
        const res = await fetch('/progress');
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json();
        if (data.memory !== undefined) updateMemoryDisplay(data.memory);
        return data;
    } catch (err) {
        console.error('Progress fetch error:', err);
        return null;
    }
}

function updateGlobalProgressBar(completedCount, currentProgressPercent) {
    const total = filesToProcess.length;
    if (total === 0) return;
    let globalPercent = (completedCount / total) * 100;
    if (currentProgressPercent !== undefined && currentFileIndex < total) {
        globalPercent += (currentProgressPercent / total);
    }
    globalPercent = Math.min(100, Math.max(0, globalPercent));
    progressContainer.style.display = 'block';
    progressBar.style.width = globalPercent + '%';
    progressBar.textContent = Math.floor(globalPercent) + '%';
}

function scrollToCurrentFile() {
    if (!fileListContainer || currentFileIndex < 0) return;
    setTimeout(() => {
        const items = fileListContainer.querySelectorAll('.file-item');
        if (currentFileIndex < items.length) {
            const targetItem = items[currentFileIndex];
            targetItem.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
        }
    }, 80);
}

function updateFileListDisplay() {
    if (filesToProcess.length === 0) {
        fileListContainer.innerHTML = '<div style="color:#64748b; text-align:center;">No files selected</div>';
        return;
    }
    let html = '';
    for (let i = 0; i < filesToProcess.length; i++) {
        const f = filesToProcess[i];
        const status = fileStatuses[i] || { status: 'pending', error: '', progress: 0 };
        let statusClass = '';
        let statusText = '';
        let progressBarHtml = '';
        switch (status.status) {
            case 'pending': statusClass = ''; statusText = '⏳ pending'; break;
            case 'processing': 
                statusClass = 'processing'; 
                statusText = `⚙️ processing ${status.progress}%`;
                progressBarHtml = `<div class="file-progress-bar"><div class="file-progress-fill" style="width:${status.progress}%;"></div></div>`;
                break;
            case 'success': statusClass = 'success'; statusText = '✔️ done'; break;
            case 'error': statusClass = 'error'; statusText = `❌ error: ${status.error || 'unknown'}`; break;
            default: statusText = '?';
        }
        html += `<div class="file-item" data-index="${i}">
                    <div class="file-row">
                        <span class="file-name">${escapeHtml(f.name)}</span>
                        <span class="file-status ${statusClass}">${statusText}</span>
                    </div>
                    ${progressBarHtml}
                 </div>`;
    }
    fileListContainer.innerHTML = html;
    if (currentFileIndex >= 0 && currentFileIndex < filesToProcess.length) {
        scrollToCurrentFile();
    }
}

async function pollTrainingProgress(fileIndex) {
    const data = await fetchProgress();
    if (!data) return true;

    if (data.training) {
        fileStatuses[fileIndex].status = 'processing';
        fileStatuses[fileIndex].progress = data.progress;
        updateFileListDisplay();

        let completed = 0;
        for (let i = 0; i < fileIndex; i++) {
            if (fileStatuses[i].status === 'success') completed++;
        }
        updateGlobalProgressBar(completed, data.progress);
        statusDiv.innerHTML = `📄 Processing "${filesToProcess[fileIndex].name}" - ${data.progress}%`;
        return true;
    } else {
        fileStatuses[fileIndex].status = 'success';
        fileStatuses[fileIndex].progress = 100;
        updateFileListDisplay();

        let completed = 0;
        for (let i = 0; i <= fileIndex; i++) {
            if (fileStatuses[i].status === 'success') completed++;
        }
        updateGlobalProgressBar(completed, 0);
        statusDiv.innerHTML = `✔️ Finished "${filesToProcess[fileIndex].name}"`;
        return false;
    }
}

async function uploadFile(file, fileIndex) {
    const formData = new FormData();
    formData.append('file', file);
    formData.append('serialize', '0');

    try {
        const response = await fetch('/', { method: 'POST', body: formData });
        const result = await response.json();
        if (result.status === 'accepted') {
            return new Promise((resolve, reject) => {
                const interval = setInterval(async () => {
                    const training = await pollTrainingProgress(fileIndex);
                    if (!training) {
                        clearInterval(interval);
                        resolve();
                    }
                }, 500);
                if (pollInterval) clearInterval(pollInterval);
                pollInterval = interval;
            });
        } else {
            throw new Error(result.message || 'Upload failed');
        }
    } catch (err) {
        fileStatuses[fileIndex].status = 'error';
        fileStatuses[fileIndex].error = err.message;
        updateFileListDisplay();
        throw err;
    }
}

async function processAllFiles() {
    if (filesToProcess.length === 0) return;

    setControlsDisabled(true);
    progressContainer.style.display = 'block';
    statusDiv.innerHTML = 'Starting learning...';
    updateGlobalProgressBar(0, 0);

    let allSuccess = true;
    for (let i = 0; i < filesToProcess.length; i++) {
        currentFileIndex = i;
        fileStatuses[i].status = 'processing';
        fileStatuses[i].progress = 0;
        updateFileListDisplay();
        statusDiv.innerHTML = `📄 Uploading "${filesToProcess[i].name}"...`;
        try {
            await uploadFile(filesToProcess[i], i);
        } catch (err) {
            console.error(`Failed to process ${filesToProcess[i].name}:`, err);
            allSuccess = false;
        }
    }

    if (pollInterval) {
        clearInterval(pollInterval);
        pollInterval = null;
    }

    await fetchProgress();

    if (serializeCheckbox.checked && allSuccess) {
        statusDiv.innerHTML = '💾 Serializing model to disk...';
        try {
            const res = await fetch('/serialize', { method: 'POST' });
            const data = await res.json();
            if (data.status === 'ok') {
                statusDiv.innerHTML = '✔️ All documents learned and model serialized!';
            } else {
                throw new Error(data.message || 'Serialization failed');
            }
        } catch (err) {
            statusDiv.innerHTML = `⚠️ Learning finished but serialization failed: ${err.message}`;
        }
    } else if (!allSuccess) {
        statusDiv.innerHTML = '⚠️ Some files failed. See list for details.';
    } else {
        statusDiv.innerHTML = '✔️ All documents learned (no serialization requested).';
    }

    setControlsDisabled(false);
    setTimeout(() => {
        if (!submitBtn.disabled) {
            progressContainer.style.display = 'none';
            progressBar.style.width = '0%';
        }
    }, 2000);
    fileInput.value = '';
    filesToProcess = [];
    fileStatuses = [];
    updateFileListDisplay();
}

function escapeHtml(str) {
    return str.replace(/[&<>]/g, function(m) {
        if (m === '&') return '&amp;';
        if (m === '<') return '&lt;';
        if (m === '>') return '&gt;';
        return m;
    });
}

form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const files = Array.from(fileInput.files);
    if (files.length === 0) {
        statusDiv.innerHTML = '⚠️ Please select at least one file.';
        return;
    }
    filesToProcess = files;
    fileStatuses = files.map(() => ({ status: 'pending', error: '', progress: 0 }));
    currentFileIndex = 0;
    updateFileListDisplay();
    await processAllFiles();
});

serializeNowBtn.addEventListener('click', async () => {
    serializeNowBtn.disabled = true;
    statusDiv.innerHTML = '💾 Serializing model...';
    try {
        const res = await fetch('/serialize', { method: 'POST' });
        const data = await res.json();
        if (data.status === 'ok') {
            statusDiv.innerHTML = '✔️ Model serialized to disk.';
            setTimeout(() => {
                if (statusDiv.innerHTML === '✔️ Model serialized to disk.') statusDiv.innerHTML = '';
            }, 3000);
        } else {
            throw new Error(data.message || 'Serialization failed');
        }
    } catch (err) {
        statusDiv.innerHTML = `❌ Serialization error: ${err.message}`;
    } finally {
        serializeNowBtn.disabled = false;
        await fetchProgress();
    }
});

fetchProgress();

fileInput.addEventListener('change', () => {
    if (pollInterval) {
        clearInterval(pollInterval);
        pollInterval = null;
    }
    progressContainer.style.display = 'none';
    progressBar.style.width = '0%';
    statusDiv.innerHTML = '';
    const files = Array.from(fileInput.files);
    filesToProcess = files;
    fileStatuses = files.map(() => ({ status: 'pending', error: '', progress: 0 }));
    currentFileIndex = 0;
    updateFileListDisplay();
    setControlsDisabled(false);
    fetchProgress();
});
