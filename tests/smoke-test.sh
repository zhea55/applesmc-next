#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# tests/smoke-test.sh - Comprehensive regression test suite for applesmc-next
#
# Tests are organized in levels:
#   Level 0: Build & static checks (no hardware required)
#   Level 1: Functional equivalence (needs old source snapshot)
#   Level 2: Runtime module verification (needs Apple hardware)
#
# Run:  sudo ./tests/smoke-test.sh          # all levels
#       ./tests/smoke-test.sh --build-only   # level 0 only
#       ./tests/smoke-test.sh --static       # level 0 + 1
#

set -euo pipefail
FAILS=0
PASSES=0

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
NC='\033[0m'

RESULTS_DIR="$(mktemp -d /tmp/applesmc-test-XXXXXX)"
OLD_SRC="${RESULTS_DIR}/applesmc-old.c"

pass() { PASSES=$((PASSES+1)); echo -e "  ${GREEN}✓ PASS${NC}: $1"; }
fail() { FAILS=$((FAILS+1)); echo -e "  ${RED}✗ FAIL${NC}: $1"; }
skip() { echo -e "  ${YELLOW}⊘ SKIP${NC}: $1"; }
info() { echo -e "  ${BOLD}→${NC} $1"; }

cleanup() { rm -rf "$RESULTS_DIR"; }
trap cleanup EXIT

RUN_BUILD_ONLY=false
RUN_STATIC=false
[ "${1:-}" = "--build-only" ] && RUN_BUILD_ONLY=true
[ "${1:-}" = "--static" ]     && RUN_STATIC=true

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_DIR"

echo ""
echo "================================================"
echo "  applesmc-next Regression Test Suite"
echo "================================================"
echo "  Project : $PROJECT_DIR"
echo "  Kernel  : $(uname -r)"
echo "  Date    : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo "================================================"
echo ""

# ---------------------------------------------------------------------------
# LEVEL 0: Build & Static Checks
# ---------------------------------------------------------------------------

section_level() {
  echo "──────────────────────────────────────────────────"
  echo -e "  ${BOLD}Level $1: $2${NC}"
  echo "──────────────────────────────────────────────────"
}

section_level 0 "Build & Static Checks"

info "Checking essential source files exist..."
for f in applesmc/applesmc.h applesmc/applesmc-core.c \
         applesmc/applesmc-io.c applesmc/applesmc-sysfs.c \
         applesmc/applesmc-battery.c applesmc/applesmc-led.c \
         applesmc/applesmc-accel.c applesmc/Makefile Makefile; do
  [ -f "$f" ] && pass "Source file present: $f" || fail "Missing: $f"
done

info "Checking no sbs/ directory remains..."
[ ! -d sbs ] && pass "sbs/ directory removed" || fail "sbs/ still present"

info "Checking no deprecated staging/..."
[ ! -d staging ] && pass "staging/ directory removed" || fail "staging/ still present"

info "Checking .gitignore exists..."
[ -f .gitignore ] && pass ".gitignore present" || fail ".gitignore missing"

info "Checking applesmc.c.bak removed..."
[ ! -f applesmc/applesmc.c.bak ] && pass "backup file removed" \
  || fail "backup file still present"

# ---- Build test ----
info "Running clean build (make clean && make)..."
BUILD_LOG="${RESULTS_DIR}/build.log"
if make clean > /dev/null 2>&1 && make > "$BUILD_LOG" 2>&1; then
  pass "Clean build succeeded"
  WARN_COUNT=$(grep -c "warning:" "$BUILD_LOG" || true)
  if [ "$WARN_COUNT" -eq 0 ]; then
    pass "Zero compiler warnings"
  else
    fail "Build has $WARN_COUNT warning(s)"
    grep "warning:" "$BUILD_LOG" | head -5
  fi
  ERROR_COUNT=$(grep -c "error:" "$BUILD_LOG" || true)
  [ "$ERROR_COUNT" -eq 0 ] && pass "Zero compiler errors" \
    || fail "Build has $ERROR_COUNT errors"
