wsl -d docker-desktop -u root mkdir -p /mnt/host/c/alpine_root/dev
wsl -d docker-desktop -u root mount -t devtmpfs devtmpfs /mnt/host/c/alpine_root/dev 2>$null
wsl -d docker-desktop -u root mkdir -p /mnt/host/c/alpine_root/proc
wsl -d docker-desktop -u root mount -t proc proc /mnt/host/c/alpine_root/proc 2>$null
wsl -d docker-desktop -u root mkdir -p /mnt/host/c/alpine_root/workspace
wsl -d docker-desktop -u root mount --bind /mnt/host/c/Users/bhaga/OneDrive/Desktop/ZeroDependency /mnt/host/c/alpine_root/workspace
wsl -d docker-desktop -u root chroot /mnt/host/c/alpine_root /bin/sh -c "cd /workspace && make CC=clang clean asan"
