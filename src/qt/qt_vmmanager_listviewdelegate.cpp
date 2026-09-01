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

// Thanks to scopchanov https://github.com/scopchanov/SO-MessageLog
// from https://stackoverflow.com/questions/53105343/is-it-possible-to-add-a-custom-widget-into-a-qlistview

VMManagerListViewDelegate::VMManagerListViewDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
    , m_ptr(new VMManagerListViewDelegateStyle)
{
    default_icon = QIcon(":/settings/qt/icons/86Box-gray.ico");
    stop_icon    = QApplication::style()->standardIcon(QStyle::SP_MediaStop);
    running_icon = QIcon(":/menuicons/qt/icons/run.ico");
    stopped_icon = QIcon(":/menuicons/qt/icons/acpi_shutdown.ico");
    paused_icon  = QIcon(":/menuicons/qt/icons/pause.ico");
    unknown_icon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxQuestion);

    highlight_color = QColor("#616161");
    bg_color        = QColor("#272727");
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
    if (!customIcon.isEmpty()) {
        const auto customPixmap = QPixmap(customIcon);
        if (!customPixmap.isNull())
            opt.icon = customPixmap;
    }

    // Set the status icon based on the process status
    QIcon status_icon;
    switch (process_status) {
        case VMManagerSystem::ProcessStatus::Running:
        case VMManagerSystem::ProcessStatus::RunningWaiting:
            status_icon = running_icon;
            break;
        case VMManagerSystem::ProcessStatus::Stopped:
            status_icon = stopped_icon;
            break;
        case VMManagerSystem::ProcessStatus::PausedWaiting:
        case VMManagerSystem::ProcessStatus::Paused:
            status_icon = paused_icon;
            break;
        default:
            status_icon = unknown_icon;
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

    // Draw a user-supplied icon when present. Otherwise use a compact family
    // badge so the list does not fall back to the legacy pixel-art 86Box mark.
    if (hasIcon) {
        painter->drawPixmap(contentRect.left(), contentRect.top(),
                            opt.icon.pixmap(m_ptr->iconSize));
    } else {
        const QString identity = QStringList({ opt.text,
                                               index.data(VMManagerModel::Roles::ConfigName).toString(),
                                               index.data(VMManagerModel::Roles::SearchList).toStringList().join(' ') })
                                     .join(' ')
                                     .toLower();
        const bool dario = identity.contains(QStringLiteral("dario"));
        const bool pcs = identity.contains(QStringLiteral("olivetti"))
                      || identity.contains(QStringLiteral("pcs"));
        const QString mark = dario ? QStringLiteral("D")
                                   : (pcs ? QStringLiteral("PCS") : QStringLiteral("PC"));
        QRect badgeRect(QPoint(contentRect.left(), contentRect.top()), m_ptr->iconSize);
        badgeRect.adjust(2, 2, -2, -2);
        QColor badgeColor = dario ? palette.link().color() : palette.highlight().color();
        badgeColor.setAlpha(palette.base().color().lightnessF() < 0.5 ? 60 : 30);
        painter->setPen(QPen(palette.mid().color(), 1));
        painter->setBrush(badgeColor);
        painter->drawRoundedRect(badgeRect, 8, 8);
        auto badgeFont = opt.font;
        badgeFont.setBold(true);
        badgeFont.setPointSizeF(qMax(7.0, badgeFont.pointSizeF() - (mark.size() > 2 ? 2.0 : 1.0)));
        painter->setFont(badgeFont);
        painter->setPen(palette.text().color());
        painter->drawText(badgeRect, Qt::AlignCenter, mark);
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
    : iconSize(32, 32)
    , smallIconSize(16, 16)
    ,
    // bottom gets a little more than the top because of the custom separator
    margins(4, 10, 8, 12)
    ,
    // Spacing between icon and text
    spacingHorizontal(8)
    , spacingVertical(4)
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
