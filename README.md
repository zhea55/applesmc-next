# applesmc-next

A Linux kernel module for battery charge threshold control on Intel-based Apple Macs, with all original `applesmc` features (temperature sensors, fan control, accelerometer, keyboard backlight, ambient light sensor).

Forked from [c---/applesmc-next](https://github.com/c---/applesmc-next) and ported to kernel 7.x. The custom SBS driver has been replaced with the kernel's built-in `battery_hook_register` API.

## Improvements Over the Original Driver

### Architecture

| Aspect | Original | This version |
|--------|----------|-------------|
| Structure | 1 × 1590-line `applesmc.c` | 6 .c files + shared header |
| SBS driver | Copied & patched in-tree | Removed (uses kernel API) |
| Code size | ~2090 lines (C + SBS) | ~860 lines (46% reduction) |

### Performance

The SMC hardware protocol is inherently slow (~120–500 µs per operation). Three micro-optimizations cut software overhead to near zero:

**`wait_status` — 3-phase polling** — Old code always slept (`usleep_range` per iteration, ≥192 µs). New code spins first with `cpu_relax()`, then `udelay(1)`, then exponential backoff sleep. Fast path: **≤0.5 µs vs ≥192 µs** (100–400× improvement).

**`read_smc` — streaming bytes** — Multi-byte reads (e.g. 4-byte sensor values) now wait fully only for the first byte. Subsequent bytes use a tight `cpu_relax()` spin (the SMC is already streaming). Cuts latency from **N × ~200 µs to ~200 µs + (N-1) × ~0.5 µs**.

**Drain loop — error recovery** — Replaced `udelay(8)` × 16 with `cpu_relax()` × 64. Total drain latency: **~128 µs → ≤1 µs**. Log level demoted from `pr_warn` to `pr_debug`.

### Bug Fixes

- **Battery threshold race condition**: Old code called SMC I/O without holding the global mutex. Now properly wrapped in `smcreg.mutex_lock/unlock`.
- **SMC protocol robustness**: `send_byte` now issues an extra `wait_status(SMC_STATUS_BUSY)` — required by some SMC firmware variants.
- **Test suite crash**: `smoke-test.sh --build-only` called an undefined `summary` function, exiting 127 despite all tests passing. Fixed.
- **License inconsistency**: Root and subdirectory `Makefile` used `GPL-2.0-or-later` while all `.c`/`.h` files used `GPL-2.0-only`. Unified to `GPL-2.0-only`.
- **Accelerometer NULL deref**: `applesmc_release_accelerometer()` could pass NULL to `input_unregister_device()` when init failed partway. Added NULL guard and failure flag.
- **Light sensor show race**: `applesmc_light_show()` used a `static int` cache without locking. Removed the cache — `applesmc_get_entry_by_key()` already returns O(1) from the register cache.

## Quick Install

```sh
git clone https://github.com/zhea55/applesmc-next.git && cd applesmc-next && make && sudo make modules_install && sudo depmod -a && sudo modprobe -r applesmc 2>/dev/null; sudo modprobe applesmc
```

## Build & Install

### Prerequisites

- **Kernel headers** matching your running kernel
  - Arch / CachyOS: `sudo pacman -S linux-cachyos-headers` (or `linux-headers` for stock Arch)
  - Debian / Ubuntu: `sudo apt install linux-headers-$(uname -r)`
  - Fedora: `sudo dnf install kernel-devel-$(uname -r)`
- **Build tools**: `make`, `gcc` / `clang`, `git`

If your kernel was built with Clang (e.g. CachyOS), the Makefile automatically uses `LLVM=1`.

### Step by step

```sh
git clone https://github.com/zhea55/applesmc-next.git
cd applesmc-next
make
sudo make modules_install
sudo depmod -a
sudo modprobe -r applesmc 2>/dev/null; sudo modprobe applesmc
```

To enable debug messages:

```sh
sudo modprobe applesmc debug=1
sudo journalctl -k | grep applesmc
```

### Verify

```sh
lsmod | grep applesmc
cat /sys/class/power_supply/BAT0/charge_control_end_threshold
ls /sys/devices/platform/applesmc.768/temp*_input
```

### Load at boot

```sh
echo "applesmc" | sudo tee /etc/modules-load.d/applesmc.conf
```

### Remove

```sh
sudo modprobe -r applesmc
sudo rm /lib/modules/$(uname -r)/updates/applesmc/applesmc.ko
sudo depmod -a
```

## Usage

Set charge limit to 80% (persists across reboots and OSes, stored in SMC NVRAM):

```sh
echo 80 | sudo tee /sys/class/power_supply/BAT0/charge_control_end_threshold
```

Read current limit:

```sh
cat /sys/class/power_supply/BAT0/charge_control_end_threshold
```

Remove limit (charge to 100%):

```sh
echo 100 | sudo tee /sys/class/power_supply/BAT0/charge_control_end_threshold
```

Adjust the green-LED threshold (set 2–3% below end threshold):

```sh
echo 78 | sudo tee /sys/class/power_supply/BAT0/charge_control_full_threshold
```

### Sysfs reference

The battery charge threshold files appear under the power supply device:

```
/sys/class/power_supply/BAT0/charge_control_end_threshold    [rw]  10–100%
/sys/class/power_supply/BAT0/charge_control_full_threshold   [rw]  0–100%
/sys/class/power_supply/BAT0/charge_control_start_threshold  [ro]  not implemented
```

The platform device at `/sys/devices/platform/applesmc.768/` exposes:

| Group | Files |
|-------|-------|
| Temperature | `temp{1..N}_label`, `temp{1..N}_input` |
| Fans | `fan{1..N}_label`, `fan{1..N}_input`, `fan{1..N}_min`, `fan{1..N}_max`, `fan{1..N}_safe`, `fan{1..N}_output`, `fan{1..N}_manual` |
| Accelerometer | `position`, `calibrate` |
| Light sensor | `light` |
| SMC info | `name`, `key_count`, `key_at_index*` |

## Project Structure

```
applesmc-next/
├── applesmc/
│   ├── applesmc.h              # Shared header
│   ├── applesmc-core.c         # Init/exit, DMI whitelist, register cache
│   ├── applesmc-io.c           # Low-level SMC I/O protocol & entry cache
│   ├── applesmc-sysfs.c        # Sysfs nodes: temp, fan, light, info
│   ├── applesmc-battery.c      # Battery charge thresholds via ACPI hook
│   ├── applesmc-led.c          # Keyboard backlight LED class device
│   ├── applesmc-accel.c        # Accelerometer input device
│   └── Makefile
├── tests/
│   └── smoke-test.sh           # Regression test suite (3 levels)
├── 45-apple                    # TLP integration plugin
├── dkms.conf
├── Makefile
├── .gitignore
└── README.md
```

### TLP integration

```sh
sudo cp 45-apple /usr/share/tlp/bat.d/
```

Then configure via `STOP_CHARGE_THRESH_BAT0` etc. in `/etc/tlp.conf`.

## Running Tests

```sh
make test                                    # Full: build + runtime (Apple HW only)
bash tests/smoke-test.sh --build-only        # Build & static checks only (no sudo)
sudo bash tests/smoke-test.sh                # All levels including runtime (Apple HW only)
```

| Level | Scope | Tests | Hardware | Sudo |
|-------|-------|-------|----------|------|
| 0 | Build, static analysis, metadata | 34 | No | No |
| 1 | Functional equivalence vs old code | 0 (reserved) | No | No |
| 2 | Runtime sysfs validation | 12 | Apple | Yes |
| **Total** | | **46** | | |

## Notes

- **Valid range**: 10–100 (min 10 prevents deep discharge lockout).
- **SMC persistence**: Values survive reboot, shutdown, sleep, and macOS/Windows Boot Camp.
- **Clearing the limit**: Reset SMC (Intel Mac: Ctrl+Option+Shift+Power for 10s) or use `SMCReset` NVRAM variable.
- **`charge_control_start_threshold`** not implemented (returns 0) — the lower-bound SMC key is unavailable on most Intel Mac firmware.
- **Temperature sensors**: Auto-discovered. Common keys include `TB0T` (battery), `TC0P` (CPU), `TG0T` (GPU).

## References

- Upstream: [c---/applesmc-next](https://github.com/c---/applesmc-next)
- AUR: [applesmc-next-dkms](https://aur.archlinux.org/packages/applesmc-next-dkms)
- Kernel driver: [drivers/hwmon/applesmc.c](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/hwmon/applesmc.c)

## License

GNU General Public License v2.0 — see [LICENSE](LICENSE).
