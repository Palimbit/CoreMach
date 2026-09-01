/*
 * BluMach collection browser.
 *
 * Author: rtzor
 * Copyright 2026 rtzor.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qt_blumach_collection.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
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

    void setProduct(const QString &productId, const QString &caption)
    {
        m_productId = productId;
        m_isBoard = productId == QStringLiteral("olivetti-pcs286s")
                 || productId == QStringLiteral("olivetti-pcs286s-16");
        QString imageId = productId;
        if (m_isBoard)
            imageId = QStringLiteral("olivetti-pcs286s-board");
        m_image.load(QStringLiteral(":/blumach/catalog/images/%1.jpg").arg(imageId));
        setAccessibleName(caption);
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
            const bool dario = m_productId.startsWith(QStringLiteral("ta-"));
            QColor accent = pal.color(QPalette::Highlight);
            accent.setAlpha(32);
            painter.setPen(Qt::NoPen);
            painter.setBrush(accent);
            painter.drawEllipse(card.center(), card.height() * 0.38, card.height() * 0.38);
            auto markFont = font();
            markFont.setBold(true);
            markFont.setPointSizeF(markFont.pointSizeF() + 12);
            painter.setFont(markFont);
            painter.setPen(pal.color(QPalette::Text));
            painter.drawText(card, Qt::AlignCenter, dario ? QStringLiteral("DARIO")
                                                          : QStringLiteral("PCS"));
        }
        painter.setClipping(false);

        auto labelFont = font();
        labelFont.setBold(true);
        labelFont.setPointSizeF(qMax(7.0, labelFont.pointSizeF() - 1.5));
        painter.setFont(labelFont);
        const QString label = QCoreApplication::translate("BluMachCollectionWidget",
                                                          m_isBoard ? "Board recreation"
                                                                    : "Concept illustration");
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
    QString m_productId;
    QPixmap m_image;
    bool    m_isBoard = false;
};

class CollectionItemDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const int type = index.data(TypeRole).toInt();
        return { option.rect.width(), type == 3 ? 58 : 30 };
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
            auto font = opt.font;
            font.setBold(true);
            if (type == 1)
                font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
            painter->setFont(font);
            painter->setPen(opt.palette.color(QPalette::PlaceholderText));
            painter->drawText(opt.rect.adjusted(8, 0, -8, 0),
                              Qt::AlignVCenter | Qt::AlignLeft, opt.text);
            painter->restore();
            return;
        }

        QRect row = opt.rect.adjusted(4, 2, -4, -2);
        if (opt.state & QStyle::State_Selected) {
            QColor selected = opt.palette.color(QPalette::Highlight);
            selected.setAlpha(44);
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected);
            const int selectionWidth = opt.widget ? opt.widget->width() - 8 : row.width();
            const QRect selectionRow(4, row.top(), selectionWidth, row.height());
            painter->drawRoundedRect(selectionRow, 7, 7);
        }
        QRect monitor(row.left() + 8, row.top() + 11, 30, 30);
        const bool dario = index.data(FamilyRole).toString().contains(QStringLiteral("dario"));
        QColor familyColor = dario ? opt.palette.color(QPalette::Link)
                                   : opt.palette.color(QPalette::Highlight);
        familyColor.setAlpha(34);
        painter->setPen(QPen(blendColor(opt.palette.color(QPalette::Base),
                                        opt.palette.color(QPalette::Text), 0.18), 1));
        painter->setBrush(familyColor);
        painter->drawRoundedRect(monitor, 7, 7);
        auto markFont = opt.font;
        markFont.setBold(true);
        markFont.setPointSizeF(qMax(7.0, markFont.pointSizeF() - (dario ? 1.0 : 2.0)));
        painter->setFont(markFont);
        painter->setPen(opt.palette.color(QPalette::Text));
        painter->drawText(monitor, Qt::AlignCenter, dario ? QStringLiteral("D")
                                                          : QStringLiteral("PCS"));

        const int textLeft = monitor.right() + 11;
        auto nameFont = opt.font;
        nameFont.setBold(true);
        painter->setFont(nameFont);
        painter->setPen(opt.palette.color(QPalette::Text));
        const int textWidth = qMax(24, row.right() - textLeft - 22);
        const QString displayName = painter->fontMetrics().elidedText(opt.text, Qt::ElideRight, textWidth);
        painter->drawText(QRect(textLeft, row.top() + 7, textWidth, 21),
                          Qt::AlignLeft | Qt::AlignVCenter, displayName);
        auto metaFont = opt.font;
        metaFont.setPointSizeF(qMax(7.0, metaFont.pointSizeF() - 1.0));
        painter->setFont(metaFont);
        painter->setPen(opt.palette.color(QPalette::PlaceholderText));
        const QString meta = QStringLiteral("%1 · %2").arg(index.data(PeriodRole).toString(),
                                                           index.data(ArchitectureRole).toString());
        const QString displayMeta = painter->fontMetrics().elidedText(meta, Qt::ElideRight, textWidth);
        painter->drawText(QRect(textLeft, row.top() + 28, textWidth, 18),
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
        painter->drawEllipse(QPointF(row.right() - 11, row.center().y()), 4, 4);
        painter->restore();
    }
};

QLabel *makeWrappedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
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

    auto *filterLayout = new QHBoxLayout();
    filterLayout->setSpacing(8);
    m_search = new QLineEdit(this);
    m_search->setClearButtonEnabled(true);
    m_statusFilter = new QComboBox(this);
    m_statusFilter->setMinimumWidth(190);
    m_resultsLabel = new QLabel(this);
    m_resultsLabel->setObjectName(QStringLiteral("blumachResultsLabel"));
    m_resultsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    filterLayout->addWidget(m_search, 1);
    filterLayout->addWidget(m_statusFilter);
    filterLayout->addWidget(m_resultsLabel);
    mainLayout->addLayout(filterLayout);

    m_splitter = new QSplitter(this);
    m_splitter->setChildrenCollapsible(false);
    m_tree = new QTreeWidget(m_splitter);
    m_tree->setHeaderHidden(true);
    m_tree->setMinimumWidth(270);
    m_tree->setMaximumWidth(430);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setIndentation(12);
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
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
    auto *badgeLayout = new QHBoxLayout();
    badgeLayout->setSpacing(6);
    for (auto **badge : { &m_statusBadge, &m_architectureBadge, &m_firmwareBadge }) {
        *badge = new QLabel(detailPanel);
        (*badge)->setObjectName(QStringLiteral("blumachBadge"));
        (*badge)->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        badgeLayout->addWidget(*badge);
    }
    badgeLayout->addStretch(1);
    productText->addLayout(badgeLayout);
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
    m_infoTabs->setDocumentMode(true);
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
    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current) { updateDetails(current); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    connect(m_statusFilter, &QComboBox::currentIndexChanged, this, [this] { applyFilter(); });
    reloadLanguage();
    updateAppearance();
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
    if (compact != m_compactLayout) {
        m_compactLayout = compact;
        m_tree->setMaximumWidth(compact ? 290 : 430);
        m_splitter->setSizes(compact ? QList<int>({ 285, 475 })
                                     : QList<int>({ 340, 660 }));
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
    m_infoTabs->setTabText(0, m_catalog.text(QStringLiteral("technical.ui.overview")));
    m_infoTabs->setTabText(1, m_catalog.text(QStringLiteral("technical.ui.research")));
    m_infoTabs->setTabText(2, m_catalog.text(QStringLiteral("technical.ui.sources")));

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

void BluMachCollectionWidget::rebuildTree()
{
    m_tree->clear();
    for (const auto &manufacturer : m_catalog.manufacturers()) {
        auto *manufacturerItem = new QTreeWidgetItem(m_tree, { manufacturer.name });
        manufacturerItem->setData(0, IdRole, manufacturer.id);
        manufacturerItem->setData(0, TypeRole, ManufacturerItem);
        manufacturerItem->setExpanded(true);
        for (const auto &family : m_catalog.families()) {
            if (family.manufacturerId != manufacturer.id || !family.parentFamilyId.isEmpty())
                continue;
            auto *familyItem = new QTreeWidgetItem(manufacturerItem, { family.name });
            familyItem->setData(0, IdRole, family.id);
            familyItem->setData(0, TypeRole, FamilyItem);
            familyItem->setExpanded(true);
            for (const auto &product : m_catalog.products()) {
                if (product.familyId != family.id)
                    continue;
                auto *productItem = new QTreeWidgetItem(familyItem, { product.name });
                productItem->setData(0, IdRole, product.id);
                productItem->setData(0, TypeRole, ProductItem);
                productItem->setData(0, PeriodRole, product.period);
                productItem->setData(0, StatusRole, product.status);
                productItem->setData(0, FamilyRole, product.familyId);
                if (const auto *platform = m_catalog.platform(product.platformId))
                    productItem->setData(0, ArchitectureRole, platform->architecture);
                else
                    productItem->setData(0, ArchitectureRole, tr("Unpreserved firmware"));
                productItem->setToolTip(0, QStringLiteral("%1\n%2")
                                               .arg(product.name, m_catalog.text(product.summaryKey)));
            }
        }
    }
    if (m_tree->topLevelItemCount())
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
}

void BluMachCollectionWidget::updateDetails(QTreeWidgetItem *item)
{
    m_selectedProductId.clear();
    m_statusBadge->hide();
    m_architectureBadge->hide();
    m_firmwareBadge->hide();
    m_warningFrame->hide();
    m_machineIllustration->hide();
    setDetailTabsAvailable(false, false);
    clearLayout(m_overviewLayout);
    clearLayout(m_researchLayout);
    clearLayout(m_sourcesLayout);
    if (!item) {
        m_title->setText(tr("No matching computers"));
        m_subtitle->setText(tr("Try changing the search text or preservation-state filter."));
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
            if (!manufacturer->historySourceUrl.isEmpty()) {
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
    m_title->setText(product->name);
    m_subtitle->setText(product->period);
    m_summary->setText(m_catalog.text(product->summaryKey));
    m_statusBadge->setText(m_catalog.statusText(product->status));
    m_statusBadge->show();
    if (const auto *platform = m_catalog.platform(product->platformId)) {
        m_architectureBadge->setText(platform->architecture);
        m_architectureBadge->show();
    }
    if (product->firmware.value(QStringLiteral("required")).toBool()) {
        const auto *platform = m_catalog.platform(product->platformId);
        const bool firmwarePreserved = platform && !platform->emulatorMachineId.isEmpty()
                                    && product->status != QStringLiteral("research")
                                    && product->status != QStringLiteral("not_bootable");
        m_firmwareBadge->setText(firmwarePreserved ? tr("Authentic firmware")
                                                   : tr("Unpreserved firmware"));
        m_firmwareBadge->show();
    }
    static_cast<MachineIllustration *>(m_machineIllustration)->setProduct(product->id, product->name);
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
    if (hasSources)
        populateTechnicalPage(*product, true);
    setDetailTabsAvailable(hasResearch, hasSources);
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

void BluMachCollectionWidget::setDetailTabsAvailable(const bool researchAvailable, const bool sourcesAvailable)
{
    if ((!researchAvailable && m_infoTabs->currentIndex() == 1)
        || (!sourcesAvailable && m_infoTabs->currentIndex() == 2))
        m_infoTabs->setCurrentIndex(0);
    m_infoTabs->setTabVisible(1, researchAvailable);
    m_infoTabs->setTabVisible(2, sourcesAvailable);
    m_infoTabs->tabBar()->setVisible(researchAvailable || sourcesAvailable);
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
    const QString needle = m_search->text().trimmed();
    const QString status = m_statusFilter->currentData().toString();
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
                    && (needle.isEmpty() || haystack.contains(needle, Qt::CaseInsensitive));
                productItem->setHidden(!visible);
                familyVisible |= visible;
                visibleProducts += visible ? 1 : 0;
            }
            familyItem->setHidden(!familyVisible);
            manufacturerVisible |= familyVisible;
        }
        manufacturerItem->setHidden(!manufacturerVisible);
    }
    m_resultsLabel->setText(visibleProducts == 1
                                ? tr("%1 computer").arg(visibleProducts)
                                : tr("%1 computers").arg(visibleProducts));
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
    const auto pal = palette();
    const QColor windowColor = pal.color(QPalette::Window);
    const QColor baseColor = pal.color(QPalette::Base);
    const QColor textColor = pal.color(QPalette::Text);
    const QColor accentColor = pal.color(QPalette::Highlight);
    const QColor linkColor = pal.color(QPalette::Link);
    const bool dark = baseColor.lightnessF() < 0.5;
    const QColor surfaceColor = blendColor(baseColor, textColor, dark ? 0.055 : 0.018);
    const QColor mutedColor = blendColor(baseColor, textColor, dark ? 0.68 : 0.58);
    const QColor borderColor = blendColor(baseColor, textColor, dark ? 0.28 : 0.16);
    const QColor badgeColor = blendColor(baseColor, accentColor, dark ? 0.26 : 0.11);
    const QColor badgeBorder = blendColor(baseColor, accentColor, dark ? 0.58 : 0.42);
    const QColor warningColor = blendColor(baseColor, linkColor, dark ? 0.20 : 0.075);
    setStyleSheet(QStringLiteral(
        "QWidget#blumachCollection { background: %1; color: %4; }"
        "QLabel#blumachCollectionIntro, QLabel#blumachResultsLabel, QLabel#blumachProductSubtitle, QLabel#blumachEvidence { color: %5; }"
        "QLabel#blumachProductSummary, QLabel#blumachWarningText { color: %4; }"
        "QLineEdit, QComboBox { color: %4; background: %2; border: 1px solid %6; border-radius: 6px; padding: 6px 8px; }"
        "QLabel#blumachBadge { color: %4; background: %7; border: 1px solid %8; border-radius: 9px; padding: 3px 9px; }"
        "QFrame#blumachWarning { color: %4; background: %9; border: 0; border-left: 3px solid %8; border-radius: 6px; }"
        "QFrame#blumachDetailSection { background: %3; border: 1px solid %6; border-radius: 7px; }"
        "QLabel#blumachSectionHeading { color: %4; font-weight: 600; }"
        "QLabel#blumachFieldName { color: %5; font-weight: 600; }"
        "QScrollArea { background: transparent; border: 0; }"
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
    update();
}
