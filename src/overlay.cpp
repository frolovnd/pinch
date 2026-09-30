#include "overlay.h"

#include "keys.h"
#include "output.h"
#include "renderer.h"
#include "toolbar.h"

#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QTextStream>
#include <QWheelEvent>

namespace {
constexpr int kHandleTolerance = 6;
constexpr int kHandleSize = 8;
constexpr int kClickThreshold = 3;
constexpr int kMinThickness = 1;
constexpr int kMaxThickness = 40;

QColor accentColor()
{
    return QColor(0x3D, 0x8B, 0xFD);
}
}

Overlay::Overlay(Capture capture, Settings settings, QString saveDir, QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint)
    , m_capture(std::move(capture))
    , m_saveDir(std::move(saveDir))
    , m_style{settings.color, settings.thickness}
{
    setGeometry(QRect(m_capture.origin, m_capture.image.size()));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);

    m_dimmed = m_capture.image.copy();
    {
        QPainter painter(&m_dimmed);
        painter.fillRect(m_dimmed.rect(), QColor(0, 0, 0, 120));
    }
    m_hintScreen = screenAt(mapFromGlobal(QCursor::pos()));

    m_toolbar = new Toolbar(this);
    m_toolbar->hide();
    m_toolbar->setTool(m_tool);
    m_toolbar->setColor(m_style.color);
    m_toolbar->setThickness(m_style.thickness);
    connect(m_toolbar, &Toolbar::toolChosen, this, &Overlay::setTool);
    connect(m_toolbar, &Toolbar::colorChosen, this, &Overlay::setColor);
    connect(m_toolbar, &Toolbar::thicknessChosen, this, &Overlay::setThickness);
    connect(m_toolbar, &Toolbar::undoRequested, this, &Overlay::undo);
    connect(m_toolbar, &Toolbar::redoRequested, this, &Overlay::redo);
    connect(m_toolbar, &Toolbar::copyRequested, this, &Overlay::copyResult);
    connect(m_toolbar, &Toolbar::quickSaveRequested, this, &Overlay::saveQuick);
    connect(m_toolbar, &Toolbar::saveAsRequested, this, &Overlay::saveAs);
    connect(m_toolbar, &Toolbar::closeRequested, this, &Overlay::cancel);
}

void Overlay::start()
{
    show();
    raise();
    activateWindow();
    setFocus(Qt::OtherFocusReason);
    grabKeyboard();
}

QImage Overlay::result() const
{
    return ::render(m_capture.image, m_selection, m_document.annotations());
}

// ---- Состояние ----

void Overlay::setSelection(const QRect& selection)
{
    m_selection = selection;
    m_cacheValid = false;
    updateToolbar();
    update();
}

void Overlay::setTool(Tool tool)
{
    commitText();
    m_defaultToolApplied = true; // любой выбор (клавиша, панель, умолчание) отменяет дальнейший умолчательный выбор
    m_tool = tool;
    m_toolbar->setTool(tool);
    updateCursor(mapFromGlobal(QCursor::pos()));
}

void Overlay::applyDefaultTool()
{
    if (m_defaultToolApplied || m_selection.isEmpty())
        return;
    setTool(Tool::Pen);
}

void Overlay::setColor(const QColor& color)
{
    m_style.color = color;
    m_toolbar->setColor(color);
    if (m_textEditing)
        m_current->style.color = color;
    update();
}

void Overlay::setThickness(int thickness)
{
    m_style.thickness = qBound(kMinThickness, thickness, kMaxThickness);
    m_toolbar->setThickness(m_style.thickness);
    if (m_textEditing)
        m_current->style.thickness = m_style.thickness;
    update();
}

void Overlay::undo()
{
    commitText();
    if (m_document.undo())
        documentChanged();
}

void Overlay::redo()
{
    commitText();
    if (m_document.redo())
        documentChanged();
}

void Overlay::documentChanged()
{
    m_cacheValid = false;
    m_toolbar->setUndoRedoEnabled(m_document.canUndo(), m_document.canRedo());
    update();
}

