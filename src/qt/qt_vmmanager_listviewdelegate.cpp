/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          86Box VM manager list view delegate module
 *
 * Authors: cold-brewed
 *          rtzor
 *
 *          Copyright 2024 cold-brewed
 *          Copyright 2026 rtzor
 */
#include <QApplication>

#include "qt_util.hpp"
#include "qt_vmmanager_listviewdelegate.hpp"
#include "qt_vmmanager_model.hpp"

namespace {
bool
isLegacyManagerIcon(const QString &iconPath)
{
    const QString normalized = iconPath.trimmed().toLower();
    return normalized == QStringLiteral(":/settings/qt/icons/86box-gray.ico")
        || normalized == QStringLiteral(":/settings/qt/icons/86box-green.ico")
        || normalized == QStringLiteral(":/settings/qt/icons/86box-red.ico")
        || normalized == QStringLiteral(":/settings/qt/icons/86box-yellow.ico");
}
} // namespace

// Thanks to scopchanov https://github.com/scopchanov/SO-MessageLog
// from https://stackoverflow.com/questions/53105343/is-it-possible-to-add-a-custom-widget-into-a-qlistview

VMManagerListViewDelegate::VMManagerListViewDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
    , m_ptr(new VMManagerListViewDelegateStyle)
{
}

VMManagerListViewDelegate::~VMManagerListViewDelegate()
    = default;

void
VMManagerListViewDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    const QPalette &palette(opt.palette);
    // opt.rect = opt.rect.adjusted(0, 0, 0, 20);
    const QRect &rect(opt.rect);
    const QRect &contentRect(rect.adjusted(m_ptr->margins.left(),
                                           m_ptr->margins.top(),
                                           -m_ptr->margins.right(),
                                           -m_ptr->margins.bottom()));

    // The status icon represents the current state of the vm. Initially set to a default state.
    auto process_variant = index.data(VMManagerModel::Roles::ProcessStatus);
    auto process_status  = process_variant.value<VMManagerSystem::ProcessStatus>();
    // The main icon, configurable. Falls back to default if it cannot be loaded.
    auto customIcon = index.data(VMManagerModel::Roles::Icon).toString();
    opt.icon        = {};
    if (!customIcon.isEmpty() && !isLegacyManagerIcon(customIcon)) {
        const auto customPixmap = QPixmap(customIcon);
        if (!customPixmap.isNull())
            opt.icon = customPixmap;
    }

    const bool hasIcon    = !opt.icon.isNull();
    QFont      f(opt.font);

    f.setPointSizeF(m_ptr->statusFontPointSize(opt.font));

    painter->save();
    painter->setClipping(true);
    painter->setClipRect(rect);
    painter->setFont(opt.font);

    painter->fillRect(rect, palette.base());
    if (opt.state & QStyle::State_Selected) {
        QColor selected = palette.highlight().color();
        selected.setAlpha(palette.base().color().lightnessF() < 0.5 ? 86 : 42);
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected);
        painter->drawRoundedRect(rect.adjusted(2, 2, -2, -2), 7, 7);
        painter->setBrush(palette.highlight());
        painter->drawRoundedRect(QRect(rect.left() + 2, rect.top() + 9, 3, rect.height() - 18), 2, 2);
    }

    // Draw a user-supplied icon when present. Otherwise use BluMach's provisional
    // Linea mark so the application brand is not repeated for every VM and the
    // legacy pixel-art 86Box icon never becomes the fallback. Replace this once
    // the definitive per-family machine icon system is available.
    if (hasIcon) {
        painter->drawPixmap(contentRect.left(), contentRect.top(),
                            opt.icon.pixmap(m_ptr->iconSize));
    } else {
        static const QIcon defaultMachineIcon(QStringLiteral(":/blumach/ui/machine-default.png"));
        painter->drawPixmap(contentRect.left(), contentRect.top(),
                            defaultMachineIcon.pixmap(m_ptr->iconSize));
    }

    // System name
    QRect systemNameRect(m_ptr->systemNameBox(opt, index));

    systemNameRect.moveTo(m_ptr->margins.left() + m_ptr->iconSize.width()
                              + m_ptr->spacingHorizontal,
                          contentRect.top());
    // If desired, font can be changed here
    //    painter->setFont(f);
    auto nameFont = opt.font;
    nameFont.setBold(true);
    painter->setFont(nameFont);
    painter->setPen(palette.text().color());
    const int availableWidth = qMax(30, rect.right() - systemNameRect.left() - m_ptr->margins.right());
    systemNameRect.setWidth(availableWidth);
    painter->drawText(systemNameRect, Qt::TextSingleLine,
                      painter->fontMetrics().elidedText(opt.text, Qt::ElideRight, availableWidth));

    // Draw status icon
    QColor statusColor = palette.mid().color();
    if (process_status == VMManagerSystem::ProcessStatus::Running
        || process_status == VMManagerSystem::ProcessStatus::RunningWaiting)
        statusColor = palette.highlight().color();
    else if (process_status == VMManagerSystem::ProcessStatus::Paused
             || process_status == VMManagerSystem::ProcessStatus::PausedWaiting)
        statusColor = palette.link().color();
    painter->setPen(Qt::NoPen);
    painter->setBrush(statusColor);
    const QPoint statusCenter(systemNameRect.left() + 5,
                              systemNameRect.bottom() + m_ptr->spacingVertical + 7);
    painter->drawEllipse(statusCenter, 4, 4);

    // This rectangle is around the status icon
    // auto point = QPoint(systemNameRect.left(), systemNameRect.bottom()
    //                         + m_ptr->spacingVertical);
    // auto point2 = QPoint(point.x() + m_ptr->smallIconSize.width(), point.y() + m_ptr->smallIconSize.height());
    // auto arect = QRect(point, point2);
    // painter->drawRect(arect);

    // Draw status text
    QRect statusRect(m_ptr->statusBox(opt, index));
    int   extraaa = 2;
    statusRect.moveTo(systemNameRect.left() + 16,
                      systemNameRect.bottom() + m_ptr->spacingVertical + extraaa);

    //    painter->setFont(opt.font);
    painter->setFont(f);
    QColor statusTextColor = palette.text().color();
    statusTextColor.setAlphaF(0.68);
    painter->setPen(statusTextColor);
    painter->drawText(statusRect, Qt::TextSingleLine,
                      index.data(VMManagerModel::Roles::ProcessStatusString).toString());

    painter->restore();
}

