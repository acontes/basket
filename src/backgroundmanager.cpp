/***************************************************************************
 *   Copyright (C) 2003 by Sébastien Laoût                                 *
 *   slaout@linux62.org                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "backgroundmanager.h"

#include <KDE/KUrl>
#include <KDE/KGlobal>
#include <KDE/KStandardDirs>
#include <KDE/KConfig>
#include <KDE/KConfigGroup>

#include <QtCore/QDir>
#include <QtGui/QPainter>
#include <QtGui/QImage>
#include <QtGui/QPixmap>

/** class BackgroundEntry: */

BackgroundEntry::BackgroundEntry(const QString &location)
{
    this->location = location;
    name           = KUrl(location).fileName();
    tiled          = false;
    pixmap         = 0;
    preview        = 0;
    customersCount = 0;
}

BackgroundEntry::~BackgroundEntry()
{
    delete pixmap;
    delete preview;
}

/** class OpaqueBackgroundEntry: */

OpaqueBackgroundEntry::OpaqueBackgroundEntry(const QString &name, const QColor &color)
{
    this->name     = name;
    this->color    = color;
    pixmap         = 0;
    customersCount = 0;
}

OpaqueBackgroundEntry::~OpaqueBackgroundEntry()
{
    delete pixmap;
}

/** class BackgroundManager: */

BackgroundManager::BackgroundManager()
{
/// kDebug() << "BackgroundManager: Found the following background images in  ";
    QStringList directories = KGlobal::dirs()->resourceDirs("data"); // eg. { "/home/seb/.kde/share/apps/", "/usr/share/apps/" }
    // For each folder:
    for (QStringList::Iterator it = directories.begin(); it != directories.end(); ++it) {
        // For each file in those directories:
        QDir dir(*it + "basket/backgrounds/", /*nameFilder=*/"*.png", /*sortSpec=*/QDir::Name | QDir::IgnoreCase, /*filterSpec=*/QDir::Files | QDir::NoSymLinks);
///     kDebug() << *it + "basket/backgrounds/  ";
        QStringList files = dir.entryList();
        for (QStringList::Iterator it2 = files.begin(); it2 != files.end(); ++it2) // TODO: If an image name is present in two folders?
            addImage(*it + "basket/backgrounds/" + *it2);
    }

/// kDebug() << ":";
/// for (BackgroundsList::Iterator it = m_backgroundsList.begin(); it != m_backgroundsList.end(); ++it)
///     kDebug() << "* " << (*it)->location << "  [ref: " << (*it)->name << "]";

    connect(&m_garbageTimer, SIGNAL(timeout()), this, SLOT(doGarbage()));
}

BackgroundManager::~BackgroundManager()
{
}

void BackgroundManager::addImage(const QString &fullPath)
{
    m_backgroundsList.append(new BackgroundEntry(fullPath));
}

BackgroundEntry* BackgroundManager::backgroundEntryFor(const QString &image)
{
    for (BackgroundsList::Iterator it = m_backgroundsList.begin(); it != m_backgroundsList.end(); ++it)
        if ((*it)->name == image)
            return *it;
    return 0;
}

OpaqueBackgroundEntry* BackgroundManager::opaqueBackgroundEntryFor(const QString &image, const QColor &color)
{
    for (OpaqueBackgroundsList::Iterator it = m_opaqueBackgroundsList.begin(); it != m_opaqueBackgroundsList.end(); ++it)
        if ((*it)->name == image && (*it)->color == color)
            return *it;
    return 0;
}

bool BackgroundManager::subscribe(const QString &image)
{
    BackgroundEntry *entry = backgroundEntryFor(image);
    if (entry) {
        // If it's the first time something subscribe to this image:
        if (!entry->pixmap) {
            // Try to load the pixmap:
            entry->pixmap = new QPixmap(entry->location);
            // Try to figure out if it's a tiled background image or not (default to NO):
            KConfig config(entry->location + ".config", KConfig::SimpleConfig);
            KConfigGroup configGroup = config.group("BasKet Background Image Configuration");
            entry->tiled = configGroup.readEntry("tiled", false);
        }
        // Return if the image loading has failed:
        if (entry->pixmap->isNull()) {
///         kDebug() << "BackgroundManager: Failed to load " << entry->location;
            return false;
        }
        // Success: effectively subscribe:
        ++entry->customersCount;
        return true;
    } else {
        // Don't exist: subscription failed:
///     kDebug() << "BackgroundManager: Requested unexisting image: " << image;
        return false;
    }
}

