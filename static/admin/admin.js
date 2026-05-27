const mainContent = document.getElementById('mainContent');

function loadHomeView() {
    mainContent.innerHTML = `
        <div class="card">
            <h2>🎓 Upload Documents for Learning 📚</h2>
            <div class="memory-info" id="memoryDisplay">Memory: -- MB</div>
            <form id="uploadForm">
                <label for="fileInput">Choose text files (txt, cpp, hpp, py, html, js, json, xml, etc.)</label>
                <input type="file" id="fileInput" name="files" multiple required>
                <div class="file-list" id="fileListContainer"></div>
                <div class="flex-row">
                    <label><input type="checkbox" id="serializeCheckbox"> Serialize to disk after every file</label>
                    <button type="button" id="serializeNowBtn">💾 Save Now 💽</button>
                </div>
                <div class="flex-row">
                    <button type="submit" id="submitBtn">👨‍🎓 Learn All 📝</button>
                    <div class="status" id="status"></div>
                </div>
            </form>
            <div class="progress-container" id="progressContainer">
                <div class="progress-bar" id="progressBar">0%</div>
            </div>
            <div id="statusConsole" class="status-console"></div>
        </div>
    `;
    attachHomeEventListeners();
}

function loadTrainView() {
    //mainContent.innerHTML = `<div class="card"><h1>🧠 Extended Train Logic</h1><p>Group training and UML attachment – coming soon.</p></div>`;
    window.location.href = '/train-uml';
}

function loadConfigView() {
    //mainContent.innerHTML = `<div class="card"><h1>⚙️ Service Configuration</h1><p>Settings page – coming soon.</p></div>`;
    window.location.href = '/config';
}

let pollInterval = null;
let currentFileIndex = 0;
let filesToProcess = [];
let fileStatuses = [];

