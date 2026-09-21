/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "launcher_page.h"

#include <blumach/engine/version.h>
#include <blumach/frontend/frontend.h>

#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace {
QString paragraph(const QString &value)
{
    return value.isEmpty() ? QString() :
        QStringLiteral("<p>%1</p>").arg(value.toHtmlEscaped());
}

QString safeLink(const QString &url, const QString &label)
{
    const QUrl target(url);
    if (!target.isValid() || target.scheme() != QStringLiteral("https"))
        return label.toHtmlEscaped();
    return QStringLiteral("<a href=\"%1\">%2</a>")
        .arg(target.toString(QUrl::FullyEncoded).toHtmlEscaped(),
             label.toHtmlEscaped());
}

bool canLaunch(const PortableCatalogMachine &machine)
{
    if (machine.adapterId.isEmpty())
        return false;
    const QByteArray id = machine.adapterId.toUtf8();
    return bm_frontend_adapter_find(id.constData()) != nullptr;
}
}

LauncherPage::LauncherPage(const PortableCatalog &catalog,
                           const QString &catalogError,
                           std::function<void(const QString &)> launch,
                           std::function<void(const QString &)> openSaved,
                           QWidget *parent)
    : QWidget(parent), catalog_(catalog), launch_(std::move(launch)),
      openSaved_(std::move(openSaved)), saved_(new QListWidget),
      openSavedButton_(new QPushButton(tr("Abrir máquina guardada"))),
      search_(new QLineEdit), tree_(new QTreeWidget), image_(new QLabel),
      title_(new QLabel), availability_(new QLabel),
      details_(new QTextBrowser), technical_(new QTextBrowser),
      context_(new QTextBrowser), sources_(new QTextBrowser),
      launchButton_(new QPushButton(tr("Configurar y arrancar")))
{
    auto *root = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("BluMach Portable %1")
                                   .arg(QString::fromLatin1(BM_ENGINE_VERSION)));
    QFont headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 6);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    root->addWidget(heading);
    root->addWidget(new QLabel(tr("Explora el catálogo histórico. Solo las máquinas "
                                  "marcadas como disponibles arrancan en el motor portable.")));

    auto *splitter = new QSplitter(Qt::Horizontal);
    auto *navigation = new QWidget;
    auto *navigationLayout = new QVBoxLayout(navigation);
    navigationLayout->addWidget(new QLabel(tr("Mis máquinas")));
    saved_->setObjectName(QStringLiteral("saved-machines"));
    saved_->setMaximumHeight(125);
    navigationLayout->addWidget(saved_);
    openSavedButton_->setObjectName(QStringLiteral("open-saved-machine"));
    openSavedButton_->setEnabled(false);
    navigationLayout->addWidget(openSavedButton_);
    navigationLayout->addWidget(new QLabel(tr("Catálogo histórico")));
    search_->setObjectName(QStringLiteral("catalog-search"));
    search_->setPlaceholderText(tr("Buscar fabricante, familia o máquina…"));
    navigationLayout->addWidget(search_);
    tree_->setObjectName(QStringLiteral("catalog-tree"));
    tree_->setHeaderLabels({ tr("Catálogo"), tr("Motor portable") });
    tree_->setUniformRowHeights(true);
    navigationLayout->addWidget(tree_, 1);
    int availableCount = 0;
    for (const auto &machine : catalog_.machines())
        availableCount += canLaunch(machine) ? 1 : 0;
    auto *counts = new QLabel(tr("%1 fichas · %2 emulables ahora")
                                  .arg(catalog_.machines().size()).arg(availableCount));
    counts->setObjectName(QStringLiteral("catalog-counts"));
    navigationLayout->addWidget(counts);
    if (!catalogError.isEmpty()) {
        auto *error = new QLabel(tr("Catálogo no disponible: %1").arg(catalogError));
        error->setWordWrap(true);
        navigationLayout->addWidget(error);
    }
    splitter->addWidget(navigation);

    auto *detailPane = new QWidget;
    auto *detailLayout = new QVBoxLayout(detailPane);
    image_->setObjectName(QStringLiteral("machine-image"));
    image_->setAlignment(Qt::AlignCenter);
    image_->setMinimumHeight(110);
    image_->setMaximumHeight(180);
    detailLayout->addWidget(image_);
    title_->setObjectName(QStringLiteral("machine-title"));
    QFont titleFont = title_->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    title_->setFont(titleFont);
    detailLayout->addWidget(title_);
    availability_->setObjectName(QStringLiteral("machine-availability"));
    detailLayout->addWidget(availability_);
    auto *tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("machine-tabs"));
    details_->setObjectName(QStringLiteral("machine-details"));
    technical_->setObjectName(QStringLiteral("machine-technical"));
    context_->setObjectName(QStringLiteral("machine-context"));
    sources_->setObjectName(QStringLiteral("machine-sources"));
    for (QTextBrowser *browser : { details_, technical_, context_, sources_ })
        browser->setOpenExternalLinks(true);
    tabs->addTab(details_, tr("Ficha"));
    tabs->addTab(technical_, tr("Técnica"));
    tabs->addTab(context_, tr("Historia"));
    tabs->addTab(sources_, tr("Fuentes"));
    detailLayout->addWidget(tabs, 1);
    launchButton_->setObjectName(QStringLiteral("launch-selected-machine"));
    launchButton_->setEnabled(false);
    detailLayout->addWidget(launchButton_);
    splitter->addWidget(detailPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);

    QHash<QString, QTreeWidgetItem *> manufacturerNodes;
    QHash<QString, QTreeWidgetItem *> familyNodes;
    for (const PortableCatalogMachine &machine : catalog_.machines()) {
        QTreeWidgetItem *manufacturerNode = manufacturerNodes.value(machine.manufacturerId);
        if (manufacturerNode == nullptr) {
            const auto *manufacturer = catalog_.manufacturer(machine.manufacturerId);
            manufacturerNode = new QTreeWidgetItem(tree_, {
                manufacturer != nullptr ? manufacturer->name : machine.manufacturerId });
            manufacturerNode->setData(0, Qt::UserRole + 1, machine.manufacturerId);
            manufacturerNodes.insert(machine.manufacturerId, manufacturerNode);
        }
        const QString familyKey = machine.manufacturerId + QLatin1Char('/') + machine.familyId;
        QTreeWidgetItem *familyNode = familyNodes.value(familyKey);
        if (familyNode == nullptr) {
            const auto *family = catalog_.family(machine.familyId);
            familyNode = new QTreeWidgetItem(manufacturerNode, {
                family != nullptr ? family->name : tr("Otros modelos") });
            familyNode->setData(0, Qt::UserRole + 1, machine.familyId);
            familyNodes.insert(familyKey, familyNode);
        }
        const bool available = canLaunch(machine);
        auto *item = new QTreeWidgetItem(familyNode, {
            machine.name, available ? tr("Disponible") : tr("Pendiente") });
        item->setData(0, Qt::UserRole, machine.productId);
        item->setToolTip(1, available ?
            tr("Se puede probar en el motor portable") :
            tr("Ficha histórica; emulación portable todavía no disponible"));
        if (selectedProductId_.isEmpty() && available)
            selectedProductId_ = machine.productId;
    }
    tree_->resizeColumnToContents(0);
    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item) {
                if (item == nullptr)
                    return;
                const QString id = item->data(0, Qt::UserRole).toString();
                if (const auto *machine = catalog_.product(id)) {
                    select(*machine);
                    return;
                }
                selectedProductId_.clear();
                launchButton_->setEnabled(false);
                image_->clear();
                title_->setText(item->text(0));
                availability_->setText(tr("Selecciona una máquina para ver su ficha"));
                technical_->clear();
                sources_->clear();
                QString information;
                if (item->parent() == nullptr) {
                    const auto *manufacturer = catalog_.manufacturer(
                        item->data(0, Qt::UserRole + 1).toString());
                    if (manufacturer != nullptr)
                        information = paragraph(manufacturer->description) +
                                      paragraph(manufacturer->history);
                } else {
                    const auto *family = catalog_.family(
                        item->data(0, Qt::UserRole + 1).toString());
                    if (family != nullptr)
                        information = paragraph(family->description);
                }
                details_->setHtml(information);
                context_->setHtml(information);
            });
    connect(search_, &QLineEdit::textChanged, this,
            [this](const QString &query) { filter(query); });
    connect(saved_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item) {
                openSavedButton_->setEnabled(item != nullptr &&
                    item->flags().testFlag(Qt::ItemIsEnabled));
            });
    connect(openSavedButton_, &QPushButton::clicked, this,
            [this] {
                if (saved_->currentItem() != nullptr)
                    openSaved_(saved_->currentItem()->data(Qt::UserRole).toString());
            });
    connect(saved_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
                if (item->flags().testFlag(Qt::ItemIsEnabled))
                    openSaved_(item->data(Qt::UserRole).toString());
            });
    connect(launchButton_, &QPushButton::clicked, this,
            [this] {
                const auto *machine = catalog_.product(selectedProductId_);
                if (machine != nullptr && canLaunch(*machine))
                    launch_(selectedProductId_);
            });
    if (!selectedProductId_.isEmpty())
        selectProduct(selectedProductId_);
    else if (tree_->topLevelItemCount() != 0) {
        QTreeWidgetItem *manufacturer = tree_->topLevelItem(0);
        if (manufacturer->childCount() != 0 && manufacturer->child(0)->childCount() != 0)
            tree_->setCurrentItem(manufacturer->child(0)->child(0));
    }
}

