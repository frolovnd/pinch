# Протоколы Wayland

## wlr-screencopy-unstable-v1.xml

- Источник: https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/raw/master/unstable/wlr-screencopy-unstable-v1.xml
- Дата загрузки: 2026-10-01; файл не изменён (sha256 `131b8f9b4aad0c8a9cf705e90d2a1511a5ca0c477637fd3400cf1cc4fa963fb8`).
- Назначение: снимок экрана в Hyprland и Sway (`src/screencopy.cpp`). Код клиента генерирует `wayland-scanner`
  при сборке (`client-header`, `private-code`) в каталог сборки `protocols/`.
- Лицензия (MIT, из заголовка XML):

```
Copyright © 2018 Simon Ser
Copyright © 2019 Andri Yngvason

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```
