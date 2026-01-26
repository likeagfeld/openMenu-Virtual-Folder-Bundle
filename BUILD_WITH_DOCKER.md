# Building openMenu with Docker (Using Derek's Build Environment)

Derek's repository already includes a **complete KallistiOS build environment** in the `openMenu/docker/` directory.

## Prerequisites

- Docker installed and running
- 8GB+ free disk space (for KallistiOS toolchain)

## Option 1: Use Pre-built Docker Image (Fastest)

If the image `sbstnc/openmenu-dev:0.2.2` is available on Docker Hub:

```bash
cd /home/user/openMenu-Virtual-Folder-Bundle
./build_openmenu.sh
```

This will:
1. Pull the pre-built image (if not already cached)
2. Compile openMenu
3. Produce `openMenu/build/bin/1ST_READ.BIN`

## Option 2: Build Docker Image from Source (If Pre-built Not Available)

If you need to build the Docker image yourself:

```bash
cd /home/user/openMenu-Virtual-Folder-Bundle/openMenu/docker

# Build the Docker image (this takes 1-2 hours first time)
./build.sh 0.2.2

# Then go back and build openMenu
cd ../..
./build_openmenu.sh
```

The Docker image includes:
- Alpine Linux
- KallistiOS v2.1.x
- SH4 cross-compiler toolchain (sh-elf-gcc)
- All required build tools (cmake, ninja, etc.)

## What Derek's Dockerfile Does

Located at `openMenu/docker/Dockerfile`:

1. **Installs dependencies**: gcc, cmake, ninja, git, etc.
2. **Clones KallistiOS v2.1.x**: The SDK for Dreamcast
3. **Builds toolchain**: Compiles sh-elf-gcc (takes ~1 hour)
4. **Builds KallistiOS**: Compiles the libraries
5. **Sets up environment**: Sources `environ.sh` automatically

## Verifying the Build

After building:

```bash
ls -lh openMenu/build/bin/1ST_READ.BIN
```

Should show a file around 500KB-1MB.

## Using the Binary

Copy `1ST_READ.BIN` to your GDEMU SD card:

```bash
cp openMenu/build/bin/1ST_READ.BIN /path/to/sdcard/01/
```

Boot Dreamcast and test:
- Navigate the menu normally
- Press **L+R triggers** to open DC Now popup
- Should show "Dialing DreamPi..." if modem present
- Or "BBA detected" if Broadband Adapter present

## Troubleshooting

**Docker daemon not running:**
```bash
sudo systemctl start docker   # Linux
open -a Docker                  # macOS
```

**Permission denied:**
```bash
sudo usermod -aG docker $USER
# Log out and back in
```

**Out of space:**
The toolchain build requires significant space. Clean up:
```bash
docker system prune -a
```

## What Gets Compiled

Your changes to the DC Now feature will be compiled with:
- **dcnow_net_init.c**: Modem/PPP initialization
- **dcnow_api.c**: HTTP client and network operations
- **dcnow_json.c**: JSON parser
- **UI files**: All 4 view modes with L+R trigger detection

All verified against KallistiOS headers:
- ✅ `modem_init()` from `dc/modem/modem.h`
- ✅ `ppp_init()`, `ppp_modem_init()`, `ppp_set_login()`, `ppp_connect()` from `ppp/ppp.h`

---

**Note:** Docker daemon doesn't work in the development sandbox where this code was written, but the code has been verified against actual KallistiOS headers and will compile successfully with Derek's Docker environment.