void LauncherPage::setProfiles(const QVector<PortableMachineProfile> &profiles)
{
    const QString selected = saved_->currentItem() != nullptr ?
        saved_->currentItem()->data(Qt::UserRole).toString() : QString();
    saved_->clear();
    for (const PortableMachineProfile &profile : profiles) {
        const auto *product = catalog_.product(profile.productId);
        const bool available = product != nullptr &&
            product->adapterId == profile.adapterId && canLaunch(*product);
        auto *item = new QListWidgetItem(
            QStringLiteral("%1 — %2").arg(profile.name,
                product != nullptr ? product->name : profile.productId), saved_);
        item->setData(Qt::UserRole, profile.id);
        if (!available) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setToolTip(tr("Este modelo ya no está disponible en el motor portable"));
        }
        if (profile.id == selected)
            saved_->setCurrentItem(item);
    }
    openSavedButton_->setEnabled(saved_->currentItem() != nullptr &&
        saved_->currentItem()->flags().testFlag(Qt::ItemIsEnabled));
}

void LauncherPage::selectProduct(const QString &productId)
{
    for (QTreeWidgetItemIterator it(tree_); *it != nullptr; ++it) {
        if ((*it)->data(0, Qt::UserRole).toString() == productId) {
            if ((*it)->parent() != nullptr) {
                (*it)->parent()->setExpanded(true);
                if ((*it)->parent()->parent() != nullptr)
                    (*it)->parent()->parent()->setExpanded(true);
            }
            tree_->setCurrentItem(*it);
            tree_->scrollToItem(*it);
            return;
        }
    }
}

