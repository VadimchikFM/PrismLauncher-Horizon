// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QWidget>

#include "net/NetJob.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTextBrowser;
class QVBoxLayout;

/**
 * The modpacks of Horizon's registry (https://playhorizon.dev/mods), for the New Instance dialog: a search field,
 * filters (categories, tags, the Minecraft version), an order, a list with icons, and a page for the selected modpack
 * with the mods it installs and every version.
 *
 * What it shows is the registry's index, as HorizonAPI reads it. It installs nothing: HorizonPackPage does, with the
 * selected modpack.
 */
class HorizonRegistryBrowser : public QWidget {
    Q_OBJECT
   public:
    /// A modpack of the index.
    struct Project {
        QString id;
        QString name;
        QString summary;
        QString description;
        QString license;
        QString source;
        QString icon;
        QStringList authors;
        QStringList categories;
        QStringList tags;
        qint64 downloads = 0;
        int stars = 0;
        /// Newest first, as the index lists them.
        QJsonArray versions;
        // Of its newest version:
        QString version;
        QString published;
        QString game;
        /// How many mods it installs.
        int mods = 0;
    };

    explicit HorizonRegistryBrowser(QWidget* parent = nullptr);
    ~HorizonRegistryBrowser() override = default;

    /// The modpack of the selected row, if a row is selected.
    const Project* selected() const;
    /// Where a modpack's icon is kept once it was fetched. The file is there only then.
    QString iconPath(const Project& project) const;

    /// Reads the index again.
    void reload();
    /// What the line under the list says.
    void setStatus(const QString& text);

   signals:
    void selectionChanged();
    /// A row was double-clicked.
    void activated();

   private:
    void indexLoaded(const QJsonObject& index);
    void buildFilters();
    void applyFilters();
    void fetchIcons();
    void showSelected();
    /// One line on what a modpack's newest version is.
    QString madeFor(const Project& project) const;

   private:
    QList<Project> m_projects;
    /// Every project's name by its id: what a modpack's mods are called
    QMap<QString, QString> m_names;

    Task::Ptr m_indexJob;
    NetJob::Ptr m_iconJob;

    QLineEdit* m_query;
    QCheckBox* m_onlyGame;
    QVBoxLayout* m_categoryLayout;
    QVBoxLayout* m_tagLayout;
    QList<QCheckBox*> m_categoryBoxes;
    QList<QCheckBox*> m_tagBoxes;
    QListWidget* m_list;
    QTextBrowser* m_details;
    QComboBox* m_sort;
    QLabel* m_status;
};
