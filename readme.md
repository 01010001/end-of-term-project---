# VCFS (Version Control File System)

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![Kernel](https://img.shields.io/badge/kernel-%3E%3D6.6-orange.svg)

**VCFS** is a proof-of-concept Linux Kernel file system with built-in, transparent file-level version control. Inspired by Apple's Time Machine and Git, VCFS automatically tracks file modifications at the kernel level, offering a built-in trash mechanism and delta-compression for efficient storage.

---

## ✨ Core Capabilities

* **Transparent Versioning**: Every time a file is modified, the kernel automatically preserves the previous state. No manual `git commit` or backup commands are required.
* **Built-in Trash Bin (Undelete)**: Deleting a file (`rm`) does not permanently erase it. Instead, the inode is marked as deleted and moved to an invisible trash bin. Files can be seamlessly restored via their inode number.
* **Delta Compression**: A background daemon continuously scans for file versions and aggressively compresses them by computing binary diffs (deltas), significantly saving disk space.
* **Full-Featured GUI**: A native GTK3-based "Time Machine" application provides an intuitive visual interface to navigate files, inspect version timelines, view diffs, and restore deleted files.

---

## 📁 Architecture & Components

The VCFS project is divided into several decoupled components:

1. **`kernel-module/`**: The core Linux VFS driver (`vcfs.ko`) and filesystem formatting tool (`mkfs.vcfs`). Implements versioning IOCTLs and directory/file operations.
2. **`daemon/`**: Background service (`vcfsd`) that handles asynchronous tasks like garbage collection and delta-compression.
3. **`cli/`**: A command-line utility (`vcfs`) for advanced users to manage versions, restore files, and interact directly with the kernel module.
4. **`gui/`**: A native GTK3 graphical application to visually interact with the filesystem.
5. **`dev-env/`**: A comprehensive, isolated Docker and QEMU-based testing environment that requires zero changes to your host machine.

---

## 🖥️ Graphical Interface (GUI)

VCFS includes a beautiful, fully functional GTK3 visual client with the following features:
- **File Explorer**: Browse the VCFS mount point with dynamic icons for files and folders.
- **Version Timeline**: Click on any file to instantly view a chronological timeline of all its historical versions, including modification dates and sizes.
- **Diff Viewer**: Compare the current state of a file against a historical version side-by-side to see exactly what changed.
- **Version Checkout**: Instantly restore a file to any past point in time with a single click.
- **Trash Bin**: View all deleted files across the filesystem and restore them to their original location.

---

## 🚀 Quick Start (Isolated Development Environment)

Developing and testing a kernel module directly on your host machine can be dangerous. VCFS provides a full development environment using Docker, QEMU, and Alpine Linux, ensuring a completely safe workspace that streams directly to your browser.

### 1. Build the Docker Environment

```bash
cd dev-env
docker compose build
docker compose run --rm --service-ports kernel-dev bash
```

### 2. Option A: Run Automated Tests

Inside the container, run the automated test suite. This will compile all components, boot a headless QEMU virtual machine, and run integration tests for file creation, versioning, and trash undeletion.

```bash
/workspace/qemu-setup.sh
```

### 3. Option B: Launch the Interactive GUI (Browser-Based)

You don't need a Linux desktop to test the GTK3 GUI! The dev environment can dynamically build a lightweight Alpine Linux VM with the Weston Wayland compositor and stream the UI to your web browser via noVNC.

```bash
# Inside the Docker container:
/workspace/qemu-test.sh gui
```

Once you see the `Starting websockify` message, open your web browser and navigate to:
👉 **`http://localhost:6080/vnc.html`**

*Note: The environment will automatically format a virtual 50MB disk, mount it, generate sample files, and launch the GUI connected to the VCFS mount point.*

---

## 🛠️ Command-Line Interface (CLI)

If you prefer the terminal, the `vcfs` CLI tool allows you to interact with the file system directly:

```bash
# View the version history of a file
vcfs log my_document.txt

# Compare changes between the current state and version 1
vcfs diff my_document.txt 0 1

# Revert a file back to a specific version ID
vcfs checkout my_document.txt <version_id>

# List all accidentally deleted files
vcfs trash --list

# Restore a deleted file using its inode number
vcfs restore <inode_no>
```

---

## 📚 Documentation

For instructions on how to install and run VCFS natively on your own Linux distribution (like Fedora or Ubuntu) rather than inside the Docker/QEMU environment, please refer to the [Native Linux Installation Guide](docs/LINUX_KURULUM_REHBERI.md).
