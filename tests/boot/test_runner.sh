#!/bin/sh
# Self-test for run_qemu.sh: runs it against a stub qemu-system-x86_64 that
# replays canned serial logs with a chosen exit status, and checks the runner's
# verdict. Needs no QEMU, ISO or build. Exit 0 = every case behaved as expected.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
RUNNER="$ROOT/tests/boot/run_qemu.sh"
MANIFEST="$ROOT/tests/boot/expected_selftests.txt"
TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT INT TERM

mkdir "$TMP/bin"
cat > "$TMP/bin/qemu-system-x86_64" <<'STUB'
#!/bin/sh
cat "$STUB_LOG"
exit "$STUB_RC"
STUB
chmod +x "$TMP/bin/qemu-system-x86_64"
: > "$TMP/fake.iso"

NAMES=$(grep -Ev '^[[:space:]]*(#|$)' "$MANIFEST")
FIRST=$(printf '%s\n' "$NAMES" | sed -n 1p)

{ echo "zto2 booting"
  for n in $NAMES; do echo "ZTO2-SELFTEST: ok $n"; done
  echo "ZTO2-SELFTEST: PASS"; } > "$TMP/good.log"
grep -Fxv "ZTO2-SELFTEST: ok $FIRST" "$TMP/good.log" > "$TMP/missing.log"
printf 'zto2 booting\n' > "$TMP/nomark.log"
printf 'zto2 booting\n*** BUGCHECK 0x7f (0x8, 0x0, 0x1, 0x0)\n' > "$TMP/df.log"
{ cat "$TMP/df.log"; echo "ZTO2-SELFTEST: PASS"; } > "$TMP/df_pass.log"
{ cat "$TMP/good.log"; echo "ZTO2-SELFTEST: FAIL x"; } > "$TMP/fail.log"
{ cat "$TMP/good.log"; echo "*** BUGCHECK 0x7f (0x1, 0, 0, 0)"; } > "$TMP/bug_pass.log"

BAD=0
# check <name> <want-exit> <log> <qemu-status> [ZTO2_EXPECT]
check() {
    name=$1 want=$2 log=$3 rc=$4 expect=$5
    env -u ZTO2_EXPECT ${expect:+ZTO2_EXPECT="$expect"} \
        PATH="$TMP/bin:$PATH" ZTO2_ISO="$TMP/fake.iso" \
        STUB_LOG="$TMP/$log" STUB_RC="$rc" "$RUNNER" >"$TMP/out.txt" 2>&1
    got=$?
    if [ "$got" -eq "$want" ]; then
        echo "ok   $name (exit $got)"
    else
        echo "FAIL $name: wanted exit $want, got $got"
        sed 's/^/     | /' "$TMP/out.txt"
        BAD=1
    fi
}

check "good log, status 33"                      0 good.log    33
check "good log, status 0"                       0 good.log    0
check "PASS but one ok line missing"             1 missing.log 33
check "good log, status 124 (hang) in default mode" 1 good.log 124
check "no PASS marker"                           1 nomark.log  33
check "qemu error status 1"                      1 good.log    1
check "FAIL line"                                1 fail.log    33
check "bugcheck in default mode"                 1 bug_pass.log 33
check "bugcheck expected, status 124"            0 df.log      124 'BUGCHECK 0x7f \(0x8,'
check "bugcheck expected, status 33"             0 df.log      33  'BUGCHECK 0x7f \(0x8,'
check "bugcheck expected, wrong code"            1 df.log      124 'BUGCHECK 0x50'
check "bugcheck expected but PASS printed"       1 df_pass.log 33  'BUGCHECK 0x7f \(0x8,'
check "custom expect, status 124, no bugcheck"   1 good.log    124 'ZTO2-SELFTEST: PASS'
check "custom expect skips the manifest"         0 missing.log 33  'ZTO2-SELFTEST: PASS'

[ "$BAD" -eq 0 ] && echo "test-runner: all cases passed"
exit "$BAD"
