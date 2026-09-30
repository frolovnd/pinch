#include "toolbar.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace {
struct ToolDef {
    Tool tool;
    const char* name;
    const char* glyph;
    const char* tip;
};

const ToolDef kTools[] = {
    {Tool::Pen, "tool-pen", "✎", "Карандаш (P)"},
    {Tool::Marker, "tool-marker", "▮", "Маркер (M)"},
    {Tool::Line, "tool-line", "╱", "Линия (L)"},
    {Tool::Arrow, "tool-arrow", "↗", "Стрелка (A)"},
    {Tool::Rect, "tool-rect", "▭", "Прямоугольник (R)"},
    {Tool::Ellipse, "tool-ellipse", "◯", "Эллипс (E)"},
    {Tool::Text, "tool-text", "T", "Текст (T)"},
    {Tool::Counter, "tool-counter", "①", "Номерок (N)"},
    {Tool::Pixelate, "tool-pixelate", "▦", "Пикселизация (B)"},
};
}

Toolbar::Toolbar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("toolbar"));
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet(QStringLiteral(
        "#toolbar { background: rgba(32, 32, 32, 235); border-radius: 6px; }"
        "QToolButton { color: white; background: transparent; border: 1px solid transparent;"
        " border-radius: 4px; font-size: 16px; min-width: 28px; min-height: 28px; }"
        "QToolButton:hover { background: rgba(255, 255, 255, 40); }"
        "QToolButton:checked { background: rgba(61, 139, 253, 160); }"
        "QToolButton:disabled { color: rgba(255, 255, 255, 80); }"
        "QLabel { color: white; padding: 0 4px; }"));

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(2);

    for (const ToolDef& def : kTools) {
        QToolButton* b = addButton(QString::fromLatin1(def.name), QString::fromUtf8(def.glyph), QString::fromUtf8(def.tip));
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

    addSeparator();
    const QVector<QColor>& colors = palette();
    for (int i = 0; i < colors.size(); ++i) {
        const QColor color = colors.at(i);
        QToolButton* b = addButton(QStringLiteral("color-%1").arg(i), QString(), color.name());
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

    addSeparator();
    m_thickness = new QLabel(this);
    m_thickness->setObjectName(QStringLiteral("thickness"));
    m_thickness->setToolTip(QStringLiteral("Толщина (колесо мыши)"));
    m_layout->addWidget(m_thickness);

    addSeparator();
    m_undo = addButton(QStringLiteral("undo"), QStringLiteral("↶"), QStringLiteral("Отменить (Ctrl+Z)"));
    m_redo = addButton(QStringLiteral("redo"), QStringLiteral("↷"), QStringLiteral("Повторить (Ctrl+Shift+Z)"));
    connect(m_undo, &QToolButton::clicked, this, &Toolbar::undoRequested);
    connect(m_redo, &QToolButton::clicked, this, &Toolbar::redoRequested);

    addSeparator();
    connect(addButton(QStringLiteral("copy"), QStringLiteral("⧉"), QStringLiteral("Копировать в буфер (Ctrl+C)")),
            &QToolButton::clicked, this, &Toolbar::copyRequested);
    connect(addButton(QStringLiteral("quicksave"), QStringLiteral("↓"), QStringLiteral("Быстро сохранить (Ctrl+S)")),
            &QToolButton::clicked, this, &Toolbar::quickSaveRequested);
    connect(addButton(QStringLiteral("saveas"), QStringLiteral("…"), QStringLiteral("Сохранить как (Ctrl+Shift+S)")),
            &QToolButton::clicked, this, &Toolbar::saveAsRequested);
    connect(addButton(QStringLiteral("close"), QStringLiteral("✕"), QStringLiteral("Закрыть (Esc)")),
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
}

void Toolbar::setUndoRedoEnabled(bool canUndo, bool canRedo)
{
    m_undo->setEnabled(canUndo);
    m_redo->setEnabled(canRedo);
}

QToolButton* Toolbar::addButton(const QString& objectName, const QString& text, const QString& toolTip)
{
    auto* b = new QToolButton(this);
    b->setObjectName(objectName);
    b->setText(text);
    b->setToolTip(toolTip);
    b->setFocusPolicy(Qt::NoFocus);
    m_layout->addWidget(b);
    return b;
}

void Toolbar::addSeparator()
{
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::VLine);
    line->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 60);"));
    m_layout->addWidget(line);
}
