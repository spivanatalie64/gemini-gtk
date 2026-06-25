# gemini-gtk (AI-Box)

A multi-AI service wrapper ("AI-Box") that provides a unified desktop interface for multiple AI chat platforms including Gemini, ChatGPT, Claude, Copilot, and more.

## Components

- **Electron app** (active): Modern desktop client with tabbed browsing across AI services, local Ollama support, dark mode, and desktop integration
- **Legacy GTK3 app** (legacy): C-based GTK3 client in `legacy_gtk/` for Google Gemini API with encrypted API key storage

## Installation and Usage

```bash
npm install
npm start
```

## Legacy GTK3 App

The `legacy_gtk/` directory contains a C/GTK3 application that interfaces with the Google Gemini API. Build with:

```bash
cd legacy_gtk
make
```

Requires `gtk+-3.0`, `json-c`, `libcurl`, and `libsodium`.

## Features

- Tabbed browsing across Gemini, AI Studio, ChatGPT, Claude, Copilot, and more
- Three mode groups: Standard, Enterprise, and Media AI services
- Local AI support via Ollama with built-in chat panel
- Desktop environment integration (`.desktop` file for Cinnamon/GNOME)
- Terminal command integration (`gemini-ai` in `~/.local/bin/`)
- Hardware-accelerated ad blocking
- Dark mode enforced
- VSCode integration support

## Keyboard Shortcuts

(No custom keyboard shortcuts currently implemented)

## Dependencies

- Node.js
- Electron
- node-fetch (^2.7.0)
- Ollama (optional, for local AI support)

## License

GPL-2.0
