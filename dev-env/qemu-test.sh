#!/bin/bash
set -e

MODE="${1:-cli}"
KVER="${KERNEL_VERSION:-6.8.5-301.fc40.x86_64}"

echo "[*] Compiling VCFS components..."
make -C /workspace/kernel-module clean >/dev/null 2>&1 || true
make -C /workspace/kernel-module >/dev/null
make -C /workspace/cli clean >/dev/null 2>&1 || true
make -C /workspace/cli >/dev/null
make -C /workspace/daemon clean >/dev/null 2>&1 || true
make -C /workspace/daemon >/dev/null

if [ "$MODE" = "gui" ]; then
    # We build the GUI inside the Alpine chroot to avoid libc conflicts
    echo "[*] Will build GUI inside Alpine environment..."
fi

if [ "$MODE" = "cli" ]; then
    echo "[*] Starting CLI Mode (Headless Busybox)..."
    INITRAMFS_DIR=/tmp/initramfs
    INITRAMFS_IMG=/tmp/initramfs.cpio.gz

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
cd /root
echo "Welcome to the VCFS Interactive Shell."
exec /bin/sh
EOF
    chmod +x "$INITRAMFS_DIR/init"

    cp -a /lib64/libc.so.* "$INITRAMFS_DIR/lib64/" || true
    cp -a /lib64/ld-linux-x86-64.so.* "$INITRAMFS_DIR/lib64/" || true
    cp -a /lib64/libz.so.* "$INITRAMFS_DIR/lib64/" || true

    cp /workspace/kernel-module/vcfs.ko "$INITRAMFS_DIR/root/"
    cp /workspace/kernel-module/mkfs.vcfs "$INITRAMFS_DIR/bin/"
    cp /workspace/cli/vcfs "$INITRAMFS_DIR/bin/"
    cp /workspace/daemon/vcfsd "$INITRAMFS_DIR/bin/"

    cd "$INITRAMFS_DIR"
    find . | cpio -o -H newc | gzip > "$INITRAMFS_IMG"
    
    dd if=/dev/zero of=/tmp/vcfs-test.img bs=1M count=50 2>/dev/null

    cd /workspace
    qemu-system-x86_64 \
        -m 512M \
        -kernel "/boot/vmlinuz-${KVER}" \
        -initrd "$INITRAMFS_IMG" \
        -append "console=ttyS0 quiet" \
        -nographic \
        -drive file=/tmp/vcfs-test.img,format=raw,if=ide

elif [ "$MODE" = "gui" ]; then
    echo "[*] Starting GUI Mode (Alpine Linux + noVNC)..."
    
    /workspace/build-alpine.sh

    echo "[*] Creating FAT32 payload image..."
    dd if=/dev/zero of=/tmp/payload.img bs=1M count=20 2>/dev/null
    mkfs.vfat /tmp/payload.img >/dev/null
    mcopy -i /tmp/payload.img /workspace/vcfs-alpine.ko ::/vcfs.ko
    mcopy -i /tmp/payload.img /workspace/gui-alpine ::/vcfs-gui
    
    echo "[*] Starting websockify (noVNC)..."
    killall websockify 2>/dev/null || true
    websockify -D --web=/usr/share/novnc 0.0.0.0:6080 127.0.0.1:5900

    echo -e "\n\033[1;32m[*] Access the GUI from your host browser at: http://localhost:6080/vnc.html\033[0m\n"
    
    qemu-system-x86_64 \
        -m 1G \
        -kernel /workspace/vmlinuz-virt \
        -initrd /workspace/initramfs-virt \
        -append "root=/dev/vda rootfstype=ext4 rw console=ttyS0" \
        -drive file=/workspace/alpine.qcow2,format=qcow2,if=virtio \
        -drive file=/tmp/payload.img,format=raw,if=virtio \
        -device virtio-gpu-pci \
        -display vnc=127.0.0.1:0 \
        -serial stdio
else
    echo "Unknown mode: $MODE. Use 'cli' or 'gui'."
    exit 1
fi