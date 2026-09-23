/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "launcher_page.h"
#include "machine_dialog.h"
#include "machine_profile_store.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <cassert>

int
main(int argc, char **argv)
{
    QApplication application(argc, argv);
    const QByteArray json = R"({
      "schema":"blumach-catalog-v3",
      "manufacturers":[{"id":"olivetti","name":"Olivetti",
        "history_key":"olivetti.history"}],
      "families":[{"id":"pcs","manufacturer_id":"olivetti",
        "name":"PCS","description_key":"pcs.family"},
        {"id":"m15","manufacturer_id":"olivetti","name":"M15"}],
      "platforms":[
        {"id":"pcs","portable_adapter_id":"olivetti-pcs86"},
        {"id":"m15","portable_adapter_id":"olivetti-m15"}],
      "products":[
        {"id":"olivetti-pcs86","name":"Olivetti PCS 86",
         "manufacturer_id":"olivetti","family_id":"pcs",
         "status":"validated","platform_id":"pcs",
         "summary_key":"pcs.summary","history_key":"pcs.history"},
        {"id":"olivetti-m15","name":"Olivetti M15",
         "manufacturer_id":"olivetti","family_id":"m15",
         "status":"experimental","platform_id":"m15",
         "summary_key":"m15.summary","history_key":"m15.history"},
        {"id":"pending","name":"Future PC","manufacturer_id":"olivetti",
         "family_id":"pcs","status":"research","platform_id":"none"}]
    })";
    const QByteArray locale = R"({
      "pcs.summary":"PCS summary", "pcs.history":"PCS history",
      "m15.summary":"M15 summary", "m15.history":"M15 history",
      "olivetti.history":"Manufacturer history", "pcs.family":"Family history"
    })";
    PortableCatalog catalog;
    QString launched;
    QString openedSaved;
    QString editedSaved;
    bool showedRunning = false;
    assert(catalog.parse(json, locale));
    LauncherPage page(catalog, QString(),
                      [&launched](const QString &id) { launched = id; },
                      [&openedSaved](const QString &id) { openedSaved = id; },
                      [&editedSaved](const QString &id) { editedSaved = id; },
                      [&showedRunning] { showedRunning = true; });
    auto *sections = page.findChild<QTabWidget *>(QStringLiteral("main-sections"));
    assert(sections != nullptr && sections->count() == 2);
    assert(sections->currentIndex() == 0);
    auto *running = page.findChild<QPushButton *>(
        QStringLiteral("show-running-machine"));
    assert(running != nullptr && !running->isEnabled());
    page.setActiveMachine(QStringLiteral("My M15"));
    assert(running->isEnabled());
    QImage liveFrame(32, 20, QImage::Format_RGB32);
    liveFrame.fill(Qt::green);
    page.setRunningPreview(liveFrame);
    auto *runningPreview = page.findChild<QLabel *>(
        QStringLiteral("running-machine-preview"));
    assert(runningPreview != nullptr && !runningPreview->pixmap().isNull());
    running->click();
    assert(showedRunning);
    page.setActiveMachine(QString());
    assert(!running->isEnabled());
    PortableMachineProfile saved;
    saved.id = QStringLiteral("profile-one");
    saved.name = QStringLiteral("My M15");
    saved.productId = QStringLiteral("olivetti-m15");
    saved.adapterId = QStringLiteral("olivetti-m15");
    QTemporaryDir previewRoot;
    assert(previewRoot.isValid());
    assert(QDir().mkpath(QDir(previewRoot.path()).filePath(saved.id)));
    const QString previewPath = QDir(previewRoot.path()).filePath(
        saved.id + QStringLiteral("/preview.png"));
    assert(liveFrame.save(previewPath));
    page.setProfileRoot(previewRoot.path());
    page.setProfiles({ saved });
    auto *savedList = page.findChild<QListWidget *>(QStringLiteral("saved-machines"));
    assert(savedList != nullptr && savedList->count() == 1);
    assert(savedList->currentRow() == 0);
    auto *savedPreview = page.findChild<QLabel *>(
        QStringLiteral("machine-preview"));
    assert(savedPreview != nullptr && !savedPreview->pixmap().isNull());
    auto *savedStatus = page.findChild<QLabel *>(
        QStringLiteral("saved-machine-status"));
    assert(savedStatus != nullptr && savedStatus->text() == QStringLiteral("Guardada"));
    page.setActiveMachine(saved.name, saved.id);
    assert(savedStatus->text() == QStringLiteral("En ejecución"));
    auto *openSaved = page.findChild<QPushButton *>(
        QStringLiteral("open-saved-machine"));
    assert(openSaved != nullptr && openSaved->isEnabled());
    openSaved->click();
    assert(openedSaved == QStringLiteral("profile-one"));
    auto *editSaved = page.findChild<QPushButton *>(
        QStringLiteral("edit-saved-machine"));
    assert(editSaved != nullptr && editSaved->isEnabled());
    editSaved->click();
    assert(editedSaved == QStringLiteral("profile-one"));
    auto *tree = page.findChild<QTreeWidget *>(QStringLiteral("catalog-tree"));
    assert(tree != nullptr && tree->topLevelItemCount() == 1);
    const auto findProduct = [tree](const QString &id) {
        for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
            if ((*it)->data(0, Qt::UserRole).toString() == id)
                return *it;
        }
        return static_cast<QTreeWidgetItem *>(nullptr);
    };
    assert(findProduct(QStringLiteral("olivetti-pcs86")) != nullptr);
    assert(findProduct(QStringLiteral("olivetti-m15")) != nullptr);
    assert(findProduct(QStringLiteral("pending")) != nullptr);
    auto *details = page.findChild<QTextBrowser *>(
        QStringLiteral("machine-details"));
    assert(details != nullptr);
    assert(details->toPlainText().contains(QStringLiteral("PCS history")));
    tree->setCurrentItem(findProduct(QStringLiteral("olivetti-m15")));
    assert(details->toPlainText().contains(QStringLiteral("M15 history")));
    assert(details->toPlainText().contains(QStringLiteral("Manufacturer history")));
    auto *availability = page.findChild<QLabel *>(QStringLiteral("machine-availability"));
    assert(availability != nullptr && availability->text().contains(
        QStringLiteral("disponible")));
    auto *launch = page.findChild<QPushButton *>(
        QStringLiteral("launch-selected-machine"));
    assert(launch != nullptr && launch->isEnabled());
    launch->click();
    assert(launched == QStringLiteral("olivetti-m15"));
    tree->setCurrentItem(tree->topLevelItem(0));
    assert(!launch->isEnabled());
    assert(details->toPlainText().contains(QStringLiteral("Manufacturer history")));
    tree->setCurrentItem(findProduct(QStringLiteral("pending")));
    assert(!launch->isEnabled());
    auto *search = page.findChild<QLineEdit *>(QStringLiteral("catalog-search"));
    assert(search != nullptr);
    search->setText(QStringLiteral("M15"));
    assert(findProduct(QStringLiteral("olivetti-m15"))->isHidden() == false);
    assert(findProduct(QStringLiteral("pending"))->isHidden());
    search->setText(QStringLiteral("no matching machine"));
    assert(!launch->isEnabled());
    search->clear();
    assert(findProduct(QStringLiteral("pending"))->isHidden() == false);
    auto *availabilityFilter = page.findChild<QComboBox *>(
        QStringLiteral("catalog-availability-filter"));
    assert(availabilityFilter != nullptr);
    availabilityFilter->setCurrentIndex(1);
    assert(findProduct(QStringLiteral("pending"))->isHidden());
    assert(!findProduct(QStringLiteral("olivetti-m15"))->isHidden());
    availabilityFilter->setCurrentIndex(2);
    assert(!findProduct(QStringLiteral("pending"))->isHidden());
    assert(findProduct(QStringLiteral("olivetti-m15"))->isHidden());
    availabilityFilter->setCurrentIndex(0);

    PortableCatalog bundled;
    QString error;
    assert(bundled.load(&error));
    assert(error.isEmpty());
    const auto *pcs86 = bundled.product(QStringLiteral("olivetti-pcs86"));
    assert(pcs86 != nullptr && !pcs86->mediaResource.isEmpty());
    assert(pcs86->expansionSlots.size() == 3);
    for (const PortableCatalogExpansionSlot &slot : pcs86->expansionSlots)
        assert(slot.bus == QStringLiteral("isa8") && !slot.modelled);
    assert(!QPixmap(pcs86->mediaResource).isNull());
    const auto *m15 = bundled.product(QStringLiteral("olivetti-m15"));
    assert(m15 != nullptr && !m15->history.isEmpty());
    assert(!m15->commercialConfiguration.isEmpty());
    assert(m15->configurationFields.size() == 1);
    assert(m15->configurationFields[0].choices.size() == 2);
    assert(m15->resources.size() == 2);
    assert(m15->resources[0].role == QStringLiteral("firmware"));
    assert(m15->resources[0].known[0].sha256.size() == 64);
    MachineDialog machineDialog(bundled, QString());
    auto *machineChoice = machineDialog.findChild<QComboBox *>(
        QStringLiteral("machine-choice"));
    assert(machineChoice != nullptr && machineChoice->isEnabled());
    machineDialog.selectProduct(QStringLiteral("olivetti-m15"));
    assert(machineDialog.productId() == QStringLiteral("olivetti-m15"));
    auto *formTabs = machineDialog.findChild<QTabWidget *>(
        QStringLiteral("machine-form-tabs"));
    assert(formTabs != nullptr && formTabs->count() == 2);
    assert(formTabs->tabText(0) == QStringLiteral("General"));
    assert(formTabs->tabText(1) == QStringLiteral("Hardware avanzado"));
    assert(machineDialog.findChild<QComboBox *>(
        QStringLiteral("commercial-configuration")) != nullptr);
    auto *memoryChoice = machineDialog.findChild<QComboBox *>(
        QStringLiteral("configuration-memory"));
    assert(memoryChoice != nullptr && formTabs->widget(0)->isAncestorOf(memoryChoice));
    assert(formTabs->isTabVisible(1));
    auto *advancedEmpty = machineDialog.findChild<QLabel *>(
        QStringLiteral("machine-advanced-empty"));
    assert(advancedEmpty != nullptr && !advancedEmpty->isHidden());
    machineDialog.setMachineSelectionLocked(true);
    assert(!machineChoice->isEnabled());
    assert(machineDialog.productId() == QStringLiteral("olivetti-m15"));
    machineDialog.setMachineSelectionLocked(false);
    assert(machineChoice->isEnabled());
    LauncherPage realPage(bundled, QString(), [](const QString &) {},
                          [](const QString &) {}, [](const QString &) {},
                          [] {});
    auto *realTree = realPage.findChild<QTreeWidget *>(QStringLiteral("catalog-tree"));
    assert(realTree != nullptr && bundled.machines().size() >= 30);
    for (QTreeWidgetItemIterator it(realTree); *it != nullptr; ++it) {
        if ((*it)->data(0, Qt::UserRole).toString() == QStringLiteral("olivetti-m15")) {
            realTree->setCurrentItem(*it);
            break;
        }
    }
    auto *realDetails = realPage.findChild<QTextBrowser *>(
        QStringLiteral("machine-details"));
    auto *realTabs = realPage.findChild<QTabWidget *>(
        QStringLiteral("machine-tabs"));
    assert(realDetails != nullptr && realTabs != nullptr);
    assert(realDetails->toPlainText().contains(QStringLiteral("Intel 80C88")));
    assert(realTabs->isTabVisible(1) && realTabs->isTabVisible(2) &&
           realTabs->isTabVisible(3));
    auto *engineering = realPage.findChild<QTextBrowser *>(
        QStringLiteral("machine-engineering"));
    assert(engineering != nullptr && engineering->toPlainText().contains(
        QStringLiteral("motor portable")));
    const QString localRoot = qEnvironmentVariable("BLUMACH_TEST_RESOURCE_ROOT");
    const QString capturePath = qEnvironmentVariable("BLUMACH_CAPTURE_LAUNCHER");
    if (!capturePath.isEmpty()) {
        const bool captureLibrary = qEnvironmentVariable(
            "BLUMACH_CAPTURE_SECTION") == QStringLiteral("library");
        const QString captureProduct = qEnvironmentVariable(
            "BLUMACH_CAPTURE_PRODUCT_ID", "olivetti-m15");
        for (QTreeWidgetItemIterator it(realTree); *it != nullptr; ++it) {
            if ((*it)->data(0, Qt::UserRole).toString() == captureProduct) {
                if ((*it)->parent() != nullptr) {
                    (*it)->parent()->setExpanded(true);
                    if ((*it)->parent()->parent() != nullptr)
                        (*it)->parent()->parent()->setExpanded(true);
                }
                realTree->setCurrentItem(*it);
                break;
            }
        }
        if (captureLibrary) {
            PortableMachineProfile sample;
            sample.id = QStringLiteral("visual-sample");
            sample.name = QStringLiteral("Mi Olivetti M15");
            sample.productId = QStringLiteral("olivetti-m15");
            sample.adapterId = QStringLiteral("olivetti-m15");
            realPage.setProfiles({sample});
            realPage.setActiveMachine(sample.name, sample.id);
            QImage sampleFrame(320, 200, QImage::Format_RGB32);
            sampleFrame.fill(QColor(140, 165, 45));
            realPage.setRunningPreview(sampleFrame);
            realPage.setProfilePreview(sample.id, sampleFrame);
        }
        realPage.findChild<QTabWidget *>(QStringLiteral("main-sections"))
            ->setCurrentIndex(captureLibrary ? 0 : 1);
        realPage.resize(1120, 730);
        QImage capture(realPage.size(), QImage::Format_ARGB32_Premultiplied);
        capture.fill(Qt::transparent);
        realPage.render(&capture);
        assert(capture.save(capturePath));
    }
    MachineDialog noRoot(bundled, QString());
    noRoot.selectProduct(QStringLiteral("olivetti-pcs86"));
    auto *commercialChoice = noRoot.findChild<QComboBox *>(
        QStringLiteral("configuration-commercial"));
    auto *floppyAChoice = noRoot.findChild<QComboBox *>(
        QStringLiteral("configuration-floppy_a"));
    auto *floppyBChoice = noRoot.findChild<QComboBox *>(
        QStringLiteral("configuration-floppy_b"));
    auto *jumpersChoice = noRoot.findChild<QComboBox *>(
        QStringLiteral("configuration-jumpers"));
    auto *pcsTabs = noRoot.findChild<QTabWidget *>(
        QStringLiteral("machine-form-tabs"));
    assert(commercialChoice != nullptr && commercialChoice->count() == 4);
    assert(floppyAChoice != nullptr && floppyBChoice != nullptr);
    assert(jumpersChoice != nullptr && jumpersChoice->count() == 257);
    assert(pcsTabs != nullptr && pcsTabs->isTabVisible(1));
    assert(pcsTabs->widget(0)->isAncestorOf(commercialChoice));
    assert(pcsTabs->widget(1)->isAncestorOf(floppyAChoice));
    assert(pcsTabs->widget(1)->isAncestorOf(floppyBChoice));
    assert(pcsTabs->widget(1)->isAncestorOf(jumpersChoice));
    auto *slotsNote = noRoot.findChild<QLabel *>(
        QStringLiteral("expansion-slots-note"));
    assert(slotsNote != nullptr && pcsTabs->widget(1)->isAncestorOf(slotsNote));
    assert(slotsNote->text().contains(QStringLiteral("3 × ISA8")));
    assert(pcsTabs->widget(0)->isAncestorOf(noRoot.findChild<QLineEdit *>(
        QStringLiteral("asset-floppy-0"))));
    assert(pcsTabs->widget(1)->isAncestorOf(noRoot.findChild<QLineEdit *>(
        QStringLiteral("asset-hard-disk-0"))));
    const QString formCapture = qEnvironmentVariable("BLUMACH_CAPTURE_MACHINE_FORM");
    if (!formCapture.isEmpty()) {
        noRoot.show();
        pcsTabs->setCurrentIndex(qEnvironmentVariableIntValue(
            "BLUMACH_CAPTURE_MACHINE_TAB"));
        application.processEvents();
        assert(noRoot.grab().save(formCapture));
        noRoot.hide();
    }
    assert(noRoot.options().value(QStringLiteral("commercial_profile")) == 1U);
    commercialChoice->setCurrentIndex(2);
    assert(noRoot.options().value(QStringLiteral("floppy_b_type")) == 1U);
    floppyBChoice->setCurrentIndex(floppyBChoice->findData(
        QStringLiteral("hd")));
    assert(noRoot.options().value(QStringLiteral("commercial_profile")) == 0U);
    jumpersChoice->setCurrentIndex(jumpersChoice->findData(
        QStringLiteral("byte_ff")));
    assert(noRoot.options().value(QStringLiteral("jumper_bank")) == 256U);
    const auto savedHardware = noRoot.options();
    MachineDialog reopened(bundled, QString());
    reopened.selectProduct(QStringLiteral("olivetti-pcs86"));
    reopened.setOptions(savedHardware);
    reopened.setEditingExistingProfile(true);
    assert(reopened.options() == savedHardware);
    MachineDialog olderProfile(bundled, QString());
    olderProfile.selectProduct(QStringLiteral("olivetti-pcs86"));
    olderProfile.setEditingExistingProfile(true);
    olderProfile.setOptions({{QStringLiteral("ems_kib"), 384U}});
    assert(olderProfile.options().value(QStringLiteral("commercial_profile")) == 0U);
    assert(olderProfile.options().value(QStringLiteral("floppy_a_type")) == 0U);
    assert(olderProfile.options().value(QStringLiteral("floppy_b_type")) == 0U);
    auto *emsChoice = noRoot.findChild<QComboBox *>(
        QStringLiteral("configuration-ems"));
    assert(emsChoice != nullptr && emsChoice->count() == 3);
    assert(noRoot.options().value(QStringLiteral("ems_kib")) == 1920U);
    emsChoice->setCurrentIndex(1);
    assert(noRoot.options().value(QStringLiteral("ems_kib")) == 384U);
    assert(noRoot.findChild<QLineEdit *>(QStringLiteral("asset-firmware-even")) != nullptr);
    {
        QTemporaryDir generatedRoot;
        assert(generatedRoot.isValid());
        QHash<QString, QString> inputPaths;
        for (const QString &role : {QStringLiteral("firmware-even"),
                                    QStringLiteral("firmware-odd")}) {
            const QString path = QDir(generatedRoot.path()).filePath(role +
                                                           QStringLiteral(".bin"));
            QFile firmware(path);
            assert(firmware.open(QIODevice::WriteOnly | QIODevice::NewOnly));
            assert(firmware.write(QByteArray(32768, '\0')) == 32768);
            firmware.close();
            inputPaths.insert(role, path);
        }
        MachineDialog generated(bundled, generatedRoot.path());
        generated.selectProduct(QStringLiteral("olivetti-pcs86"));
        generated.setPaths(inputPaths);
        auto *preset = generated.findChild<QComboBox *>(
            QStringLiteral("configuration-commercial"));
        assert(preset != nullptr);
        preset->setCurrentIndex(preset->findData(QStringLiteral("floppy_xta")));
        auto *generatedEms = generated.findChild<QComboBox *>(
            QStringLiteral("configuration-ems"));
        auto *generateDisk = generated.findChild<QCheckBox *>(
            QStringLiteral("generate-hard-disk-0"));
        assert(generatedEms != nullptr);
        assert(generateDisk != nullptr && generateDisk->isChecked());
        generatedEms->setCurrentIndex(generatedEms->findData(QStringLiteral("384")));
        assert(preset->currentData().toString() == QStringLiteral("floppy_xta"));
        assert(generateDisk->isChecked());
        auto *diskStatus = generated.findChild<QLabel *>(
            QStringLiteral("asset-status-hard-disk-0"));
        assert(diskStatus != nullptr && diskStatus->text().contains(
            QStringLiteral("imagen de disco vacía")));
        static_cast<QDialog &>(generated).accept();
        assert(generated.result() == QDialog::Accepted);
        const QString diskPath = generated.paths().value(
            QStringLiteral("hard-disk-0"));
        const QString diskFolder = QDir(generatedRoot.path()).filePath(
            QStringLiteral("olivetti/pcs86/hard-disks/working"));
        assert(QFileInfo(diskPath).absolutePath() == diskFolder);
        assert(QFileInfo(diskPath).size() == 21411840);
        QFile disk(diskPath);
        assert(disk.open(QIODevice::ReadOnly));
        while (!disk.atEnd()) {
            const QByteArray block = disk.read(64 * 1024);
            assert(!block.isEmpty());
            assert(block.count('\0') == block.size());
        }
        disk.close();
        PortableMachineProfile profile;
        profile.name = QStringLiteral("PCS 86 with XTA");
        profile.productId = QStringLiteral("olivetti-pcs86");
        profile.adapterId = QStringLiteral("olivetti-pcs86");
        profile.assets = generated.paths();
        profile.options = generated.options();
        MachineProfileStore store(QDir(generatedRoot.path()).filePath(
            QStringLiteral("profiles")));
        QString saveError;
        assert(store.save(&profile, &saveError));
        const QVector<PortableMachineProfile> loaded = store.load();
        assert(loaded.size() == 1);
        assert(loaded.first().options.value(QStringLiteral("commercial_profile")) == 3U);
        assert(loaded.first().assets.value(QStringLiteral("hard-disk-0")) == diskPath);
        MachineDialog existing(bundled, generatedRoot.path());
        existing.selectProduct(QStringLiteral("olivetti-pcs86"));
        existing.setEditingExistingProfile(true);
        existing.setPaths(loaded.first().assets);
        existing.setOptions(loaded.first().options);
        auto *existingGenerate = existing.findChild<QCheckBox *>(
            QStringLiteral("generate-hard-disk-0"));
        assert(existingGenerate != nullptr && !existingGenerate->isChecked());
        static_cast<QDialog &>(existing).accept();
        assert(existing.result() == QDialog::Accepted);
        assert(existing.paths().value(QStringLiteral("hard-disk-0")) == diskPath);
        assert(QDir(diskFolder).entryList({QStringLiteral("*.img")}, QDir::Files).size() == 1);

        MachineDialog upgrade(bundled, generatedRoot.path());
        upgrade.selectProduct(QStringLiteral("olivetti-pcs86"));
        upgrade.setEditingExistingProfile(true);
        upgrade.setPaths(inputPaths);
        upgrade.setOptions({{QStringLiteral("commercial_profile"), 0U},
                            {QStringLiteral("ems_kib"), 384U}});
        auto *upgradePreset = upgrade.findChild<QComboBox *>(
            QStringLiteral("configuration-commercial"));
        auto *upgradeEms = upgrade.findChild<QComboBox *>(
            QStringLiteral("configuration-ems"));
        assert(upgradePreset != nullptr && upgradeEms != nullptr);
        upgradePreset->setCurrentIndex(upgradePreset->findData(
            QStringLiteral("floppy_xta")));
        upgradeEms->setCurrentIndex(upgradeEms->findData(
            QStringLiteral("1920")));
        assert(upgradePreset->currentData().toString() ==
               QStringLiteral("floppy_xta"));
        auto *upgradeGenerate = upgrade.findChild<QCheckBox *>(
            QStringLiteral("generate-hard-disk-0"));
        assert(upgradeGenerate != nullptr && upgradeGenerate->isChecked());
        auto *upgradeStatus = upgrade.findChild<QLabel *>(
            QStringLiteral("asset-status-hard-disk-0"));
        assert(upgradeStatus != nullptr && upgradeStatus->text().contains(
            QStringLiteral("imagen de disco vacía")));
        static_cast<QDialog &>(upgrade).accept();
        assert(upgrade.result() == QDialog::Accepted);
        const QString newDisk = upgrade.paths().value(
            QStringLiteral("hard-disk-0"));
        assert(!newDisk.isEmpty() && newDisk != diskPath);
        assert(QFileInfo(newDisk).size() == 21411840);
        assert(QDir(diskFolder).entryList({QStringLiteral("*.img")}, QDir::Files).size() == 2);

        MachineDialog customWithXta(bundled, generatedRoot.path());
        customWithXta.selectProduct(QStringLiteral("olivetti-pcs86"));
        customWithXta.setPaths(inputPaths);
        auto *customPreset = customWithXta.findChild<QComboBox *>(
            QStringLiteral("configuration-commercial"));
        auto *driveA = customWithXta.findChild<QComboBox *>(
            QStringLiteral("configuration-floppy_a"));
        auto *customGenerate = customWithXta.findChild<QCheckBox *>(
            QStringLiteral("generate-hard-disk-0"));
        assert(customPreset != nullptr && driveA != nullptr &&
               customGenerate != nullptr);
        customPreset->setCurrentIndex(customPreset->findData(
            QStringLiteral("floppy_xta")));
        assert(customGenerate->isChecked());
        driveA->setCurrentIndex(driveA->findData(QStringLiteral("hd")));
        assert(customPreset->currentData().toString() == QStringLiteral("custom"));
        assert(customGenerate->isChecked());
        static_cast<QDialog &>(customWithXta).accept();
        assert(customWithXta.result() == QDialog::Accepted);
        assert(customWithXta.options().value(QStringLiteral("commercial_profile")) == 0U);
        assert(customWithXta.options().value(QStringLiteral("floppy_a_type")) == 2U);
        assert(QFileInfo(customWithXta.paths().value(
            QStringLiteral("hard-disk-0"))).size() == 21411840);
        assert(QDir(diskFolder).entryList({QStringLiteral("*.img")}, QDir::Files).size() == 3);

        MachineDialog manualCustom(bundled, generatedRoot.path());
        manualCustom.selectProduct(QStringLiteral("olivetti-pcs86"));
        manualCustom.setEditingExistingProfile(true);
        manualCustom.setPaths(inputPaths);
        manualCustom.setOptions({{QStringLiteral("commercial_profile"), 0U},
                                 {QStringLiteral("floppy_a_type"), 2U}});
        auto *manualGenerate = manualCustom.findChild<QCheckBox *>(
            QStringLiteral("generate-hard-disk-0"));
        assert(manualGenerate != nullptr && !manualGenerate->isChecked());
        manualGenerate->setChecked(true);
        static_cast<QDialog &>(manualCustom).accept();
        assert(manualCustom.result() == QDialog::Accepted);
        assert(QFileInfo(manualCustom.paths().value(
            QStringLiteral("hard-disk-0"))).size() == 21411840);
        assert(QDir(diskFolder).entryList({QStringLiteral("*.img")}, QDir::Files).size() == 4);

        MachineDialog noXta(bundled, generatedRoot.path());
        noXta.selectProduct(QStringLiteral("olivetti-pcs86"));
        auto *noXtaPreset = noXta.findChild<QComboBox *>(
            QStringLiteral("configuration-commercial"));
        auto *noXtaGenerate = noXta.findChild<QCheckBox *>(
            QStringLiteral("generate-hard-disk-0"));
        assert(noXtaPreset != nullptr && noXtaGenerate != nullptr);
        noXtaPreset->setCurrentIndex(noXtaPreset->findData(
            QStringLiteral("floppy_xta")));
        assert(noXtaGenerate->isChecked());
        noXtaPreset->setCurrentIndex(noXtaPreset->findData(
            QStringLiteral("one_floppy")));
        assert(!noXtaGenerate->isChecked());
    }
    auto *missingChoice = noRoot.findChild<QComboBox *>(
        QStringLiteral("firmware-choice-firmware-even"));
    assert(missingChoice != nullptr && missingChoice->count() == 1);
    assert(noRoot.paths().isEmpty());
    if (!localRoot.isEmpty()) {
        MachineDialog pcs86(bundled, localRoot);
        pcs86.selectProduct(QStringLiteral("olivetti-pcs86"));
        assert(pcs86.paths().contains(QStringLiteral("firmware-even")));
        assert(pcs86.paths().contains(QStringLiteral("firmware-odd")));
        assert(pcs86.paths().contains(QStringLiteral("floppy-0")));
        assert(!pcs86.paths().contains(QStringLiteral("hard-disk-0")));
        auto *biosChoice = pcs86.findChild<QComboBox *>(
            QStringLiteral("firmware-choice-firmware-even"));
        auto *biosEditor = pcs86.findChild<QLineEdit *>(
            QStringLiteral("asset-firmware-even"));
        assert(biosChoice != nullptr && biosEditor != nullptr);
        assert(biosChoice->count() == 2);
        assert(biosChoice->currentData().toString() == biosEditor->text());
        assert(biosChoice->itemData(0, Qt::ToolTipRole).toString().contains(
            QStringLiteral("SHA-256:")));
        pcs86.selectCustomFirmware();
        assert(biosChoice->currentIndex() == biosChoice->count() - 1);
        assert(biosEditor->text().isEmpty() && !biosEditor->isHidden());
        biosChoice->setCurrentIndex(0);
        const QString detected = biosEditor->text();
        const QString custom = QDir(localRoot).filePath(QStringLiteral("custom-test.rom"));
        biosChoice->setCurrentIndex(biosChoice->count() - 1);
        assert(biosEditor->text().isEmpty() && !biosEditor->isHidden());
        biosEditor->setText(custom);
        biosChoice->setCurrentIndex(0);
        assert(biosEditor->text() == detected && biosEditor->isHidden());
        biosChoice->setCurrentIndex(biosChoice->count() - 1);
        assert(biosEditor->text() == custom && !biosEditor->isHidden());
        MachineDialog m15Dialog(bundled, localRoot);
        m15Dialog.selectProduct(QStringLiteral("olivetti-m15"));
        auto *ramChoice = m15Dialog.findChild<QComboBox *>(
            QStringLiteral("configuration-memory"));
        assert(ramChoice != nullptr && ramChoice->count() == 2);
        ramChoice->setCurrentIndex(0);
        assert(m15Dialog.options().value(QStringLiteral("ram_kib")) == 256U);
        assert(m15Dialog.paths().contains(QStringLiteral("firmware")));
        assert(m15Dialog.findChild<QComboBox *>(
            QStringLiteral("firmware-choice-firmware"))->count() == 2);
        MachineDialog saved(bundled, localRoot);
        saved.selectProduct(QStringLiteral("olivetti-pcs86"));
        saved.setPaths({{QStringLiteral("firmware-even"),
                         pcs86.paths().value(QStringLiteral("firmware-even"))}});
        saved.setEditingExistingProfile(true);
        assert(!saved.paths().contains(QStringLiteral("firmware-odd")));
    }
    for (QTreeWidgetItemIterator it(realTree); *it != nullptr; ++it) {
        if ((*it)->data(0, Qt::UserRole).toString() ==
            QStringLiteral("olivetti-pcs86")) {
            realTree->setCurrentItem(*it);
            break;
        }
    }
    return 0;
}
