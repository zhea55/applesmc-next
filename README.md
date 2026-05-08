# applesmc-next

A Linux kernel module that enables battery charge threshold control on Intel-based Apple Mac computers via SMC (System Management Controller).

This is a fork of [c---/applesmc-next](https://github.com/c---/applesmc-next) ported to kernel 7.x. The original project's custom SBS (Smart Battery System) driver was replaced with the kernel's built-in `battery_hook_register` API, removing the need for a separate patched SBS driver.

## Features

This module extends the standard Linux `applesmc` driver with additional capabilities:

- **Battery charge limit** – set a maximum charge percentage (`BCLM` SMC key)
- **Full charge LED threshold** – control when the MagSafe/power LED turns green (`BFCL` SMC key)
- All original `applesmc` features: temperature sensors, fan speed monitoring & control, accelerometer, keyboard backlight, ambient light sensor

### Sysfs Interface

The charge threshold files appear under the battery power supply device:

```
/sys/class/power_supply/BAT0/charge_control_end_threshold    [rw]  charge ceiling (10–100%)
/sys/class/power_supply/BAT0/charge_control_full_threshold   [rw]  LED-green threshold (0–100%)
/sys/class/power_supply/BAT0/charge_control_start_threshold  [ro]  not implemented (returns 0)
```

The platform device at `/sys/devices/platform/applesmc.768/` exposes:

| Group | Files |
|-------|-------|
| Temperature | `temp{1..N}_label`, `temp{1..N}_input` |
| Fans | `fan{1..N}_label`, `fan{1..N}_input`, `fan{1..N}_min`, `fan{1..N}_max`, `fan{1..N}_safe`, `fan{1..N}_output`, `fan{1..N}_manual` |
| Accelerometer | `position`, `calibrate` (on supported models) |
| Light sensor | `light` (on supported models) |
| SMC info | `name`, `key_count`, `key_at_index*` |

## Prerequisites

- **Kernel headers** matching your running kernel
  - Arch / CachyOS: `sudo pacman -S linux-cachyos-headers` (or `linux-headers` for stock Arch)
  - Debian / Ubuntu: `sudo apt install linux-headers-$(uname -r)`
  - Fedora: `sudo dnf install kernel-devel-$(uname -r)`
- **Build tools**: `make`, `gcc`, `git`

## Manual Build & Install

### 1. Clone the repository

```sh
git clone https://github.com/zhea55/applesmc-next.git
cd applesmc-next
```

### 2. Build the module

```sh
make
```

This runs the kernel build system against your current kernel's build directory (`/lib/modules/$(uname -r)/build`). On success, you will find the compiled module at `applesmc/applesmc.ko`.

### 3. Install the module

```sh
sudo make modules_install
```

This copies `applesmc.ko` to `/lib/modules/$(uname -r)/updates/applesmc/applesmc.ko` (path may be `extra/` on Debian/Ubuntu).

### 4. Update module dependencies

```sh
sudo depmod -a
```

### 5. Load the module

```sh
sudo modprobe applesmc
```

### 6. Verify it is working

```sh
lsmod | grep applesmc
# Should show: applesmc             <size>  <count>

# Check the charge threshold interface:
cat /sys/class/power_supply/BAT0/charge_control_end_threshold
# Should print a number (default 100)

# Check platform sensors:
ls /sys/devices/platform/applesmc.768/temp*_input
```

### 7. (Optional) Load at boot

```sh
echo "applesmc" | sudo tee /etc/modules-load.d/applesmc.conf
```

### Removing the module

```sh
sudo modprobe -r applesmc
sudo rm /lib/modules/$(uname -r)/updates/applesmc/applesmc.ko
sudo depmod -a
```

## Usage

### Set a charge limit

Stop charging at 80%:

```sh
echo 80 | sudo tee /sys/class/power_supply/BAT0/charge_control_end_threshold
```

The limit is **persistent across reboots, shutdowns, and even operating systems** (macOS, Windows). It is stored in SMC NVRAM and only cleared by resetting the SMC (see Notes below).

### Read the current limit

```sh
cat /sys/class/power_supply/BAT0/charge_control_end_threshold
```

### Remove the limit (charge to 100%)

```sh
echo 100 | sudo tee /sys/class/power_supply/BAT0/charge_control_end_threshold
```

### Adjust the LED threshold

The `charge_control_full_threshold` key (`BFCL`) controls when the MagSafe/power LED turns from orange to green. This is cosmetic only. It should be set 2–3% below the end threshold:

```sh
echo 78 | sudo tee /sys/class/power_supply/BAT0/charge_control_full_threshold
```

If not set, the LED behavior depends on your SMC firmware defaults.

### Monitor battery status

```sh
cat /sys/class/power_supply/BAT0/status
cat /sys/class/power_supply/BAT0/capacity
```

## Integration with TLP

A TLP plugin script is provided at `45-apple` in this repository. To use it:

```sh
sudo cp 45-apple /usr/share/tlp/bat.d/
```

This allows TLP to manage the charge thresholds via its configuration (`STOP_CHARGE_THRESH_BAT0`, etc.).

## Notes

- **Valid range**: `charge_control_end_threshold` accepts values from **10 to 100**. The minimum of 10 is enforced by the driver to prevent deep discharge lockout.
- **SMC persistence**: Values written to `BCLM` and `BFCL` are stored in the SMC's dedicated NVRAM. They survive:
  - Reboot
  - Shutdown
  - Sleep / hibernation
  - Booting into macOS or Windows (Boot Camp)
- **Clearing the limit**: Reset your SMC to clear all threshold values:
  - Intel Mac: `Control + Option + Shift + Power` (left side, 10 sec) or disconnect battery
  - Or use the `SMCReset` NVRAM variable
- **`charge_control_start_threshold`** is not implemented (returns 0). The SMC key for a lower charge floor is not available on most Intel Mac firmware versions.
- **Temperature sensors**: Sensor keys are auto-discovered from SMC firmware. Common sensors include `TB0T` (battery), `TC0P` (CPU), `TG0T` (GPU). Labels show the raw 4-character SMC key name.

## References

- Upstream project: [c---/applesmc-next](https://github.com/c---/applesmc-next)
- Arch Linux AUR package: [applesmc-next-dkms](https://aur.archlinux.org/packages/applesmc-next-dkms)
- Linux kernel `applesmc` driver: [drivers/hwmon/applesmc.c](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/hwmon/applesmc.c)

## License

GNU General Public License v2.0 – see [LICENSE](LICENSE).