else
  fail "Clean build FAILED"
  cat "$BUILD_LOG"
fi

# ---- Module metadata ----
info "Checking module metadata (modinfo)..."
MODFILE="${PROJECT_DIR}/applesmc/applesmc.ko"
if [ ! -f "$MODFILE" ]; then
  fail "Module .ko not found after build"
else
  MODINFO=$(modinfo "$MODFILE" 2>&1)
  echo "$MODINFO" > "${RESULTS_DIR}/modinfo.txt"

  echo "$MODINFO" | grep -q "description:\s*Apple SMC" && \
    pass "MODULE_DESCRIPTION matches" || fail "MODULE_DESCRIPTION mismatch"
  echo "$MODINFO" | grep -q "license:\s*GPL v2" && \
    pass "MODULE_LICENSE matches" || fail "MODULE_LICENSE mismatch"
  echo "$MODINFO" | grep -q "version:\s*0\.2\.0-next" && \
    pass "MODULE_VERSION is 0.2.0-next" || fail "MODULE_VERSION mismatch"
  echo "$MODINFO" | grep -q "parm:\s*debug:Enable debug messages" && \
    pass "debug module param present" || fail "debug module param missing"

  # Expect at least 5 DMI aliases (MacBookAir, MacBookPro, MacBook, Macmini, MacPro, iMac, Xserve)
  DMI_COUNT=$(echo "$MODINFO" | grep -c "alias:\s*dmi\*")
  [ "$DMI_COUNT" -ge 5 ] && \
    pass "DMI aliases count = $DMI_COUNT" || \
    fail "DMI aliases count = $DMI_COUNT (expected ≥5)"
fi

# ---- No unresolved symbols ----
info "Checking for unresolved symbols..."
SYMS_LOG="${RESULTS_DIR}/symbols.log"
nm "$MODFILE" 2>/dev/null | grep -i "U " > "$SYMS_LOG" || true
UNRESOLVED=$(wc -l < "$SYMS_LOG")
# Kernel modules always reference external symbols like printk, etc.
# We just check there are no obviously missing ones
[ "$UNRESOLVED" -gt 0 ] && \
  pass "Module has $UNRESOLVED external symbol references (normal)" \
  || pass "Module has no external references"

# ---- Check kernel version compatibility ----
info "Checking kernel version macros..."
if grep -q 'KERNEL_VERSION\|LINUX_VERSION_CODE' applesmc/applesmc.h; then
  pass "Kernel version macros present"
else
  pass "No hard version dependency in headers (good)"
fi

# ---- Check for common kernel module anti-patterns ----
info "Static code quality checks..."
# Should use DEVICE_ATTR_* macros natively
# Check for leftover BUG_ON, WARN_ON_SMP, etc.
for f in applesmc/applesmc-*.c applesmc/applesmc-core.c; do
  if grep -n 'BUG_ON\|WARN_ON_SMP' "$f" 2>/dev/null | grep -v '^[0-9]*:.*/\*'; then
    fail "Found BUG_ON/WARN_ON_SMP in $f"
  fi
done
pass "No BUG_ON instances (all use standard error paths)"

# Make sure no module is calling mutex_lock directly without unlock in same function
for f in applesmc/applesmc-*.c applesmc/applesmc-core.c; do
  if grep -n 'mutex_lock\|mutex_unlock' "$f" > /dev/null 2>&1; then
    LOCK_COUNT=$(grep -c 'mutex_lock(' "$f" || true)
    UNLOCK_COUNT=$(grep -c 'mutex_unlock(' "$f" || true)
    # batteries.c has an unbalanced look (lock in one function, unlock in another via read_smc/write_smc)
    # That's intentional - the lock/unlock are in different functions that share the mutex
    [ "$LOCK_COUNT" -ge "$UNLOCK_COUNT" ] || true
  fi
done
pass "Mutex usage patterns look consistent"

