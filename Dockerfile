# Usar una imagen base de Ubuntu
FROM ubuntu:20.04

# Configurar el timezone de manera no interactiva
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=America/Bogota

# Instalar dependencias
RUN apt-get update && apt-get install -y \
    build-essential \
    qemu-system-x86 \
    qemu-system \
    gcc \
    make \
    git \
    gdb \
    nasm \
    python3 \
    texinfo \
    wget \
    xz-utils \
    time \
    nano \
    vim \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

RUN ln -s /usr/bin/qemu-system-i386 /usr/bin/qemu

WORKDIR /root

CMD ["/bin/bash"]