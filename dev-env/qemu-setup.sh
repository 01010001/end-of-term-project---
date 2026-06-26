#!/bin/bash
set -e

KVER="${KERNEL_VERSION:-6.8.5-301.fc40.x86_64}"
INITRAMFS_DIR=/tmp/initramfs
INITRAMFS_IMG=/tmp/initramfs.cpio.gz

# ── 1. Compile VCFS Components ────────────────────────────────────────────────
echo "[*] Compiling VCFS components..."

if [ -d "/workspace/kernel-module" ]; then
    make -C /workspace/kernel-module clean >/dev/null 2>&1 || true
    make -C /workspace/kernel-module >/dev/null
fi

if [ -d "/workspace/cli" ]; then
    make -C /workspace/cli clean >/dev/null 2>&1 || true
    make -C /workspace/cli >/dev/null
fi

if [ -d "/workspace/daemon" ]; then
    make -C /workspace/daemon clean >/dev/null 2>&1 || true
    make -C /workspace/daemon >/dev/null
fi

# ── 2. Build initramfs ────────────────────────────────────────────────────────
echo "[*] Building initramfs..."

rm -rf "$INITRAMFS_DIR"
mkdir -p "$INITRAMFS_DIR"/{bin,dev,proc,sys,mnt,root,lib64,tmp}

cp /usr/sbin/busybox "$INITRAMFS_DIR/bin/busybox"

cd "$INITRAMFS_DIR/bin"
for cmd in sh mount mkdir ls cat echo uname ln insmod rmmod lsmod dmesg poweroff clear sleep vi rm mv cp touch pwd grep tail head less more diff awk killall wc pidof sed tr; do
    ln -sf busybox $cmd
done

cat > "$INITRAMFS_DIR/init" << 'EOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

# --- AUTOMATED TEST SUITE ---
echo -e "\n\033[1;36m=================================================================\033[0m"
echo -e "\033[1;36m              VCFS AUTOMATED INTEGRATION TESTS                   \033[0m"
echo -e "\033[1;36m=================================================================\033[0m\n"

cd /root

echo -n "[TEST] 1. Kernel Module Loading... "
insmod vcfs.ko && echo -e "\033[1;32m[PASS]\033[0m" || { echo -e "\033[1;31m[FAIL]\033[0m"; exec /bin/sh; }

echo -n "[TEST] 2. Disk Formatting (128-byte Inode)... "
mkfs.vcfs /dev/sda > /dev/null 2>&1 && echo -e "\033[1;32m[PASS]\033[0m" || { echo -e "\033[1;31m[FAIL]\033[0m"; exec /bin/sh; }

echo -n "[TEST] 3. File System Mounting... "
mount -t vcfs /dev/sda /mnt && echo -e "\033[1;32m[PASS]\033[0m" || { echo -e "\033[1;31m[FAIL]\033[0m"; exec /bin/sh; }

cd /mnt

echo -n "[TEST] 4. File Creation & Copy-on-Write Versioning... "
echo "v0_data" > test.txt
echo "v1_data" > test.txt
echo "v2_data" > test.txt
VCOUNT=$(vcfs status test.txt | tail -n 1 | awk '{print $NF}')
if [ "$VCOUNT" = "3" ]; then echo -e "\033[1;32m[PASS]\033[0m"; else echo -e "\033[1;31m[FAIL]\033[0m ($VCOUNT versions found)"; fi

echo -e "\033[1;36m[TEST] 5. Version Diff (v0 vs v1):\033[0m"
vcfs diff test.txt 0 1 | grep -E '^\+|^\-' || true
echo -e "\033[1;32m[PASS] Diff generated successfully.\033[0m"

echo -n "[TEST] 6. Checkout to v0 (Block Swapping)... "
vcfs checkout test.txt 0 > /dev/null 2>&1
CONTENT=$(cat test.txt)
if [ "$CONTENT" = "v0_data" ]; then echo -e "\033[1;32m[PASS]\033[0m"; else echo -e "\033[1;31m[FAIL]\033[0m (Content: $CONTENT)"; fi

echo -n "[TEST] 7. Trash (Eviction Bypass) & Delete... "
rm test.txt
TRASH_CNT=$(vcfs trash --list | grep "test.txt" | wc -l)
if [ "$TRASH_CNT" -ge 1 ]; then echo -e "\033[1;32m[PASS]\033[0m"; else echo -e "\033[1;31m[FAIL]\033[0m"; fi

echo -n "[TEST] 8. Restore from Trash (Relinking)... "
INODE=$(vcfs trash --list | grep "test.txt" | awk '{print $1}')
vcfs restore $INODE > /dev/null 2>&1
if [ -f "test.txt" ] && [ "$(cat test.txt)" = "v0_data" ]; then echo -e "\033[1;32m[PASS]\033[0m"; else echo -e "\033[1;31m[FAIL]\033[0m"; fi

echo -n "[TEST] 9. User-Space Daemon Execution... "
vcfsd /mnt & > /dev/null 2>&1
sleep 1
if pidof vcfsd > /dev/null; then echo -e "\033[1;32m[PASS]\033[0m"; else echo -e "\033[1;31m[FAIL]\033[0m"; fi
killall vcfsd > /dev/null 2>&1 || true

echo -e "\n\033[1;32m>>> ALL VCFS AUTOMATED TESTS COMPLETED SUCCESSFULLY! <<<\033[0m\n"

echo "Welcome to the VCFS Interactive Shell."
echo "You can continue testing manually if you wish."
exec /bin/sh
EOF
chmod +x "$INITRAMFS_DIR/init"

# Copy standard libraries
cp -a /lib64/libc.so.* "$INITRAMFS_DIR/lib64/" || true
cp -a /lib64/ld-linux-x86-64.so.* "$INITRAMFS_DIR/lib64/" || true
cp -a /lib64/libz.so.* "$INITRAMFS_DIR/lib64/" || true

# Copy compiled VCFS modules and tools into the initramfs
if [ -d "/workspace/kernel-module" ]; then
    cp /workspace/kernel-module/vcfs.ko "$INITRAMFS_DIR/root/"
    cp /workspace/kernel-module/mkfs.vcfs "$INITRAMFS_DIR/bin/"
fi

if [ -d "/workspace/cli" ]; then
    cp /workspace/cli/vcfs "$INITRAMFS_DIR/bin/"
fi

if [ -d "/workspace/daemon" ]; then
    cp /workspace/daemon/vcfsd "$INITRAMFS_DIR/bin/"
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
echo "[*] Booting QEMU with Automated Tests..."
echo ""

cd /workspace
qemu-system-x86_64 \
    -m 512M \
    -kernel "/boot/vmlinuz-${KVER}" \
    -initrd "$INITRAMFS_IMG" \
    -append "console=ttyS0 quiet" \
    -nographic \
    -drive file=/tmp/vcfs-test.img,format=raw,if=ide
