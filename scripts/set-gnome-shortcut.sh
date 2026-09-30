#!/usr/bin/env bash
# Переназначает существующий ярлык GNOME custom0 (сейчас Shift+Super+S → gnome-screenshot -a)
# на hot-screenshot. Запускает только пользователь. Печатает прежние значения для отката.
set -euo pipefail

BIN="${1:-$HOME/.local/bin/hot-screenshot}"
if [[ ! -x "$BIN" ]]; then
    echo "Не найден исполняемый файл: $BIN" >&2
    exit 1
fi
BIN="$(realpath "$BIN")"
if [[ "$BIN" == *"'"* ]]; then
    echo "Путь не должен содержать апостроф: $BIN" >&2
    exit 1
fi

LIST_SCHEMA=org.gnome.settings-daemon.plugins.media-keys
SCHEMA=org.gnome.settings-daemon.plugins.media-keys.custom-keybinding
KPATH=/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom0/

if ! gsettings get "$LIST_SCHEMA" custom-keybindings | grep -qF "$KPATH"; then
    echo "Ярлык custom0 не найден в custom-keybindings — создайте ярлык в настройках GNOME вручную." >&2
    exit 1
fi

echo "Прежние значения (для отката через gsettings set):"
for key in name command binding; do
    printf '  %s = %s\n' "$key" "$(gsettings get "$SCHEMA:$KPATH" "$key")"
done

gsettings set "$SCHEMA:$KPATH" name "'hot-screenshot'"
gsettings set "$SCHEMA:$KPATH" command "'$BIN'"
echo "Готово: $(gsettings get "$SCHEMA:$KPATH" binding) → $BIN"