void LauncherPage::filter(const QString &query)
{
    QTreeWidgetItem *first = nullptr;
    for (int m = 0; m < tree_->topLevelItemCount(); ++m) {
        QTreeWidgetItem *manufacturerNode = tree_->topLevelItem(m);
        bool manufacturerVisible = false;
        for (int f = 0; f < manufacturerNode->childCount(); ++f) {
            QTreeWidgetItem *familyNode = manufacturerNode->child(f);
            bool familyVisible = false;
            for (int p = 0; p < familyNode->childCount(); ++p) {
                QTreeWidgetItem *item = familyNode->child(p);
                const auto *machine = catalog_.product(item->data(0, Qt::UserRole).toString());
                if (machine == nullptr)
                    continue;
                const QString haystack = QStringList {
                    machine->name, machine->summary,
                    manufacturerNode->text(0), familyNode->text(0),
                    machine->aliases.join(QLatin1Char(' ')),
                    machine->tags.join(QLatin1Char(' '))
                }.join(QLatin1Char(' '));
                const bool visible = haystack.contains(query, Qt::CaseInsensitive);
                item->setHidden(!visible);
                familyVisible |= visible;
                if (visible && first == nullptr)
                    first = item;
            }
            familyNode->setHidden(!familyVisible);
            manufacturerVisible |= familyVisible;
            familyNode->setExpanded(!query.isEmpty());
        }
        manufacturerNode->setHidden(!manufacturerVisible);
        manufacturerNode->setExpanded(!query.isEmpty());
    }
    if (first == nullptr) {
        tree_->setCurrentItem(nullptr);
        selectedProductId_.clear();
        title_->setText(tr("Sin resultados"));
        availability_->clear();
        image_->clear();
        details_->clear();
        technical_->clear();
        context_->clear();
        sources_->clear();
        launchButton_->setEnabled(false);
    } else if (tree_->currentItem() == nullptr ||
               tree_->currentItem()->data(0, Qt::UserRole).toString().isEmpty() ||
               tree_->currentItem()->isHidden() ||
               (tree_->currentItem()->parent() != nullptr &&
                tree_->currentItem()->parent()->isHidden())) {
        tree_->setCurrentItem(first);
    }
}

