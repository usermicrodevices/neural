const btnTrain = document.getElementById('btnTrain');
const btnTrainUml = document.getElementById('btnTrainUml');
const btnConfig = document.getElementById('btnConfig');
const btnDbShow = document.getElementById('btnDbShow');
const btnStop = document.getElementById('btnStop');

btnTrain.onclick = () => window.location.href = '/';
btnTrainUml.onclick = () => window.location.href = '/train_uml';
btnConfig.onclick = () => window.location.href = '/config';
btnDbShow.onclick = () => window.location.href = '/show_db';
btnStop.onclick = async () => {
    if (confirm('Stop the service?')) {
        const res = await fetch('/stop', { method: 'POST' });
        alert((await res.json()).message || 'Stopping...');
    }
};

let allTables = [];
let currentTable = '';
let currentFilter = '';
let currentOffset = 0;
const PAGE_SIZE = 10;
let isLoading = false;
let hasMore = true;
let scrollContainer = null;

async function loadTableList() {
    try {
        const res = await fetch('/show_db', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ cmd: 'list_tables' })
        });
        const data = await res.json();
        if (Array.isArray(data)) {
            allTables = data;
            renderTableList(allTables);
        } else if (data.error) {
            throw new Error(data.error);
        } else {
            throw new Error('Unexpected response format');
        }
    } catch (err) {
        document.getElementById('tablesList').innerHTML = `<li class="error">Error loading tables: ${err.message}</li>`;
    }
}

function renderTableList(tables) {
    const container = document.getElementById('tablesList');
    if (!tables.length) {
        container.innerHTML = '<li>No tables found</li>';
        return;
    }
    container.innerHTML = tables.map(t => `<li data-table="${t}">${t}</li>`).join('');
    document.querySelectorAll('#tablesList li').forEach(li => {
        li.addEventListener('click', () => {
            document.querySelectorAll('#tablesList li').forEach(l => l.classList.remove('active'));
            li.classList.add('active');
            const tableName = li.getAttribute('data-table');
            resetAndLoadTable(tableName);
        });
    });
}

function resetAndLoadTable(tableName, filterText = '') {
    currentTable = tableName;
    currentFilter = filterText;
    currentOffset = 0;
    hasMore = true;
    isLoading = false;
    const contentArea = document.getElementById('contentArea');
    contentArea.innerHTML = '<p>Loading...</p>';
    loadMoreRows(true);
}

async function loadMoreRows(reset = false) {
    if (isLoading || (!hasMore && !reset)) return;
    isLoading = true;
    if (reset) {
        currentOffset = 0;
        hasMore = true;
        const contentArea = document.getElementById('contentArea');
        contentArea.innerHTML = '<div class="table-loading">Loading...</div>';
    }
    try {
        const res = await fetch('/get_table', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                table: currentTable,
                filter: currentFilter,
                offset: currentOffset,
                limit: PAGE_SIZE
            })
        });
        const data = await res.json();
        if (data.error) throw new Error(data.error);
        
        if (reset) {
            renderFullTable(currentTable, data.columns, data.rows, currentFilter);
        } else {
            appendRows(data.rows);
        }
        hasMore = data.has_more === true;
        currentOffset += data.rows.length;
        
        if (!hasMore && !reset) {
            showNoMoreMessage();
        }
    } catch (err) {
        const contentArea = document.getElementById('contentArea');
        contentArea.innerHTML = `<p class="error">Error: ${err.message}</p>`;
    } finally {
        isLoading = false;
        attachScrollListener();
    }
}

