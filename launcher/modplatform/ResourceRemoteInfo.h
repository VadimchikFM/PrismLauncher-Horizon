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

#include <QImage>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <functional>
#include <optional>

#include "modplatform/ModIndex.h"
#include "net/NetJob.h"

class Resource;
class ResourceFolderModel;

/**
 * What the platform an installed resource came from says of it (its summary, authors, licence and icon), for the
 * resources whose own files say little: shader packs, which hold no description or icon, and Horizon's native mods,
 * which hold only their id and version.
 *
 * A model's resources are looked up by their metadata: the ones installed from Horizon's registry, Modrinth or
 * CurseForge. Each project is asked for once per run of the launcher, a platform's all at once. Icons are kept in the
 * launcher's cache, beside the same icons of the download pages.
 */
class ResourceRemoteInfo : public QObject {
    Q_OBJECT

   public:
    struct Info {
        QString name;
        QString summary;
        QStringList authors;
        QString homepage;
        QString license;
        QUrl icon;
    };
    struct Project {
        ModPlatform::ResourceProvider provider;
        QString id;
    };

    /// Fills in what a resource's own files leave out.
    using ApplyInfo = std::function<void(Resource&, const Info&)>;
    /// Gives a resource the icon of its project, kept in the file `path`.
    using ApplyIcon = std::function<void(Resource&, const QImage&, const QString& path)>;
    /// Which project a resource is.
    using ProjectOf = std::function<std::optional<Project>(const Resource&)>;

    ResourceRemoteInfo(ResourceFolderModel* model, ApplyInfo applyInfo, ApplyIcon applyIcon, ProjectOf projectOf = fromMetadata);

    /// The project of a resource's metadata, if it has metadata of a platform this knows.
    static std::optional<Project> fromMetadata(const Resource& resource);

   private:
    /// Once the model is done with the current changes
    void schedule();
    /// Gives each resource what is known of its project, and asks for what isn't.
    void apply();
    void fetchHorizon(const QStringList& ids);
    void fetchPlatform(ModPlatform::ResourceProvider provider, const QStringList& ids);
    void fetchIcons(const QList<std::pair<QUrl, QString>>& icons);
    void refresh();
    /// Every model's: what one asked for reaches the others too
    static void scheduleAll();

    ResourceFolderModel* m_model;
    ApplyInfo m_applyInfo;
    ApplyIcon m_applyIcon;
    ProjectOf m_projectOf;
    bool m_scheduled = false;
};
