#!/bin/bash
set -e

KVER="${KERNEL_VERSION:-6.8.5-301.fc40.x86_64}"
INITRAMFS_DIR=/tmp/initramfs
INITRAMFS_IMG=/tmp/initramfs.cpio.gz

# ── 1. Compile VCFS Components ────────────────────────────────────────────────
echo "[*] Compiling VCFS components..."

if [ -d "/workspace/src-vcfs" ]; then
    make -C /workspace/src-vcfs
fi

if [ -d "/workspace/vcfs-cli" ]; then
    make -C /workspace/vcfs-cli
fi

if [ -d "/workspace/vcfs-daemon" ]; then
    make -C /workspace/vcfs-daemon
fi

# ── 2. Build initramfs ────────────────────────────────────────────────────────
echo "[*] Building initramfs..."

rm -rf "$INITRAMFS_DIR"
mkdir -p "$INITRAMFS_DIR"/{bin,dev,proc,sys,mnt,root,lib64}

cp /usr/sbin/busybox "$INITRAMFS_DIR/bin/busybox"

cd "$INITRAMFS_DIR/bin"
for cmd in sh mount mkdir ls cat echo uname ln insmod rmmod lsmod dmesg poweroff clear sleep vi; do
    ln -sf busybox $cmd
done

cat > "$INITRAMFS_DIR/init" << 'EOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
exec /bin/sh
EOF
chmod +x "$INITRAMFS_DIR/init"

# Copy standard libraries
cp -a /lib64/libc.so.* "$INITRAMFS_DIR/lib64/" || true
cp -a /lib64/ld-linux-x86-64.so.* "$INITRAMFS_DIR/lib64/" || true
cp -a /lib64/libz.so.* "$INITRAMFS_DIR/lib64/" || true

# Copy compiled VCFS modules and tools into the initramfs
if [ -d "/workspace/src-vcfs" ]; then
    cp /workspace/src-vcfs/vcfs.ko "$INITRAMFS_DIR/root/"
    cp /workspace/src-vcfs/mkfs.vcfs "$INITRAMFS_DIR/bin/"
fi

if [ -d "/workspace/vcfs-cli" ]; then
    cp /workspace/vcfs-cli/vcfs "$INITRAMFS_DIR/bin/"
fi

if [ -d "/workspace/vcfs-daemon" ]; then
    cp /workspace/vcfs-daemon/vcfsd "$INITRAMFS_DIR/bin/"
fi

# Pack the initramfs
cd "$INITRAMFS_DIR"
find . | cpio -o -H newc | gzip > "$INITRAMFS_IMG"
echo "[*] initramfs built: $INITRAMFS_IMG"

# ── 3. Build test disk image ───────────────────────────────────────────────
echo "[*] Creating VCFS test disk image (/tmp/vcfs-test.img)..."
dd if=/dev/zero of=/tmp/vcfs-test.img bs=1M count=50 2>/dev/null

# ── 4. Boot QEMU ──────────────────────────────────────────────────────────────
echo ""
echo "[*] Booting QEMU..."
echo "[*] Inside the VM, your tools are ready! Run:"
echo "      cd /root"
echo "      insmod vcfs.ko"
echo "      mkfs.vcfs /dev/sda"
echo "      mount -o loop -t vcfs /dev/sda /mnt"
echo "      vcfsd /mnt &"
echo ""

cd /workspace
qemu-system-x86_64 \
    -m 512M \
    -kernel "/boot/vmlinuz-${KVER}" \
    -initrd "$INITRAMFS_IMG" \
    -append "console=ttyS0 quiet" \
    -nographic \
    -drive file=/tmp/vcfs-test.img,format=raw,if=ide