void Overlay::updateToolbar()
{
    if (m_selection.isEmpty() || m_drag == Drag::Selecting) {
        m_toolbar->hide();
        return;
    }
    const QSize size = m_toolbar->sizeHint();
    m_toolbar->resize(size);
    m_toolbar->move(placeToolbar(m_selection, size, m_capture.screens));
    m_toolbar->show();
    m_toolbar->raise();
}

void Overlay::updateCursor(QPoint pos)
{
    if (m_selection.isEmpty()) {
        setCursor(Qt::CrossCursor);
        return;
    }
    switch (hitTestHandle(m_selection, pos, kHandleTolerance)) {
    case Handle::TopLeft:
    case Handle::BottomRight:
        setCursor(Qt::SizeFDiagCursor);
        return;
    case Handle::TopRight:
    case Handle::BottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        return;
    case Handle::Top:
    case Handle::Bottom:
        setCursor(Qt::SizeVerCursor);
        return;
    case Handle::Left:
    case Handle::Right:
        setCursor(Qt::SizeHorCursor);
        return;
    case Handle::Move:
        if (m_tool == Tool::None)
            setCursor(Qt::SizeAllCursor);
        else if (m_tool == Tool::Text)
            setCursor(Qt::IBeamCursor);
        else
            setCursor(Qt::CrossCursor);
        return;
    case Handle::None:
        setCursor(m_tool == Tool::None ? Qt::CrossCursor : Qt::ArrowCursor);
        return;
    }
}

QRect Overlay::screenAt(QPoint pos) const
{
    for (const QRect& s : m_capture.screens) {
        if (s.contains(pos))
            return s;
    }
    return {};
}

QRect Overlay::bounds() const
{
    return m_capture.image.rect();
}

// ---- Рисование ----

void Overlay::beginAnnotation(QPoint pos)
{
    Annotation a;
    a.tool = m_tool;
    a.style = m_style;
    a.points = {pos};
    switch (m_tool) {
    case Tool::Counter:
        m_document.add(a);
        documentChanged();
        return;
    case Tool::Text:
        m_current = a;
        m_textEditing = true;
        update();
        return;
    case Tool::Line:
    case Tool::Arrow:
    case Tool::Rect:
    case Tool::Ellipse:
    case Tool::Pixelate:
        a.points.append(pos); // [начало, конец]
        break;
    case Tool::Pen:
    case Tool::Marker:
    case Tool::None:
        break;
    }
    m_current = a;
    m_drag = Drag::Drawing;
    update();
}

void Overlay::finishAnnotation()
{
    if (!m_current)
        return;
    const Annotation a = *m_current;
    m_current.reset();

    bool valid = false;
    switch (a.tool) {
    case Tool::Pen:
        valid = !a.points.isEmpty();
        break;
    case Tool::Marker:
        valid = a.points.size() >= 2;
        break;
    case Tool::Line:
    case Tool::Arrow:
        valid = a.points.at(0) != a.points.at(1);
        break;
    case Tool::Rect:
    case Tool::Ellipse:
    case Tool::Pixelate: {
        const QRect r = rectFromPoints(a.points.at(0), a.points.at(1));
        valid = r.width() >= 2 && r.height() >= 2;
        break;
    }
    case Tool::None:
    case Tool::Text:
    case Tool::Counter:
        break;
    }
    if (valid) {
        m_document.add(a);
        documentChanged();
    } else {
        update();
    }
}

void Overlay::commitText()
{
    if (!m_textEditing)
        return;
    m_textEditing = false;
    const Annotation a = *m_current;
    m_current.reset();
    if (!a.text.isEmpty()) {
        m_document.add(a);
        documentChanged();
    } else {
        update();
    }
}

