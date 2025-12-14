#!/usr/bin/env bash
set -euo pipefail

# Create ~/.config/octopus and a random password file, then create an encrypted
# tarball of the provided source and move it into /opt/octopus owned by root.

CONFIG_DIR="$HOME/.config/octopus"
PASSWORD_FILE="$CONFIG_DIR/password.txt"

mkdir -p "$CONFIG_DIR"

if [ -f "$PASSWORD_FILE" ]; then
  echo "Password file already exists at $PASSWORD_FILE"
else
  # generate a 256-bit hex password
  if command -v openssl >/dev/null 2>&1; then
    openssl rand -hex 32 > "$PASSWORD_FILE"
  else
    # fallback to /dev/urandom+sha256
    head -c 64 /dev/urandom | sha256sum | awk '{print $1}' > "$PASSWORD_FILE"
  fi
  chmod 600 "$PASSWORD_FILE"
  echo "Wrote password to $PASSWORD_FILE"
fi

if [ $# -lt 1 ]; then
  echo "Usage: $0 <path-to-file-or-dir-to-archive>"
  exit 2
fi

SRC="$1"
if [ ! -e "$SRC" ]; then
  echo "Source path does not exist: $SRC" >&2
  exit 3
fi

TMP_ENC=$(mktemp /tmp/octopus.XXXXXX.enc)

# Create a pipe: tar the provided path (preserve relative name) and encrypt to tmp
SRCDIR=$(dirname "$SRC")
SRCBASE=$(basename "$SRC")

tar -C "$SRCDIR" -czf - "$SRCBASE" | \
  openssl enc -aes-256-cbc -salt -pbkdf2 -pass file:"$PASSWORD_FILE" -out "$TMP_ENC"

echo "Encrypted archive created at $TMP_ENC"

# Create /opt/octopus and move the encrypted file there as root (prompts for sudo password)
sudo mkdir -p /opt/octopus
sudo mv "$TMP_ENC" /opt/octopus/store.tar.gz.enc
sudo chown root:root /opt/octopus
sudo chown root:root /opt/octopus/store.tar.gz.enc
sudo chmod 0700 /opt/octopus
sudo chmod 0600 /opt/octopus/store.tar.gz.enc

echo "Encrypted archive moved to /opt/octopus/store.tar.gz.enc (owned by root)."
echo "To decrypt later, use scripts/octopus_extract.sh which will prompt for sudo." 
