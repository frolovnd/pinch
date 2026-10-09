#!/usr/bin/env bash
# Собирает .deb в чистом контейнере Ubuntu (нужен docker): зависимости, сборка, все тесты, cpack.
# Пакет — в dist/, имя с версией дистрибутива: pinch_<версия>_ubuntu<NN.NN>_amd64.deb.
# Образ по умолчанию — ubuntu:24.04 (Qt 6.4, минимальная поддерживаемая версия); другой: PINCH_DEB_IMAGE=ubuntu:26.04.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="${PINCH_DEB_IMAGE:-ubuntu:24.04}"
mkdir -p "$root/dist"

# Исходники монтируются только для чтения: сборка идёт в каталоге контейнера.
docker run --rm -v "$root:/src:ro" -v "$root/dist:/dist" \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" "$image" bash -euo pipefail -c '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends \
        qt6-base-dev qt6-wayland qt6-tools-dev qt6-l10n-tools libwayland-dev libwayland-bin \
        pkg-config cmake g++ ninja-build dpkg-dev file dbus fonts-noto-core >/dev/null
    cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build /build -j"$(nproc)"
    ctest --test-dir /build --output-on-failure
    (cd /build && cpack -G DEB)
    . /etc/os-release
    for deb in /build/*.deb; do
        name="$(basename "$deb" _amd64.deb)_${ID}${VERSION_ID}_amd64.deb"
        cp "$deb" "/dist/$name"
        chown "$HOST_UID:$HOST_GID" "/dist/$name"
        echo "/dist/$name"
    done
'
