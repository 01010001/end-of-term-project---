#!/bin/bash
set -e

KVER="${KERNEL_VERSION:-6.8.5-301.fc40.x86_64}"
INITRAMFS_DIR=/tmp/initramfs
INITRAMFS_IMG=/tmp/initramfs.cpio.gz
MODULES_IMG=/tmp/modules.img
MODULES_MNT=/tmp/mnt

# ── 1. Build initramfs ────────────────────────────────────────────────────────
echo "[*] Building initramfs..."

rm -rf "$INITRAMFS_DIR"
mkdir -p "$INITRAMFS_DIR"/{bin,dev,proc,sys,mnt}

cp /usr/sbin/busybox "$INITRAMFS_DIR/bin/busybox"

cd "$INITRAMFS_DIR/bin"
for cmd in sh mount mkdir ls cat echo uname ln insmod rmmod lsmod dmesg poweroff; do
    ln -sf busybox $cmd
done

cat > "$INITRAMFS_DIR/init" << 'EOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mount /dev/sda /mnt
exec /bin/sh
EOF
chmod +x "$INITRAMFS_DIR/init"

mkdir -p /tmp/initramfs/lib64
cp /lib64/libc.so.6 /tmp/initramfs/lib64/
cp /lib64/ld-linux-x86-64.so.2 /tmp/initramfs/lib64/

cd "$INITRAMFS_DIR"
find . | cpio -o -H newc | gzip > "$INITRAMFS_IMG"
echo "[*] initramfs built: $INITRAMFS_IMG"

# ── 2. Build modules disk image ───────────────────────────────────────────────
echo "[*] Building modules disk image..."


# helloguys.ko
dd if=/dev/zero of="$MODULES_IMG" bs=1M count=10 2>/dev/null
mkfs.ext2 -F "$MODULES_IMG" > /dev/null

# simplefs / vcfs test image
dd if=/dev/zero of=/tmp/vcfs-test.img bs=1M count=50

mkdir -p "$MODULES_MNT"
mount -o loop "$MODULES_IMG" "$MODULES_MNT"

# compile and copy vcfs
if [ -d "/workspace/src-vcfs" ]; then
    make -C /workspace/src-vcfs
    cp /workspace/src-vcfs/*.ko "$MODULES_MNT/"
    cp /workspace/src-vcfs/mkfs.vcfs "$MODULES_MNT/"
fi

# compile and copy cli
if [ -d "/workspace/vcfs-cli" ]; then
    make -C /workspace/vcfs-cli
    cp /workspace/vcfs-cli/vcfs "$MODULES_MNT/"
fi

# compile and copy daemon
if [ -d "/workspace/vcfs-daemon" ]; then
    make -C /workspace/vcfs-daemon
    cp /workspace/vcfs-daemon/vcfsd "$MODULES_MNT/"
fi

umount "$MODULES_MNT"
echo "[*] Modules copied to disk image"

# ── 3. Boot QEMU ──────────────────────────────────────────────────────────────
echo ""
echo "[*] Booting QEMU..."
echo "[*] Inside the VM, run:"
echo "      mount /dev/sda /mnt"
echo "      insmod /mnt/vcfs.ko"
echo "      /mnt/mkfs.vcfs /dev/sdb"
echo "      mkdir -p /vcfs"
echo "      mount -o loop -t vcfs /dev/sdb /vcfs"
echo ""

qemu-system-x86_64 \
    -m 512M \
    -kernel "/boot/vmlinuz-${KVER}" \
    -initrd "$INITRAMFS_IMG" \
    -append "console=ttyS0 quiet" \
    -nographic \
    -drive file="$MODULES_IMG",format=raw,if=ide \
    -drive file=/tmp/vcfs-test.img,format=raw,if=ide