bool BackgroundManager::subscribe(const QString &image, const QColor &color)
{
    BackgroundEntry *backgroundEntry = backgroundEntryFor(image);

    // First, if the image doesn't exist, isn't subscribed, or failed to load then we don't go further:
    if (!backgroundEntry || !backgroundEntry->pixmap || backgroundEntry->pixmap->isNull()) {
///     kDebug() << "BackgroundManager: Requested an unexisting or unsubscribed image: (" << image << "," << color.name() << ")...";
        return false;
    }

    OpaqueBackgroundEntry *opaqueBackgroundEntry = opaqueBackgroundEntryFor(image, color);

    // If this couple is requested for the first time or it haven't been subscribed for a long time enough, create it:
    if (!opaqueBackgroundEntry) {
///     kDebug() << "BackgroundManager: Computing (" << image << "," << color.name() << ")...";
        opaqueBackgroundEntry = new OpaqueBackgroundEntry(image, color);
        opaqueBackgroundEntry->pixmap = new QPixmap(backgroundEntry->pixmap->size());
        opaqueBackgroundEntry->pixmap->fill(color);
        QPainter painter(opaqueBackgroundEntry->pixmap);
        painter.drawPixmap(0, 0, *(backgroundEntry->pixmap));
        painter.end();
        m_opaqueBackgroundsList.append(opaqueBackgroundEntry);
    }

    // We are now sure the entry exist, do the subscription:
    ++opaqueBackgroundEntry->customersCount;
    return true;
}

void BackgroundManager::unsubscribe(const QString &image)
{
    BackgroundEntry *entry = backgroundEntryFor(image);

    if (!entry) {
///     kDebug() << "BackgroundManager: Wanted to unsuscribe a not subscribed image: " << image;
        return;
    }

    --entry->customersCount;
    if (entry->customersCount <= 0)
        requestDelayedGarbage();
}

void BackgroundManager::unsubscribe(const QString &image, const QColor &color)
{
    OpaqueBackgroundEntry *entry = opaqueBackgroundEntryFor(image, color);

    if (!entry) {
///     kDebug() << "BackgroundManager: Wanted to unsuscribe a not subscribed colored image: (" << image << "," << color.name() << ")";
        return;
    }

    --entry->customersCount;
    if (entry->customersCount <= 0)
        requestDelayedGarbage();
}

QPixmap* BackgroundManager::pixmap(const QString &image)
{
    BackgroundEntry *entry = backgroundEntryFor(image);

    if (!entry || !entry->pixmap || entry->pixmap->isNull()) {
///     kDebug() << "BackgroundManager: Requested an unexisting or unsubscribed image: " << image;
        return 0;
    }

    return entry->pixmap;
}

QPixmap* BackgroundManager::opaquePixmap(const QString &image, const QColor &color)
{
    OpaqueBackgroundEntry *entry = opaqueBackgroundEntryFor(image, color);

    if (!entry || !entry->pixmap || entry->pixmap->isNull()) {
///     kDebug() << "BackgroundManager: Requested an unexisting or unsubscribed colored image: (" << image << "," << color.name() << ")";
        return 0;
    }

    return entry->pixmap;
}

bool BackgroundManager::tiled(const QString &image)
{
    BackgroundEntry *entry = backgroundEntryFor(image);

    if (!entry || !entry->pixmap || entry->pixmap->isNull()) {
///     kDebug() << "BackgroundManager: Requested an unexisting or unsubscribed image: " << image;
        return false;
    }

    return entry->tiled;
}

bool BackgroundManager::exists(const QString &image)
{
    for (BackgroundsList::iterator it = m_backgroundsList.begin(); it != m_backgroundsList.end(); ++it)
        if ((*it)->name == image)
            return true;
    return false;
}

QStringList BackgroundManager::imageNames()
{
    QStringList list;
    for (BackgroundsList::iterator it = m_backgroundsList.begin(); it != m_backgroundsList.end(); ++it)
        list.append((*it)->name);
    return list;
}

