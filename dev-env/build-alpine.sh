#!/bin/bash
set -e

ALPINE_VER="3.19"
ALPINE_FULL_VER="3.19.1"
ROOTFS_URL="https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VER}/releases/x86_64/alpine-minirootfs-${ALPINE_FULL_VER}-x86_64.tar.gz"

IMG_RAW="/workspace/alpine.raw"
IMG_QCOW2="/workspace/alpine.qcow2"
MNT_DIR="/tmp/alpine_mnt"

if [ -f "$IMG_QCOW2" ]; then
    echo "[*] Alpine image already exists. Skipping build."
    exit 0
fi

echo "[*] Downloading Alpine rootfs..."
wget -qO /tmp/alpine.tar.gz "$ROOTFS_URL"

echo "[*] Creating raw disk image (4GB)..."
dd if=/dev/zero of="$IMG_RAW" bs=1M count=4096 2>/dev/null
mkfs.ext4 -F "$IMG_RAW" >/dev/null 2>&1

mkdir -p "$MNT_DIR"
mount -o loop "$IMG_RAW" "$MNT_DIR"

echo "[*] Extracting rootfs..."
tar -xzf /tmp/alpine.tar.gz -C "$MNT_DIR"

echo "[*] Configuring chroot environment..."
cp /etc/resolv.conf "$MNT_DIR/etc/"

# Configure fstab
cat > "$MNT_DIR/etc/fstab" << 'EOF'
/dev/vda / ext4 rw,relatime,errors=remount-ro 0 1
tmpfs /tmp tmpfs rw,nosuid,nodev 0 0
proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0
sysfs /sys sysfs rw,nosuid,nodev,noexec,relatime 0 0
devtmpfs /dev devtmpfs rw,nosuid,relatime,size=10240k,nr_inodes=256000,mode=755 0 0
EOF

# Configure mkinitfs to include ext4 support
mkdir -p "$MNT_DIR/etc/mkinitfs"
echo 'features="ata base ide scsi usb virtio ext4"' > "$MNT_DIR/etc/mkinitfs/mkinitfs.conf"

# Copy workspace into chroot so we can compile GUI and Kernel module inside Alpine
cp -r /workspace/gui "$MNT_DIR/payload_src"
cp -r /workspace/kernel-module "$MNT_DIR/kernel_src"
cp -r /workspace/daemon "$MNT_DIR/daemon"

echo "[*] Installing packages and compiling GUI/Kernel in Alpine..."
chroot "$MNT_DIR" /bin/sh -c "
    apk update && \
    apk add --no-cache openrc alpine-base linux-lts eudev mesa-dri-gallium weston weston-backend-drm weston-shell-desktop weston-terminal gtk+3.0 font-dejavu font-liberation gcc make pkgconf gtk+3.0-dev libc-dev agetty linux-lts-dev linux-headers gmp-dev mpc1-dev mpfr-dev python3 seatd && \
    adduser root seat && \
    rc-update add devfs boot && \
    rc-update add udev sysinit && \
    rc-update add udev-trigger sysinit && \
    rc-update add udev-settle sysinit && \
    rc-update add root default && \
    rc-update add local default && \
    rc-update add seatd default && \
    make -C /payload_src clean || true && \
    make -C /payload_src && \
    make -C /kernel_src clean || true && \
    KDIR=\$(find /usr/src -name \"linux-headers-*lts*\" -type d | head -n 1) && \
    echo > \$KDIR/scripts/Makefile.gcc-plugins && \
    make -C \$KDIR M=/kernel_src modules
"

echo "[*] Saving musl-compiled GUI and Kernel binaries..."
cp "$MNT_DIR/payload_src/vcfs-gui" /workspace/gui-alpine
cp "$MNT_DIR/kernel_src/vcfs.ko" /workspace/vcfs-alpine.ko

echo "[*] Setting up GUI init script via profile..."
cat > "$MNT_DIR/root/start-gui.sh" << 'EOF'
#!/bin/sh
echo "[*] Remounting root as Read-Write..."
mount -o remount,rw /

echo "[*] VCFS Guest Initialization..."
mkdir -p /payload
mount -t vfat /dev/vdb /payload 2>/dev/null
insmod /payload/vcfs.ko >/dev/null 2>&1 || true

export XDG_RUNTIME_DIR=/tmp/xdg
mkdir -p $XDG_RUNTIME_DIR
chmod 0700 $XDG_RUNTIME_DIR

mkdir -p ~/.config
cat > ~/.config/weston.ini << 'INI'
[core]
backend=drm-backend.so
shell=desktop-shell.so
idle-time=0

[autolaunch]
path=/payload/vcfs-gui

[launcher]
icon=/usr/share/weston/icon_terminal.png
path=/usr/bin/weston-terminal

[launcher]
icon=/usr/share/icons/hicolor/256x256/apps/weston.png
path=/payload/vcfs-gui
INI

echo "[*] Waiting for seatd to be available..."
while [ ! -S /run/seatd.sock ]; do
    sleep 0.1
done

echo "[*] Starting Weston and VCFS GUI..."
# Force the environment variables directly onto the Weston execution process
XDG_RUNTIME_DIR=/tmp/xdg WAYLAND_DISPLAY=wayland-1 weston > /tmp/weston.log 2>&1 &
EOF
chmod +x "$MNT_DIR/root/start-gui.sh"

cat > "$MNT_DIR/etc/profile.d/vcfs.sh" << 'EOF'
if [ "$(tty)" = "/dev/tty1" ]; then
    /root/start-gui.sh
fi
EOF
chmod +x "$MNT_DIR/etc/profile.d/vcfs.sh"

# Setup root auto-login for tty1 to see logs easily
sed -i 's/^tty1::respawn:\/sbin\/getty 38400 tty1/tty1::respawn:\/sbin\/agetty -a root --noclear 38400 tty1/' "$MNT_DIR/etc/inittab"
echo "[*] Extracting kernel and initramfs to boot externally..."
cp "$MNT_DIR/boot/vmlinuz-lts" /workspace/vmlinuz-virt
cp "$MNT_DIR/boot/initramfs-lts" /workspace/initramfs-virt

echo "[*] Cleaning up..."
rm "$MNT_DIR/etc/resolv.conf"
rm -rf "$MNT_DIR/payload_src"
rm -rf "$MNT_DIR/kernel_src"
rm -rf "$MNT_DIR/daemon"
umount "$MNT_DIR"

echo "[*] Converting to qcow2 format..."
qemu-img convert -f raw -O qcow2 "$IMG_RAW" "$IMG_QCOW2"
rm "$IMG_RAW"

echo "[*] Alpine image build complete."