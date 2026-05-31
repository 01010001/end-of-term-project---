FROM fedora:40

ARG KERNEL_VERSION=6.8.5-301.fc40.x86_64

RUN dnf update -y && dnf install -y \
    gcc make perl git vim \
    elfutils-libelf-devel \
    openssl-devel \
    bc flex bison \
    clang llvm sparse \
    kmod \
    qemu-system-x86 \
    qemu-img \
    busybox \
    e2fsprogs \
    novnc \
    python3-websockify \
    mtools \
    dosfstools \
    wget \
    tar \
    && dnf clean all

RUN dnf install -y \
    kernel-${KERNEL_VERSION} \
    kernel-devel-${KERNEL_VERSION} \
    && dnf clean all

RUN mkdir -p /lib/modules/${KERNEL_VERSION} && \
    ln -sf /usr/src/kernels/${KERNEL_VERSION} \
           /lib/modules/${KERNEL_VERSION}/build

ENV KERNEL_VERSION=${KERNEL_VERSION}
WORKDIR /workspace