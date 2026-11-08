#!/bin/sh
# Boots build/zto2.iso under QEMU and checks the serial output.
# Builds nothing. Env: ZTO2_EXPECT (regex, default 'ZTO2-SELFTEST: PASS').
# Exit: 0 = expected output seen, 77 = QEMU missing (skipped), 1 = failure.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ISO="$ROOT/build/zto2.iso"
EXPECT=${ZTO2_EXPECT:-'ZTO2-SELFTEST: PASS'}

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "SKIP: qemu-system-x86_64 not found"
    exit 77
fi
if [ ! -f "$ISO" ]; then
    echo "FAIL: $ISO not found (run 'make iso')"
    exit 1
fi

OUT=$(timeout 30 qemu-system-x86_64 -cdrom "$ISO" -serial stdio -display none \
    -no-reboot -m 256 -device isa-debug-exit,iobase=0xf4,iosize=0x04 2>&1 </dev/null)
printf '%s\n' "$OUT"

if printf '%s\n' "$OUT" | grep -q 'ZTO2-SELFTEST: FAIL'; then
    echo "FAIL: guest reported ZTO2-SELFTEST: FAIL"
    exit 1
fi
if printf '%s\n' "$OUT" | grep -Eq "$EXPECT"; then
    exit 0
fi
echo "FAIL: output did not match '$EXPECT'"
exit 1