function renderFullTable(tableName, columns, rows, filterText) {
    const contentArea = document.getElementById('contentArea');
    const html = `
        <h3>Table: ${escapeHtml(tableName)}</h3>
        <div class="search-row">
            <input type="text" id="contentSearch" placeholder="🔍 Search rows..." value="${escapeHtml(filterText)}">
            <button id="searchContentBtn">Search</button>
            ${filterText ? '<button id="clearSearchBtn">Clear</button>' : ''}
        </div>
        <div class="table-scroll-container" style="max-height: 500px; overflow-y: auto;">
            <table id="dataTable" style="width:100%; border-collapse: collapse;">
                <thead style="position: sticky; top:0; background:#f8fafc;">
                    <tr>${columns.map(c => `<th style="border:1px solid #e2e8f0; padding:0.5rem;">${escapeHtml(c)}</th>`).join('')}</tr>
                </thead>
                <tbody id="tableBody">
                    ${rows.map(row => `<tr>${row.map(cell => `<td style="border:1px solid #e2e8f0; padding:0.5rem;">${escapeHtml(String(cell ?? ''))}</td>`).join('')}</tr>`).join('')}
                </tbody>
            </table>
        </div>
        <p id="rowCount" style="margin-top:0.5rem;">${rows.length} row(s) loaded</p>
    `;
    contentArea.innerHTML = html;
    attachSearchHandlers(tableName);
    attachScrollListener();
}

function appendRows(rows) {
    const tbody = document.getElementById('tableBody');
    if (!tbody) return;
    rows.forEach(row => {
        const tr = document.createElement('tr');
        row.forEach(cell => {
            const td = document.createElement('td');
            td.style.border = '1px solid #e2e8f0';
            td.style.padding = '0.5rem';
            td.textContent = escapeHtml(String(cell ?? ''));
            tr.appendChild(td);
        });
        tbody.appendChild(tr);
    });
    const rowCountSpan = document.getElementById('rowCount');
    if (rowCountSpan) {
        const current = parseInt(rowCountSpan.textContent) || 0;
        rowCountSpan.textContent = `${current + rows.length} row(s) loaded`;
    }
}

function showNoMoreMessage() {
    let msg = document.getElementById('noMoreMsg');
    if (!msg) {
        msg = document.createElement('p');
        msg.id = 'noMoreMsg';
        msg.style.color = '#64748b';
        msg.style.textAlign = 'center';
        msg.style.marginTop = '1rem';
        document.getElementById('contentArea').appendChild(msg);
    }
    msg.textContent = '✓ No more rows to load.';
}

function attachScrollListener() {
    const container = document.querySelector('.table-scroll-container');
    if (!container) return;
    const onScroll = () => {
        if (isLoading || !hasMore) return;
        const scrollTop = container.scrollTop;
        const scrollHeight = container.scrollHeight;
        const clientHeight = container.clientHeight;
        if (scrollTop + clientHeight >= scrollHeight - 100) {
            loadMoreRows(false);
        }
    };
    container.removeEventListener('scroll', onScroll);
    container.addEventListener('scroll', onScroll);
}

function attachSearchHandlers(tableName) {
    const searchBtn = document.getElementById('searchContentBtn');
    if (searchBtn) {
        const newBtn = searchBtn.cloneNode(true);
        searchBtn.parentNode.replaceChild(newBtn, searchBtn);
        newBtn.addEventListener('click', () => {
            const filter = document.getElementById('contentSearch').value;
            resetAndLoadTable(tableName, filter);
        });
    }
    const clearBtn = document.getElementById('clearSearchBtn');
    if (clearBtn) {
        const newClear = clearBtn.cloneNode(true);
        clearBtn.parentNode.replaceChild(newClear, clearBtn);
        newClear.addEventListener('click', () => resetAndLoadTable(tableName, ''));
    }
}

function escapeHtml(str) {
    return str.replace(/[&<>]/g, function(m) {
        if (m === '&') return '&amp;';
        if (m === '<') return '&lt;';
        if (m === '>') return '&gt;';
        return m;
    });
}

document.getElementById('tableSearch').addEventListener('input', (e) => {
    const term = e.target.value.toLowerCase();
    const filtered = allTables.filter(t => t.toLowerCase().includes(term));
    renderTableList(filtered);
});

loadTableList();