QMargins
VMManagerListViewDelegate::contentsMargins() const
{
    return m_ptr->margins;
}

void
VMManagerListViewDelegate::setContentsMargins(const int left, const int top, const int right, const int bottom) const
{
    m_ptr->margins = QMargins(left, top, right, bottom);
}

int
VMManagerListViewDelegate::horizontalSpacing() const
{
    return m_ptr->spacingHorizontal;
}

void
VMManagerListViewDelegate::setHorizontalSpacing(const int spacing) const
{
    m_ptr->spacingHorizontal = spacing;
}

int
VMManagerListViewDelegate::verticalSpacing() const
{
    return m_ptr->spacingVertical;
}

void
VMManagerListViewDelegate::setVerticalSpacing(const int spacing) const
{
    m_ptr->spacingVertical = spacing;
}

QSize
VMManagerListViewDelegate::sizeHint(const QStyleOptionViewItem &option,
                                    const QModelIndex          &index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    const int textHeight = m_ptr->systemNameBox(opt, index).height()
        + m_ptr->spacingVertical + m_ptr->statusBox(opt, index).height();
    const int iconHeight = m_ptr->iconSize.height();
    const int h          = textHeight > iconHeight ? textHeight : iconHeight;

    // return the same width
    // for height, add margins on top and bottom *plus* either the text or icon height, whichever is greater
    // Note: text height is the combined value of the system name and the status just below the name
    return { opt.rect.width(), m_ptr->margins.top() + h + m_ptr->margins.bottom() };
}

VMManagerListViewDelegateStyle::VMManagerListViewDelegateStyle()
    : iconSize(30, 30)
    ,
    // bottom gets a little more than the top because of the custom separator
    margins(4, 6, 8, 7)
    ,
    // Spacing between icon and text
    spacingHorizontal(8)
    , spacingVertical(2)
{
    //
}

QRect
VMManagerListViewDelegateStyle::statusBox(const QStyleOptionViewItem &option,
                                          const QModelIndex          &index) const
{
    QFont f(option.font);

    f.setPointSizeF(statusFontPointSize(option.font));

    return QFontMetrics(f).boundingRect(index.data(VMManagerModel::Roles::ProcessStatusString).toString()).adjusted(0, 0, 1, 1);
}

qreal
VMManagerListViewDelegateStyle::statusFontPointSize(const QFont &f) const
{
    return 0.9 * f.pointSize();
    //    return 1*f.pointSize();
}

QRect
VMManagerListViewDelegateStyle::systemNameBox(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    return option.fontMetrics.boundingRect(option.text).adjusted(0, 0, 1, 1);
}
