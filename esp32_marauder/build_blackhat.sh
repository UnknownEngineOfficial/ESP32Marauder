#!/usr/bin/env bash
# Build wrapper: injects real credentials from secrets.env as -D flags.
# Run from repo root (or anywhere — it resolves paths relative to itself).
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Script lives inside the sketch dir (esp32_marauder/), so PROJECT == DIR.
PROJECT="$DIR"
SECRETS="$PROJECT/secrets.env"

FQBN="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=min_spiffs"

# --- load secrets.env (KEY=VALUE) into EXTRA_FLAGS as -DKEY=VALUE ---
EXTRA_FLAGS=""
if [[ -f "$SECRETS" ]]; then
  while IFS='=' read -r key val; do
    [[ -z "$key" || "$key" == \#* ]] && continue
    key="${key//[$'\r\n']/}"
    val="${val//[$'\r\n']/}"
    EXTRA_FLAGS="$EXTRA_FLAGS -D${key}=\"${val}\""
  done < "$SECRETS"
else
  echo "WARNING: $SECRETS not found — building with CHANGE_ME placeholders." >&2
fi

# --- fixed diag/feature flags ---
EXTRA_FLAGS="$EXTRA_FLAGS -DHEADLESS_DIAG -DAPI_AP_ONLY_DIAG -DAPI_WIFI_OWNER"

echo "=== extra flags ==="
echo "$EXTRA_FLAGS"

arduino-cli compile \
  --fqbn "$FQBN" \
  --build-property "compiler.cpp.extra_flags=$EXTRA_FLAGS" \
  --build-property "compiler.optimization_flags=-Os" \
  "$PROJECT"
