const slider = document.getElementById('thresholdSlider');
const thresholdSpan = document.getElementById('thresholdValue');
slider.addEventListener('input', function() {
    thresholdSpan.textContent = slider.value;
});

const chatDiv = document.getElementById('chat');
const promptInput = document.getElementById('promptInput');
const askBtn = document.getElementById('askBtn');

function escapeHtml(text) {
    const div = document.createElement('div');
    div.appendChild(document.createTextNode(text));
    return div.innerHTML;
}

function isCode(text) {
    if (text.indexOf('\n') === -1) return false;
    const lines = text.split('\n');
    let indentCount = 0, nonEmpty = 0;
    for (const line of lines) {
        if (line.trim() === '') continue;
        nonEmpty++;
        if (line.charAt(0) === ' ' || line.charAt(0) === '\t') indentCount++;
    }
    if (nonEmpty > 0 && indentCount / nonEmpty > 0.3) return true;
    const codeChars = /[{}();\[\]=+\-*/<>!&|^~%#]/g;
    const charMatches = text.match(codeChars);
    return charMatches && charMatches.length > 3;
}

function addMessage(role, text, chunkId) {
    const msgDiv = document.createElement('div');
    msgDiv.className = 'message';

    const roleSpan = document.createElement('span');
    roleSpan.className = role;
    roleSpan.textContent = (role === 'user' ? 'You: ' : 'Assistant: ');

    const contentSpan = document.createElement('span');
    if (role === 'assistant' && isCode(text)) {
        contentSpan.innerHTML = '<pre><code>' + escapeHtml(text) + '</code></pre>';
    } else {
        contentSpan.textContent = text;
    }

    msgDiv.appendChild(roleSpan);
    msgDiv.appendChild(contentSpan);

    if (role === 'assistant' && chunkId !== null && chunkId !== -1) {
        const fbSpan = document.createElement('span');
        fbSpan.className = 'feedback';
        fbSpan.innerHTML = ' <span class="thumbs-up" data-chunk="' + chunkId + '">👍</span>' +
                          ' <span class="thumbs-down" data-chunk="' + chunkId + '">👎</span>';
        msgDiv.appendChild(fbSpan);
    }

    chatDiv.appendChild(msgDiv);
    chatDiv.scrollTop = chatDiv.scrollHeight;

    if (role === 'assistant' && chunkId !== null && chunkId !== -1) {
        const up = msgDiv.querySelector('.thumbs-up');
        const down = msgDiv.querySelector('.thumbs-down');
        up.addEventListener('click', () => sendFeedback(chunkId, true, text));
        down.addEventListener('click', () => sendFeedback(chunkId, false, text));
    }
}

async function sendFeedback(chunkId, isPositive, questionText) {
    console.log('Feedback: chunk ' + chunkId + ', positive=' + isPositive + ', question=' + questionText);
    try {
        const resp = await fetch('/feedback', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ chunk_id: chunkId, positive: isPositive, question: questionText })
        });
        if (!resp.ok) throw new Error('Feedback failed');
        const result = await resp.json();
        console.log('Feedback saved:', result);
    } catch (err) {
        console.error('Feedback error:', err);
    }
}

async function askQuestion() {
    const prompt = promptInput.value.trim();
    if (!prompt) return;
    const threshold = slider.value;
    addMessage('user', prompt);
    promptInput.value = '';
    askBtn.disabled = true;
    try {
        const resp = await fetch('/ask', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: 'prompt=' + encodeURIComponent(prompt) + '&threshold=' + encodeURIComponent(threshold)
        });
        const data = await resp.json();
        addMessage('assistant', data.answer, data.chunk_id);
    } catch (err) {
        addMessage('assistant', 'Error: ' + err.message);
    } finally {
        askBtn.disabled = false;
        promptInput.focus();
    }
}

askBtn.addEventListener('click', askQuestion);
document.getElementById('clearBtn').addEventListener('click', () => {
    document.getElementById('chat').innerHTML = '';
});
promptInput.addEventListener('keypress', (e) => {
    if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault();
        askQuestion();
    }
});
promptInput.focus();
