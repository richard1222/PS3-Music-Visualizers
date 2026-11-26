#!/bin/bash
exec > /mnt/c/Users/nevin/Downloads/neon-build.log 2>&1
cd /mnt/c/Users/nevin/Documents/Codex/2026-11-12/le/work/PS3-Music-Visualizers/standalone || exit 1
echo "=== HOST TESTS ==="
make host-test 2>&1 | tail -25
echo "=== BUILD PKG ==="
make pkg 2>&1 | tail -40
echo "=== RESULT ==="
ls -la neon_lounge.pkg
sha256sum neon_lounge.pkg
echo "=== BUILD SCRIPT DONE ==="
