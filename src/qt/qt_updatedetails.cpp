/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Update details module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include "qt_updatedetails.hpp"
#include "ui_qt_updatedetails.h"
#include "qt_defs.hpp"

#include <QDesktopServices>
#include <QPushButton>

UpdateDetails::
    UpdateDetails(const UpdateCheck::UpdateResult &updateResult, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::UpdateDetails)
{
    ui->setupUi(this);
    ui->updateTitle->setText(tr("<b>An update to BluMach is available!</b>"));
    QString currentVersionText = tr("You are currently running version <b>%1</b>.").arg(updateResult.currentVersion);
    const QString latestVersionText = tr("<b>Version %1</b> is now available.").arg(updateResult.latestVersion);
    if (updateResult.currentVersion.isEmpty())
        currentVersionText = "";

    const auto updateDetailsText = QString("%1 %2%3").arg(latestVersionText, currentVersionText.append(' '), tr("Would you like to visit the download page?"));
    ui->updateDetails->setText(updateDetailsText);

    ui->updateText->setMarkdown(githubUpdateToMarkdown(updateResult.githubInfo));

    const auto downloadButton = new QPushButton(tr("Visit download page"));
    ui->buttonBox->addButton(downloadButton, QDialogButtonBox::AcceptRole);
    // Override accepted to mean "I want to visit the download page"
    connect(ui->buttonBox, &QDialogButtonBox::accepted, [] {
        visitDownloadPage();
    });
    const auto logo = QIcon(EMU_ICON_PATH).pixmap(QSize(64, 64));

    ui->icon->setPixmap(logo);
}

UpdateDetails::~UpdateDetails()
    = default;

QString
UpdateDetails::githubUpdateToMarkdown(const QList<UpdateCheck::GithubReleaseInfo> &releaseInfoList)
{
    // The github release info can be rather large so we'll only
    // display the most recent one
    QList<UpdateCheck::GithubReleaseInfo> singleRelease;
    if (!releaseInfoList.isEmpty()) {
        singleRelease.append(releaseInfoList.first());
    }
    QStringList fullText;
    for (const auto &release : singleRelease) {
        fullText.append(QString("#### %1").arg(release.name));
        // Github body text should already be in markdown and can just
        // be placed here as-is
        fullText.append(release.body);
        fullText.append("\n\n\n---\n\n\n");
    }
    // pop off the last hr
    fullText.removeLast();
    return fullText.join("\n");
}
void
UpdateDetails::visitDownloadPage()
{
    QDesktopServices::openUrl(QUrl("https://github.com/BluMach/BluMach/releases/latest"));
}
