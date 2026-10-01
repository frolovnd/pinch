<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ru">
<context>
    <name>pinch</name>
    <message id="app.description">
        <source>Screenshot tool: select an area and annotate it</source>
        <translation>Скриншот области экрана с рисованием</translation>
    </message>
    <message id="overlay.hint">
        <source>Drag to select · Esc to cancel</source>
        <translation>Выделите область · Esc — отмена</translation>
    </message>
    <message id="toolbar.tool.pen">
        <source>Pen (P)</source>
        <translation>Карандаш (P)</translation>
    </message>
    <message id="toolbar.tool.marker">
        <source>Marker (M)</source>
        <translation>Маркер (M)</translation>
    </message>
    <message id="toolbar.tool.line">
        <source>Line (L)</source>
        <translation>Линия (L)</translation>
    </message>
    <message id="toolbar.tool.arrow">
        <source>Arrow (A)</source>
        <translation>Стрелка (A)</translation>
    </message>
    <message id="toolbar.tool.rect">
        <source>Rectangle (R)</source>
        <translation>Прямоугольник (R)</translation>
    </message>
    <message id="toolbar.tool.ellipse">
        <source>Ellipse (E)</source>
        <translation>Эллипс (E)</translation>
    </message>
    <message id="toolbar.tool.text">
        <source>Text (T)</source>
        <translation>Текст (T)</translation>
    </message>
    <message id="toolbar.tool.counter">
        <source>Counter (N)</source>
        <translation>Номерок (N)</translation>
    </message>
    <message id="toolbar.tool.pixelate">
        <source>Pixelate (B)</source>
        <translation>Пикселизация (B)</translation>
    </message>
    <message id="toolbar.thickness.preset">
        <source>Thickness %1 px (key %2)</source>
        <translation>Толщина %1 px (клавиша %2)</translation>
    </message>
    <message id="toolbar.thickness.slider">
        <source>Thickness (mouse wheel, keys 1–5)</source>
        <translation>Толщина (колесо мыши, клавиши 1–5)</translation>
    </message>
    <message id="toolbar.thickness.label">
        <source>Thickness (mouse wheel)</source>
        <translation>Толщина (колесо мыши)</translation>
    </message>
    <message id="toolbar.undo">
        <source>Undo (Ctrl+Z)</source>
        <translation>Отменить (Ctrl+Z)</translation>
    </message>
    <message id="toolbar.redo">
        <source>Redo (Ctrl+Shift+Z)</source>
        <translation>Повторить (Ctrl+Shift+Z)</translation>
    </message>
    <message id="toolbar.copy">
        <source>Copy to clipboard (Ctrl+C)</source>
        <translation>Копировать в буфер (Ctrl+C)</translation>
    </message>
    <message id="toolbar.quicksave">
        <source>Quick save (Ctrl+S)</source>
        <translation>Быстро сохранить (Ctrl+S)</translation>
    </message>
    <message id="toolbar.saveas">
        <source>Save as (Ctrl+Shift+S)</source>
        <translation>Сохранить как (Ctrl+Shift+S)</translation>
    </message>
    <message id="toolbar.close">
        <source>Close (Esc)</source>
        <translation>Закрыть (Esc)</translation>
    </message>
    <message id="dialog.save.title">
        <source>Save screenshot</source>
        <translation>Сохранить скриншот</translation>
    </message>
    <message id="dialog.save.filter">
        <source>PNG (*.png)</source>
        <translation>PNG (*.png)</translation>
    </message>
    <message id="dialog.error.title">
        <source>pinch</source>
        <translation>pinch</translation>
    </message>
    <message id="error.save.create_temp">
        <source>Could not create a temporary file in “%1”: %2</source>
        <translation>Не удалось создать временный файл в каталоге «%1»: %2</translation>
    </message>
    <message id="error.save.write">
        <source>Could not write “%1”: %2</source>
        <translation>Не удалось записать «%1»: %2</translation>
    </message>
    <message id="error.save.rename">
        <source>Could not save “%1”: %2</source>
        <translation>Не удалось сохранить «%1»: %2</translation>
    </message>
    <message id="error.save.exists">
        <source>File “%1” already exists</source>
        <translation>Файл «%1» уже существует</translation>
    </message>
    <message id="error.save.encode">
        <source>Could not encode PNG</source>
        <translation>Не удалось закодировать PNG</translation>
    </message>
    <message id="error.save.names_exhausted">
        <source>All file names for this second are taken</source>
        <translation>Все имена файлов на эту секунду заняты</translation>
    </message>
    <message id="log.dumpable">
        <source>Could not disable memory dumps (PR_SET_DUMPABLE)</source>
        <translation>Не удалось отключить дампы памяти (PR_SET_DUMPABLE)</translation>
    </message>
    <message id="log.no_runtime_dir">
        <source>XDG_RUNTIME_DIR is not set - running without the instance lock</source>
        <translation>XDG_RUNTIME_DIR не задан — работаю без блокировки экземпляра</translation>
    </message>
    <message id="log.lock_failed">
        <source>Could not acquire lock %1 - running without it</source>
        <translation>Не удалось взять блокировку %1 — работаю без неё</translation>
    </message>
    <message id="error.capture.title">
        <source>pinch</source>
        <translation>pinch</translation>
    </message>
    <message id="error.capture.failed">
        <source>Could not capture the screen:</source>
        <translation>Не удалось снять экран:</translation>
    </message>
    <message id="error.capture.qt_grab">
        <source>could not grab the screen via Qt</source>
        <translation>не удалось снять экран средствами Qt</translation>
    </message>
    <message id="error.capture.unavailable_in_build">
        <source>method is not available in this build</source>
        <translation>метод недоступен в этой сборке</translation>
    </message>
    <message id="error.capture.unknown">
        <source>unknown error</source>
        <translation>неизвестная ошибка</translation>
    </message>
    <message id="error.capture.unknown_method">
        <source>unknown method PINCH_CAPTURE=%1 (allowed: x11, screencopy, kwin, portal)</source>
        <translation>неизвестный метод PINCH_CAPTURE=%1 (допустимо: x11, screencopy, kwin, portal)</translation>
    </message>
    <message id="error.capture.wayland.not_built">
        <source>this build has no Wayland support</source>
        <translation>сборка без поддержки Wayland</translation>
    </message>
    <message id="error.capture.wayland.no_connection">
        <source>no connection to the Wayland compositor</source>
        <translation>нет соединения с Wayland</translation>
    </message>
    <message id="error.capture.wayland.connection_error">
        <source>Wayland connection error: %1</source>
        <translation>ошибка соединения с Wayland: %1</translation>
    </message>
    <message id="error.capture.wayland.timeout">
        <source>the Wayland compositor did not respond within %1 s</source>
        <translation>композитор Wayland не ответил за %1 с</translation>
    </message>
    <message id="error.capture.screencopy.unsupported">
        <source>the compositor does not offer wlr-screencopy (zwlr_screencopy_manager_v1) or wl_shm</source>
        <translation>композитор не предоставляет wlr-screencopy (zwlr_screencopy_manager_v1) или wl_shm</translation>
    </message>
    <message id="error.capture.screencopy.bad_buffer">
        <source>unsupported format or buffer size</source>
        <translation>неподдерживаемый формат или размер буфера</translation>
    </message>
    <message id="error.capture.screencopy.alloc_failed">
        <source>could not allocate a buffer for the frame: %1</source>
        <translation>не удалось выделить буфер для кадра: %1</translation>
    </message>
    <message id="error.capture.screencopy.frame_failed">
        <source>the compositor failed to copy the frame</source>
        <translation>композитор не смог скопировать кадр</translation>
    </message>
    <message id="error.capture.screencopy.no_match">
        <source>could not match Wayland outputs to monitors</source>
        <translation>не удалось сопоставить выходы Wayland с мониторами</translation>
    </message>
</context>
</TS>
