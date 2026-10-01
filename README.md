# pinch

A small screenshot tool for Linux: select an area, draw on it, copy it to the clipboard or save it.
Inspired by Flameshot.

## Supported environments

- X11
- Wayland: GNOME, KDE Plasma, Hyprland, Sway (and other wlroots compositors)

Multiple monitors are supported. The interface is in English or Russian, following the system language.

## Install

Ubuntu / Debian:

```bash
sudo apt install qt6-base-dev qt6-wayland qt6-tools-dev qt6-l10n-tools libwayland-dev libwayland-bin cmake g++
```

Arch:

```bash
sudo pacman -S qt6-base qt6-wayland qt6-tools wayland cmake gcc noto-fonts
```

Build and install:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
cmake --install build --prefix ~/.local
```

This installs `~/.local/bin/pinch`, the icon and a menu entry. If a newer compiler stops the build on warnings,
add `-DPINCH_WERROR=OFF` to the first command.

## Hotkey

pinch takes a screenshot when it starts, so bind it to a key:

- **GNOME:** `scripts/set-gnome-shortcut.sh` (sets Shift+Super+S), or Settings → Keyboard → Custom Shortcuts.
- **KDE Plasma:** System Settings → Keyboard → Shortcuts → Add New → Application → pinch.
- **Hyprland:** `bind = SUPER SHIFT, S, exec, ~/.local/bin/pinch`
- **Sway:** `bindsym $mod+Shift+s exec ~/.local/bin/pinch`

## Usage

Press the hotkey, drag to select an area (a click selects the whole monitor). The pen is active right away —
draw inside the selection, then copy or save.

| Key | Action |
|---|---|
| Mouse | Select an area; click = whole monitor |
| Ctrl+A | Select all monitors |
| P M L A R E T N B | Pen, marker, line, arrow, rectangle, ellipse, text, number, pixelate |
| V | Move / resize the selection (drag outside it to start a new one) |
| Shift while drawing | 45° lines, squares and circles |
| 1 – 5 | Line width presets; mouse wheel changes it by 1 |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Ctrl+C or Enter | Copy to clipboard |
| Ctrl+S | Save to ~/Pictures/Screenshots |
| Ctrl+Shift+S | Save as… |
| Esc | Finish text / close |

Hotkeys work in any keyboard layout.

## Notes

- **KDE:** run the installed `pinch` (after `cmake --install`); otherwise screenshots go through the slower portal.
- **GNOME:** the first screenshot may need permission: Settings → Apps → pinch → Screenshots.
- If capturing fails, pinch shows the reason. You can force a method:
  `PINCH_CAPTURE=x11|screencopy|kwin|portal pinch`.
