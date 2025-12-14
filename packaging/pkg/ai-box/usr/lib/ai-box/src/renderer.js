const { ipcRenderer } = require('electron');

const icons = {
    'gemini': '✨',
    'studio': '🧠',
    'chatgpt': '🤖',
    'claude': '🎭',
    'copilot': '✈️',
    'vertex': '🌩️',
    'openai-platform': '⚙️',
    'azure-ai': '🔷',
    'watsonx': '🧠',
    'midjourney': '🎨',
    'dalle': '🖼️',
    'stable-diffusion': '🌈',
    'runway': '🎬',
    'pika': '⚡'
};

const labels = {
    'gemini': 'Gemini',
    'studio': 'AI Studio',
    'chatgpt': 'ChatGPT',
    'claude': 'Claude',
    'copilot': 'Copilot',
    'vertex': 'Vertex AI',
    'openai-platform': 'OpenAI Platform',
    'azure-ai': 'Azure AI',
    'watsonx': 'WatsonX',
    'midjourney': 'Midjourney',
    'dalle': 'DALL-E',
    'stable-diffusion': 'Stable Diffusion',
    'runway': 'Runway',
    'pika': 'Pika'
};

ipcRenderer.on('update-tabs', (event, services) => {
    const container = document.getElementById('tabs-container');
    container.innerHTML = ''; // Clear existing

    services.forEach((service, index) => {
        const tab = document.createElement('div');
        tab.className = `tab ${index === 0 ? 'active' : ''}`;
        tab.id = `tab-${service.id}`;
        tab.onclick = () => switchTab(service.id);

        // Icon
        const iconSpan = document.createElement('span');
        iconSpan.className = 'icon';
        iconSpan.innerText = icons[service.id] || '🔗';

        // Text
        const textNode = document.createTextNode(labels[service.id] || service.id);

        tab.appendChild(iconSpan);
        tab.appendChild(textNode);
        container.appendChild(tab);
    });
});

function switchTab(tabId) {
    // Visual Update
    document.querySelectorAll('.tab').forEach(el => el.classList.remove('active'));
    const activeTab = document.getElementById(`tab-${tabId}`);
    if (activeTab) activeTab.classList.add('active');

    // Logic Update
    ipcRenderer.send('switch-tab', tabId);
}

// Mode Switching
function switchMode(mode) {
    // Visual Update
    document.querySelectorAll('.mode-btn').forEach(btn => btn.classList.remove('active'));
    const activeBtn = document.querySelector(`[data-mode="${mode}"]`);
    if (activeBtn) activeBtn.classList.add('active');

    // Logic Update
    ipcRenderer.send('switch-mode', mode);
}

function integrateDesktop() {
    ipcRenderer.send('integrate-desktop');
}

function openVSCodeDialog() {
    document.getElementById('vscode-dialog').style.display = 'flex';
}

function closeVSCodeDialog() {
    document.getElementById('vscode-dialog').style.display = 'none';
}

function confirmVSCodeIntegration() {
    ipcRenderer.send('integrate-vscode');
    closeVSCodeDialog();
}

// Local AI Panel
function toggleLocalPanel() {
    const panel = document.getElementById('local-panel');
    panel.classList.toggle('open');
}

function checkOllamaStatus() {
    ipcRenderer.send('check-ollama');
}

function installOllama() {
    document.getElementById('status-text').textContent = 'Installing Ollama...';
    ipcRenderer.send('install-ollama');
}

function sendLocalMessage() {
    const input = document.getElementById('local-chat-input');
    const message = input.value.trim();
    if (!message) return;

    const model = document.getElementById('model-select').value;

    // Add user message to chat
    addChatMessage('user', message);
    input.value = '';

    // Send to Ollama
    ipcRenderer.send('ollama-chat', { model, message });
}

function addChatMessage(role, content) {
    const messagesDiv = document.getElementById('chat-messages');
    const msgDiv = document.createElement('div');
    msgDiv.className = `message ${role}`;
    msgDiv.textContent = content;
    messagesDiv.appendChild(msgDiv);
    messagesDiv.scrollTop = messagesDiv.scrollHeight;
}

// IPC Listeners
ipcRenderer.on('ollama-status', (event, { installed }) => {
    const statusDot = document.getElementById('status-dot');
    const statusText = document.getElementById('status-text');
    const installSection = document.getElementById('install-section');
    const chatSection = document.getElementById('chat-section');

    if (installed) {
        statusDot.className = 'status-dot online';
        statusText.textContent = 'Ollama is running';
        installSection.style.display = 'none';
        chatSection.style.display = 'block';
    } else {
        statusDot.className = 'status-dot offline';
        statusText.textContent = 'Ollama not installed';
        installSection.style.display = 'block';
        chatSection.style.display = 'none';
    }
});

ipcRenderer.on('ollama-install-result', (event, { success, error }) => {
    if (success) {
        checkOllamaStatus();
    } else {
        alert('Installation failed: ' + error);
    }
});

ipcRenderer.on('ollama-response', (event, { response, error }) => {
    if (error) {
        addChatMessage('assistant', 'Error: ' + error);
    } else {
        addChatMessage('assistant', response);
    }
});

// Check Ollama status on load
setTimeout(checkOllamaStatus, 1000);
