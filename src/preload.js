const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('electronAPI', {
    switchTab: (tabId) => ipcRenderer.send('switch-tab', tabId),
    switchMode: (mode) => ipcRenderer.send('switch-mode', mode),
    integrateDesktop: () => ipcRenderer.send('integrate-desktop'),
    integrateVscode: () => ipcRenderer.send('integrate-vscode'),
    checkOllama: () => ipcRenderer.send('check-ollama'),
    installOllama: () => ipcRenderer.send('install-ollama'),
    ollamaChat: (data) => ipcRenderer.send('ollama-chat', data),
    onUpdateTabs: (callback) => {
        ipcRenderer.on('update-tabs', (_event, services) => callback(services));
    },
    onOllamaStatus: (callback) => {
        ipcRenderer.on('ollama-status', (_event, data) => callback(data));
    },
    onOllamaInstallResult: (callback) => {
        ipcRenderer.on('ollama-install-result', (_event, data) => callback(data));
    },
    onOllamaResponse: (callback) => {
        ipcRenderer.on('ollama-response', (_event, data) => callback(data));
    }
});