bool Overlay::handleTextKey(QKeyEvent* event)
{
    if (!m_textEditing)
        return false;
    if (event->key() == Qt::Key_Escape) {
        commitText();
        return true;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        commitText();
        return false; // сочетание обработает keyPressEvent
    }
    switch (event->key()) {
    case Qt::Key_Backspace:
        m_current->text.chop(1);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        m_current->text += QLatin1Char('\n');
        break;
    default: {
        const QString text = event->text();
        if (!text.isEmpty() && text.at(0).isPrint())
            m_current->text += text;
        break;
    }
    }
    update();
    return true;
}

void Overlay::paintCurrent(QPainter& painter) const
{
    if (!m_current)
        return;
    painter.save();
    painter.setClipRect(m_selection);
    drawAnnotation(painter, *m_current, m_document.nextCounterNumber());
    if (m_textEditing) {
        // Курсор в конце последней строки, по тем же метрикам, что и drawAnnotation.
        const QFontMetrics metrics(textFont(m_current->style.thickness));
        const QStringList lines = m_current->text.split(QLatin1Char('\n'));
        const QPoint origin = m_current->points.constFirst();
        const int x = origin.x() + metrics.horizontalAdvance(lines.constLast());
        const int y = origin.y() + int(lines.size() - 1) * metrics.lineSpacing();
        painter.setPen(QPen(m_current->style.color, 2));
        painter.drawLine(x, y, x, y + metrics.height());
    }
    painter.restore();
}

// ---- Вывод ----

void Overlay::closeOverlay()
{
    releaseKeyboard();
    hide();
}

void Overlay::copyResult()
{
    commitText();
    if (m_selection.isEmpty())
        return;
    const QImage image = result();
    closeOverlay();
    emit copyRequested(image);
}

void Overlay::saveQuick()
{
    commitText();
    if (m_selection.isEmpty())
        return;
    QString path;
    const WriteResult r = quickSave(result(), m_saveDir, QDateTime::currentDateTime(), &path);
    if (r.status != WriteResult::Ok) {
        showError(r.error);
        return;
    }
    QTextStream(stdout) << path << Qt::endl;
    closeOverlay();
    emit finished();
}

void Overlay::saveAs()
{
    commitText();
    if (m_selection.isEmpty())
        return;
    const QImage image = result();
    closeOverlay(); // окно поверх всех: иначе диалог окажется под ним

    // Собственный диалог Qt: суффикс .png добавляется до вопроса о перезаписи.
    QFileDialog dialog(nullptr, QStringLiteral("Сохранить скриншот"));
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(QStringLiteral("PNG (*.png)"));
    dialog.setDefaultSuffix(QStringLiteral("png"));
    dialog.setDirectory(m_saveDir);
    dialog.selectFile(quickSaveFileName(QDateTime::currentDateTime(), 0));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        start();
        return;
    }

    const QString path = dialog.selectedFiles().constFirst();
    const QByteArray png = encodePng(image);
    const WriteResult r = png.isEmpty()
        ? WriteResult{WriteResult::Error, QStringLiteral("не удалось закодировать PNG")}
        : writeFileAtomic(path, png, WriteMode::Replace);
    if (r.status != WriteResult::Ok) {
        showError(r.error);
        return;
    }
    QTextStream(stdout) << path << Qt::endl;
    emit finished();
}

void Overlay::cancel()
{
    closeOverlay();
    emit finished();
}

void Overlay::showError(const QString& text)
{
    closeOverlay();
    QMessageBox::critical(nullptr, QStringLiteral("pinch"), text);
    start();
}

// ---- Мышь и клавиатура ----

void Overlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    commitText(); // клик в любом месте завершает вводимый текст
    const QPoint pos = event->position().toPoint();
    m_pressPos = pos;
    m_selectionAtPress = m_selection;

    if (m_selection.isEmpty()) {
        m_drag = Drag::Selecting;
        return;
    }
    const Handle handle = hitTestHandle(m_selection, pos, kHandleTolerance);
    if (handle == Handle::None) {
        // Вне выделения: без инструмента — новое выделение (аннотации остаются), с инструментом — ничего.
        if (m_tool == Tool::None) {
            m_drag = Drag::Selecting;
            setSelection(QRect());
        }
        return;
    }
    if (handle != Handle::Move) {
        m_drag = Drag::Resizing;
        m_handle = handle;
        return;
    }
    if (m_tool == Tool::None) {
        m_drag = Drag::Moving;
        m_handle = Handle::Move;
        return;
    }
    beginAnnotation(pos);
}

