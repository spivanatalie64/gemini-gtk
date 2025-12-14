#!/usr/bin/env bash
set -euo pipefail

# Extract the encrypted store from /opt/octopus using the locally stored password.
# This script will use sudo to read the encrypted file (so the user will be prompted
# for their sudo password) and then decrypt using the password in ~/.config/octopus/password.txt

CONFIG_DIR="$HOME/.config/octopus"
PASSWORD_FILE="$CONFIG_DIR/password.txt"

if [ ! -f "$PASSWORD_FILE" ]; then
  echo "Password file not found at $PASSWORD_FILE" >&2
  echo "Run scripts/octopus_init.sh first to create the password and store the archive." >&2
  exit 1
fi

DEST="${1:-$PWD}"
if [ ! -d "$DEST" ]; then
  echo "Destination directory does not exist: $DEST" >&2
  exit 2
fi

TMP_ENC=$(mktemp /tmp/octopus.XXXXXX.enc)

# Use sudo to read the root-owned encrypted file into a temporary file. This will prompt for sudo password.
sudo cat /opt/octopus/store.tar.gz.enc > "$TMP_ENC"

# Decrypt using openssl reading password from the password file (no password on command line)
openssl enc -d -aes-256-cbc -pbkdf2 -pass file:"$PASSWORD_FILE" -in "$TMP_ENC" | tar -xz -C "$DEST"

rm -f "$TMP_ENC"
echo "Extraction completed to $DEST"
