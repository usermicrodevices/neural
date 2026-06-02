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

async function getSourceTypes() {
    try {
        const res = await fetch('/src_types');
        sourceTypes = await res.json();
    } catch (err) {
        console.error('Failed to get source types:', err);
        sourceTypes = [{id:1, name:"C++"}, {id:2, name:"Python"}, {id:3, name:"JS"}];
    }
}

function addSourceEntry(fileIndex = null) {
    const container = document.getElementById('sourcesContainer');
    const div = document.createElement('div');
    div.className = 'source-entry';
    div.dataset.index = fileIndex !== null ? fileIndex : Date.now();

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

document.getElementById('umlForm').onsubmit = async (evt) => {
    evt.preventDefault();
    const formData = new FormData(evt.target);
    const sourceFiles = document.querySelectorAll('input[name="source_files[]"]');
    if (sourceFiles.length === 0) {
        alert('Please add at least one source file.');
        return;
    }
    const statusDiv = document.getElementById('status');
    const consoleDiv = document.getElementById('statusConsole');
    statusDiv.innerHTML = 'Creating UML container...';
    try {
        const res = await fetch('/train_uml', { method: 'POST', body: formData });
        const data = await res.json();
        if (data.status === 'ok') {
            statusDiv.innerHTML = '✔️ UML container created successfully.';
            consoleDiv.innerHTML = '<div class="status-console success">UML container saved.</div>';
            evt.target.reset();
            document.getElementById('sourcesContainer').innerHTML = '';
            addSourceEntry();
        } else {
            throw new Error(data.message);
        }
    } catch (err) {
        statusDiv.innerHTML = `❌ Error: ${err.message}`;
        consoleDiv.innerHTML = `<div class="status-console error">${err.message}</div>`;
    }
};

getSourceTypes().then(() => {
    addSourceEntry();
});
