#include "toolbar.h"

#include <QCoreApplication>

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <QToolButton>

namespace {
struct ToolDef {
    Tool tool;
    const char* name;
    const char* glyph;
    const char* tip;
};

const ToolDef kTools[] = {
    {Tool::Pen, "tool-pen", "✎", QT_TRID_NOOP("toolbar.tool.pen")},
    {Tool::Marker, "tool-marker", "▮", QT_TRID_NOOP("toolbar.tool.marker")},
    {Tool::Line, "tool-line", "╱", QT_TRID_NOOP("toolbar.tool.line")},
    {Tool::Arrow, "tool-arrow", "↗", QT_TRID_NOOP("toolbar.tool.arrow")},
    {Tool::Rect, "tool-rect", "▭", QT_TRID_NOOP("toolbar.tool.rect")},
    {Tool::Ellipse, "tool-ellipse", "◯", QT_TRID_NOOP("toolbar.tool.ellipse")},
    {Tool::Text, "tool-text", "T", QT_TRID_NOOP("toolbar.tool.text")},
    {Tool::Counter, "tool-counter", "①", QT_TRID_NOOP("toolbar.tool.counter")},
    {Tool::Pixelate, "tool-pixelate", "▦", QT_TRID_NOOP("toolbar.tool.pixelate")},
};
}

Toolbar::Toolbar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("toolbar"));
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor); // не наследовать перекрестие/«перемещение» оверлея
    setStyleSheet(QStringLiteral(
        "#toolbar { background: rgba(32, 32, 32, 235); border-radius: 6px; }"
        "QToolButton { color: white; background: transparent; border: 1px solid transparent;"
        " border-radius: 4px; font-size: 16px; min-width: 28px; min-height: 28px; }"
        "QToolButton:hover { background: rgba(255, 255, 255, 40); }"
        "QToolButton:checked { background: rgba(61, 139, 253, 160); }"
        "QToolButton:disabled { color: rgba(255, 255, 255, 80); }"
        "QLabel { color: white; padding: 0 4px; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(2);
    const auto newRow = [root] {
        auto* row = new QHBoxLayout;
        row->setSpacing(2);
        root->addLayout(row);
        return row;
    };
    QHBoxLayout* toolsRow = newRow();
    QHBoxLayout* styleRow = newRow();
    QHBoxLayout* actionsRow = newRow();

    // Ряд 1: инструменты.
    for (const ToolDef& def : kTools) {
        QToolButton* b = addButton(toolsRow, QString::fromLatin1(def.name), QString::fromUtf8(def.glyph),
                                   qtTrId(def.tip));
        b->setCheckable(true);
        const Tool tool = def.tool;
        connect(b, &QToolButton::clicked, this, [this, tool] {
            // Повторное нажатие активного инструмента выключает его.
            const Tool next = tool == m_tool ? Tool::None : tool;
            setTool(next);
            emit toolChosen(next);
        });
        m_toolButtons.insert(tool, b);
    }

    // Ряд 2: цвета и толщина.
    const QVector<QColor>& colors = palette();
    for (int i = 0; i < colors.size(); ++i) {
        const QColor color = colors.at(i);
        QToolButton* b = addButton(styleRow, QStringLiteral("color-%1").arg(i), QString(), color.name());
        b->setCheckable(true);
        b->setStyleSheet(QStringLiteral(
            "QToolButton { background: %1; min-width: 18px; max-width: 18px; min-height: 18px; max-height: 18px;"
            " border: 2px solid rgba(255, 255, 255, 60); border-radius: 3px; }"
            "QToolButton:checked { border: 2px solid white; }").arg(color.name()));
        connect(b, &QToolButton::clicked, this, [this, color] {
            setColor(color);
            emit colorChosen(color);
        });
        m_colorButtons.append(b);
    }

    addSeparator(styleRow);
    // Значок пресета: белый круг, диаметр растёт с толщиной.
    static const int kDiameters[] = {4, 6, 9, 12, 16};
    const QVector<int>& presets = thicknessPresets();
    for (int i = 0; i < presets.size(); ++i) {
        const int value = presets.at(i);
        QToolButton* b = addButton(styleRow, QStringLiteral("thickness-%1").arg(i), QString(),
                                   qtTrId("toolbar.thickness.preset").arg(value).arg(i + 1));
        b->setCheckable(true);
        QPixmap pixmap(18, 18);
        pixmap.fill(Qt::transparent);
        {
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::white);
            const qreal d = kDiameters[i];
            painter.drawEllipse(QRectF((18 - d) / 2, (18 - d) / 2, d, d));
        }
        b->setIcon(QIcon(pixmap));
        connect(b, &QToolButton::clicked, this, [this, value] {
            setThickness(value);
            emit thicknessChosen(value);
        });
        m_presetButtons.append(b);
    }

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setObjectName(QStringLiteral("thickness-slider"));
    m_slider->setRange(1, 40);
    m_slider->setSingleStep(1);
    m_slider->setPageStep(5);
    m_slider->setFixedWidth(90);
    m_slider->setFocusPolicy(Qt::NoFocus);
    m_slider->setToolTip(qtTrId("toolbar.thickness.slider"));
    m_slider->installEventFilter(this); // колесо над ползунком уходит оверлею, а не меняет ползунок
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        setThickness(v);
        emit thicknessChosen(v);
    });
    styleRow->addWidget(m_slider);

    m_thickness = new QLabel(this);
    m_thickness->setObjectName(QStringLiteral("thickness"));
    m_thickness->setToolTip(qtTrId("toolbar.thickness.label"));
    styleRow->addWidget(m_thickness);

    // Ряд 3: история слева, вывод справа.
    m_undo = addButton(actionsRow, QStringLiteral("undo"), QStringLiteral("↶"), qtTrId("toolbar.undo"));
    m_redo = addButton(actionsRow, QStringLiteral("redo"), QStringLiteral("↷"), qtTrId("toolbar.redo"));
    connect(m_undo, &QToolButton::clicked, this, &Toolbar::undoRequested);
    connect(m_redo, &QToolButton::clicked, this, &Toolbar::redoRequested);

    actionsRow->addStretch();
    connect(addButton(actionsRow, QStringLiteral("copy"), QStringLiteral("⧉"), qtTrId("toolbar.copy")),
            &QToolButton::clicked, this, &Toolbar::copyRequested);
    connect(addButton(actionsRow, QStringLiteral("quicksave"), QStringLiteral("↓"), qtTrId("toolbar.quicksave")),
            &QToolButton::clicked, this, &Toolbar::quickSaveRequested);
    connect(addButton(actionsRow, QStringLiteral("saveas"), QStringLiteral("…"), qtTrId("toolbar.saveas")),
            &QToolButton::clicked, this, &Toolbar::saveAsRequested);
    connect(addButton(actionsRow, QStringLiteral("close"), QStringLiteral("✕"), qtTrId("toolbar.close")),
            &QToolButton::clicked, this, &Toolbar::closeRequested);

    setTool(Tool::None);
    setThickness(4);
    setUndoRedoEnabled(false, false);
}