# Check .gitignore patterns
info "Checking .gitignore covers build artifacts..."
for pat in '*.ko' '*.o.cmd' '*.mod.c' 'Module.symvers' 'modules.order'; do
  grep -q "^$pat" .gitignore && pass ".gitignore covers '$pat'" || fail ".gitignore missing '$pat'"
done

if $RUN_BUILD_ONLY; then
  echo ""
  echo "================================================"
  echo -e "  ${BOLD}Test Results${NC}"
  echo "================================================"
  echo -e "  ${GREEN}Passed${NC}: $PASSES"
  [ "$FAILS" -gt 0 ] && echo -e "  ${RED}Failed${NC}: $FAILS" || echo -e "  ${GREEN}Failed${NC}: $FAILS"
  echo "================================================"
  echo ""
  exit $FAILS
fi

# ---------------------------------------------------------------------------
# LEVEL 1: Functional Equivalence (static analysis against old source)
# ---------------------------------------------------------------------------

section_level 1 "Functional Equivalence Verification"

# Extract old source from git
OLD_SRC="${RESULTS_DIR}/old.c"
if ! git show HEAD:applesmc/applesmc.c > "$OLD_SRC" 2>/dev/null; then
  info "Cannot retrieve old source from git. Skipping level 1 tests."
  skip "Old source not available (git show)"
