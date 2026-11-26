#!/bin/bash
exec > /mnt/c/Users/nevin/Downloads/neon-test.log 2>&1
cd /mnt/c/Users/nevin/Documents/Codex/2026-11-12/le/work/PS3-Music-Visualizers/standalone || exit 1
make host-test 2>&1 | grep -E "passed|FAIL|Error|failures"
echo "=== HOST TEST SCRIPT DONE ==="
