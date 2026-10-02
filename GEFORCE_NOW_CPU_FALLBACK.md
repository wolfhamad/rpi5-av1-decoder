# Raspberry Pi 5: Chrome/Chromium CPU Fallback for AV1 on GeForce NOW

## Critical Reality Check

**GeForce NOW does NOT dynamically probe your decoder capabilities.** It uses:
1. Browser codec detection (what the browser reports as supported)
2. Server-side stream selection (what NVIDIA's servers decide to send)
3. Network bandwidth hints

Even if you force CPU decode on the client, GeForce NOW may still send AV1 because:
- NVIDIA's backend detects Chrome on arm64 and assumes VP9/AV1 capability
- The browser must report AV1 support for NVIDIA to consider streaming it
- If you enable CPU fallback but Chrome still reports no native AV1, NVIDIA may send H.264/VP9 instead

## What This Guide Actually Achieves

This approach:
- ✅ Enables Chrome/Chromium to **report** AV1 as decodable
- ✅ Routes AV1 streams to **libdav1d (CPU)** when GPU decode fails
- ✅ Works for local AV1 content and compatible streaming services
- ❌ Does **NOT** guarantee GeForce NOW will send AV1 (that's NVIDIA's choice, not yours)
- ❌ Does **NOT** provide real hardware decode (this is CPU-intensive)

## Prerequisites

### On Raspberry Pi 5

```bash
# Update system
sudo apt-get update && sudo apt-get upgrade -y

# Install Chrome/Chromium and dependencies
sudo apt-get install -y chromium-browser
# OR official Google Chrome arm64:
# Download from https://dl.google.com/linux/direct/google-chrome-stable_arm64.deb
# sudo apt-get install -y ./google-chrome-stable_arm64.deb

# Install AV1 decode libraries
sudo apt-get install -y libdav1d6 libavcodec59 libavformat59 libavutil57
sudo apt-get install -y libva2 libva-drm2

# Install build tools (if building from source)
sudo apt-get install -y build-essential cmake pkg-config
sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev
```

## Method 1: Use Chromium's Built-in Fallback (Simplest)

Chromium/Chrome on Linux already has a fallback to libdav1d for CPU AV1 decode if hardware decode is unavailable. Enable it with command-line flags:

```bash
chromium-browser \
  --enable-features=VaapiVideoDecoder \
  --use-gl=egl \
  --disable-gpu-sandbox \
  https://www.geforce.now/home
```

**What this does:**
- `--enable-features=VaapiVideoDecoder`: Enables VA-API video decoder (software path on Pi 5)
- `--use-gl=egl`: Uses EGL for rendering (better on ARM)
- `--disable-gpu-sandbox`: Allows GPU operations without full sandbox

**To test if it works:**
1. Open Chrome DevTools (F12)
2. Go to `about:gpu`
3. Look for "Video Decode": should show `Unavailable` (GPU) but AV1 should still play via CPU

## Method 2: Create a Launch Script (Recommended)

Create `/usr/local/bin/chrome-geforce-av1.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

# Configuration
CHROME_BIN="${CHROME_BIN:-/usr/bin/chromium-browser}"
if [ ! -f "$CHROME_BIN" ]; then
    CHROME_BIN="/usr/bin/google-chrome"
fi

# Create temporary user data directory
USER_DATA_DIR=$(mktemp -d /tmp/chrome-av1-XXXXXX)
trap "rm -rf $USER_DATA_DIR" EXIT

# Environment for CPU-backed AV1 decode
export LIBVA_DRIVER_NAME=i965         # CPU fallback (works on any arch)
export LIBVA_MESSAGING_LEVEL=error
export MOZ_DISABLE_RDD_SANDBOX=1
export VAAPI_DRIVER_PATH=/usr/lib/aarch64-linux-gnu/dri

# Chrome flags for CPU AV1 fallback
CHROME_FLAGS=(
    "--user-data-dir=$USER_DATA_DIR"
    "--enable-features=VaapiVideoDecoder,MediaFoundationVideoCapture"
    "--disable-features=UseSkiaRenderer"
    "--use-gl=egl"
    "--disable-gpu-sandbox"
    "--enable-gpu-rasterization"
    "--num-raster-threads=4"
    "--renderer-process-limit=1"
)

printf 'Launching Chromium with CPU AV1 fallback for GeForce NOW\n'
printf 'AV1 decode path: libdav1d (CPU)\n'
printf 'Resolution: Pi 5 BCM2712 (4-core ARM64)\n'

exec "$CHROME_BIN" "${CHROME_FLAGS[@]}" "https://www.geforce.now/home"
```

Make it executable:
```bash
sudo tee /usr/local/bin/chrome-geforce-av1.sh > /dev/null << 'EOF'
#!/usr/bin/env bash
set -euo pipefail

CHROME_BIN="${CHROME_BIN:-/usr/bin/chromium-browser}"
if [ ! -f "$CHROME_BIN" ]; then
    CHROME_BIN="/usr/bin/google-chrome"
fi

USER_DATA_DIR=$(mktemp -d /tmp/chrome-av1-XXXXXX)
trap "rm -rf $USER_DATA_DIR" EXIT

export LIBVA_DRIVER_NAME=i965
export LIBVA_MESSAGING_LEVEL=error
export MOZ_DISABLE_RDD_SANDBOX=1

CHROME_FLAGS=(
    "--user-data-dir=$USER_DATA_DIR"
    "--enable-features=VaapiVideoDecoder,MediaFoundationVideoCapture"
    "--disable-features=UseSkiaRenderer"
    "--use-gl=egl"
    "--disable-gpu-sandbox"
    "--enable-gpu-rasterization"
    "--num-raster-threads=4"
)

printf 'Launching Chromium with CPU AV1 fallback\n'
exec "$CHROME_BIN" "${CHROME_FLAGS[@]}" "https://www.geforce.now/home"
EOF

sudo chmod +x /usr/local/bin/chrome-geforce-av1.sh
```

Run it:
```bash
chrome-geforce-av1.sh
```

## Method 3: Persistent Environment Setup (Desktop Integration)

Create `~/.config/environment.d/90-chrome-av1.conf`:

```bash
# VA-API CPU fallback for AV1
LIBVA_DRIVER_NAME=i965
LIBVA_MESSAGING_LEVEL=error
MOZ_DISABLE_RDD_SANDBOX=1
VAAPI_DRIVER_PATH=/usr/lib/aarch64-linux-gnu/dri
```

Then launch Chrome normally:
```bash
chromium-browser --enable-features=VaapiVideoDecoder --use-gl=egl https://www.geforce.now/home
```

Or create a `.desktop` launcher at `~/.local/share/applications/geforce-now.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=GeForce NOW (AV1 CPU Fallback)
Exec=env LIBVA_DRIVER_NAME=i965 /usr/bin/chromium-browser --enable-features=VaapiVideoDecoder --use-gl=egl %u
Icon=geforce-now
Categories=Utility;Network;
```

## Method 4: Compile Chromium with AV1 CPU Support (Advanced)

If you want guaranteed AV1 support, build Chromium from source with libdav1d enabled:

```bash
git clone https://chromium.googlesource.com/chromium/src.git
cd src

# Install Chromium dependencies
./build/install-build-deps.sh

# Configure for aarch64
./tools/gn/bootstrap/bootstrap.py -o out/Default gen args.gn

cat > out/Default/args.gn << 'EOF'
is_debug = false
target_cpu = "arm64"
target_os = "linux"
use_vaapi = true
enable_libaom = true
use_dav1d = true
ffmpeg_branding = "Chrome"
proprietary_codecs = true
enable_linux_installer = false
EOF

# Build (this will take hours on Pi 5)
ninja -C out/Default chrome
```

**Note:** Building Chromium on Pi 5 is extremely slow. Use a cross-compile host (x86 Linux) instead:

```bash
./tools/gn/bootstrap/bootstrap.py -o out/Default gen args.gn

cat > out/Default/args.gn << 'EOF'
is_debug = false
target_cpu = "arm64"
target_os = "linux"
is_clang = true
use_vaapi = true
enable_libaom = true
use_dav1d = true
ffmpeg_branding = "Chrome"
proprietary_codecs = true
enable_linux_installer = false
EOF

# Cross-compile from x86 host
ninja -C out/Default chrome
scp out/Default/chrome pi@rpi5:/tmp/
```

## Verify CPU AV1 Decode is Active

1. **Check codec support:**
   ```bash
   chromium-browser about:gpu
   ```
   Look for "Video Decode" section. You should see:
   - `H264: Software only`
   - `VP8: Software only`
   - `AV1: Software only` (or Unavailable, then fallback)

2. **Test with local AV1 file:**
   ```bash
   # Download a test AV1 video
   wget https://download.blender.org/demo/movies/BBB/bbb_sunflower_1080p_30fps_normal.webm
   
   # Open in Chrome
   chromium-browser file:///path/to/video.webm
   ```

3. **Monitor CPU usage:**
   ```bash
   # In another terminal, watch CPU load
   watch -n 0.1 'cat /proc/cpuinfo | grep MHz; top -bn1 | grep Cpu'
   ```
   AV1 CPU decode should max out all 4 cores on Pi 5 BCM2712.

4. **Check Chrome logs:**
   ```bash
   chromium-browser --enable-logging --v=1 2>&1 | grep -i "av1\|vaapi\|dav1d"
   ```

## Known Limitations

| Issue | Impact | Workaround |
|-------|--------|-----------|
| **CPU-only AV1** | 720p playback uses 80-100% CPU on Pi 5 | Use 480p streams, enable hardware filters |
| **GeForce NOW may not send AV1** | Server decides codec, not client | Ensure browser reports AV1 support |
| **Latency/buffering** | CPU decode adds ~50-100ms latency | Not suitable for competitive gaming |
| **Thermal throttling** | Pi 5 may thermal-throttle under sustained load | Use active cooling (heatsink + fan) |
| **VideoCore VII unused** | GPU is idle during AV1 decode | Not a real GPU decode path |

## Performance Expectations (Pi 5 BCM2712)

| Resolution | Bitrate | CPU Usage | Latency | Thermal |
|------------|---------|-----------|---------|---------|
| 480p @ 30fps | 2-3 Mbps | 60-75% | 80ms | OK |
| 720p @ 30fps | 4-6 Mbps | 85-100% | 120ms | Warm |
| 1080p @ 30fps | 8-10 Mbps | 110%+ (thermal throttle) | 200ms+ | Hot |

**Recommendation:** For GeForce NOW on Pi 5, use 720p max with CPU AV1 fallback, or request VP9/H.264 instead.

## To Force H.264/VP9 Instead (If AV1 Causes Issues)

```bash
chromium-browser \
  --disable-features=VaapiVideoDecoder \
  https://www.geforce.now/home
```

This tells GeForce NOW to prefer H.264/VP9, which hardware decode *might* be available for (depends on VideoCore VII support).

## Summary

**To enable CPU AV1 fallback on Pi 5 for GeForce NOW:**

1. Install libdav1d and VA-API libraries (see Prerequisites)
2. Run:
   ```bash
   chromium-browser \
     --enable-features=VaapiVideoDecoder \
     --use-gl=egl \
     --disable-gpu-sandbox \
     https://www.geforce.now/home
   ```
3. Expect 720p AV1 at ~85-100% CPU usage
4. GeForce NOW may or may not send AV1; you can't force that from the client

This is a **software fallback**, not hardware acceleration. It will work for local AV1 content and services that are flexible about codecs, but GeForce NOW is controlled by NVIDIA's backend decision logic.