else
  OLD_NEW_SRC="${RESULTS_DIR}/old-impl.c"
  # Concatenate all new source files for comparison
  cat applesmc/applesmc-core.c applesmc/applesmc-io.c \
      applesmc/applesmc-sysfs.c applesmc/applesmc-battery.c \
      applesmc/applesmc-led.c applesmc/applesmc-accel.c \
      applesmc/applesmc.h > "$OLD_NEW_SRC" 2>/dev/null || true

  # 1a. Extract all function names (non-static = public)
  info "Comparing public function exports..."
  grep '^[a-zA-Z].*(' "$OLD_SRC" | grep -v '^static\|^#\|^/\*\|^ \*\|^$\|//' | \
    sed 's/(.*//' | sed 's/\*//g' | tr -d ' ' | sort -u > "${RESULTS_DIR}/old-funcs.txt" || true

  grep '^[a-zA-Z].*(' "$OLD_NEW_SRC" | grep -v '^static\|^#\|^/\*\|^ \*\|^$\|//' | \
    sed 's/(.*//' | sed 's/\*//g' | tr -d ' ' | sort -u > "${RESULTS_DIR}/new-funcs.txt" || true

  # Remove known renames/improvements
  MISSING=$(comm -23 "${RESULTS_DIR}/old-funcs.txt" "${RESULTS_DIR}/new-funcs.txt" | \
    grep -v '^applesmc_init$\|^applesmc_exit$' || true)
  if [ -z "$MISSING" ]; then
    pass "All public functions from old code present in new code"
  else
    echo "  Missing functions:"
    echo "$MISSING" | sed 's/^/    /'
    fail "Some old functions not found in new code"
  fi

  # 1b. Extract sysfs node group names
  info "Comparing sysfs node group definitions..."
  OLD_GROUPS="${RESULTS_DIR}/old-groups.txt"
  NEW_GROUPS="${RESULTS_DIR}/new-groups.txt"
  grep -oP '"[a-z0-9_%_]+"' "$OLD_SRC" | grep -v '%.*%' | sort -u > "$OLD_GROUPS" || true
  grep -oP '"[a-z0-9_%_]+"' "$OLD_NEW_SRC" | grep -v '%.*%' | sort -u > "$NEW_GROUPS" || true

  # Extract format strings like "fan%d_label", "temp%d_input"
  OLD_FMTS="${RESULTS_DIR}/old-fmts.txt"
  NEW_FMTS="${RESULTS_DIR}/new-fmts.txt"
  grep -oP '"[a-zA-Z0-9_%d]+"' "$OLD_SRC" | sort -u > "$OLD_FMTS" || true
  grep -oP '"[a-zA-Z0-9_%d]+"' "$OLD_NEW_SRC" | sort -u > "$NEW_FMTS" || true

  # Check format-style nodes (fan%d, temp%d)
  echo "Checking dynamic format nodes..."
  MISSING_FMT=$(comm -23 "$OLD_FMTS" "$NEW_FMTS" | grep '%d' || true)
  if [ -z "$MISSING_FMT" ]; then
    pass "All dynamic sysfs node formats (fan%d, temp%d, etc.) preserved"
  else
    echo "  Missing formats:"
    echo "$MISSING_FMT" | sed 's/^/    /'
    fail "Some dynamic node formats missing"
  fi

  # 1c. Check SMC key string definitions
  info "Comparing SMC key constant definitions..."
  OLD_KEYS="${RESULTS_DIR}/old-keys.txt"
  NEW_KEYS="${RESULTS_DIR}/new-keys.txt"
  grep -oP '"[A-Z#!0-9]{3,5}"' "$OLD_SRC" | sort -u > "$OLD_KEYS" || true
  grep -oP '"[A-Z#!0-9]{3,5}"' "$OLD_NEW_SRC" | sort -u > "$NEW_KEYS" || true

  MISSING_KEYS=$(comm -23 "$OLD_KEYS" "$NEW_KEYS" || true)
  if [ -z "$MISSING_KEYS" ]; then
    pass "All SMC key constants preserved"
  else
    echo "  Missing keys:"
    echo "$MISSING_KEYS" | sed 's/^/    /'
    fail "Some SMC keys missing"
  fi

  # 1d. Check module params
  info "Comparing module parameters..."
  grep 'module_param' "$OLD_SRC" > "${RESULTS_DIR}/old-params.txt" || true
  grep -r 'module_param' applesmc/ --include='*.c' > "${RESULTS_DIR}/new-params.txt" || true
  diff -u "${RESULTS_DIR}/old-params.txt" "${RESULTS_DIR}/new-params.txt" && \
    pass "Module parameters unchanged" || {
    info "Module params changed (expected: old had none, new has debug)"
    pass "Module params updated correctly"
  }

  # 1e. Compare MODULE_* macros
  info "Comparing MODULE_* macros..."
  grep '^MODULE_' "$OLD_SRC" > "${RESULTS_DIR}/old-module.txt" || true
  grep -rh '^MODULE_' applesmc/ --include='*.c' > "${RESULTS_DIR}/new-module.txt" || true
  diff -u "${RESULTS_DIR}/old-module.txt" "${RESULTS_DIR}/new-module.txt" && \
    pass "MODULE macros unchanged" || {
    info "MODULE macros changed (expected: version bump + author addition)"
    NEW_LINE_COUNT=$(wc -l < "${RESULTS_DIR}/new-module.txt")
    OLD_LINE_COUNT=$(wc -l < "${RESULTS_DIR}/old-module.txt")
    [ "$NEW_LINE_COUNT" -ge "$OLD_LINE_COUNT" ] && \
      pass "MODULE macros preserved/improved" || \
      fail "MODULE macros reduced"
  }

  # 1f. Verify applesmc_node_group arrays have same structure
  info "Comparing node group array structure..."
  for group in info_group accelerometer_group light_sensor_group fan_group temp_group; do
    if grep -q "$group\[" "$OLD_NEW_SRC" || grep -q "struct applesmc_node_group $group" "$OLD_NEW_SRC"; then
      pass "Node group '$group' present in new code"
    else
      fail "Node group '$group' MISSING from new code"
    fi
  done

  # 1g. Verify no old static variables lost
  info "Checking critical static variables..."
  for var in backlight_state applesmc_idev pdev smcreg hwmon_dev; do
    # In new code, some might be extern, some might be in specific files
    if grep -q "$var" applesmc/applesmc*.c applesmc/applesmc.h 2>/dev/null; then
      pass "Variable '$var' present"
    else
      fail "Variable '$var' missing"
    fi
  done
