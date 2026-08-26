/*
 * BluMach collection browser.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qt_blumach_collection.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace {
constexpr auto IdRole   = Qt::UserRole;
constexpr auto TypeRole = Qt::UserRole + 1;

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
} // namespace

BluMachCollectionWidget::BluMachCollectionWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    m_heading = new QLabel(this);
    QFont headingFont = m_heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 5);
    headingFont.setBold(true);
    m_heading->setFont(headingFont);
    mainLayout->addWidget(m_heading);

    m_intro = new QLabel(this);
    m_intro->setWordWrap(true);
    mainLayout->addWidget(m_intro);

    auto *filterLayout = new QHBoxLayout();
    m_search = new QLineEdit(this);
    m_statusFilter = new QComboBox(this);
    filterLayout->addWidget(m_search, 1);
    filterLayout->addWidget(m_statusFilter);
    mainLayout->addLayout(filterLayout);

    auto *splitter = new QSplitter(this);
    m_tree = new QTreeWidget(splitter);
    m_tree->setHeaderHidden(true);
    m_tree->setMinimumWidth(280);

    auto *detailPanel = new QWidget(splitter);
    auto *detailLayout = new QVBoxLayout(detailPanel);
    m_title = new QLabel(detailPanel);
    QFont titleFont = m_title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_subtitle = new QLabel(detailPanel);
    m_subtitle->setWordWrap(true);
    m_details = new QTextBrowser(detailPanel);
    m_details->setOpenExternalLinks(true);
    detailLayout->addWidget(m_title);
    detailLayout->addWidget(m_subtitle);
    detailLayout->addWidget(m_details, 1);

    auto *buttonLayout = new QHBoxLayout();
    m_machinesButton = new QPushButton(detailPanel);
    m_createButton   = new QPushButton(detailPanel);
    m_createButton->setEnabled(false);
    buttonLayout->addWidget(m_machinesButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(m_createButton);
    detailLayout->addLayout(buttonLayout);

    splitter->addWidget(m_tree);
    splitter->addWidget(detailPanel);
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter, 1);

    QString error;
    if (!m_catalog.load(&error)) {
        m_title->setText(tr("Catalog unavailable"));
        m_subtitle->setText(error);
    }

    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current) { updateDetails(current); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    connect(m_statusFilter, &QComboBox::currentIndexChanged, this, [this] { applyFilter(); });
    connect(m_createButton, &QPushButton::clicked, this, [this] {
        if (!m_selectedProductId.isEmpty())
            emit createMachineRequested(m_selectedProductId);
    });
    connect(m_machinesButton, &QPushButton::clicked, this, &BluMachCollectionWidget::showMachinesRequested);
    reloadLanguage();
}

void
BluMachCollectionWidget::reloadLanguage()
{
    m_catalog.reloadLocale();
    const QString selected = m_selectedProductId;
    m_heading->setText(tr("Historical computer collection"));
    m_intro->setText(tr("Explore computers by manufacturer and family, review their preservation status, and create a historically accurate machine."));
    m_search->setPlaceholderText(tr("Search models, aliases or hardware"));
    m_machinesButton->setText(tr("My machines"));
    m_createButton->setText(tr("Create this machine…"));
    m_statusFilter->clear();
    m_statusFilter->addItem(tr("All preservation states"), QString());
    for (const auto &status : { QStringLiteral("validated"), QStringLiteral("partial"), QStringLiteral("experimental"), QStringLiteral("research") })
        m_statusFilter->addItem(m_catalog.statusText(status), status);
    rebuildTree();
    if (!selected.isEmpty()) {
        for (auto iterator = QTreeWidgetItemIterator(m_tree); *iterator; ++iterator) {
            if ((*iterator)->data(0, IdRole).toString() == selected) {
                m_tree->setCurrentItem(*iterator);
                break;
            }
        }
    }
}

void
BluMachCollectionWidget::rebuildTree()
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
                auto *productItem = new QTreeWidgetItem(familyItem, { product.name, product.id });
                productItem->setData(0, IdRole, product.id);
                productItem->setData(0, TypeRole, ProductItem);
                productItem->setToolTip(0, m_catalog.text(product.summaryKey));
            }
        }
    }
    if (m_tree->topLevelItemCount())
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
}

void
BluMachCollectionWidget::updateDetails(QTreeWidgetItem *item)
{
    m_selectedProductId.clear();
    m_createButton->setEnabled(false);
    if (!item)
        return;

    const auto id   = item->data(0, IdRole).toString();
    const auto type = item->data(0, TypeRole).toInt();
    if (type == ManufacturerItem) {
        if (const auto *manufacturer = m_catalog.manufacturer(id)) {
            m_title->setText(manufacturer->name);
            m_subtitle->setText(tr("Manufacturer"));
            m_details->setHtml(QStringLiteral("<p>%1</p>").arg(m_catalog.text(manufacturer->descriptionKey).toHtmlEscaped()));
        }
    } else if (type == FamilyItem) {
        if (const auto *family = m_catalog.family(id)) {
            m_title->setText(family->name);
            m_subtitle->setText(tr("Computer family"));
            m_details->setHtml(QStringLiteral("<p>%1</p>").arg(m_catalog.text(family->descriptionKey).toHtmlEscaped()));
        }
    } else if (const auto *product = m_catalog.product(id)) {
        m_selectedProductId = product->id;
        m_createButton->setEnabled(product->status != QStringLiteral("research"));
        m_title->setText(product->name);
        m_subtitle->setText(tr("%1 · %2 · %3").arg(product->period, m_catalog.statusText(product->status), m_catalog.text(product->summaryKey)));
        const QString html = QStringLiteral("<h3>%1</h3><p>%2</p><h3>%3</h3>%4")
                                 .arg(tr("History"), m_catalog.text(product->historyKey).toHtmlEscaped(), tr("Hardware"), hardwareHtml(*product));
        m_details->setHtml(html);
    }
}

QString
BluMachCollectionWidget::hardwareHtml(const BluMachProduct &product) const
{
    QString html = QStringLiteral("<table cellspacing='6'>");
    for (auto it = product.hardware.constBegin(); it != product.hardware.constEnd(); ++it) {
        html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>")
                    .arg(m_catalog.text(QStringLiteral("hardware.%1").arg(it.key())).toHtmlEscaped(), jsonValueText(it.value()).toHtmlEscaped());
    }
    html += QStringLiteral("</table>");
    return html;
}

void
BluMachCollectionWidget::applyFilter()
{
    const QString needle = m_search->text().trimmed();
    const QString status = m_statusFilter->currentData().toString();
    for (int mi = 0; mi < m_tree->topLevelItemCount(); ++mi) {
        auto *manufacturerItem = m_tree->topLevelItem(mi);
        bool  manufacturerVisible = false;
        for (int fi = 0; fi < manufacturerItem->childCount(); ++fi) {
            auto *familyItem = manufacturerItem->child(fi);
            bool  familyVisible = false;
            for (int pi = 0; pi < familyItem->childCount(); ++pi) {
                auto *productItem = familyItem->child(pi);
                const auto *product = m_catalog.product(productItem->data(0, IdRole).toString());
                const QString haystack = product ? QStringList({ product->name, product->aliases.join(' '), product->tags.join(' '), m_catalog.text(product->summaryKey) }).join(' ') : QString();
                const bool visible = product && (status.isEmpty() || product->status == status) && (needle.isEmpty() || haystack.contains(needle, Qt::CaseInsensitive));
                productItem->setHidden(!visible);
                familyVisible |= visible;
            }
            familyItem->setHidden(!familyVisible);
            manufacturerVisible |= familyVisible;
        }
        manufacturerItem->setHidden(!manufacturerVisible);
    }
}
