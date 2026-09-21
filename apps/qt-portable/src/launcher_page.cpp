/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "launcher_page.h"

#include <blumach/engine/version.h>
#include <blumach/frontend/frontend.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStyle>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace {
QString
machineHtml(const PortableCatalogMachine &machine)
{
    QString html = QStringLiteral("<h1>%1</h1>")
                       .arg(machine.name.toHtmlEscaped());
    if (!machine.period.isEmpty())
        html += QStringLiteral("<p><b>%1</b></p>")
                    .arg(machine.period.toHtmlEscaped());
    if (!machine.summary.isEmpty())
        html += QStringLiteral("<p>%1</p>")
                    .arg(machine.summary.toHtmlEscaped());
    if (!machine.history.isEmpty())
        html += QStringLiteral("<h2>Historia</h2><p>%1</p>")
                    .arg(machine.history.toHtmlEscaped());
    if (!machine.warning.isEmpty())
        html += QStringLiteral("<p><b>%1</b></p>")
                    .arg(machine.warning.toHtmlEscaped());
    html += QStringLiteral(
        "<p><i>Vista previa del motor portable. La ficha histórica no implica "
        "paridad completa de emulación. La BIOS y los discos no se distribuyen.</i></p>");
    for (const PortableCatalogSection &section : machine.sections) {
        html += QStringLiteral("<h2>%1</h2>")
                    .arg(section.title.toHtmlEscaped());
        for (const PortableCatalogFact &fact : section.facts) {
            QString value = fact.value.toHtmlEscaped();
            const QUrl url(fact.url);
            if (url.isValid() && url.scheme() == QStringLiteral("https"))
                value = QStringLiteral("<a href=\"%1\">%2</a>")
                            .arg(url.toString(QUrl::FullyEncoded).toHtmlEscaped(),
                                 value);
            html += QStringLiteral("<p><b>%1</b><br>%2</p>")
                        .arg(fact.label.toHtmlEscaped(), value);
        }
    }
    return html;
}
}

LauncherPage::LauncherPage(const PortableCatalog &catalog,
                           const QString &catalogError,
                           std::function<void(const QString &)> launch,
                           QWidget *parent)
    : QWidget(parent), launch_(std::move(launch)),
      details_(new QTextBrowser), launchButton_(new QPushButton(tr("Configurar y arrancar")))
{
    auto *root = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("BluMach Portable %1")
                                   .arg(QString::fromLatin1(BM_ENGINE_VERSION)));
    QFont font = heading->font();
    font.setPointSize(font.pointSize() + 6);
    font.setBold(true);
    heading->setFont(font);
    root->addWidget(heading);
    root->addWidget(new QLabel(tr("Máquinas disponibles para pruebas locales. "
                                  "Selecciona una ficha y aporta tu firmware.")));

    auto *splitter = new QSplitter(Qt::Horizontal);
    auto *cards = new QWidget;
    auto *cardsLayout = new QVBoxLayout(cards);
    cardsLayout->setSpacing(12);
    for (const PortableCatalogMachine &machine : catalog.machines()) {
        const QByteArray id = machine.adapterId.toUtf8();
        if (bm_frontend_adapter_find(id.constData()) == nullptr)
            continue;
        auto *card = new QFrame;
        card->setFrameShape(QFrame::StyledPanel);
        card->setObjectName(QStringLiteral("machine-card-%1").arg(machine.productId));
        auto *cardLayout = new QVBoxLayout(card);
        auto *image = new QLabel;
        image->setAlignment(Qt::AlignCenter);
        image->setMinimumHeight(105);
        const QPixmap illustration(machine.mediaResource);
        if (!illustration.isNull())
            image->setPixmap(illustration.scaled(250, 105, Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation));
        else
            image->setPixmap(style()->standardIcon(QStyle::SP_ComputerIcon)
                                 .pixmap(80, 80));
        cardLayout->addWidget(image);
        auto *name = new QLabel(machine.name);
        QFont nameFont = name->font();
        nameFont.setBold(true);
        name->setFont(nameFont);
        cardLayout->addWidget(name);
        auto *summary = new QLabel(machine.summary);
        summary->setWordWrap(true);
        cardLayout->addWidget(summary);
        auto *selectButton = new QPushButton(tr("Ver ficha"));
        selectButton->setObjectName(QStringLiteral("select-%1").arg(machine.productId));
        cardLayout->addWidget(selectButton);
        connect(selectButton, &QPushButton::clicked, this,
                [this, machine] { select(machine); });
        cardsLayout->addWidget(card);
        if (selectedProductId_.isEmpty())
            select(machine);
    }
    if (!catalogError.isEmpty()) {
        auto *error = new QLabel(tr("Catálogo no disponible: %1").arg(catalogError));
        error->setWordWrap(true);
        cardsLayout->addWidget(error);
    }
    if (selectedProductId_.isEmpty())
        cardsLayout->addWidget(new QLabel(tr("No hay máquinas portables disponibles.")));
    cardsLayout->addStretch();
    auto *cardsScroll = new QScrollArea;
    cardsScroll->setWidgetResizable(true);
    cardsScroll->setWidget(cards);
    splitter->addWidget(cardsScroll);

    auto *detailPane = new QWidget;
    auto *detailLayout = new QVBoxLayout(detailPane);
    details_->setOpenExternalLinks(true);
    details_->setObjectName(QStringLiteral("machine-details"));
    detailLayout->addWidget(details_);
    launchButton_->setEnabled(!selectedProductId_.isEmpty());
    launchButton_->setObjectName(QStringLiteral("launch-selected-machine"));
    connect(launchButton_, &QPushButton::clicked, this,
            [this] { if (!selectedProductId_.isEmpty()) launch_(selectedProductId_); });
    detailLayout->addWidget(launchButton_);
    splitter->addWidget(detailPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);
}

void
LauncherPage::select(const PortableCatalogMachine &machine)
{
    selectedProductId_ = machine.productId;
    details_->setHtml(machineHtml(machine));
    if (launchButton_ != nullptr)
        launchButton_->setEnabled(true);
}
