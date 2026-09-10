/*
 * BluMach local catalogue skin packages.
 *
 * Author: rtzor
 * Copyright 2026 rtzor.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QT_BLUMACH_SKIN_HPP
#define QT_BLUMACH_SKIN_HPP

#include <QHash>
#include <QString>

struct BluMachManufacturerMark {
    QString imagePath;
    QString background;
};

class BluMachCatalogSkin final {
public:
    bool loadConfigured(QString *errorMessage = nullptr);
    bool loadDirectory(const QString &directory, QString *errorMessage = nullptr);

    static QString configuredDirectory();
    static bool    manufacturerMarksEnabled();
    static bool    inspectDirectory(const QString &directory, QString *name = nullptr,
                                    QString *errorMessage = nullptr,
                                    QString *warningMessage = nullptr);

    [[nodiscard]] QString displayName() const;
    [[nodiscard]] BluMachManufacturerMark manufacturerMark(const QString &manufacturerId) const;

private:
    QString                         m_displayName;
    QHash<QString, BluMachManufacturerMark> m_manufacturerMarks;
};

#endif
