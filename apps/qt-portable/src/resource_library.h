/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_RESOURCE_LIBRARY_H
#define BLUMACH_PORTABLE_RESOURCE_LIBRARY_H

#include "portable_catalog.h"

#include <QString>
#include <QVector>

struct PortableResourceMatch {
    QString role;
    QString name;
    QString path;
    QString sha256;
    bool preferred = false;
};

struct PortableResourceScan {
    QVector<PortableResourceMatch> matches;
    QString error;
    bool limited = false;

    QString preferredPath(const QString &role) const;
    QString nameForPath(const QString &role, const QString &path) const;
};

/* No writable media, archives or symlinks are inspected. The scan is bounded. */
PortableResourceScan scanPortableResources(const QString &root,
                                            const PortableCatalogMachine &machine);

#endif