QPixmap* BackgroundManager::preview(const QString &image)
{
    static const int    MAX_WIDTH  = 100;
    static const int    MAX_HEIGHT = 75;
    static const QColor PREVIEW_BG = Qt::white;

    BackgroundEntry *entry = backgroundEntryFor(image);

    if (!entry) {
///     kDebug() << "BackgroundManager: Requested the preview of an unexisting image: " << image;
        return false;
    }

    // The easiest way: already computed:
    if (entry->preview)
        return entry->preview;

    // Then, try to load the preview from file:
    QString previewPath = KGlobal::dirs()->findResource("data", "basket/backgrounds/previews/" + entry->name);
    QPixmap *previewPixmap = new QPixmap(previewPath);
    // Success:
    if (!previewPixmap->isNull()) {
///     kDebug() << "BackgroundManager: Loaded image preview for " << entry->location << " from file " << previewPath;
        entry->preview = previewPixmap;
        return entry->preview;
    }

    // We failed? Then construct it:
    // Note: if a preview is requested, it's because the user is currently choosing an image.
    // Since we need that image to create the preview, we keep the image in memory.
    // Then, it will already be loaded when user press [OK] in the background image chooser.
    // BUT we also delay a garbage because we don't want EVERY images to be loaded if the user use only a few of them, of course:

    // Already used? Good: we don't have to load it...
    if (!entry->pixmap) {
        // Note: it's a code duplication from BackgroundManager::subscribe(const QString &image),
        // Because, as we are loading the pixmap we ALSO need to know if it's a tile or not, in case that image will soon be used (and not destroyed by the garbager):
        entry->pixmap = new QPixmap(entry->location);
        // Try to figure out if it's a tiled background image or not (default to NO):
        KConfig config(entry->location + ".config");
        KConfigGroup configGroup = config.group("BasKet Background Image Configuration");
        entry->tiled = configGroup.readEntry("tiled", false);
    }

    // The image cannot be loaded, we failed:
    if (entry->pixmap->isNull())
        return 0;

    // Good that we are still alive: entry->pixmap contains the pixmap to rescale down for the preview:
    // Compute new size:
    int width  = entry->pixmap->width();
    int height = entry->pixmap->height();
    if (width > MAX_WIDTH) {
        height = height * MAX_WIDTH / width;
        width  = MAX_WIDTH;
    }
    if (height > MAX_HEIGHT) {
        width  = width * MAX_HEIGHT / height;
        height = MAX_HEIGHT;
    }
    // And create the resulting pixmap:
    QPixmap *result = new QPixmap(width, height);
    result->fill(PREVIEW_BG);
    QImage imageToScale = entry->pixmap->toImage();
    QPixmap pmScaled = QPixmap::fromImage(imageToScale.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    QPainter painter(result);
    painter.drawPixmap(0, 0, pmScaled);
    painter.end();

    // Saving it to file for later:
    QString folder = KGlobal::dirs()->saveLocation("data", "basket/backgrounds/previews/");
    result->save(folder + entry->name, "PNG");

    // Ouf! That's done:
    entry->preview = result;
    requestDelayedGarbage();
    return entry->preview;
}

QString BackgroundManager::pathForImageName(const QString &image)
{
    BackgroundEntry *entry = backgroundEntryFor(image);
    if (entry == 0)
        return "";
    else
        return entry->location;
}

QString BackgroundManager::previewPathForImageName(const QString &image)
{
    BackgroundEntry *entry = backgroundEntryFor(image);
    if (entry == 0)
        return "";
    else {
        QString previewPath = KGlobal::dirs()->findResource("data", "basket/backgrounds/previews/" + entry->name);
        QDir dir;
        if (!dir.exists(previewPath))
            return "";
        else
            return previewPath;
    }
}

void BackgroundManager::requestDelayedGarbage()
{
    static const int DELAY = 60/*seconds*/;

    if (!m_garbageTimer.isActive()) {
        m_garbageTimer.setSingleShot(true);
        m_garbageTimer.start(DELAY * 1000/*ms*/);
    }
}

void BackgroundManager::doGarbage()
{
/// kDebug() << "BackgroundManager: Doing garbage...";

/// kDebug() << "BackgroundManager: Images:";
    for (BackgroundsList::Iterator it = m_backgroundsList.begin(); it != m_backgroundsList.end(); ++it) {
        BackgroundEntry *entry = *it;
///     kDebug() << "* " << entry->name << ": used " << entry->customersCount << " times";
        if (entry->customersCount <= 0 && entry->pixmap) {
///         kDebug() << " [Deleted cached pixmap]";
            delete entry->pixmap;
            entry->pixmap = 0;
        }
///     kDebug();
    }

/// kDebug() << "BackgroundManager: Opaque Cached Images:";
    for (OpaqueBackgroundsList::Iterator it = m_opaqueBackgroundsList.begin(); it != m_opaqueBackgroundsList.end();) {
        OpaqueBackgroundEntry *entry = *it;
///     kDebug() << "* " << entry->name << "," << entry->color.name() << ": used " << entry->customersCount << " times";
        if (entry->customersCount <= 0) {
///         kDebug() << " [Deleted entry]";
            delete entry->pixmap;
            entry->pixmap = 0;
            it = m_opaqueBackgroundsList.erase(it);
        } else
            ++it;
///     kDebug();
    }
}

