/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "launcher_page.h"

#include <blumach/engine/version.h>
#include <blumach/frontend/frontend.h>

#include <QHash>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
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

void showPreview(QLabel *label, const QImage &frame)
{
    if (frame.isNull()) {
        label->setPixmap(QPixmap());
        label->setText(QObject::tr("Sin captura"));
        return;
    }
    label->clear();
    label->setPixmap(QPixmap::fromImage(frame).scaled(
        label->size() - QSize(8, 8), Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
}
}

LauncherPage::LauncherPage(const PortableCatalog &catalog,
                           const QString &catalogError,
                           std::function<void(const QString &)> launch,
                           std::function<void(const QString &)> openSaved,
                           std::function<void(const QString &)> editSaved,
                           std::function<void()> showRunning,
                           QWidget *parent)
    : QWidget(parent), catalog_(catalog), launch_(std::move(launch)),
      openSaved_(std::move(openSaved)), editSaved_(std::move(editSaved)),
      showRunning_(std::move(showRunning)), saved_(new QListWidget),
      emptyLibrary_(new QLabel),
      showRunningButton_(new QPushButton(tr("Ver máquina en ejecución"))),
      runningStatus_(new QLabel), runningPreview_(new QLabel),
      runningCard_(new QFrame), sections_(new QTabWidget),
      search_(new QLineEdit), availabilityFilter_(new QComboBox),
      results_(new QLabel), tree_(new QTreeWidget), image_(new QLabel),
      mediaCaption_(new QLabel), title_(new QLabel), period_(new QLabel),
      summary_(new QLabel), catalogStatus_(new QLabel),
      availability_(new QLabel), warning_(new QLabel),
      details_(new QTextBrowser), technical_(new QTextBrowser),
      engineering_(new QTextBrowser), sources_(new QTextBrowser),
      tabs_(new QTabWidget),
      launchButton_(new QPushButton(tr("Crear máquina…")))
{
    setObjectName(QStringLiteral("portableLauncher"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);
    auto *masthead = new QHBoxLayout;
    auto *heading = new QLabel(QStringLiteral("BluMach"));
    heading->setObjectName(QStringLiteral("launcher-brand"));
    QFont headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 7);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    masthead->addWidget(heading);
    auto *edition = new QLabel(tr("Colección histórica  ·  motor portable %1")
                                   .arg(QString::fromLatin1(BM_ENGINE_VERSION)));
    edition->setObjectName(QStringLiteral("launcher-edition"));
    masthead->addWidget(edition);
    masthead->addStretch(1);
    root->addLayout(masthead);
    sections_->setObjectName(QStringLiteral("main-sections"));
    root->addWidget(sections_, 1);

    auto *libraryPage = new QWidget;
    auto *libraryLayout = new QVBoxLayout(libraryPage);
    auto *libraryTitle = new QLabel(tr("Mis máquinas"));
    QFont libraryFont = libraryTitle->font();
    libraryFont.setPointSize(libraryFont.pointSize() + 4);
    libraryFont.setBold(true);
    libraryTitle->setFont(libraryFont);
    libraryLayout->addWidget(libraryTitle);
    auto *libraryHint = new QLabel(tr("Tus máquinas guardadas y la sesión actual. "
                                      "Para crear otra, explora el catálogo histórico."));
    libraryHint->setWordWrap(true);
    libraryLayout->addWidget(libraryHint);
    runningStatus_->setObjectName(QStringLiteral("running-machine-status"));
    showRunningButton_->setObjectName(QStringLiteral("show-running-machine"));
    showRunningButton_->setEnabled(false);
    libraryLayout->addWidget(runningStatus_);
    runningCard_->setObjectName(QStringLiteral("running-machine-card"));
    runningCard_->setMaximumWidth(850);
    auto *runningLayout = new QHBoxLayout(runningCard_);
    runningPreview_->setObjectName(QStringLiteral("running-machine-preview"));
    runningPreview_->setFixedSize(180, 112);
    runningPreview_->setAlignment(Qt::AlignCenter);
    showPreview(runningPreview_, QImage());
    runningLayout->addWidget(runningPreview_);
    auto *runningActions = new QVBoxLayout;
    runningActions->addWidget(new QLabel(tr("Vista de la sesión actual")));
    showRunningButton_->setMaximumWidth(240);
    runningActions->addWidget(showRunningButton_);
    runningActions->addStretch(1);
    runningLayout->addLayout(runningActions);
    runningLayout->addStretch(1);
    runningCard_->hide();
    libraryLayout->addWidget(runningCard_);
    emptyLibrary_->setText(tr("Aún no has creado ninguna máquina. "
                              "Elige un modelo en el catálogo para empezar."));
    emptyLibrary_->setWordWrap(true);
    emptyLibrary_->setObjectName(QStringLiteral("empty-machine-library"));
    libraryLayout->addWidget(emptyLibrary_);
    saved_->setObjectName(QStringLiteral("saved-machines"));
    saved_->setSpacing(8);
    saved_->setMaximumWidth(850);
    saved_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    libraryLayout->addWidget(saved_);
    auto *browseCatalog = new QPushButton(tr("Explorar catálogo histórico"));
    browseCatalog->setObjectName(QStringLiteral("browse-catalog"));
    browseCatalog->setMaximumWidth(240);
    libraryLayout->addWidget(browseCatalog);
    libraryLayout->addStretch(1);
    sections_->addTab(libraryPage, tr("Mis máquinas"));
    setActiveMachine(QString());

    auto *catalogPage = new QWidget;
    auto *catalogLayout = new QVBoxLayout(catalogPage);
    auto *intro = new QLabel(tr("Explora los equipos documentados. La ficha histórica y la "
                                "disponibilidad del emulador se indican por separado."));
    intro->setWordWrap(true);
    catalogLayout->addWidget(intro);

    auto *splitter = new QSplitter(Qt::Horizontal);
    auto *navigation = new QWidget;
    auto *navigationLayout = new QVBoxLayout(navigation);
    search_->setObjectName(QStringLiteral("catalog-search"));
    search_->setPlaceholderText(tr("Buscar fabricante, familia o máquina…"));
    search_->setClearButtonEnabled(true);
    navigationLayout->addWidget(search_);
    availabilityFilter_->setObjectName(QStringLiteral("catalog-availability-filter"));
    availabilityFilter_->addItem(tr("Todas las máquinas"), QStringLiteral("all"));
    availabilityFilter_->addItem(tr("Emulables ahora"), QStringLiteral("ready"));
    availabilityFilter_->addItem(tr("Pendientes de portar"), QStringLiteral("pending"));
    navigationLayout->addWidget(availabilityFilter_);
    tree_->setObjectName(QStringLiteral("catalog-tree"));
    tree_->setHeaderLabels({ tr("Catálogo") });
    tree_->setUniformRowHeights(true);
    navigationLayout->addWidget(tree_, 1);
    int availableCount = 0;
    for (const auto &machine : catalog_.machines())
        availableCount += canLaunch(machine) ? 1 : 0;
    auto *counts = new QLabel(tr("%1 fichas · %2 emulables ahora")
                                  .arg(catalog_.machines().size()).arg(availableCount));
    counts->setObjectName(QStringLiteral("catalog-counts"));
    navigationLayout->addWidget(counts);
    results_->setObjectName(QStringLiteral("catalog-results"));
    navigationLayout->addWidget(results_);
    if (!catalogError.isEmpty()) {
        auto *error = new QLabel(tr("Catálogo no disponible: %1").arg(catalogError));
        error->setWordWrap(true);
        navigationLayout->addWidget(error);
    }
    splitter->addWidget(navigation);

    auto *detailPane = new QWidget;
    auto *detailLayout = new QVBoxLayout(detailPane);
    detailLayout->setContentsMargins(4, 0, 0, 0);
    detailLayout->setSpacing(9);
    auto *hero = new QFrame;
    hero->setObjectName(QStringLiteral("catalog-hero"));
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(14, 12, 14, 12);
    heroLayout->setSpacing(14);
    auto *heroText = new QVBoxLayout;
    heroText->setSpacing(5);
    period_->setObjectName(QStringLiteral("machine-period"));
    heroText->addWidget(period_);
    title_->setObjectName(QStringLiteral("machine-title"));
    title_->setWordWrap(true);
    QFont titleFont = title_->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    title_->setFont(titleFont);
    heroText->addWidget(title_);
    summary_->setObjectName(QStringLiteral("machine-summary"));
    summary_->setWordWrap(true);
    heroText->addWidget(summary_);
    auto *badges = new QHBoxLayout;
    catalogStatus_->setObjectName(QStringLiteral("machine-catalog-status"));
    availability_->setObjectName(QStringLiteral("machine-availability"));
    for (QLabel *badge : {catalogStatus_, availability_}) {
        badge->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        badges->addWidget(badge);
    }
    badges->addStretch(1);
    heroText->addLayout(badges);
    heroText->addStretch(1);
    heroLayout->addLayout(heroText, 1);
    auto *media = new QVBoxLayout;
    image_->setObjectName(QStringLiteral("machine-image"));
    image_->setAlignment(Qt::AlignCenter);
    image_->setFixedSize(210, 125);
    media->addWidget(image_);
    mediaCaption_->setObjectName(QStringLiteral("machine-media-caption"));
    mediaCaption_->setAlignment(Qt::AlignCenter);
    mediaCaption_->setWordWrap(true);
    mediaCaption_->setMaximumWidth(210);
    media->addWidget(mediaCaption_);
    heroLayout->addLayout(media);
    detailLayout->addWidget(hero);
    warning_->setObjectName(QStringLiteral("machine-warning"));
    warning_->setWordWrap(true);
    warning_->setVisible(false);
    detailLayout->addWidget(warning_);
    tabs_->setObjectName(QStringLiteral("machine-tabs"));
    tabs_->setDocumentMode(true);
    details_->setObjectName(QStringLiteral("machine-details"));
    technical_->setObjectName(QStringLiteral("machine-technical"));
    engineering_->setObjectName(QStringLiteral("machine-engineering"));
    sources_->setObjectName(QStringLiteral("machine-sources"));
    for (QTextBrowser *browser : {details_, technical_, sources_})
        browser->setOpenExternalLinks(true);
    engineering_->setOpenExternalLinks(false);
    connect(engineering_, &QTextBrowser::anchorClicked, this,
            [](const QUrl &url) {
        if (url.scheme() == QStringLiteral("https"))
            QDesktopServices::openUrl(url);
    });
    tabs_->addTab(details_, tr("Ficha"));
    tabs_->addTab(technical_, tr("Investigación"));
    tabs_->addTab(engineering_, tr("Ingeniería"));
    tabs_->addTab(sources_, tr("Fuentes"));
    detailLayout->addWidget(tabs_, 1);
    launchButton_->setObjectName(QStringLiteral("launch-selected-machine"));
    launchButton_->setEnabled(false);
    detailLayout->addWidget(launchButton_);
    splitter->addWidget(detailPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({320, 780});
    catalogLayout->addWidget(splitter, 1);
    sections_->addTab(catalogPage, tr("Catálogo histórico"));

    const QPalette colors = palette();
    const QString border = colors.color(QPalette::Mid).name();
    const QString panel = colors.color(QPalette::Base).name();
    const QString muted = colors.color(QPalette::PlaceholderText).name();
    const QString accent = colors.color(QPalette::Link).name();
    setStyleSheet(QStringLiteral(
        "QFrame#catalog-hero { background: %1; border: 1px solid %2; border-radius: 9px; }"
        "QLabel#launcher-edition, QLabel#machine-period, QLabel#machine-media-caption, "
        "QLabel#catalog-counts, QLabel#catalog-results { color: %3; }"
        "QLabel#machine-catalog-status, QLabel#machine-availability { "
        "border: 1px solid %2; border-radius: 7px; padding: 3px 7px; }"
        "QLabel#machine-warning { border-left: 3px solid %4; padding: 7px; }"
        "QLabel#machine-image { border: 1px solid %2; border-radius: 4px; }"
        "QFrame#machine-card, QFrame#running-machine-card { "
        "background: %1; border: 1px solid %2; border-radius: 6px; }"
        "QLabel#machine-preview, QLabel#running-machine-preview { "
        "background: %2; color: %3; border-radius: 4px; }"
        "QPushButton#launch-selected-machine { padding: 8px 16px; }"
    ).arg(panel, border, muted, accent));

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
        auto *item = new QTreeWidgetItem(familyNode, {machine.name});
        item->setData(0, Qt::UserRole, machine.productId);
        item->setToolTip(0, available ?
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
                launchButton_->setText(tr("Selecciona una máquina"));
                image_->clear();
                image_->hide();
                mediaCaption_->clear();
                mediaCaption_->hide();
                title_->setText(item->text(0));
                period_->clear();
                summary_->clear();
                catalogStatus_->clear();
                availability_->setText(tr("Selecciona una máquina para ver su ficha"));
                warning_->hide();
                technical_->clear();
                engineering_->clear();
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
                tabs_->setTabVisible(1, false);
                tabs_->setTabVisible(2, false);
                tabs_->setTabVisible(3, false);
                tabs_->setCurrentIndex(0);
            });
    connect(search_, &QLineEdit::textChanged, this,
            [this](const QString &query) { filter(query); });
    connect(availabilityFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this] { filter(search_->text()); });
    connect(saved_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) {
                if (item->flags().testFlag(Qt::ItemIsEnabled))
                    openSaved_(item->data(Qt::UserRole).toString());
            });
    connect(showRunningButton_, &QPushButton::clicked, this,
            [this] { showRunning_(); });
    connect(browseCatalog, &QPushButton::clicked, this,
            [this] { sections_->setCurrentIndex(1); });
    connect(launchButton_, &QPushButton::clicked, this,
            [this] {
                const auto *machine = catalog_.product(selectedProductId_);
                if (machine != nullptr && canLaunch(*machine))
                    launch_(selectedProductId_);
            });
    const QString initialProductId = selectedProductId_;
    filter(QString());
    if (!initialProductId.isEmpty())
        selectProduct(initialProductId);
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
    saved_->setVisible(!profiles.isEmpty());
    emptyLibrary_->setVisible(profiles.isEmpty());
    const int visibleRows = static_cast<int>(
        std::min<qsizetype>(profiles.size(), 3));
    saved_->setFixedHeight(std::min(420, 146 * visibleRows + 12));
    profilePreviewLabels_.clear();
    profileStatusLabels_.clear();
    profileAvailable_.clear();
    for (const PortableMachineProfile &profile : profiles) {
        const auto *product = catalog_.product(profile.productId);
        const bool available = product != nullptr &&
            product->adapterId == profile.adapterId && canLaunch(*product);
        auto *item = new QListWidgetItem(saved_);
        item->setSizeHint(QSize(0, 138));
        item->setData(Qt::UserRole, profile.id);
        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("machine-card"));
        auto *cardLayout = new QHBoxLayout(card);
        auto *preview = new QLabel;
        preview->setObjectName(QStringLiteral("machine-preview"));
        preview->setFixedSize(180, 112);
        preview->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(preview);
        auto *information = new QVBoxLayout;
        auto *name = new QLabel(profile.name);
        QFont nameFont = name->font();
        nameFont.setBold(true);
        name->setFont(nameFont);
        information->addWidget(name);
        information->addWidget(new QLabel(
            product != nullptr ? product->name : profile.productId));
        auto *status = new QLabel;
        status->setObjectName(QStringLiteral("saved-machine-status"));
        status->setText(!available ? tr("Emulación no disponible") :
            profile.id == activeProfileId_ ? tr("En ejecución") :
            tr("Guardada"));
        information->addWidget(status);
        information->addStretch(1);
        auto *actions = new QHBoxLayout;
        auto *open = new QPushButton(tr("Abrir"));
        open->setObjectName(QStringLiteral("open-saved-machine"));
        open->setEnabled(available);
        auto *edit = new QPushButton(tr("Editar configuración"));
        edit->setObjectName(QStringLiteral("edit-saved-machine"));
        edit->setEnabled(available);
        actions->addWidget(open);
        actions->addWidget(edit);
        actions->addStretch(1);
        information->addLayout(actions);
        cardLayout->addLayout(information, 1);
        connect(open, &QPushButton::clicked, this,
                [this, id = profile.id] { openSaved_(id); });
        connect(edit, &QPushButton::clicked, this,
                [this, id = profile.id] { editSaved_(id); });
        saved_->setItemWidget(item, card);
        profilePreviewLabels_.insert(profile.id, preview);
        profileStatusLabels_.insert(profile.id, status);
        profileAvailable_.insert(profile.id, available);
        if (!previews_.contains(profile.id) && !profileRoot_.isEmpty()) {
            const QImage stored(QDir(profileRoot_).filePath(
                profile.id + QStringLiteral("/preview.png")));
            if (!stored.isNull())
                previews_.insert(profile.id, stored);
        }
        showPreview(preview, previews_.value(profile.id));
        if (!available) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setToolTip(tr("Este modelo ya no está disponible en el motor portable"));
        }
        if (profile.id == selected)
            saved_->setCurrentItem(item);
    }
    if (saved_->currentItem() == nullptr) {
        for (int index = 0; index < saved_->count(); ++index) {
            QListWidgetItem *item = saved_->item(index);
            if (item->flags().testFlag(Qt::ItemIsEnabled)) {
                saved_->setCurrentItem(item);
                break;
            }
        }
    }
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
    int matches = 0;
    const QString availability = availabilityFilter_->currentData().toString();
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
                const bool ready = canLaunch(*machine);
                const bool visible = haystack.contains(query, Qt::CaseInsensitive) &&
                    (availability == QStringLiteral("all") ||
                     (availability == QStringLiteral("ready") && ready) ||
                     (availability == QStringLiteral("pending") && !ready));
                item->setHidden(!visible);
                familyVisible |= visible;
                if (visible) {
                    ++matches;
                    if (first == nullptr)
                        first = item;
                }
            }
            familyNode->setHidden(!familyVisible);
            manufacturerVisible |= familyVisible;
            familyNode->setExpanded(!query.isEmpty() || availability != QStringLiteral("all"));
        }
        manufacturerNode->setHidden(!manufacturerVisible);
        manufacturerNode->setExpanded(!query.isEmpty() || availability != QStringLiteral("all"));
    }
    results_->setText(tr("%1 resultados").arg(matches));
    if (first == nullptr) {
        tree_->setCurrentItem(nullptr);
        selectedProductId_.clear();
        title_->setText(tr("Sin resultados"));
        period_->clear();
        summary_->clear();
        catalogStatus_->clear();
        availability_->clear();
        image_->clear();
        image_->hide();
        mediaCaption_->clear();
        mediaCaption_->hide();
        warning_->hide();
        details_->clear();
        technical_->clear();
        engineering_->clear();
        sources_->clear();
        tabs_->setTabVisible(1, false);
        tabs_->setTabVisible(2, false);
        tabs_->setTabVisible(3, false);
        tabs_->setCurrentIndex(0);
        launchButton_->setEnabled(false);
        launchButton_->setText(tr("Selecciona una máquina"));
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
    QString period = machine.period;
    if (!machine.architecture.isEmpty())
        period += period.isEmpty() ? machine.architecture :
            QStringLiteral(" · %1").arg(machine.architecture);
    period_->setText(period);
    title_->setText(machine.name);
    summary_->setText(machine.summary);
    catalogStatus_->setText(tr("Ficha: %1").arg(
        machine.statusLabel.isEmpty() ? machine.status : machine.statusLabel));
    availability_->setText(available ?
        tr("Motor: disponible para pruebas") :
        tr("Motor: pendiente de portar"));
    warning_->setText(machine.warning);
    warning_->setVisible(!machine.warning.isEmpty());
    launchButton_->setEnabled(available);
    launchButton_->setText(available ? tr("Crear máquina…") :
        tr("Emulación aún no disponible"));
    const QPixmap illustration(machine.mediaResource);
    image_->setVisible(!illustration.isNull());
    mediaCaption_->setVisible(!illustration.isNull() && !machine.mediaLabel.isEmpty());
    image_->setPixmap(illustration.isNull() ? QPixmap() :
        illustration.scaled(image_->size() - QSize(10, 10),
                            Qt::KeepAspectRatio, Qt::SmoothTransformation));
    mediaCaption_->setText(machine.mediaLabel);

    QString overview;
    if (!machine.history.isEmpty())
        overview += QStringLiteral("<h2>%1</h2>").arg(tr("La máquina")) +
                    paragraph(machine.history);
    if (!machine.hardware.isEmpty()) {
        overview += QStringLiteral("<h2>%1</h2><table cellspacing=\"8\">")
            .arg(tr("Hardware documentado"));
        for (const PortableCatalogFact &fact : machine.hardware)
            overview += QStringLiteral("<tr><th align=\"left\" valign=\"top\">%1</th>"
                                       "<td>%2</td></tr>")
                .arg(fact.label.toHtmlEscaped(), fact.value.toHtmlEscaped());
        overview += QStringLiteral("</table>");
    }
    const auto *manufacturer = catalog_.manufacturer(machine.manufacturerId);
    const auto *family = catalog_.family(machine.familyId);
    if (manufacturer != nullptr) {
        overview += QStringLiteral("<h2>%1</h2>").arg(manufacturer->name.toHtmlEscaped());
        overview += paragraph(manufacturer->description);
        overview += paragraph(manufacturer->history);
    }
    if (family != nullptr) {
        overview += QStringLiteral("<h2>%1</h2>").arg(family->name.toHtmlEscaped());
        overview += paragraph(family->description);
    }
    overview += QStringLiteral("<p><i>%1</i></p>").arg(
        tr("La ficha describe la máquina histórica y puede reflejar el estado "
           "del producto anterior. No certifica paridad del motor portable. "
           "No se distribuyen BIOS ni discos.").toHtmlEscaped());
    details_->setHtml(overview);

    QString technical;
    QString references;
    for (const PortableCatalogSection &section : machine.sections) {
        QString &target = section.sources ? references : technical;
        target += QStringLiteral("<h2>%1</h2>").arg(section.title.toHtmlEscaped());
        if (!section.evidence.isEmpty())
            target += QStringLiteral("<p><i>%1</i></p>")
                .arg(section.evidence.toHtmlEscaped());
        target += paragraph(section.description);
        for (const PortableCatalogFact &fact : section.facts)
            target += QStringLiteral("<p><b>%1</b><br>%2</p>")
                .arg(fact.label.toHtmlEscaped(), safeLink(fact.url, fact.value));
    }
    technical_->setHtml(technical);
    if (manufacturer != nullptr) {
        for (const PortableCatalogReference &reference : manufacturer->references)
            references += QStringLiteral("<p>%1%2</p>")
                .arg(safeLink(reference.url, reference.title),
                     reference.publisher.isEmpty() ? QString() :
                     QStringLiteral(" — %1").arg(reference.publisher.toHtmlEscaped()));
    }
    sources_->setHtml(references);

    QString engineering;
    if (!machine.implementationDocument.isEmpty()) {
        QFile document(QStringLiteral(":/blumach/catalog/documents/%1")
                           .arg(machine.implementationDocument));
        if (document.open(QIODevice::ReadOnly)) {
            engineering = tr("# Memoria de implementación\n\n"
                             "Este documento describe la implementación anterior. "
                             "No acredita que el motor portable tenga las mismas funciones.\n\n");
            engineering += QString::fromUtf8(document.readAll());
        }
    }
    engineering_->setMarkdown(engineering);
    tabs_->setTabVisible(1, !technical.isEmpty());
    tabs_->setTabVisible(2, !engineering.isEmpty());
    tabs_->setTabVisible(3, !references.isEmpty());
    if (!tabs_->isTabVisible(tabs_->currentIndex()))
        tabs_->setCurrentIndex(0);
}

