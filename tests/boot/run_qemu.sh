#!/bin/sh
# Boots build/zto2.iso under QEMU and checks the serial output.
# Builds nothing. Env: ZTO2_EXPECT (regex, default 'ZTO2-SELFTEST: PASS'),
# ZTO2_ISO (ISO path, default build/zto2.iso).
# Always fails on 'ZTO2-SELFTEST: FAIL'; fails on 'BUGCHECK' unless ZTO2_EXPECT
# itself mentions BUGCHECK (the fault test expects one).
# Default mode (ZTO2_EXPECT unset) also requires a 'ZTO2-SELFTEST: ok <name>'
# line for every name in tests/boot/expected_selftests.txt, so a self-test
# dropped from the kernel's list cannot pass silently.
# QEMU status 33 is the normal clean exit (isa-debug-exit, value 0x10); 0 is
# accepted too. Status 124 (timeout) is accepted only when ZTO2_EXPECT mentions
# BUGCHECK; otherwise a hang is a failure.
# Exit: 0 = expected output seen, 77 = QEMU missing (skipped), 1 = failure.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ISO=${ZTO2_ISO:-"$ROOT/build/zto2.iso"}
MANIFEST="$ROOT/tests/boot/expected_selftests.txt"
EXPECT=${ZTO2_EXPECT:-}
DEFAULT_MODE=0
if [ -z "$EXPECT" ]; then
    DEFAULT_MODE=1
    EXPECT='ZTO2-SELFTEST: PASS'
fi
EXPECT_BUGCHECK=0
if printf '%s\n' "$EXPECT" | grep -q 'BUGCHECK'; then
    EXPECT_BUGCHECK=1
fi

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
RC=$?
printf '%s\n' "$OUT"

case $RC in
    0|33) ;;
    124)
        # A kernel that halts after a bugcheck never exits QEMU, so the fault
        # test relies on the timeout; anything else timing out is a hang.
        if [ "$EXPECT_BUGCHECK" -ne 1 ]; then
            echo "FAIL: qemu timed out (kernel hung or did not exit)"
            exit 1
        fi
        ;;
    *) echo "FAIL: qemu exited with status $RC"; exit 1 ;;
esac
if printf '%s\n' "$OUT" | grep -q 'ZTO2-SELFTEST: FAIL'; then
    echo "FAIL: guest reported ZTO2-SELFTEST: FAIL"
    exit 1
fi
if [ "$EXPECT_BUGCHECK" -ne 1 ] && printf '%s\n' "$OUT" | grep -q 'BUGCHECK'; then
    echo "FAIL: guest bugchecked (ZTO2_EXPECT does not mention BUGCHECK)"
    exit 1
fi
if ! printf '%s\n' "$OUT" | grep -Eq -e "$EXPECT"; then
    echo "FAIL: output did not match '$EXPECT'"
    exit 1
fi
if [ "$EXPECT_BUGCHECK" -eq 1 ] && printf '%s\n' "$OUT" | grep -q 'ZTO2-SELFTEST: PASS'; then
    echo "FAIL: fault test reached ZTO2-SELFTEST: PASS"
    exit 1
fi
if [ "$DEFAULT_MODE" -eq 1 ]; then
    if [ ! -f "$MANIFEST" ]; then
        echo "FAIL: $MANIFEST not found"
        exit 1
    fi
    NAMES=$(grep -Ev '^[[:space:]]*(#|$)' "$MANIFEST")
    if [ -z "$NAMES" ]; then
        echo "FAIL: $MANIFEST lists no self-tests"
        exit 1
    fi
    LINES=$(printf '%s\n' "$OUT" | tr -d '\r')
    MISSING=0
    for NAME in $NAMES; do
        if ! printf '%s\n' "$LINES" | grep -Fxq "ZTO2-SELFTEST: ok $NAME"; then
            echo "FAIL: missing 'ZTO2-SELFTEST: ok $NAME'"
            MISSING=1
        fi
    done
    [ "$MISSING" -eq 0 ] || exit 1
fi
exit 0