function attachHomeEventListeners() {
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
    const statusConsoleDiv = document.getElementById('statusConsole');

    function setControlsDisabled(disabled) {
        submitBtn.disabled = disabled;
        fileInput.disabled = disabled;
        serializeCheckbox.disabled = disabled;
        serializeNowBtn.disabled = disabled;
    }

    function updateMemoryDisplay(memBytes) {
        if (memBytes !== undefined) {
            const memMB = (memBytes / (1024 * 1024)).toFixed(2);
            memoryDisplay.innerHTML = `🔋 Memory used: ${memMB} MB`;
        }
    }

    function setStatusConsole(message, isError = false) {
        if (statusConsoleDiv) {
            statusConsoleDiv.innerHTML = message;
            statusConsoleDiv.classList.remove('error', 'success');
            if (isError) {
                statusConsoleDiv.classList.add('error');
            } else {
                statusConsoleDiv.classList.add('success');
            }
        }
    }

    function clearStatusConsole() {
        if (statusConsoleDiv) {
            statusConsoleDiv.innerHTML = '';
            statusConsoleDiv.classList.remove('success', 'error');
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
                case 'partial': statusClass = 'partial'; statusText = '🔔 partial duplicate'; break;
                case 'warning': statusClass = 'warning'; statusText = '⚠️ full duplicate'; break;
                case 'error': statusClass = 'error'; statusText = `❌ error: ${status.error || 'unknown'}`; break;
                case 'success': statusClass = 'success'; statusText = '✔️ done'; break;
                default: statusText = '?';
            }
            html += `<div class="file-item" data-index="${i}">
                        <div class="file-row">
                            <span class="file-name">${escapeHtml(f.name)}</span>
                            <span class="file-status ${statusClass}">${statusText}</span>
                        </div>
                        <div class="file-tags">
                            <input type="text" class="tag-input" data-index="${i}"
                            placeholder="Tags (semicolon separated)" value="${escapeHtml(f.tags || '')}">
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
            let statusIcon = 'success';
            let statusText = '✔️ done';
            if (data.fileResult === 2) {
                statusIcon = 'warning';
                statusText = '⚠️ full duplicate';
            } else if (data.fileResult === 1) {
                statusIcon = 'partial';
                statusText = '🔔 partial duplicate';
            }
            fileStatuses[fileIndex].status = statusIcon;
            fileStatuses[fileIndex].error = (statusIcon !== 'success') ? statusText : '';
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
        const doSerialize = serializeCheckbox.checked ? '1' : '0';
        formData.append('serialize', doSerialize);
        const tags = filesToProcess[fileIndex].tags || '';
        formData.append('tags', tags);

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

        progressContainer.style.display = 'block';
        statusDiv.innerHTML = 'Starting learning...';
        updateGlobalProgressBar(0, 0);
        setControlsDisabled(true);

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

        const warnings = [];
        for (let i = 0; i < fileStatuses.length; i++) {
            const st = fileStatuses[i];
            if (st.status === 'warning') {
                warnings.push(`⚠️ "${filesToProcess[i].name}" – full duplicate (no new chunks)`);
            } else if (st.status === 'partial') {
                warnings.push(`🔔 "${filesToProcess[i].name}" – partial duplicate (some chunks skipped)`);
            } else if (st.status === 'error') {
                warnings.push(`❌ "${filesToProcess[i].name}" – error: ${st.error}`);
            }
        }
        if (warnings.length > 0) {
            setStatusConsole(warnings.join('<br>'), true);
        } else {
            setStatusConsole('👌 All files processed successfully.');
        }

        if (!allSuccess) {
            statusDiv.innerHTML = '⚠️ Some files failed. See list for details.';
        } else {
            statusDiv.innerHTML = '✔️ All documents learned.';
        }

        setControlsDisabled(false);
        setTimeout(() => {
            if (!submitBtn.disabled) {
                progressContainer.style.display = 'none';
                progressBar.style.width = '0%';
            }
        }, 2000);
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
        if (filesToProcess.length === 0) {
            statusDiv.innerHTML = '⚠️ Please select at least one file.';
            return;
        }
        fileStatuses = filesToProcess.map(() => ({ status: 'pending', error: '', progress: 0 }));
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
            setStatusConsole(`Serialization error: ${err.message}`, true);
        } finally {
            serializeNowBtn.disabled = false;
            await fetchProgress();
        }
    });

    fileInput.addEventListener('change', () => {
        if (pollInterval) {
            clearInterval(pollInterval);
            pollInterval = null;
        }
        progressContainer.style.display = 'none';
        progressBar.style.width = '0%';
        statusDiv.innerHTML = '';
        clearStatusConsole();
        const files = Array.from(fileInput.files);
        filesToProcess = files.map(file => {
            let name = file.name;
            let lastDot = name.lastIndexOf('.');
            let baseName = lastDot !== -1 ? name.substring(0, lastDot) : name;
            baseName = baseName.replace(/\./g, '_');
            file.tags = baseName;
            return file;
        });
        fileStatuses = files.map(() => ({ status: 'pending', error: '', progress: 0 }));
        currentFileIndex = 0;
        updateFileListDisplay();
        setControlsDisabled(false);
        fetchProgress();
    });

    fileListContainer.addEventListener('input', (evt) => {
        if (evt.target.classList.contains('tag-input')) {
            const idx = parseInt(evt.target.dataset.index, 10);
            if (!isNaN(idx) && filesToProcess[idx]) {
                filesToProcess[idx].tags = evt.target.value;
            }
        }
    });

    fetchProgress();
}

document.getElementById('homeBtn').addEventListener('click', () => {
    loadHomeView();
});

document.getElementById('trainUmlBtn').addEventListener('click', () => {
    loadTrainView();
});

document.getElementById('configBtn').addEventListener('click', () => {
    loadConfigView();
});

document.getElementById('stopBtn').addEventListener('click', async () => {
    if (confirm('Are you sure you want to stop the service?')) {
        try {
            const res = await fetch('/stop', { method: 'POST' });
            const data = await res.json();
            alert(data.message || 'Service stopping...');
        } catch (err) {
            alert('Failed to stop service: ' + err.message);
        }
    }
});

loadHomeView();