void LauncherPage::setProfileRoot(const QString &root)
{
    profileRoot_ = root;
}

void LauncherPage::setActiveMachine(const QString &name,
                                    const QString &profileId)
{
    const bool running = !name.isEmpty();
    activeProfileId_ = running ? profileId : QString();
    runningStatus_->setText(running ?
        tr("En ejecución: %1").arg(name) :
        tr("No hay ninguna máquina en ejecución."));
    runningCard_->setVisible(running);
    showRunningButton_->setEnabled(running);
    if (!running)
        setRunningPreview(QImage());
    for (auto it = profileStatusLabels_.cbegin();
         it != profileStatusLabels_.cend(); ++it) {
        it.value()->setText(!profileAvailable_.value(it.key()) ?
            tr("Emulación no disponible") :
            running && it.key() == activeProfileId_ ?
                tr("En ejecución") : tr("Guardada"));
    }
}

void LauncherPage::setRunningPreview(const QImage &frame)
{
    showPreview(runningPreview_, frame);
}

void LauncherPage::setProfilePreview(const QString &profileId,
                                      const QImage &frame)
{
    if (profileId.isEmpty() || frame.isNull() ||
        !profilePreviewLabels_.contains(profileId))
        return;
    previews_.insert(profileId, frame);
    if (QLabel *label = profilePreviewLabels_.value(profileId, nullptr))
        showPreview(label, frame);
}
