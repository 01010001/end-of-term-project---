# VCFS (Version Control File System)

This repository contains a proof-of-concept Linux Kernel file system with file-level version control, an optimization daemon, a CLI tool, and a GTK-based GUI.

## Docker Development Environment

A full development environment is provided via Docker. This ensures a consistent environment for compiling the Linux Kernel module, the CLI, and the Daemon.

```bash
# Build the Docker image
docker compose build

# Run the Docker container
docker compose run --rm kernel-dev bash
```

## Compilation and VM Execution

Inside the Docker container, you can use the provided script to automatically build all components (VCFS kernel module, CLI, and Daemon) and boot into a lightweight QEMU virtual machine for testing. **An automated test suite will run on boot to verify all functionalities.**

```bash
# This will build the tools and start the VM
/workspace/qemu-setup.sh
```

## Running the Components in QEMU

Once the QEMU VM boots, you will be dropped into a root shell. 
The compiled components are already placed directly into your PATH (`/bin`) and the `/root` directory inside the RAM disk.

### 1. Mounting the File System

First, insert the kernel module, format the test disk, and mount the file system:

```bash
cd /root

# Insert the VCFS kernel module
insmod vcfs.ko

# Format the test disk with VCFS (QEMU maps it to /dev/sda)
mkfs.vcfs /dev/sda

# Mount the filesystem (without loop, as it is a block device)
mount -t vcfs /dev/sda /mnt
cd /mnt
```

### 2. Starting the Optimization Daemon

The User-Space Daemon periodically scans the file system and performs Delta Compression (Thinning) to save disk space.

```bash
# Start the daemon in the background
vcfsd /mnt &

# Check logs (if necessary)
dmesg
```

### 3. Using the CLI

You can use the CLI tool to manage versions and the trash mechanism.

```bash
# Check version status of a file
vcfs status file.txt

# View version history (includes file size and active version indicator)
vcfs log file.txt

# Compare two versions of a file
vcfs diff file.txt 0 1

# Checkout a previous version
vcfs checkout file.txt <version_id>

# List deleted files in the trash bin
vcfs trash --list

# Restore a deleted file from trash (Using the Inode Number from the list)
vcfs restore <inode_no>

# Permanently purge the trash bin
vcfs trash --clean
```

### 4. Running the Native GUI

The GUI is written in C and GTK3 to provide a zero-overhead native application. Because QEMU is running without an X server (nographic mode), the GUI is intended to be built and run on a full Linux Desktop environment.

To test the GUI on a Linux Desktop with GTK3 installed:

```bash
cd dev-env/vcfs-gui
make
./vcfs-gui
```
