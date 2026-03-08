Kernel Module Development Environment via Docker

```bash
docker compuse build
docker compose run --rm kernel-dev bash
```

inside the container
```
/workspace/test/qemu-setup.sh
```

inside qemu vm
```bash
insmod /mnt/simplefs.ko
/mnt/mkfs.simplefs /dev/sdb
mkdir /simplefs
mount -o loop -t simplefs /dev/sdb /simplefs
cd /simplefs
```
