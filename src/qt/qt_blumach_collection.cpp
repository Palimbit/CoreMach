/*
 * BluMach collection browser.
 *
 * Author: rtzor
 * Copyright 2026 rtzor.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qt_blumach_collection.hpp"
#include "qt_blumach_formfactoricon.hpp"
#include "qt_vmmanager_config.hpp"
#include "qt_util.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QUrl>

namespace {
constexpr auto IdRole           = Qt::UserRole;
constexpr auto TypeRole         = Qt::UserRole + 1;
constexpr auto PeriodRole       = Qt::UserRole + 2;
constexpr auto ArchitectureRole = Qt::UserRole + 3;
constexpr auto StatusRole       = Qt::UserRole + 4;
constexpr auto FamilyRole       = Qt::UserRole + 5;
constexpr auto FormFactorRole   = Qt::UserRole + 6;
constexpr auto CountRole        = Qt::UserRole + 7;
constexpr auto BrandMarkRole    = Qt::UserRole + 8;
constexpr auto BrandMarkBackgroundRole = Qt::UserRole + 9;

QColor
blendColor(const QColor &background, const QColor &foreground, const qreal amount)
{
    const qreal inverse = 1.0 - amount;
    return QColor::fromRgbF(background.redF() * inverse + foreground.redF() * amount,
                            background.greenF() * inverse + foreground.greenF() * amount,
                            background.blueF() * inverse + foreground.blueF() * amount);
}

QString
jsonValueText(const QJsonValue &value)
{
    if (value.isArray()) {
        QStringList values;
        for (const auto &item : value.toArray())
            values.append(item.toVariant().toString());
        return values.join(QStringLiteral(", "));
    }
    if (value.isBool())
        return value.toBool() ? QStringLiteral("Yes") : QStringLiteral("No");
    return value.toVariant().toString();
}

class MachineIllustration final : public QWidget {
public:
    explicit MachineIllustration(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(210, 118);
        setMaximumWidth(280);
        setFixedHeight(122);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    void setProduct(const QString &caption, const QString &formFactor,
                    const QString &genericLabel, const QJsonObject &media,
                    const QString &mediaLabel)
    {
        m_formFactor = formFactor;
        m_genericLabel = genericLabel;
        m_mediaLabel = mediaLabel;
        m_image.load(media.value(QStringLiteral("resource")).toString());
        if (m_image.isNull()) {
            m_mediaLabel = m_genericLabel;
        }
        setAccessibleName(QStringLiteral("%1 — %2").arg(caption, m_mediaLabel));
        setAccessibleDescription(m_mediaLabel);
        update();
    }

    QSize sizeHint() const override { return { 250, 122 }; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto pal = palette();
        const QRectF card(1, 1, width() - 2, height() - 2);
        painter.setPen(QPen(pal.color(QPalette::Mid), 1));
        painter.setBrush(blendColor(pal.color(QPalette::Base), pal.color(QPalette::Text), 0.035));
        painter.drawRoundedRect(card, 9, 9);

        painter.setClipPath([card] {
            QPainterPath path;
            path.addRoundedRect(card, 9, 9);
            return path;
        }());
        if (!m_image.isNull()) {
            const QPixmap scaled = m_image.scaled(size(), Qt::KeepAspectRatioByExpanding,
                                                  Qt::SmoothTransformation);
            painter.drawPixmap((width() - scaled.width()) / 2,
                               (height() - scaled.height()) / 2, scaled);
        } else {
            QColor accent = pal.color(QPalette::Highlight);
            accent.setAlpha(22);
            painter.setPen(Qt::NoPen);
            painter.setBrush(accent);
            painter.drawEllipse(card.center(), card.height() * 0.39, card.height() * 0.39);
            const int iconSide = qRound(card.height() * 0.66);
            const QRect iconRect(qRound(card.center().x() - iconSide / 2.0),
                                 qRound(card.center().y() - iconSide / 2.0),
                                 iconSide, iconSide);
            BluMachFormFactorIcon::paint(&painter, iconRect, m_formFactor, pal);
        }
        painter.setClipping(false);

        auto labelFont = font();
        labelFont.setBold(true);
        labelFont.setPointSizeF(qMax(7.0, labelFont.pointSizeF() - 1.5));
        painter.setFont(labelFont);
        const QString label = m_mediaLabel;
        const int labelWidth = painter.fontMetrics().horizontalAdvance(label) + 18;
        QRectF labelRect(10, 10, labelWidth, 24);
        painter.setPen(Qt::NoPen);
        QColor labelBackground = pal.color(QPalette::Base);
        labelBackground.setAlpha(224);
        painter.setBrush(labelBackground);
        painter.drawRoundedRect(labelRect, 12, 12);
        painter.setPen(pal.color(QPalette::Text));
        painter.drawText(labelRect, Qt::AlignCenter, label);
    }

private:
    QString m_formFactor;
    QString m_genericLabel;
    QString m_mediaLabel;
    QPixmap m_image;
};

class CollectionItemDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const int type = index.data(TypeRole).toInt();
        return { option.rect.width(), type == 3 ? 50 : 30 };
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const int type = index.data(TypeRole).toInt();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (type != 3) {
            const int count = index.data(CountRole).toInt();
            const bool manufacturer = type == 1;
            auto font = opt.font;
            font.setBold(true);
            if (manufacturer)
                font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
            painter->setFont(font);
            painter->setPen(manufacturer ? opt.palette.color(QPalette::Text)
                                         : opt.palette.color(QPalette::PlaceholderText));
            const QRect textRect = opt.rect.adjusted(8, 0, -40, 0);
            const QString brandMarkResource = index.data(BrandMarkRole).toString();
            const QColor brandMarkBackground(index.data(BrandMarkBackgroundRole).toString());
            QPixmap brandMark(brandMarkResource);
            if (manufacturer && !brandMark.isNull()) {
                const QSize available(qMin(104, textRect.width()), qMax(16, textRect.height() - 8));
                const qreal devicePixelRatio = painter->device()->devicePixelRatioF();
                QPixmap scaled = brandMark.scaled(available * devicePixelRatio,
                                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
                scaled.setDevicePixelRatio(devicePixelRatio);
                const QSize logicalSize(qRound(scaled.width() / devicePixelRatio),
                                        qRound(scaled.height() / devicePixelRatio));
                const QRect markRect(textRect.left(), textRect.center().y() - logicalSize.height() / 2,
                                     logicalSize.width(), logicalSize.height());
                if (brandMarkBackground.isValid()) {
                    const QRect backgroundRect = markRect.adjusted(-5, -3, 5, 3);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(brandMarkBackground);
                    painter->drawRoundedRect(backgroundRect, 3, 3);
                }
                painter->drawPixmap(markRect, scaled);
            } else {
                painter->drawText(textRect,
                                  Qt::AlignVCenter | Qt::AlignLeft, opt.text);
            }
            if (count > 0) {
                const QString countText = QString::number(count);
                const int badgeWidth = qMax(22, painter->fontMetrics().horizontalAdvance(countText) + 12);
                const QRect badge(opt.rect.right() - badgeWidth - 7, opt.rect.center().y() - 9,
                                  badgeWidth, 18);
                QColor badgeBackground = manufacturer ? opt.palette.color(QPalette::Highlight)
                                                      : opt.palette.color(QPalette::AlternateBase);
                badgeBackground.setAlpha(manufacturer ? 42 : 140);
                painter->setPen(Qt::NoPen);
                painter->setBrush(badgeBackground);
                painter->drawRoundedRect(badge, 9, 9);
                painter->setPen(opt.palette.color(QPalette::PlaceholderText));
                painter->drawText(badge, Qt::AlignCenter, countText);
            }
            painter->restore();
            return;
        }

        QRect row = opt.rect.adjusted(4, 1, -4, -1);
        if (opt.state & QStyle::State_Selected) {
            QColor selected = opt.palette.color(QPalette::Highlight);
            selected.setAlpha(44);
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected);
            const int selectionWidth = opt.widget ? opt.widget->width() - 8 : row.width();
            const QRect selectionRow(4, row.top(), selectionWidth, row.height());
            painter->drawRoundedRect(selectionRow, 7, 7);
        }
        QRect device(row.left() + 8, row.top() + 10, 28, 28);
        QColor deviceColor = opt.palette.color(QPalette::Highlight);
        deviceColor.setAlpha(24);
        painter->setPen(QPen(blendColor(opt.palette.color(QPalette::Base),
                                        opt.palette.color(QPalette::Text), 0.18), 1));
        painter->setBrush(deviceColor);
        painter->drawRoundedRect(device, 7, 7);
        BluMachFormFactorIcon::paint(painter, device, index.data(FormFactorRole).toString(), opt.palette);

        const int textLeft = device.right() + 11;
        auto nameFont = opt.font;
        nameFont.setBold(true);
        painter->setFont(nameFont);
        painter->setPen(opt.palette.color(QPalette::Text));
        const int textWidth = qMax(24, row.right() - textLeft - 22);
        const QString displayName = painter->fontMetrics().elidedText(opt.text, Qt::ElideRight, textWidth);
        painter->drawText(QRect(textLeft, row.top() + 4, textWidth, 20),
                          Qt::AlignLeft | Qt::AlignVCenter, displayName);
        auto metaFont = opt.font;
        metaFont.setPointSizeF(qMax(7.0, metaFont.pointSizeF() - 1.0));
        painter->setFont(metaFont);
        painter->setPen(opt.palette.color(QPalette::PlaceholderText));
        const QString meta = QStringLiteral("%1 · %2").arg(index.data(PeriodRole).toString(),
                                                           index.data(ArchitectureRole).toString());
        const QString displayMeta = painter->fontMetrics().elidedText(meta, Qt::ElideRight, textWidth);
        painter->drawText(QRect(textLeft, row.top() + 24, textWidth, 17),
                          Qt::AlignLeft | Qt::AlignVCenter, displayMeta);

        QColor statusColor = opt.palette.color(QPalette::Mid);
        const auto status = index.data(StatusRole).toString();
        if (status == QStringLiteral("validated"))
            statusColor = opt.palette.color(QPalette::Highlight);
        else if (status == QStringLiteral("partial") || status == QStringLiteral("experimental"))
            statusColor = opt.palette.color(QPalette::Link);
        else if (status == QStringLiteral("not_bootable"))
            statusColor = opt.palette.color(QPalette::Shadow);
        painter->setPen(Qt::NoPen);
        painter->setBrush(statusColor);
        painter->drawEllipse(QPointF(row.right() - 11, row.center().y()), 3.5, 3.5);
        painter->restore();
    }
};

QLabel *makeWrappedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("blumachBodyText"));
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QLabel *makeSectionHeading(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("blumachSectionHeading"));
    auto font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}
} // namespace

BluMachCollectionWidget::BluMachCollectionWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("blumachCollection"));
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 14, 20, 16);
    mainLayout->setSpacing(8);
    m_heading = new QLabel(this);
    m_heading->setObjectName(QStringLiteral("blumachCollectionHeading"));
    auto headingFont = m_heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 4);
    headingFont.setBold(true);
    m_heading->setFont(headingFont);
    mainLayout->addWidget(m_heading);
    m_intro = makeWrappedLabel(QString(), this);
    m_intro->setObjectName(QStringLiteral("blumachCollectionIntro"));
    mainLayout->addWidget(m_intro);

    m_filterLayout = new QGridLayout();
    m_filterLayout->setHorizontalSpacing(8);
    m_filterLayout->setVerticalSpacing(7);
    m_search = new QLineEdit(this);
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(tr("Search historical computers"));
    m_statusFilter = new QComboBox(this);
    m_statusFilter->setMinimumWidth(190);
    m_advancedFiltersButton = new QToolButton(this);
    m_advancedFiltersButton->setObjectName(QStringLiteral("blumachAdvancedFiltersButton"));
    m_advancedFiltersButton->setCheckable(true);
    m_advancedFiltersButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_advancedFiltersButton->setIcon(QIcon::fromTheme(
        QStringLiteral("view-filter"), style()->standardIcon(QStyle::SP_FileDialogDetailedView)));
    m_clearFiltersButton = new QToolButton(this);
    m_clearFiltersButton->setObjectName(QStringLiteral("blumachClearFiltersButton"));
    m_clearFiltersButton->setIcon(QIcon::fromTheme(
        QStringLiteral("edit-clear"), style()->standardIcon(QStyle::SP_DialogResetButton)));
    m_clearFiltersButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_clearFiltersButton->setVisible(false);
    m_advancedFiltersPanel = new QFrame(this);
    m_advancedFiltersPanel->setObjectName(QStringLiteral("blumachAdvancedFilters"));
    m_advancedFiltersPanel->setVisible(false);
    m_advancedFiltersLayout = new QGridLayout(m_advancedFiltersPanel);
    m_advancedFiltersLayout->setContentsMargins(10, 9, 10, 9);
    m_advancedFiltersLayout->setHorizontalSpacing(8);
    m_advancedFiltersLayout->setVerticalSpacing(7);
    m_resultsLabel = new QLabel(this);
    m_resultsLabel->setObjectName(QStringLiteral("blumachResultsLabel"));
    m_resultsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_resultsLabel->setAccessibleName(tr("Search results"));
    mainLayout->addLayout(m_filterLayout);

    m_splitter = new QSplitter(this);
    m_splitter->setChildrenCollapsible(false);
    m_tree = new QTreeWidget(m_splitter);
    m_tree->setHeaderHidden(true);
    m_tree->setMinimumWidth(270);
    m_tree->setMaximumWidth(430);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setIndentation(14);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setAccessibleName(tr("Historical computer catalogue"));
    m_tree->setItemDelegate(new CollectionItemDelegate(m_tree));

    auto *detailPanel = new QWidget(m_splitter);
    auto *detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(18, 4, 2, 0);
    detailLayout->setSpacing(10);
    auto *productHeader = new QHBoxLayout();
    productHeader->setSpacing(18);
    auto *productText = new QVBoxLayout();
    productText->setSpacing(5);
    m_title = new QLabel(detailPanel);
    m_title->setObjectName(QStringLiteral("blumachProductTitle"));
    m_title->setWordWrap(true);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto titleFont = m_title->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_subtitle = new QLabel(detailPanel);
    m_subtitle->setObjectName(QStringLiteral("blumachProductSubtitle"));
    m_subtitle->setWordWrap(true);
    m_subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_summary = makeWrappedLabel(QString(), detailPanel);
    m_summary->setObjectName(QStringLiteral("blumachProductSummary"));
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    productText->addWidget(m_title);
    productText->addWidget(m_subtitle);
    productText->addWidget(m_summary);
    m_badgeLayout = new QGridLayout();
    m_badgeLayout->setHorizontalSpacing(6);
    m_badgeLayout->setVerticalSpacing(5);
    int badgeColumn = 0;
    for (auto **badge : { &m_statusBadge, &m_architectureBadge }) {
        *badge = new QLabel(detailPanel);
        (*badge)->setObjectName(QStringLiteral("blumachBadge"));
        (*badge)->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        m_badgeLayout->addWidget(*badge, 0, badgeColumn++);
    }
    m_badgeLayout->setColumnStretch(2, 1);
    productText->addLayout(m_badgeLayout);
    productText->addStretch(1);
    productHeader->addLayout(productText, 1);
    m_machineIllustration = new MachineIllustration(detailPanel);
    productHeader->addWidget(m_machineIllustration, 0, Qt::AlignTop);
    detailLayout->addLayout(productHeader);

    m_warningFrame = new QFrame(detailPanel);
    m_warningFrame->setObjectName(QStringLiteral("blumachWarning"));
    auto *warningLayout = new QHBoxLayout(m_warningFrame);
    warningLayout->setContentsMargins(12, 9, 12, 9);
    m_warningLabel = makeWrappedLabel(QString(), m_warningFrame);
    m_warningLabel->setObjectName(QStringLiteral("blumachWarningText"));
    warningLayout->addWidget(m_warningLabel);
    detailLayout->addWidget(m_warningFrame);

    m_infoTabs = new QTabWidget(detailPanel);
    m_infoTabs->setAccessibleName(tr("Machine information"));
    m_infoTabs->setDocumentMode(true);
    m_infoTabs->setElideMode(Qt::ElideRight);
    m_infoTabs->tabBar()->setUsesScrollButtons(false);
    const auto createPage = [this](QScrollArea **scroll, QVBoxLayout **layout) {
        *scroll = new QScrollArea(m_infoTabs);
        (*scroll)->setWidgetResizable(true);
        (*scroll)->setFrameShape(QFrame::NoFrame);
        auto *container = new QWidget(*scroll);
        *layout = new QVBoxLayout(container);
        (*layout)->setContentsMargins(4, 14, 10, 10);
        (*layout)->setSpacing(12);
        (*scroll)->setWidget(container);
        return *scroll;
    };
    m_infoTabs->addTab(createPage(&m_overviewScroll, &m_overviewLayout), QString());
    m_infoTabs->addTab(createPage(&m_researchScroll, &m_researchLayout), QString());
    m_engineeringView = new QTextBrowser(m_infoTabs);
    m_engineeringView->setObjectName(QStringLiteral("blumachEngineering"));
    m_engineeringView->setFrameShape(QFrame::NoFrame);
    m_engineeringView->setOpenLinks(false);
    m_engineeringView->document()->setDocumentMargin(16.0);
    connect(m_engineeringView, &QTextBrowser::anchorClicked, this,
            [](const QUrl &url) { QDesktopServices::openUrl(url); });
    m_infoTabs->addTab(m_engineeringView, QString());
    m_infoTabs->addTab(createPage(&m_sourcesScroll, &m_sourcesLayout), QString());
    detailLayout->addWidget(m_infoTabs, 1);

    m_splitter->addWidget(m_tree);
    m_splitter->addWidget(detailPanel);
    m_splitter->setSizes({ 340, 660 });
    m_splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(m_splitter, 1);

    QString error;
    if (!m_catalog.load(&error)) {
        m_title->setText(tr("Catalog unavailable"));
        m_subtitle->setText(error);
    }
    m_skin.loadConfigured();
    loadUiState();
    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current) { updateDetails(current); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    connect(m_statusFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { applyFilter(); });
    connect(m_advancedFiltersButton, &QToolButton::toggled, m_advancedFiltersPanel,
            &QWidget::setVisible);
    connect(m_clearFiltersButton, &QToolButton::clicked, this, &BluMachCollectionWidget::clearFilters);
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this] { if (!m_filterRevealActive) saveUiState(); });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this] { if (!m_filterRevealActive) saveUiState(); });
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] { saveUiState(); });
    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, m_search, [this] {
        m_search->setFocus();
        m_search->selectAll();
    });
    auto *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, [this] {
        if (m_advancedFiltersButton->isChecked()) {
            m_advancedFiltersButton->setChecked(false);
            m_advancedFiltersButton->setFocus();
        } else if (!m_search->text().isEmpty()) {
            clearFilters();
        }
    });
    QWidget::setTabOrder(m_search, m_advancedFiltersButton);
    QWidget::setTabOrder(m_advancedFiltersButton, m_tree);
    QWidget::setTabOrder(m_tree, m_infoTabs);
    reloadLanguage();
    updateAppearance();
    // The catalogue is constructed before the main-window header connects to
    // selectionContextChanged. Repeat the initial context once the event loop
    // starts so the primary action always reflects the visible first product.
    QTimer::singleShot(0, this, [this] { updateDetails(m_tree->currentItem()); });
}

void BluMachCollectionWidget::createSelectedMachine()
{
    const auto *product = m_catalog.product(m_selectedProductId);
    if (!product || !canCreateProduct(*product))
        return;
    if (const auto *platform = m_catalog.platform(product->platformId))
        emit createMachineRequested(product->id, platform->emulatorMachineId);
}

void BluMachCollectionWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateResponsiveLayout();
}

void BluMachCollectionWidget::updateResponsiveLayout()
{
    const bool compact = width() < 900;
    const bool narrow = width() < 700;
    if (compact != m_compactLayout) {
        m_compactLayout = compact;
        if (auto *mainLayout = qobject_cast<QVBoxLayout *>(layout()))
            mainLayout->setContentsMargins(compact ? 12 : 20, compact ? 10 : 14,
                                           compact ? 12 : 20, compact ? 12 : 16);
        m_tree->setMinimumWidth(compact ? 210 : 270);
        m_tree->setMaximumWidth(compact ? 250 : 430);
        m_splitter->setSizes(compact ? QList<int>({ 225, 535 })
                                     : QList<int>({ 340, 660 }));

        updateAppearance();
    }
    if (narrow != m_narrowLayout) {
        m_narrowLayout = narrow;
        rebuildFilterLayout();

        for (auto *badge : { m_statusBadge, m_architectureBadge })
            m_badgeLayout->removeWidget(badge);
        m_badgeLayout->addWidget(m_statusBadge, 0, 0);
        m_badgeLayout->addWidget(m_architectureBadge, 0, 1);
    }
    m_machineIllustration->setVisible(!m_selectedProductId.isEmpty() && width() >= 900);
}

void BluMachCollectionWidget::openTechnicalLink(const QUrl &url)
{
    if (url.scheme() != QStringLiteral("blumach-doc")) {
        QDesktopServices::openUrl(url);
        return;
    }
    QString documentName = url.path();
    if (documentName.isEmpty())
        documentName = url.toString().section(':', 1);
    if (documentName.startsWith('/'))
        documentName.remove(0, 1);
    documentName = QUrl::fromPercentEncoding(documentName.toUtf8());
    if (documentName.isEmpty() || QFileInfo(documentName).fileName() != documentName || documentName.contains(QStringLiteral("..")))
        return;
    const QString documentPath = QCoreApplication::applicationDirPath()
                               + QStringLiteral("/catalog/documents/") + documentName;
    if (QFileInfo(documentPath).isFile())
        QDesktopServices::openUrl(QUrl::fromLocalFile(documentPath));
}

void BluMachCollectionWidget::reloadLanguage()
{
    m_catalog.reloadLocale();
    const QString selected = m_selectedProductId;
    const QString selectedStatus = m_statusFilter->currentData().toString();
    m_heading->setText(tr("Historical computer collection"));
    m_intro->setText(tr("Explore computers by manufacturer and family, review their preservation status, and create a historically accurate machine."));
    m_search->setPlaceholderText(tr("Search models, aliases or hardware"));
    m_clearFiltersButton->setText(m_catalog.text(QStringLiteral("filter.clear_all")));
    m_clearFiltersButton->setToolTip(m_catalog.text(QStringLiteral("filter.clear_all")));
    m_clearFiltersButton->setAccessibleName(m_catalog.text(QStringLiteral("filter.clear_all")));
    m_infoTabs->setTabText(0, m_catalog.text(QStringLiteral("technical.ui.overview")));
    m_infoTabs->setTabText(1, m_catalog.text(QStringLiteral("technical.ui.research")));
    m_infoTabs->setTabText(2, m_catalog.text(QStringLiteral("technical.ui.engineering")));
    m_infoTabs->setTabText(3, m_catalog.text(QStringLiteral("technical.ui.sources")));

    QSet<QString> availableStatuses;
    for (const auto &product : m_catalog.products())
        availableStatuses.insert(product.status);
    const QSignalBlocker blocker(m_statusFilter);
    m_statusFilter->clear();
    m_statusFilter->addItem(tr("All preservation states"), QString());
    for (const auto &status : { QStringLiteral("validated"), QStringLiteral("partial"), QStringLiteral("experimental"), QStringLiteral("research"), QStringLiteral("not_bootable") }) {
        if (availableStatuses.contains(status))
            m_statusFilter->addItem(m_catalog.statusText(status), status);
    }
    const int statusIndex = m_statusFilter->findData(selectedStatus);
    m_statusFilter->setCurrentIndex(statusIndex >= 0 ? statusIndex : 0);
    rebuildFacetFilters();
    rebuildFilterLayout();
    updateAdvancedFiltersButton();
    rebuildTree();
    applyFilter();
    if (!selected.isEmpty()) {
        for (auto iterator = QTreeWidgetItemIterator(m_tree); *iterator; ++iterator) {
            if ((*iterator)->data(0, IdRole).toString() == selected && !(*iterator)->isHidden()) {
                m_tree->setCurrentItem(*iterator);
                break;
            }
        }
    }
}

void
BluMachCollectionWidget::reloadSkin()
{
    m_skin.loadConfigured();
    const QString selected = m_selectedProductId;
    rebuildTree();
    applyFilter();
    if (!selected.isEmpty()) {
        for (auto iterator = QTreeWidgetItemIterator(m_tree); *iterator; ++iterator) {
            if ((*iterator)->data(0, IdRole).toString() == selected && !(*iterator)->isHidden()) {
                m_tree->setCurrentItem(*iterator);
                break;
            }
        }
    }
}

void BluMachCollectionWidget::rebuildFacetFilters()
{
    for (auto it = m_facetFilters.cbegin(); it != m_facetFilters.cend(); ++it) {
        m_facetSelections.insert(it.key(), it.value()->currentData().toString());
        delete it.value();
    }
    m_facetFilters.clear();

    for (const auto &facet : m_catalog.filterFacets()) {
        auto *filter = new QComboBox(this);
        const QString facetLabel = m_catalog.facetLabel(facet.id);
        filter->setMinimumWidth(140);
        filter->setPlaceholderText(facetLabel);
        filter->setToolTip(facetLabel);
        filter->setAccessibleName(facetLabel);
        filter->addItem(m_catalog.text(QStringLiteral("filter.any")), QString());
        for (const auto &value : facet.values)
            filter->addItem(m_catalog.facetValueText(facet.id, value.id), value.id);
        const QString selection = m_facetSelections.value(facet.id);
        filter->setCurrentIndex(selection.isEmpty() ? -1 : filter->findData(selection));
        connect(filter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { applyFilter(); });
        m_facetFilters.insert(facet.id, filter);
    }
}

void BluMachCollectionWidget::rebuildFilterLayout()
{
    while (m_filterLayout->count())
        delete m_filterLayout->takeAt(0);
    while (m_advancedFiltersLayout->count())
        delete m_advancedFiltersLayout->takeAt(0);

    const int filterColumns = m_narrowLayout ? 2 : 3;
    m_advancedFiltersLayout->addWidget(m_statusFilter, 0, 0);
    int advancedRow = 0;
    int advancedColumn = 1;
    for (const auto &facet : m_catalog.filterFacets()) {
        if (!m_facetFilters.contains(facet.id))
            continue;
        m_advancedFiltersLayout->addWidget(m_facetFilters.value(facet.id), advancedRow, advancedColumn++);
        if (advancedColumn == filterColumns) {
            advancedColumn = 0;
            ++advancedRow;
        }
    }
    m_advancedFiltersLayout->addWidget(m_clearFiltersButton, advancedRow,
                                       advancedColumn, 1,
                                       qMax(1, filterColumns - advancedColumn));
    for (int index = 0; index < filterColumns; ++index)
        m_advancedFiltersLayout->setColumnStretch(index, 1);

    if (m_narrowLayout) {
        m_filterLayout->addWidget(m_search, 0, 0, 1, 2);
        m_filterLayout->addWidget(m_advancedFiltersButton, 1, 0);
        m_filterLayout->addWidget(m_resultsLabel, 1, 1);
        m_filterLayout->addWidget(m_advancedFiltersPanel, 2, 0, 1, 2);
        m_filterLayout->setColumnStretch(0, 1);
        m_filterLayout->setColumnStretch(1, 1);
        return;
    }

    m_filterLayout->addWidget(m_search, 0, 0);
    m_filterLayout->addWidget(m_advancedFiltersButton, 0, 1);
    m_filterLayout->addWidget(m_resultsLabel, 0, 2);
    m_filterLayout->addWidget(m_advancedFiltersPanel, 1, 0, 1, 3);
    m_filterLayout->setColumnStretch(0, 1);
}

void BluMachCollectionWidget::updateAdvancedFiltersButton()
{
    int activeFilters = m_statusFilter->currentData().toString().isEmpty() ? 0 : 1;
    for (const auto *filter : m_facetFilters)
        activeFilters += !filter->currentData().toString().isEmpty();

    const QString label = m_catalog.text(QStringLiteral("filter.advanced"));
    m_advancedFiltersButton->setText(activeFilters == 0
                                         ? label
                                         : QStringLiteral("%1 (%2)").arg(label).arg(activeFilters));
    m_advancedFiltersButton->setToolTip(label);
    m_advancedFiltersButton->setAccessibleName(label);
    m_clearFiltersButton->setVisible(activeFilters > 0 || !m_search->text().trimmed().isEmpty());
}

void BluMachCollectionWidget::clearFilters()
{
    const QSignalBlocker searchBlocker(m_search);
    const QSignalBlocker statusBlocker(m_statusFilter);
    m_search->clear();
    m_statusFilter->setCurrentIndex(0);
    for (auto *filter : m_facetFilters) {
        const QSignalBlocker blocker(filter);
        filter->setCurrentIndex(-1);
    }
    applyFilter();
    m_search->setFocus();
}

QSet<QString> BluMachCollectionWidget::expandedTreeIds() const
{
    QSet<QString> ids;
    for (auto iterator = QTreeWidgetItemIterator(m_tree); *iterator; ++iterator) {
        if ((*iterator)->isExpanded() && (*iterator)->data(0, TypeRole).toInt() != ProductItem)
            ids.insert((*iterator)->data(0, IdRole).toString());
    }
    return ids;
}

void BluMachCollectionWidget::restoreTreeExpansion(const QSet<QString> &expandedIds)
{
    m_restoringUiState = true;
    for (auto iterator = QTreeWidgetItemIterator(m_tree); *iterator; ++iterator) {
        if ((*iterator)->data(0, TypeRole).toInt() != ProductItem)
            (*iterator)->setExpanded(expandedIds.contains((*iterator)->data(0, IdRole).toString()));
    }
    m_restoringUiState = false;
}

void BluMachCollectionWidget::loadUiState()
{
    const VMManagerConfig config(VMManagerConfig::ConfigType::General);
    m_selectedProductId = config.getStringValue(QStringLiteral("blumach_catalog_selected_product"));
    const QString expandedValue = config.getStringValue(QStringLiteral("blumach_catalog_expanded_nodes"));
    m_hasSavedTreeState = !expandedValue.isEmpty();
    const auto expanded = expandedValue.split('|', Qt::SkipEmptyParts);
    m_savedExpandedIds = QSet<QString>(expanded.cbegin(), expanded.cend());
    m_savedExpandedIds.remove(QStringLiteral("-"));
    bool widthOk = false;
    const int treeWidth = config.getStringValue(QStringLiteral("blumach_catalog_tree_width")).toInt(&widthOk);
    if (widthOk && treeWidth >= m_tree->minimumWidth() && treeWidth <= m_tree->maximumWidth())
        m_splitter->setSizes({ treeWidth, qMax(1, width() - treeWidth) });
}

void BluMachCollectionWidget::saveUiState()
{
    if (m_restoringUiState || m_rebuildingTree)
        return;
    const VMManagerConfig config(VMManagerConfig::ConfigType::General);
    config.setStringValue(QStringLiteral("blumach_catalog_selected_product"), m_selectedProductId);
    m_savedExpandedIds = expandedTreeIds();
    m_hasSavedTreeState = true;
    QStringList expanded(m_savedExpandedIds.cbegin(), m_savedExpandedIds.cend());
    expanded.sort();
    config.setStringValue(QStringLiteral("blumach_catalog_expanded_nodes"),
                          expanded.isEmpty() ? QStringLiteral("-") : expanded.join('|'));
    if (!m_splitter->sizes().isEmpty())
        config.setStringValue(QStringLiteral("blumach_catalog_tree_width"), QString::number(m_splitter->sizes().first()));
    config.sync();
}

void BluMachCollectionWidget::rebuildTree()
{
    m_rebuildingTree = true;
    m_tree->clear();
    int manufacturerIndex = 0;
    for (const auto &manufacturer : m_catalog.manufacturers()) {
        auto *manufacturerItem = new QTreeWidgetItem(m_tree, { manufacturer.name });
        manufacturerItem->setData(0, IdRole, manufacturer.id);
        manufacturerItem->setData(0, TypeRole, ManufacturerItem);
        const auto mark = m_skin.manufacturerMark(manufacturer.id);
        manufacturerItem->setData(0, BrandMarkRole, mark.imagePath);
        manufacturerItem->setData(0, BrandMarkBackgroundRole, mark.background);
        manufacturerItem->setExpanded(manufacturerIndex == 0);
        int manufacturerCount = 0;
        int familyIndex = 0;
        for (const auto &family : m_catalog.families()) {
            if (family.manufacturerId != manufacturer.id || !family.parentFamilyId.isEmpty())
                continue;
            auto *familyItem = new QTreeWidgetItem(manufacturerItem, { family.name });
            familyItem->setData(0, IdRole, family.id);
            familyItem->setData(0, TypeRole, FamilyItem);
            familyItem->setExpanded(manufacturerIndex == 0 && familyIndex == 0);
            int familyCount = 0;
            for (const auto &product : m_catalog.products()) {
                if (product.familyId != family.id)
                    continue;
                auto *productItem = new QTreeWidgetItem(familyItem, { product.name });
                productItem->setData(0, IdRole, product.id);
                productItem->setData(0, TypeRole, ProductItem);
                productItem->setData(0, PeriodRole, product.period);
                productItem->setData(0, StatusRole, product.status);
                productItem->setData(0, FamilyRole, product.familyId);
                const auto formFactors = product.facets.value(QStringLiteral("form_factor")).toArray();
                productItem->setData(0, FormFactorRole,
                                     formFactors.isEmpty() ? QString() : formFactors.at(0).toString());
                if (const auto *platform = m_catalog.platform(product.platformId))
                    productItem->setData(0, ArchitectureRole, platform->architecture);
                else
                    productItem->setData(0, ArchitectureRole, tr("Unpreserved firmware"));
                productItem->setToolTip(0, QStringLiteral("%1\n%2")
                                               .arg(product.name, m_catalog.text(product.summaryKey)));
                ++familyCount;
            }
            familyItem->setData(0, CountRole, familyCount);
            manufacturerCount += familyCount;
            ++familyIndex;
        }
        manufacturerItem->setData(0, CountRole, manufacturerCount);
        ++manufacturerIndex;
    }
    if (m_hasSavedTreeState)
        restoreTreeExpansion(m_savedExpandedIds);
    bool restoredSelection = false;
    for (auto iterator = QTreeWidgetItemIterator(m_tree); *iterator; ++iterator) {
        if ((*iterator)->data(0, TypeRole).toInt() == ProductItem) {
            if ((*iterator)->data(0, IdRole).toString() == m_selectedProductId) {
                m_tree->setCurrentItem(*iterator);
                restoredSelection = true;
                break;
            }
        }
    }
    if (!restoredSelection) {
        for (auto iterator = QTreeWidgetItemIterator(m_tree); *iterator; ++iterator) {
            if ((*iterator)->data(0, TypeRole).toInt() == ProductItem) {
                m_tree->setCurrentItem(*iterator);
                break;
            }
        }
    }
    m_rebuildingTree = false;
}

bool BluMachCollectionWidget::matchesFacetFilters(const BluMachProduct &product) const
{
    if (product.filterProfiles.isEmpty())
        return matchesFacetFilters(product.facets, {});
    for (const auto &profileValue : product.filterProfiles) {
        const auto profile = profileValue.toObject();
        if (matchesFacetFilters(product.facets, profile.value(QStringLiteral("facets")).toObject()))
            return true;
    }
    return false;
}

bool BluMachCollectionWidget::matchesFacetFilters(const QJsonObject &commonFacets,
                                                   const QJsonObject &profileFacets) const
{
    for (auto it = m_facetFilters.cbegin(); it != m_facetFilters.cend(); ++it) {
        const QString selectedValue = it.value()->currentData().toString();
        if (selectedValue.isEmpty())
            continue;
        bool found = false;
        for (const auto *facets : { &commonFacets, &profileFacets }) {
            for (const auto &value : facets->value(it.key()).toArray()) {
                if (value.toString() == selectedValue) {
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found)
            return false;
    }
    return true;
}

void BluMachCollectionWidget::updateDetails(QTreeWidgetItem *item)
{
    m_selectedProductId.clear();
    m_statusBadge->hide();
    m_architectureBadge->hide();
    m_warningFrame->hide();
    m_machineIllustration->hide();
    setDetailTabsAvailable(false, false, false);
    clearLayout(m_overviewLayout);
    clearLayout(m_researchLayout);
    clearLayout(m_sourcesLayout);
    m_engineeringView->clear();
    if (!item) {
        m_title->setText(m_catalog.text(QStringLiteral("filter.no_results")));
        m_subtitle->setText(m_catalog.text(QStringLiteral("filter.no_results_hint")));
        m_summary->clear();
        m_overviewLayout->addStretch(1);
        emit selectionContextChanged({}, {}, false);
        return;
    }

    const auto id = item->data(0, IdRole).toString();
    const auto type = item->data(0, TypeRole).toInt();
    if (type == ManufacturerItem) {
        if (const auto *manufacturer = m_catalog.manufacturer(id)) {
            m_title->setText(manufacturer->name);
            m_subtitle->setText(tr("Manufacturer"));
            m_summary->setText(m_catalog.text(manufacturer->descriptionKey));
            if (!manufacturer->historyKey.isEmpty()) {
                m_overviewLayout->addWidget(makeSectionHeading(tr("History"), m_overviewScroll));
                m_overviewLayout->addWidget(makeWrappedLabel(m_catalog.text(manufacturer->historyKey), m_overviewScroll));
            }
            if (!manufacturer->historyReferences.isEmpty()) {
                m_overviewLayout->addWidget(makeSectionHeading(m_catalog.text(QStringLiteral("manufacturer.history.references")), m_overviewScroll));
                for (const auto &reference : manufacturer->historyReferences) {
                    auto *label = makeWrappedLabel({}, m_overviewScroll);
                    label->setTextFormat(Qt::RichText);
                    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
                    label->setText(QStringLiteral("%1 — <a href=\"%2\">%3</a>")
                                       .arg(reference.publisher.toHtmlEscaped(), reference.url.toHtmlEscaped(), reference.title.toHtmlEscaped()));
                    connect(label, &QLabel::linkActivated, this, [this](const QString &url) {
                        openTechnicalLink(QUrl(url));
                    });
                    m_overviewLayout->addWidget(label);
                }
            } else if (!manufacturer->historySourceUrl.isEmpty()) {
                auto *source = new QToolButton(m_overviewScroll);
                source->setText(m_catalog.text(QStringLiteral("manufacturer.history.source")));
                source->setAutoRaise(true);
                connect(source, &QToolButton::clicked, this, [this, manufacturer] {
                    openTechnicalLink(QUrl(manufacturer->historySourceUrl));
                });
                m_overviewLayout->addWidget(source, 0, Qt::AlignLeft);
            }
        }
        m_overviewLayout->addStretch(1);
        emit selectionContextChanged({}, {}, false);
        return;
    }
    if (type == FamilyItem) {
        if (const auto *family = m_catalog.family(id)) {
            m_title->setText(family->name);
            m_subtitle->setText(tr("Computer family"));
            m_summary->setText(m_catalog.text(family->descriptionKey));
        }
        m_overviewLayout->addStretch(1);
        emit selectionContextChanged({}, {}, false);
        return;
    }

    const auto *product = m_catalog.product(id);
    if (!product) {
        emit selectionContextChanged({}, {}, false);
        return;
    }
    m_selectedProductId = product->id;
    saveUiState();
    m_title->setText(product->name);
    m_subtitle->setText(product->period);
    m_summary->setText(m_catalog.text(product->summaryKey));
    m_statusBadge->setText(m_catalog.statusText(product->status));
    m_statusBadge->show();
    if (const auto *platform = m_catalog.platform(product->platformId)) {
        m_architectureBadge->setText(platform->architecture);
        m_architectureBadge->show();
    }
    const auto formFactors = product->facets.value(QStringLiteral("form_factor")).toArray();
    const QString formFactor = formFactors.isEmpty() ? QStringLiteral("desktop")
                                                     : formFactors.at(0).toString();
    const QString genericLabel = m_catalog.text(QStringLiteral("media.kind.generic_form_factor"))
                                     .arg(m_catalog.facetValueText(QStringLiteral("form_factor"), formFactor));
    const QString mediaLabel = product->media.isEmpty()
                                 ? genericLabel
                                 : m_catalog.text(product->media.value(QStringLiteral("label_key")).toString());
    static_cast<MachineIllustration *>(m_machineIllustration)
        ->setProduct(product->name, formFactor, genericLabel, product->media, mediaLabel);
    updateResponsiveLayout();
    if (!product->warningKey.isEmpty()) {
        m_warningLabel->setText(m_catalog.text(product->warningKey));
        m_warningFrame->show();
    }
    populateOverview(*product);
    bool hasResearch = false;
    bool hasSources = false;
    for (const auto &sectionValue : product->technical) {
        if (isSourceSection(sectionValue.toObject()))
            hasSources = true;
        else
            hasResearch = true;
    }
    if (hasResearch)
        populateTechnicalPage(*product, false);
    const bool hasEngineering =
        !product->implementation.value(QStringLiteral("document")).toString().isEmpty();
    if (hasEngineering)
        populateEngineeringPage(*product);
    if (hasSources)
        populateTechnicalPage(*product, true);
    setDetailTabsAvailable(hasResearch, hasEngineering, hasSources);
    emit selectionContextChanged(product->id, product->name, canCreateProduct(*product));
}

void BluMachCollectionWidget::populateOverview(const BluMachProduct &product)
{
    clearLayout(m_overviewLayout);
    m_overviewLayout->addWidget(makeSectionHeading(tr("History"), m_overviewScroll));
    m_overviewLayout->addWidget(makeWrappedLabel(m_catalog.text(product.historyKey), m_overviewScroll));
    m_overviewLayout->addWidget(makeSectionHeading(tr("Hardware"), m_overviewScroll));
    auto *hardwareFrame = new QFrame(m_overviewScroll);
    hardwareFrame->setObjectName(QStringLiteral("blumachDetailSection"));
    auto *hardwareGrid = new QGridLayout(hardwareFrame);
    hardwareGrid->setContentsMargins(14, 12, 14, 12);
    hardwareGrid->setHorizontalSpacing(18);
    hardwareGrid->setVerticalSpacing(8);
    int row = 0;
    for (auto it = product.hardware.constBegin(); it != product.hardware.constEnd(); ++it) {
        auto *name = new QLabel(m_catalog.text(QStringLiteral("hardware.%1").arg(it.key())), hardwareFrame);
        name->setObjectName(QStringLiteral("blumachFieldName"));
        hardwareGrid->addWidget(name, row, 0, Qt::AlignTop);
        hardwareGrid->addWidget(makeWrappedLabel(jsonValueText(it.value()), hardwareFrame), row, 1);
        ++row;
    }
    hardwareGrid->setColumnStretch(1, 1);
    m_overviewLayout->addWidget(hardwareFrame);
    if (!product.facets.isEmpty()) {
        m_overviewLayout->addWidget(makeSectionHeading(m_catalog.text(QStringLiteral("filter.classification")), m_overviewScroll));
        auto *facetFrame = new QFrame(m_overviewScroll);
        facetFrame->setObjectName(QStringLiteral("blumachDetailSection"));
        auto *facetGrid = new QGridLayout(facetFrame);
        facetGrid->setContentsMargins(14, 12, 14, 12);
        facetGrid->setHorizontalSpacing(18);
        facetGrid->setVerticalSpacing(8);
        int row = 0;
        for (const auto &facet : m_catalog.filterFacets()) {
            const auto values = product.facets.value(facet.id).toArray();
            if (values.isEmpty())
                continue;
            QStringList labels;
            for (const auto &value : values)
                labels.append(m_catalog.facetValueText(facet.id, value.toString()));
            auto *name = new QLabel(m_catalog.facetLabel(facet.id), facetFrame);
            name->setObjectName(QStringLiteral("blumachFieldName"));
            facetGrid->addWidget(name, row, 0, Qt::AlignTop);
            facetGrid->addWidget(makeWrappedLabel(labels.join(QStringLiteral(", ")), facetFrame), row, 1);
            ++row;
        }
        facetGrid->setColumnStretch(1, 1);
        m_overviewLayout->addWidget(facetFrame);
    }
    m_overviewLayout->addStretch(1);
}

void BluMachCollectionWidget::populateTechnicalPage(const BluMachProduct &product, const bool sourcesPage)
{
    auto *targetLayout = sourcesPage ? m_sourcesLayout : m_researchLayout;
    auto *targetScroll = sourcesPage ? m_sourcesScroll : m_researchScroll;
    clearLayout(targetLayout);
    for (const auto &sectionValue : product.technical) {
        const auto section = sectionValue.toObject();
        if (isSourceSection(section) != sourcesPage)
            continue;
        auto *sectionFrame = new QFrame(targetScroll);
        sectionFrame->setObjectName(QStringLiteral("blumachDetailSection"));
        auto *sectionLayout = new QVBoxLayout(sectionFrame);
        sectionLayout->setContentsMargins(14, 12, 14, 12);
        sectionLayout->setSpacing(8);
        sectionLayout->addWidget(makeSectionHeading(m_catalog.text(section.value(QStringLiteral("title_key")).toString()), sectionFrame));
        const QString evidenceKey = section.value(QStringLiteral("evidence_key")).toString();
        if (!evidenceKey.isEmpty()) {
            auto *evidence = makeWrappedLabel(tr("Evidence: %1").arg(m_catalog.text(evidenceKey)), sectionFrame);
            evidence->setObjectName(QStringLiteral("blumachEvidence"));
            sectionLayout->addWidget(evidence);
        }
        const QString descriptionKey = section.value(QStringLiteral("description_key")).toString();
        if (!descriptionKey.isEmpty())
            sectionLayout->addWidget(makeWrappedLabel(m_catalog.text(descriptionKey), sectionFrame));
        auto *entries = new QGridLayout();
        entries->setHorizontalSpacing(18);
        entries->setVerticalSpacing(8);
        int row = 0;
        for (const auto &entryValue : section.value(QStringLiteral("entries")).toArray()) {
            const auto entry = entryValue.toObject();
            auto *name = new QLabel(m_catalog.text(entry.value(QStringLiteral("label_key")).toString()), sectionFrame);
            name->setObjectName(QStringLiteral("blumachFieldName"));
            entries->addWidget(name, row, 0, Qt::AlignTop);
            const QString value = m_catalog.text(entry.value(QStringLiteral("value_key")).toString());
            const QString url = entry.value(QStringLiteral("url")).toString();
            const QString document = entry.value(QStringLiteral("document")).toString();
            if (!url.isEmpty() || !document.isEmpty()) {
                auto *link = new QToolButton(sectionFrame);
                link->setText(value);
                link->setToolButtonStyle(Qt::ToolButtonTextOnly);
                link->setAutoRaise(true);
                link->setCursor(Qt::PointingHandCursor);
                const QUrl target = document.isEmpty()
                                      ? QUrl(url)
                                      : QUrl(QStringLiteral("blumach-doc:%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(document))));
                connect(link, &QToolButton::clicked, this, [this, target] { openTechnicalLink(target); });
                entries->addWidget(link, row, 1, Qt::AlignLeft | Qt::AlignTop);
            } else {
                entries->addWidget(makeWrappedLabel(value, sectionFrame), row, 1);
            }
            ++row;
        }
        entries->setColumnStretch(1, 1);
        sectionLayout->addLayout(entries);
        targetLayout->addWidget(sectionFrame);
    }
    targetLayout->addStretch(1);
}

void BluMachCollectionWidget::populateEngineeringPage(const BluMachProduct &product)
{
    const QString document = product.implementation.value(QStringLiteral("document")).toString();
    QFile file(QStringLiteral(":/blumach/catalog/documents/%1").arg(document));
    if (!file.open(QIODevice::ReadOnly)) {
        m_engineeringView->setPlainText(m_catalog.text(QStringLiteral("technical.ui.preparing")));
        return;
    }

    QString introduction = m_catalog.text(QStringLiteral("technical.ui.engineering_intro"));
    const QString language = product.implementation.value(QStringLiteral("language")).toString();
    if (!language.isEmpty() && language != m_catalog.localeCode())
        introduction += QStringLiteral("\n\n")
                      + m_catalog.text(QStringLiteral("technical.ui.engineering_language"));

    const QString markdown = QStringLiteral("> %1\n\n%2")
                                 .arg(introduction.replace(QStringLiteral("\n\n"), QStringLiteral("\n> \n> ")),
                                      QString::fromUtf8(file.readAll()));
    m_engineeringView->document()->setBaseUrl(
        QUrl(QStringLiteral("https://github.com/BluMach/86Box/blob/master/doc/machines/")));
    m_engineeringView->setMarkdown(markdown);
    m_engineeringView->moveCursor(QTextCursor::Start);
}

void BluMachCollectionWidget::setDetailTabsAvailable(const bool researchAvailable,
                                                     const bool engineeringAvailable,
                                                     const bool sourcesAvailable)
{
    if ((!researchAvailable && m_infoTabs->currentIndex() == 1)
        || (!engineeringAvailable && m_infoTabs->currentIndex() == 2)
        || (!sourcesAvailable && m_infoTabs->currentIndex() == 3))
        m_infoTabs->setCurrentIndex(0);
    m_infoTabs->setTabVisible(1, researchAvailable);
    m_infoTabs->setTabVisible(2, engineeringAvailable);
    m_infoTabs->setTabVisible(3, sourcesAvailable);
    m_infoTabs->tabBar()->setVisible(researchAvailable || engineeringAvailable || sourcesAvailable);
}

void BluMachCollectionWidget::clearLayout(QLayout *layout)
{
    while (auto *item = layout->takeAt(0)) {
        if (item->layout())
            clearLayout(item->layout());
        delete item->widget();
        delete item;
    }
}

bool BluMachCollectionWidget::isSourceSection(const QJsonObject &section) const
{
    const auto key = section.value(QStringLiteral("title_key")).toString();
    return key == QStringLiteral("technical.section.sources")
        || key == QStringLiteral("technical.section.research_material");
}

bool BluMachCollectionWidget::canCreateProduct(const BluMachProduct &product) const
{
    const auto *platform = m_catalog.platform(product.platformId);
    return product.status != QStringLiteral("research")
        && product.status != QStringLiteral("not_bootable")
        && platform != nullptr
        && !platform->emulatorMachineId.isEmpty();
}

void BluMachCollectionWidget::applyFilter()
{
    updateAdvancedFiltersButton();
    const QString needle = m_search->text().trimmed();
    const QString status = m_statusFilter->currentData().toString();
    bool hasAdvancedSelection = !status.isEmpty();
    for (const auto *filter : m_facetFilters)
        hasAdvancedSelection |= !filter->currentData().toString().isEmpty();
    const bool revealMatches = !needle.isEmpty() || hasAdvancedSelection;
    if (revealMatches && !m_filterRevealActive) {
        m_expandedBeforeFilter = expandedTreeIds();
        m_filterRevealActive = true;
    } else if (!revealMatches && m_filterRevealActive) {
        m_filterRevealActive = false;
        restoreTreeExpansion(m_expandedBeforeFilter);
        m_expandedBeforeFilter.clear();
    }
    int visibleProducts = 0;
    for (int mi = 0; mi < m_tree->topLevelItemCount(); ++mi) {
        auto *manufacturerItem = m_tree->topLevelItem(mi);
        bool manufacturerVisible = false;
        const auto *manufacturer = m_catalog.manufacturer(manufacturerItem->data(0, IdRole).toString());
        for (int fi = 0; fi < manufacturerItem->childCount(); ++fi) {
            auto *familyItem = manufacturerItem->child(fi);
            bool familyVisible = false;
            const auto *family = m_catalog.family(familyItem->data(0, IdRole).toString());
            for (int pi = 0; pi < familyItem->childCount(); ++pi) {
                auto *productItem = familyItem->child(pi);
                const auto *product = m_catalog.product(productItem->data(0, IdRole).toString());
                const auto *platform = product ? m_catalog.platform(product->platformId) : nullptr;
                const QString haystack = product
                    ? QStringList({ product->name, product->aliases.join(' '), product->tags.join(' '),
                                    product->period, m_catalog.text(product->summaryKey),
                                    manufacturer ? manufacturer->name : QString(),
                                    family ? family->name : QString(),
                                    platform ? platform->architecture : QString() }).join(' ')
                    : QString();
                const bool visible = product
                    && (status.isEmpty() || product->status == status)
                    && matchesFacetFilters(*product)
                    && (needle.isEmpty() || haystack.contains(needle, Qt::CaseInsensitive));
                productItem->setHidden(!visible);
                familyVisible |= visible;
                visibleProducts += visible ? 1 : 0;
            }
            familyItem->setHidden(!familyVisible);
            if (revealMatches && familyVisible)
                familyItem->setExpanded(true);
            manufacturerVisible |= familyVisible;
        }
        manufacturerItem->setHidden(!manufacturerVisible);
        if (revealMatches && manufacturerVisible)
            manufacturerItem->setExpanded(true);
    }
    m_resultsLabel->setText(visibleProducts == 0 && revealMatches
                                ? m_catalog.text(QStringLiteral("filter.no_results"))
                                : (visibleProducts == 1
                                       ? tr("%1 computer").arg(visibleProducts)
                                       : tr("%1 computers").arg(visibleProducts)));
    const auto itemIsVisible = [](QTreeWidgetItem *item) {
        for (auto *current = item; current; current = current->parent()) {
            if (current->isHidden())
                return false;
        }
        return true;
    };
    if (auto *current = m_tree->currentItem(); current && itemIsVisible(current))
        return;
    for (auto iterator = QTreeWidgetItemIterator(m_tree); *iterator; ++iterator) {
        auto *candidate = *iterator;
        if (candidate->data(0, TypeRole).toInt() == ProductItem && itemIsVisible(candidate)) {
            m_tree->setCurrentItem(candidate);
            m_tree->scrollToItem(candidate);
            return;
        }
    }
    if (m_tree->currentItem())
        m_tree->setCurrentItem(nullptr);
    else
        updateDetails(nullptr);
}

void BluMachCollectionWidget::updateAppearance()
{
    const auto pal = QApplication::palette();
    bool dark = pal.color(QPalette::Window).lightnessF() < 0.5;
#ifdef Q_OS_WINDOWS
    dark = !util::isWindowsLightTheme();
#endif
    const QColor windowColor = pal.color(QPalette::Window);
    const QColor baseColor = pal.color(QPalette::Base);
    const QColor textColor = pal.color(QPalette::Text);
    const QColor accentColor = pal.color(QPalette::Highlight);
    const QColor linkColor = pal.color(QPalette::Link);
    const QColor surfaceColor = blendColor(baseColor, textColor, dark ? 0.055 : 0.018);
    const QColor mutedColor = blendColor(baseColor, textColor, dark ? 0.68 : 0.58);
    const QColor borderColor = blendColor(baseColor, textColor, dark ? 0.28 : 0.16);
    const QColor badgeColor = blendColor(baseColor, accentColor, dark ? 0.26 : 0.11);
    const QColor badgeBorder = blendColor(baseColor, accentColor, dark ? 0.58 : 0.42);
    const QColor warningColor = blendColor(baseColor, linkColor, dark ? 0.20 : 0.075);
    auto contentPalette = pal;
    contentPalette.setColor(QPalette::Base, baseColor);
    contentPalette.setColor(QPalette::AlternateBase, surfaceColor);
    contentPalette.setColor(QPalette::Text, textColor);
    contentPalette.setColor(QPalette::WindowText, textColor);
    contentPalette.setColor(QPalette::PlaceholderText, mutedColor);
    m_tree->setPalette(contentPalette);
    setStyleSheet(QStringLiteral(
        "QWidget#blumachCollection { background: %1; color: %4; }"
        "QLabel#blumachCollectionHeading, QLabel#blumachProductTitle, QLabel#blumachBodyText { color: %4; background: transparent; }"
        "QLabel#blumachCollectionIntro, QLabel#blumachResultsLabel, QLabel#blumachProductSubtitle, QLabel#blumachEvidence { color: %5; background: transparent; }"
        "QLabel#blumachProductSummary, QLabel#blumachWarningText { color: %4; background: transparent; }"
        "QLineEdit, QComboBox { color: %4; background: %2; border: 1px solid %6; border-radius: 6px; padding: 6px 8px; }"
        "QFrame#blumachAdvancedFilters { background: %3; border: 1px solid %6; border-radius: 7px; }"
        "QToolButton#blumachAdvancedFiltersButton { color: %4; background: %3; border: 1px solid %6; border-radius: 6px; padding: 6px 10px; }"
        "QToolButton#blumachAdvancedFiltersButton:checked { background: %7; border-color: %8; }"
        "QLabel#blumachBadge { color: %4; background: %7; border: 1px solid %8; border-radius: 9px; padding: 3px 9px; }"
        "QWidget#blumachCollection[compact=\"true\"] QLabel#blumachBadge { padding: 2px 6px; }"
        "QFrame#blumachWarning { color: %4; background: %9; border: 0; border-left: 3px solid %8; border-radius: 6px; }"
        "QFrame#blumachDetailSection { background: %3; border: 1px solid %6; border-radius: 7px; }"
        "QLabel#blumachSectionHeading { color: %4; font-weight: 600; }"
        "QLabel#blumachFieldName { color: %5; font-weight: 600; }"
        "QScrollArea { background: transparent; border: 0; }"
        "QTextBrowser#blumachEngineering { color: %4; background: %2; border: 0; padding: 2px 8px; }"
        "QTabWidget::pane { background: %2; border: 0; border-top: 1px solid %6; }"
        "QTabBar { background: %2; }"
        "QTabBar::tab { background: transparent; color: %5; border: 0; padding: 9px 8px; }"
        "QTabBar::tab:selected { color: %4; border-bottom: 2px solid %8; font-weight: 600; }"
        "QTreeWidget { background: %2; border: 1px solid %6; border-radius: 7px; outline: 0; show-decoration-selected: 0; }"
        "QTreeWidget::item:selected, QTreeWidget::item:focus { background: transparent; border: 0; }"
        "QToolButton { color: %8; text-decoration: none; }")
        .arg(windowColor.name(), baseColor.name(), surfaceColor.name(), textColor.name(),
             mutedColor.name(), borderColor.name(), badgeColor.name(), badgeBorder.name(),
             warningColor.name()));
    setProperty("compact", m_compactLayout);
    style()->unpolish(this);
    style()->polish(this);
    update();
}
