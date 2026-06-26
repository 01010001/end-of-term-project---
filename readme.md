# VCFS (Version Control File System)

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![Kernel](https://img.shields.io/badge/kernel-%3E%3D5.15-orange.svg)

VCFS is a proof-of-concept Linux Kernel file system with built-in, transparent file-level version control. Inspired by Apple's Time Machine and Git, VCFS automatically tracks changes, offering a built-in trash mechanism and delta-compression for efficient storage.

---

## 📁 Repository Structure

- **`kernel-module/`**: The core VCFS Linux kernel driver and formatting tools (`mkfs.vcfs`).
- **`daemon/`**: Background optimization service performing automatic Delta Compression (thinning).
- **`cli/`**: The command-line interface for managing versions, restoring files, and inspecting history.
- **`gui/`**: A native GTK3-based "Time Machine" application for visual version management.
- **`dev-env/`**: Docker and QEMU scripts to provide a safe, isolated development and testing environment.
- **`docs/`**: Additional documentation, including native Linux installation guides.
- **`tests/`**: Integration and unit testing scripts.

## 🚀 Quick Start (Isolated Development Environment)

Developing a kernel module directly on your host machine can be dangerous. VCFS provides a full development environment using Docker and QEMU, ensuring a consistent and safe workspace without modifying your host system.

### 1. Build and Enter the Environment

```bash
cd dev-env
# Build the isolated Docker image
docker compose build

# Enter the interactive shell inside the container
docker compose run --rm --service-ports kernel-dev bash
```

### 2. Automated Compilation and Virtual Testing

Inside the container, run the setup script. This will automatically compile the kernel module, CLI, and Daemon, build a temporary `initramfs`, and boot into a lightweight QEMU virtual machine.

```bash
# This will build the tools, run automated tests, and start the VM
/workspace/qemu-setup.sh
```

### 3. Manual Testing in QEMU

Once QEMU boots up, you will be dropped into a root shell with all VCFS tools available in your `PATH`.

```bash
cd /root

# 1. Insert the VCFS kernel module
insmod vcfs.ko

# 2. Format the virtual test disk
mkfs.vcfs /dev/sda

# 3. Mount the filesystem
mount -t vcfs /dev/sda /mnt
cd /mnt

# 4. Start the optimization daemon
vcfsd /mnt &
```

## 🛠️ Usage Examples (CLI)

The `vcfs` CLI tool allows you to interact with the file system's versioning capabilities directly:

```bash
# View the version history of a file
vcfs log my_document.txt

# Compare changes between two versions
vcfs diff my_document.txt 0 1

# Revert a file back to a specific version
vcfs checkout my_document.txt <version_id>

# List accidentally deleted files
vcfs trash --list

# Restore a deleted file (using its inode number)
vcfs restore <inode_no>
```

## 🖥️ Graphical Interface (GUI)

VCFS includes a native C/GTK3 application ("Time Machine") for users who prefer visual interaction.

**Option 1: Testing inside the Docker Environment (noVNC)**
You don't need a Linux desktop to test the GUI! The development environment includes a script that builds a complete Alpine Linux image with Wayland, runs the GUI, and streams it to your browser.

Inside the Docker container, simply run:
```bash
/workspace/qemu-test.sh gui
```
Then, open your web browser and go to `http://localhost:6080/vnc.html` to interact with the GUI visually.

**Option 2: Running Natively on a Linux Desktop**
If you are on a full Linux desktop environment with GTK3 installed, you can compile and run it natively:
```bash
cd gui
make
./vcfs-gui
```

## 📚 Documentation

For instructions on how to install and run VCFS natively on your own Linux distribution (like Fedora or Ubuntu) rather than inside Docker/QEMU, please refer to the [Native Linux Installation Guide](docs/LINUX_KURULUM_REHBERI.md).
