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

#include <QJsonObject>
#include <functional>

#include "modplatform/ResourceAPI.h"

/**
 * Horizon's registry (https://playhorizon.dev/mods) as a resource platform, beside Modrinth and CurseForge: the mods,
 * resource packs, shader packs and modpacks made for Horizon, Minecraft rewritten in Rust.
 *
 * The registry is one file, its index (`index.json`, docs/api/mod-index.md in horizon-rust/horizon), which lists every
 * project with every version: searching, a project's page and its versions are all read from it. It is downloaded
 * once and kept for a few minutes, so browsing makes no further requests.
 */
class HorizonAPI : public ResourceAPI {
   public:
    static inline const QString IndexUrl = QStringLiteral("https://horizon-rust.github.io/horizon-mods/index.json");

    enum Sorting : unsigned int { ByRelevance, ByDownloads, ByUpdate, ByName };

    auto getSortingMethods() const -> QList<SortingMethod> override;

    Task::Ptr searchProjects(SearchArgs&&, Callback<QList<ModPlatform::IndexedPack::Ptr>>&&) const override;
    std::pair<Task::Ptr, QByteArray*> getProject(QString addonId, bool askRetry = true) const override;
    std::pair<Task::Ptr, QByteArray*> getProjects(QStringList addonIds) const override;
    Task::Ptr getProjectInfo(ProjectInfoArgs&&, Callback<ModPlatform::IndexedPack::Ptr>&&, bool askRetry = true) const override;
    Task::Ptr getProjectVersions(VersionSearchArgs&&, Callback<QVector<ModPlatform::IndexedVersion>>&&) const override;
    Task::Ptr getDependencyVersion(DependencySearchArgs&&, Callback<ModPlatform::IndexedVersion>&&) const override;

    auto getSearchURL(const SearchArgs&) const -> std::optional<QString> override { return IndexUrl; }
    auto getInfoURL(const QString&) const -> std::optional<QString> override { return IndexUrl; }
    auto getVersionsURL(const VersionSearchArgs&) const -> std::optional<QString> override { return IndexUrl; }
    auto getDependencyURL(const DependencySearchArgs&) const -> std::optional<QString> override { return std::nullopt; }

    void loadIndexedPack(ModPlatform::IndexedPack&, QJsonObject&) const override;
    ModPlatform::IndexedVersion loadIndexedPackVersion(QJsonObject&, ModPlatform::ResourceType) const override;
    QJsonArray documentToArray(QJsonDocument&) const override;
    void loadExtraPackInfo(ModPlatform::IndexedPack&, QJsonObject&) const override;

    /**
     * A task that has the registry's index ready, then calls `ready` with it (before it succeeds). The index is read
     * again only when the one in hand is a few minutes old.
     */
    static Task::Ptr withIndex(std::function<void(const QJsonObject&)> ready, std::function<void(const QString&)> failed = {});
    /// The index of the last read, if it is not too old; else an empty object.
    static QJsonObject cachedIndex();
    /// The index of the last read however old, from this run of the launcher or an earlier one; empty when there was none.
    static QJsonObject knownIndex();
    /// Where the index of the last read is kept.
    static QString indexCachePath();

    /// The project of the index whose id is `id`; an empty object when it has none.
    static QJsonObject findProject(const QJsonObject& index, const QString& id);
    /// A project of the index, with its versions and its page, for the launcher's resource pages.
    static ModPlatform::IndexedPack::Ptr packFrom(const QJsonObject& project);
    /// The categories the index's projects of `type` use, the most used first.
    static QList<ModPlatform::Category> categories(const QJsonObject& index, ModPlatform::ResourceType type);
    /// Whether a project of the index is of the kind `type` names.
    static bool isOfType(const QJsonObject& project, ModPlatform::ResourceType type);
};
