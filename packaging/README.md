# AI-Box Packaging

This directory contains packaging files for distribution.

## PKGBUILD (Arch Linux)

Build and install the package:

```bash
cd packaging
makepkg -si
```

This will:
- Clone the repository
- Install npm dependencies
- Package the application to `/usr/lib/ai-box`
- Create a launcher script at `/usr/bin/ai-box`
- Install a desktop entry

## Usage After Installation

Run from terminal:
```bash
ai-box
```

Or launch from your application menu: **AI-Box**

## Dependencies

- `electron` - Electron framework
- `nodejs` - Node.js runtime
- `npm` - Node package manager
- `ollama` (optional) - For local AI support