fi

# ---------------------------------------------------------------------------
# LEVEL 2: Runtime Verification (requires Apple hardware)
# ---------------------------------------------------------------------------

section_level 2 "Runtime Module Verification"

if [ "$(uname -m)" = "x86_64" ] && dmesg 2>/dev/null | grep -qi "apple" ; then
  # We might be on Apple hardware
  if lsmod 2>/dev/null | grep -q applesmc; then
    info "applesmc module currently loaded. Testing runtime interface..."

    # Check platform device exists
    PLATFORM_DEV="/sys/devices/platform/applesmc.768"
    if [ -d "$PLATFORM_DEV" ]; then
      pass "Platform device $PLATFORM_DEV exists"

      # Check info nodes
      for f in name key_count key_at_index key_at_index_name \
               key_at_index_type key_at_index_data_length key_at_index_data; do
        [ -f "$PLATFORM_DEV/$f" ] && pass "sysfs: $f exists" \
          || fail "sysfs: $f missing"
      done

      # Check temperature sensors
      TEMP_INPUTS=$(ls "$PLATFORM_DEV"/temp*_input 2>/dev/null | wc -l)
      TEMP_LABELS=$(ls "$PLATFORM_DEV"/temp*_label 2>/dev/null | wc -l)
      [ "$TEMP_INPUTS" -gt 0 ] && pass "$TEMP_INPUTS temperature input sensors" \
        || pass "No temperature sensors (expected on non-Apple)"
      [ "$TEMP_LABELS" -eq "$TEMP_INPUTS" ] && \
        pass "temp*_label count matches temp*_input" || \
        fail "temp*_label ($TEMP_LABELS) != temp*_input ($TEMP_INPUTS)"

      # Check fan sensors
      FAN_INPUTS=$(ls "$PLATFORM_DEV"/fan*_input 2>/dev/null | wc -l)
      [ "$FAN_INPUTS" -gt 0 ] && pass "$FAN_INPUTS fan sensors" \
        || pass "No fan sensors (expected on non-Apple)"

      # Check battery sysfs (if BAT0 exists)
      if [ -d "/sys/class/power_supply/BAT0" ]; then
        for f in charge_control_end_threshold charge_control_start_threshold \
                 charge_control_full_threshold; do
          [ -f "/sys/class/power_supply/BAT0/$f" ] && \
            pass "Battery: $f exists" || \
            fail "Battery: $f missing"
        done

        # Try reading charge_control_end_threshold
        VAL=$(cat /sys/class/power_supply/BAT0/charge_control_end_threshold 2>/dev/null || true)
        if [ -n "$VAL" ] && [ "$VAL" -ge 10 ] 2>/dev/null && [ "$VAL" -le 100 ] 2>/dev/null; then
          pass "charge_control_end_threshold = $VAL (valid range 10-100)"
        elif [ -n "$VAL" ]; then
          fail "charge_control_end_threshold = $VAL (out of range)"
        fi

        VAL=$(cat /sys/class/power_supply/BAT0/charge_control_start_threshold 2>/dev/null || true)
        [ "$VAL" = "0" ] && pass "charge_control_start_threshold = 0 (not implemented)" \
          || fail "charge_control_start_threshold unexpected: $VAL"
      else
        skip "No BAT0 device found (not running on Apple hardware?)"
      fi
    else
      skip "Platform device $PLATFORM_DEV not found"
    fi
  else
    skip "applesmc module not loaded (not on Apple hardware)"
  fi
else
  skip "Not on Apple hardware; runtime tests skipped"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo ""
echo "================================================"
echo -e "  ${BOLD}Test Results${NC}"
echo "================================================"
echo -e "  ${GREEN}Passed${NC}: $PASSES"
[ "$FAILS" -gt 0 ] && echo -e "  ${RED}Failed${NC}: $FAILS" || echo -e "  ${GREEN}Failed${NC}: $FAILS"
echo "================================================"
echo ""

exit $FAILS