void Overlay::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    switch (m_drag) {
    case Drag::None:
        updateCursor(pos);
        if (m_selection.isEmpty()) {
            const QRect screen = screenAt(pos);
            if (screen != m_hintScreen) {
                m_hintScreen = screen;
                update();
            }
        }
        return;
    case Drag::Selecting:
        setSelection(rectFromPoints(m_pressPos, pos).intersected(bounds()));
        return;
    case Drag::Moving:
    case Drag::Resizing:
        setSelection(applyHandleDrag(m_selectionAtPress, m_handle, pos - m_pressPos, bounds()));
        return;
    case Drag::Drawing: {
        if (!m_current)
            return;
        Annotation& a = *m_current;
        if (a.tool == Tool::Pen || a.tool == Tool::Marker) {
            if (pos != a.points.constLast())
                a.points.append(pos);
        } else {
            QPoint end = pos;
            if (event->modifiers().testFlag(Qt::ShiftModifier)) {
                const bool line = a.tool == Tool::Line || a.tool == Tool::Arrow;
                end = line ? snapLine45(a.points.at(0), pos) : snapSquare(a.points.at(0), pos);
            }
            a.points[1] = end;
        }
        update();
        return;
    }
    }
}

void Overlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPoint pos = event->position().toPoint();
    const Drag drag = m_drag;
    m_drag = Drag::None;
    m_handle = Handle::None;

    if (drag == Drag::Selecting) {
        // Клик без перетаскивания выделяет монитор под курсором (в мёртвой зоне — ничего).
        if ((pos - m_pressPos).manhattanLength() < kClickThreshold)
            setSelection(screenAt(pos));
        else
            setSelection(rectFromPoints(m_pressPos, pos).intersected(bounds()));
        applyDefaultTool(); // выделение зафиксировано отпусканием кнопки
    }
    else if (drag == Drag::Drawing)
        finishAnnotation();
    updateToolbar();
    updateCursor(pos);
}

// Двойной клик не должен работать как второе нажатие (Qt по умолчанию вызывает mousePressEvent).
void Overlay::mouseDoubleClickEvent(QMouseEvent* event)
{
    event->accept();
}

void Overlay::wheelEvent(QWheelEvent* event)
{
    // Тачпады и hi-res колёса шлют дробные шаги: копим, меняем толщину на 1 за каждые 120 единиц.
    const int delta = event->angleDelta().y();
    // Смена направления сбрасывает остаток: иначе он гасил бы первые единицы обратного движения.
    if ((delta > 0 && m_wheelAccumulator < 0) || (delta < 0 && m_wheelAccumulator > 0))
        m_wheelAccumulator = 0;
    m_wheelAccumulator += delta;
    const int steps = m_wheelAccumulator / 120;
    if (steps != 0) {
        m_wheelAccumulator -= steps * 120;
        setThickness(m_style.thickness + steps);
    }
    event->accept();
}

