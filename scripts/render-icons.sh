#!/usr/bin/env bash
# Пересобирает набор иконок hicolor из data/icons/pinch-source.png.
# Только для разработчика (нужен ImageMagick); готовые PNG лежат в репозитории.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/data/icons/pinch-source.png"

for n in 16 24 32 48 64 128 256 512; do
    dir="$root/data/icons/hicolor/${n}x${n}/apps"
    mkdir -p "$dir"
    # Lanczos сохраняет прозрачность (RGBA); -strip убирает метаданные, чтобы результат был воспроизводимым.
    convert "$src" -filter Lanczos -resize "${n}x${n}" -strip "$dir/pinch.png"
done
