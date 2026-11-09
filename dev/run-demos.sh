#!/usr/bin/env bash
# Headless UI checks (no window on the real session, no system changes).
#   dev/run-demos.sh [outdir]   → PNG screenshots of the main states
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-/tmp/things-shots}
mkdir -p "$OUT" "$OUT/bin"
export QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1

# 1) tour: search / detail / installed / dialogs / ops queue (dry-run: nothing is executed)
./things --dry-run --search firefox --demo tour --shot-dir "$OUT"

# 2) real privilege pipeline with a fake `sudo` (password: demo): dialog, wrong password,
#    askpass over the in-RAM socket, live output.
cp dev/fake-sudo "$OUT/bin/sudo" && chmod +x "$OUT/bin/sudo"
PATH="$OUT/bin:$PATH" ./things --demo password --shot-dir "$OUT"
echo "screenshots in $OUT"