void Overlay::keyPressEvent(QKeyEvent* event)
{
    if (handleTextKey(event))
        return;
    const Qt::KeyboardModifiers mods = event->modifiers()
        & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool ctrl = mods.testFlag(Qt::ControlModifier);
    const bool shift = mods.testFlag(Qt::ShiftModifier);
    const int key = layoutIndependentKey(event->key(), event->nativeScanCode());

    if (key == Qt::Key_Escape) {
        cancel();
        return;
    }
    if (ctrl) {
        switch (key) {
        case Qt::Key_C:
            copyResult();
            return;
        case Qt::Key_S:
            if (shift)
                saveAs();
            else
                saveQuick();
            return;
        case Qt::Key_Z:
            if (shift)
                redo();
            else
                undo();
            return;
        case Qt::Key_Y:
            redo();
            return;
        case Qt::Key_A:
            setSelection(bounds());
            applyDefaultTool();
            return;
        default:
            return;
        }
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        copyResult();
        return;
    }
    if (mods != Qt::NoModifier || m_selection.isEmpty())
        return;
    switch (key) {
    case Qt::Key_P: setTool(Tool::Pen); return;
    case Qt::Key_M: setTool(Tool::Marker); return;
    case Qt::Key_L: setTool(Tool::Line); return;
    case Qt::Key_A: setTool(Tool::Arrow); return;
    case Qt::Key_R: setTool(Tool::Rect); return;
    case Qt::Key_E: setTool(Tool::Ellipse); return;
    case Qt::Key_T: setTool(Tool::Text); return;
    case Qt::Key_N: setTool(Tool::Counter); return;
    case Qt::Key_B: setTool(Tool::Pixelate); return;
    case Qt::Key_V: setTool(Tool::None); return;
    case Qt::Key_1: case Qt::Key_2: case Qt::Key_3: case Qt::Key_4: case Qt::Key_5:
        setThickness(Toolbar::thicknessPresets().at(key - Qt::Key_1));
        return;
    default: return;
    }
}

// ---- Отрисовка ----

void Overlay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.drawImage(0, 0, m_dimmed);
    if (m_selection.isEmpty()) {
        if (m_drag == Drag::None)
            paintHint(painter);
        return;
    }
    if (!m_cacheValid) {
        m_cache = ::render(m_capture.image, m_selection, m_document.annotations());
        m_cacheValid = true;
    }
    painter.drawImage(m_selection.topLeft(), m_cache);
    paintCurrent(painter);
    paintSelectionFrame(painter);
    paintSizeLabel(painter);
}

void Overlay::paintSelectionFrame(QPainter& painter) const
{
    const QRect& s = m_selection;
    painter.setPen(QPen(accentColor(), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(s.adjusted(0, 0, -1, -1));

    const QPoint handles[] = {
        s.topLeft(), QPoint(s.center().x(), s.top()), s.topRight(), QPoint(s.right(), s.center().y()),
        s.bottomRight(), QPoint(s.center().x(), s.bottom()), s.bottomLeft(), QPoint(s.left(), s.center().y()),
    };
    painter.setPen(Qt::NoPen);
    painter.setBrush(accentColor());
    for (const QPoint& p : handles)
        painter.drawRect(QRect(p.x() - kHandleSize / 2, p.y() - kHandleSize / 2, kHandleSize, kHandleSize));
}

void Overlay::paintSizeLabel(QPainter& painter) const
{
    const QString text = QStringLiteral("%1×%2").arg(m_selection.width()).arg(m_selection.height());
    const QFontMetrics metrics(painter.font());
    const QSize size(metrics.horizontalAdvance(text) + 12, metrics.height() + 6);
    // Над выделением, если там есть видимая часть монитора; иначе внутри, на видимом мониторе.
    const QPoint topLeft = sizeLabelPosition(m_selection, size, m_capture.screens);
    const QRect box(topLeft, size);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 160));
    painter.drawRoundedRect(box, 4, 4);
    painter.setPen(Qt::white);
    painter.drawText(box, Qt::AlignCenter, text);
}

void Overlay::paintHint(QPainter& painter) const
{
    QRect screen = m_hintScreen;
    if (screen.isEmpty())
        screen = m_capture.screens.isEmpty() ? rect() : m_capture.screens.constFirst();
    const QString text = QStringLiteral(
        "Выделите область мышью · клик — весь монитор · Ctrl+A — все мониторы · Esc — отмена");
    QFont font = painter.font();
    font.setPixelSize(18);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const QSize size(metrics.horizontalAdvance(text) + 40, metrics.height() + 24);
    const QRect box(screen.center() - QPoint(size.width() / 2, size.height() / 2), size);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRoundedRect(box, 8, 8);
    painter.setPen(Qt::white);
    painter.drawText(box, Qt::AlignCenter, text);
}
