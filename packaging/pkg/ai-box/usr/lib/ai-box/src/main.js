const { app, BrowserWindow, BrowserView, ipcMain, nativeTheme, session, dialog } = require('electron');
const path = require('path');
const fs = require('fs');
const os = require('os');
const { exec } = require('child_process');

let mainWindow;
const views = {}; // Key: serviceId -> BrowserView
const TAB_HEIGHT = 50;
let currentMode = 'standard'; // 'standard', 'enterprise', or 'media'

// Service Configuration
const standardServices = [
    { id: 'gemini', url: 'https://gemini.google.com' },
    { id: 'studio', url: 'https://aistudio.google.com' },
    { id: 'chatgpt', url: 'https://chatgpt.com' },
    { id: 'claude', url: 'https://claude.ai' },
    { id: 'copilot', url: 'https://copilot.microsoft.com' }
];

const enterpriseServices = [
    { id: 'vertex', url: 'https://console.cloud.google.com/vertex-ai' },
    { id: 'openai-platform', url: 'https://platform.openai.com' },
    { id: 'azure-ai', url: 'https://ai.azure.com' },
    { id: 'watsonx', url: 'https://dataplatform.cloud.ibm.com/' }
];

const mediaServices = [
    { id: 'midjourney', url: 'https://www.midjourney.com/' },
    { id: 'dalle', url: 'https://chatgpt.com/?model=gpt-4' },
    { id: 'stable-diffusion', url: 'https://stablediffusionweb.com/' },
    { id: 'runway', url: 'https://runwayml.com/' },
    { id: 'pika', url: 'https://pika.art/' }
];

// Dark Mode Enforcement
nativeTheme.themeSource = 'dark';

function createWindow() {
    mainWindow = new BrowserWindow({
        width: 1200,
        height: 800,
        titleBarStyle: 'hidden',
        titleBarOverlay: {
            color: 'rgba(0,0,0,0)',
            symbolColor: '#ffffff'
        },
        webPreferences: {
            nodeIntegration: true,
            contextIsolation: false
        },
        backgroundColor: '#131314'
    });

    mainWindow.loadFile(path.join(__dirname, 'index.html'));

    setupAdBlocking();
    initializeViews([...standardServices, ...enterpriseServices, ...mediaServices]);

    // Initial render (defaults to Standard)
    setActiveMode('standard');

    mainWindow.on('resize', () => {
        const active = mainWindow.getBrowserView();
        if (active) updateViewBounds(active);
    });
}

function setupAdBlocking() {
    const filter = {
        urls: ['*://*.doubleclick.net/*', '*://*.googleadservices.com/*', '*://*.googlesyndication.com/*', '*://*.moatads.com/*']
    };

    session.defaultSession.webRequest.onBeforeRequest(filter, (details, callback) => {
        callback({ cancel: true });
    });
}

function initializeViews(serviceList) {
    serviceList.forEach(service => {
        const view = new BrowserView({
            webPreferences: {
                nodeIntegration: false,
                contextIsolation: true
            }
        });
        view.webContents.loadURL(service.url);
        views[service.id] = view;
    });
}

function updateViewBounds(view) {
    const contentBounds = mainWindow.getContentBounds();
    view.setBounds({
        x: 0,
        y: TAB_HEIGHT,
        width: contentBounds.width,
        height: contentBounds.height - TAB_HEIGHT
    });
}

function setActiveMode(mode) {
    currentMode = mode;
    let currentSet;

    if (mode === 'enterprise') {
        currentSet = enterpriseServices;
    } else if (mode === 'media') {
        currentSet = mediaServices;
    } else {
        currentSet = standardServices;
    }

    // Inform renderer to update tabs
    mainWindow.webContents.send('update-tabs', currentSet);

    // Switch to first tab of the new set
    setActiveView(currentSet[0].id);
}

function setActiveView(viewId) {
    const view = views[viewId];
    if (!view) return;

    mainWindow.setBrowserView(view);
    updateViewBounds(view);
}

app.whenReady().then(() => {
    createWindow();

    app.on('activate', () => {
        if (BrowserWindow.getAllWindows().length === 0) createWindow();
    });
});

app.on('window-all-closed', () => {
    if (process.platform !== 'darwin') app.quit();
});

ipcMain.on('switch-tab', (event, tabId) => {
    setActiveView(tabId);
});

ipcMain.on('switch-mode', (event, mode) => {
    setActiveMode(mode);
});

ipcMain.on('integrate-desktop', (event) => {
    const homeDir = os.homedir();
    const applicationsDir = path.join(homeDir, '.local', 'share', 'applications');
    const desktopFile = path.join(applicationsDir, 'gemini-wrapper.desktop');

    if (!fs.existsSync(applicationsDir)) {
        fs.mkdirSync(applicationsDir, { recursive: true });
    }

    const content = [
        '[Desktop Entry]',
        'Name=Gemini AI Wrapper',
        'Comment=Premium AI Client',
        'Exec=npm start --prefix "' + path.resolve(__dirname, '..') + '"',
        'Icon=utilities-terminal',
        'Terminal=false',
        'Type=Application',
        'Categories=Utility;Accessory;',
        ''
    ].join('\n');

    try {
        fs.writeFileSync(desktopFile, content);
        dialog.showMessageBox(mainWindow, { type: 'info', message: 'Successfully added to Desktop/Menu!', buttons: ['OK'] });
    } catch (err) {
        dialog.showErrorBox('Integration Failed', err.message);
    }
});

ipcMain.on('integrate-vscode', (event) => {
    const homeDir = os.homedir();
    const binDir = path.join(homeDir, '.local', 'bin');
    const scriptFile = path.join(binDir, 'gemini-ai');

    if (!fs.existsSync(binDir)) {
        fs.mkdirSync(binDir, { recursive: true });
    }

    const content = [
        '#!/bin/bash',
        'cd "' + path.resolve(__dirname, '..') + '"',
        'npm start > /dev/null 2>&1 &',
        ''
    ].join('\n');

    try {
        fs.writeFileSync(scriptFile, content);
        fs.chmodSync(scriptFile, '755');
        dialog.showMessageBox(mainWindow, { type: 'info', message: 'Successfully created "gemini-ai" command!', buttons: ['OK'] });
    } catch (err) {
        dialog.showErrorBox('Integration Failed', err.message);
    }
});

// Ollama Integration
ipcMain.on('check-ollama', (event) => {
    exec('which ollama', (error, stdout) => {
        const installed = !error && stdout.trim().length > 0;
        event.reply('ollama-status', { installed });
    });
});

ipcMain.on('install-ollama', (event) => {
    const installCmd = 'curl -fsSL https://ollama.com/install.sh | sh';

    exec(installCmd, (error, stdout, stderr) => {
        if (error) {
            event.reply('ollama-install-result', { success: false, error: error.message });
        } else {
            event.reply('ollama-install-result', { success: true });
        }
    });
});

ipcMain.on('ollama-chat', (event, { model, message }) => {
    const fetch = require('node-fetch');

    fetch('http://localhost:11434/api/generate', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
            model: model,
            prompt: message,
            stream: false
        })
    })
        .then(res => res.json())
        .then(data => {
            event.reply('ollama-response', { response: data.response });
        })
        .catch(err => {
            event.reply('ollama-response', { error: err.message });
        });
});