void LauncherPage::select(const PortableCatalogMachine &machine)
{
    selectedProductId_ = machine.productId;
    const bool available = canLaunch(machine);
    title_->setText(machine.name);
    availability_->setText(available ?
        tr("Emulación portable disponible para pruebas") :
        tr("Ficha histórica · emulación portable todavía no disponible"));
    launchButton_->setEnabled(available);
    const QPixmap illustration(machine.mediaResource);
    image_->setPixmap(!illustration.isNull() ?
        illustration.scaled(420, 170, Qt::KeepAspectRatio, Qt::SmoothTransformation) :
        style()->standardIcon(QStyle::SP_ComputerIcon).pixmap(96, 96));

    QString overview = QStringLiteral("<h1>%1</h1>").arg(machine.name.toHtmlEscaped());
    overview += paragraph(machine.period);
    overview += paragraph(machine.summary);
    if (!machine.history.isEmpty())
        overview += QStringLiteral("<h2>%1</h2>").arg(tr("La máquina")) +
                    paragraph(machine.history);
    if (!machine.warning.isEmpty())
        overview += QStringLiteral("<h2>%1</h2>").arg(tr("Límites de la ficha")) +
                    paragraph(machine.warning);
    overview += QStringLiteral("<p><i>%1</i></p>").arg(
        tr("La ficha describe la máquina histórica y puede reflejar el estado "
           "del producto anterior. No certifica paridad del motor portable. "
           "No se distribuyen BIOS ni discos.").toHtmlEscaped());
    details_->setHtml(overview);

    QString technical;
    for (const PortableCatalogSection &section : machine.sections) {
        technical += QStringLiteral("<h2>%1</h2>").arg(section.title.toHtmlEscaped());
        for (const PortableCatalogFact &fact : section.facts)
            technical += QStringLiteral("<p><b>%1</b><br>%2</p>")
                .arg(fact.label.toHtmlEscaped(), safeLink(fact.url, fact.value));
    }
    technical_->setHtml(technical);

    const auto *manufacturer = catalog_.manufacturer(machine.manufacturerId);
    const auto *family = catalog_.family(machine.familyId);
    QString history;
    if (manufacturer != nullptr) {
        history += QStringLiteral("<h2>%1</h2>").arg(manufacturer->name.toHtmlEscaped());
        history += paragraph(manufacturer->description);
        history += paragraph(manufacturer->history);
    }
    if (family != nullptr) {
        history += QStringLiteral("<h2>%1</h2>").arg(family->name.toHtmlEscaped());
        history += paragraph(family->description);
    }
    context_->setHtml(history);

    QString references;
    if (manufacturer != nullptr) {
        for (const PortableCatalogReference &reference : manufacturer->references)
            references += QStringLiteral("<p>%1%2</p>")
                .arg(safeLink(reference.url, reference.title),
                     reference.publisher.isEmpty() ? QString() :
                     QStringLiteral(" — %1").arg(reference.publisher.toHtmlEscaped()));
    }
    for (const PortableCatalogSection &section : machine.sections) {
        for (const PortableCatalogFact &fact : section.facts) {
            if (!fact.url.isEmpty())
                references += QStringLiteral("<p>%1</p>").arg(safeLink(fact.url, fact.value));
        }
    }
    sources_->setHtml(references);
}
