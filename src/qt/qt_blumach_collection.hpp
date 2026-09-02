/*
 * BluMach collection browser.
 *
 * Author: rtzor
 * Copyright 2026 rtzor.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QT_BLUMACH_COLLECTION_HPP
#define QT_BLUMACH_COLLECTION_HPP

#include "qt_blumach_catalog.hpp"

#include <QWidget>

class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QLayout;
class QResizeEvent;
class QScrollArea;
class QSplitter;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QUrl;
class QVBoxLayout;

class BluMachCollectionWidget final : public QWidget {
    Q_OBJECT

public:
    explicit BluMachCollectionWidget(QWidget *parent = nullptr);
    void reloadLanguage();

public slots:
    void createSelectedMachine();
    void updateAppearance();

signals:
    void createMachineRequested(const QString &productId, const QString &emulatorMachineId);
    void showMachinesRequested();
    void selectionContextChanged(const QString &productId, const QString &productName, bool canCreate);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    enum ItemType { ManufacturerItem = 1, FamilyItem, ProductItem };

    void rebuildTree();
    void updateDetails(QTreeWidgetItem *item);
    void applyFilter();
    void setDetailTabsAvailable(bool researchAvailable, bool sourcesAvailable);
    void populateOverview(const BluMachProduct &product);
    void populateTechnicalPage(const BluMachProduct &product, bool sourcesPage);
    void clearLayout(QLayout *layout);
    void openTechnicalLink(const QUrl &url);
    bool isSourceSection(const QJsonObject &section) const;
    bool canCreateProduct(const BluMachProduct &product) const;
    void updateResponsiveLayout();

    BluMachCatalog m_catalog;
    QLabel         *m_heading = nullptr;
    QLabel         *m_intro = nullptr;
    QLineEdit      *m_search = nullptr;
    QComboBox      *m_statusFilter = nullptr;
    QLabel         *m_resultsLabel = nullptr;
    QGridLayout    *m_filterLayout = nullptr;
    QSplitter      *m_splitter = nullptr;
    QTreeWidget    *m_tree = nullptr;
    QLabel         *m_title = nullptr;
    QLabel         *m_subtitle = nullptr;
    QLabel         *m_summary = nullptr;
    QLabel         *m_statusBadge = nullptr;
    QLabel         *m_architectureBadge = nullptr;
    QLabel         *m_firmwareBadge = nullptr;
    QGridLayout    *m_badgeLayout = nullptr;
    QFrame         *m_warningFrame = nullptr;
    QLabel         *m_warningLabel = nullptr;
    QWidget        *m_machineIllustration = nullptr;
    QTabWidget     *m_infoTabs = nullptr;
    QScrollArea    *m_overviewScroll = nullptr;
    QScrollArea    *m_researchScroll = nullptr;
    QScrollArea    *m_sourcesScroll = nullptr;
    QVBoxLayout    *m_overviewLayout = nullptr;
    QVBoxLayout    *m_researchLayout = nullptr;
    QVBoxLayout    *m_sourcesLayout = nullptr;
    QString         m_selectedProductId;
    bool            m_compactLayout = false;
    bool            m_narrowLayout = false;
};

#endif
