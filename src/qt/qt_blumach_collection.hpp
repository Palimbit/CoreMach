/*
 * BluMach collection browser.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QT_BLUMACH_COLLECTION_HPP
#define QT_BLUMACH_COLLECTION_HPP

#include "qt_blumach_catalog.hpp"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QUrl;

class BluMachCollectionWidget final : public QWidget {
    Q_OBJECT

public:
    explicit BluMachCollectionWidget(QWidget *parent = nullptr);
    void reloadLanguage();

signals:
    void createMachineRequested(const QString &productId, const QString &emulatorMachineId);
    void showMachinesRequested();

private:
    enum ItemType { ManufacturerItem = 1, FamilyItem, ProductItem };

    void rebuildTree();
    void updateDetails(QTreeWidgetItem *item);
    void applyFilter();
    void openTechnicalLink(const QUrl &url);
    QString hardwareHtml(const BluMachProduct &product) const;
    QString technicalHtml(const BluMachProduct &product) const;

    BluMachCatalog m_catalog;
    QLabel         *m_heading = nullptr;
    QLabel         *m_intro = nullptr;
    QLineEdit      *m_search = nullptr;
    QComboBox      *m_statusFilter = nullptr;
    QTreeWidget    *m_tree = nullptr;
    QLabel         *m_title = nullptr;
    QLabel         *m_subtitle = nullptr;
    QTabWidget     *m_infoTabs = nullptr;
    QTextBrowser   *m_overview = nullptr;
    QTextBrowser   *m_technical = nullptr;
    QPushButton    *m_createButton = nullptr;
    QPushButton    *m_machinesButton = nullptr;
    QString         m_selectedProductId;
};

#endif