const QVector<QColor>& Toolbar::palette()
{
    static const QVector<QColor> colors = {
        QColor(0xE5, 0x39, 0x35), QColor(0xFB, 0x8C, 0x00), QColor(0xFD, 0xD8, 0x35), QColor(0x43, 0xA0, 0x47),
        QColor(0x1E, 0x88, 0xE5), QColor(0x8E, 0x24, 0xAA), QColor(0x00, 0x00, 0x00), QColor(0xFF, 0xFF, 0xFF),
    };
    return colors;
}

const QVector<int>& Toolbar::thicknessPresets()
{
    static const QVector<int> presets = {2, 4, 8, 14, 24};
    return presets;
}

void Toolbar::wheelEvent(QWheelEvent* event)
{
    forwardWheel(event, this);
}

bool Toolbar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_slider && event->type() == QEvent::Wheel) {
        forwardWheel(static_cast<QWheelEvent*>(event), m_slider);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void Toolbar::forwardWheel(QWheelEvent* event, QWidget* source)
{
    // Явная пересылка родителю: не зависим от того, распространяет ли Qt колесо вверх по иерархии.
    QWidget* parent = parentWidget();
    if (!parent) {
        event->ignore();
        return;
    }
    const QPointF local = QPointF(source->mapTo(this, event->position().toPoint())) + pos();
    QWheelEvent forwarded(local, event->globalPosition(), event->pixelDelta(),
                          event->angleDelta(), event->buttons(), event->modifiers(), event->phase(),
                          event->inverted(), event->source());
    QApplication::sendEvent(parent, &forwarded);
    event->accept();
}

void Toolbar::setTool(Tool tool)
{
    m_tool = tool;
    for (auto it = m_toolButtons.cbegin(); it != m_toolButtons.cend(); ++it)
        it.value()->setChecked(it.key() == tool);
}

void Toolbar::setColor(const QColor& color)
{
    for (int i = 0; i < m_colorButtons.size(); ++i)
        m_colorButtons.at(i)->setChecked(palette().at(i) == color);
}

void Toolbar::setThickness(int thickness)
{
    m_thickness->setText(QStringLiteral("%1px").arg(thickness));
    const QVector<int>& presets = thicknessPresets();
    for (int i = 0; i < m_presetButtons.size(); ++i)
        m_presetButtons.at(i)->setChecked(presets.at(i) == thickness);
    // Программное обновление не должно порождать thicknessChosen.
    const QSignalBlocker blocker(m_slider);
    m_slider->setValue(thickness);
}

void Toolbar::setUndoRedoEnabled(bool canUndo, bool canRedo)
{
    m_undo->setEnabled(canUndo);
    m_redo->setEnabled(canRedo);
}

QToolButton* Toolbar::addButton(QHBoxLayout* row, const QString& objectName, const QString& text, const QString& toolTip)
{
    auto* b = new QToolButton(this);
    b->setObjectName(objectName);
    b->setText(text);
    b->setToolTip(toolTip);
    b->setFocusPolicy(Qt::NoFocus);
    row->addWidget(b);
    return b;
}

void Toolbar::addSeparator(QHBoxLayout* row)
{
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::VLine);
    line->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 60);"));
    row->addWidget(line);
}
