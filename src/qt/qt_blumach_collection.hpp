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
#include "qt_blumach_skin.hpp"

#include <QWidget>
#include <QSet>

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
class QTextBrowser;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QUrl;
class QVBoxLayout;

class BluMachCollectionWidget final : public QWidget {
    Q_OBJECT

public:
    explicit BluMachCollectionWidget(QWidget *parent = nullptr);
    void reloadLanguage();
    void reloadSkin();

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
    void rebuildFacetFilters();
    void rebuildFilterLayout();
    void updateAdvancedFiltersButton();
    void clearFilters();
    void restoreTreeExpansion(const QSet<QString> &expandedIds);
    QSet<QString> expandedTreeIds() const;
    void loadUiState();
    void saveUiState();
    void updateDetails(QTreeWidgetItem *item);
    void applyFilter();
    bool matchesFacetFilters(const BluMachProduct &product) const;
    bool matchesFacetFilters(const QJsonObject &commonFacets, const QJsonObject &profileFacets) const;
    void setDetailTabsAvailable(bool researchAvailable, bool engineeringAvailable, bool sourcesAvailable);
    void populateOverview(const BluMachProduct &product);
    void populateTechnicalPage(const BluMachProduct &product, bool sourcesPage);
    void populateEngineeringPage(const BluMachProduct &product);
    void clearLayout(QLayout *layout);
    void openTechnicalLink(const QUrl &url);
    bool isSourceSection(const QJsonObject &section) const;
    bool canCreateProduct(const BluMachProduct &product) const;
    void updateResponsiveLayout();

    BluMachCatalog m_catalog;
    BluMachCatalogSkin m_skin;
    QLabel         *m_heading = nullptr;
    QLabel         *m_intro = nullptr;
    QLineEdit      *m_search = nullptr;
    QComboBox      *m_statusFilter = nullptr;
    QToolButton    *m_advancedFiltersButton = nullptr;
    QToolButton    *m_clearFiltersButton = nullptr;
    QFrame         *m_advancedFiltersPanel = nullptr;
    QGridLayout    *m_advancedFiltersLayout = nullptr;
    QHash<QString, QComboBox *> m_facetFilters;
    QHash<QString, QString>     m_facetSelections;
    QLabel         *m_resultsLabel = nullptr;
    QGridLayout    *m_filterLayout = nullptr;
    QSplitter      *m_splitter = nullptr;
    QTreeWidget    *m_tree = nullptr;
    QLabel         *m_title = nullptr;
    QLabel         *m_subtitle = nullptr;
    QLabel         *m_summary = nullptr;
    QLabel         *m_statusBadge = nullptr;
    QLabel         *m_architectureBadge = nullptr;
    QGridLayout    *m_badgeLayout = nullptr;
    QFrame         *m_warningFrame = nullptr;
    QLabel         *m_warningLabel = nullptr;
    QWidget        *m_machineIllustration = nullptr;
    QTabWidget     *m_infoTabs = nullptr;
    QScrollArea    *m_overviewScroll = nullptr;
    QScrollArea    *m_researchScroll = nullptr;
    QScrollArea    *m_sourcesScroll = nullptr;
    QTextBrowser   *m_engineeringView = nullptr;
    QVBoxLayout    *m_overviewLayout = nullptr;
    QVBoxLayout    *m_researchLayout = nullptr;
    QVBoxLayout    *m_sourcesLayout = nullptr;
    QString         m_selectedProductId;
    QSet<QString>   m_expandedBeforeFilter;
    QSet<QString>   m_savedExpandedIds;
    bool            m_hasSavedTreeState = false;
    bool            m_restoringUiState = false;
    bool            m_rebuildingTree = false;
    bool            m_filterRevealActive = false;
    bool            m_compactLayout = false;
    bool            m_narrowLayout = false;
};

#endif
