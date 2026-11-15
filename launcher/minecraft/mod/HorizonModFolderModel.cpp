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

#include "HorizonModFolderModel.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QMap>

#include "Application.h"
#include "BaseInstance.h"
#include "minecraft/HorizonBackend.h"
#include "minecraft/HorizonMods.h"
#include "settings/SettingsObject.h"

namespace {

/// The SHA-256 of a mod's file, as the build records it; read again only when the file changes
QString sha256Of(const QFileInfo& file)
{
    static QHash<QString, QString> s_known;
    const auto key = QString("%1|%2|%3").arg(file.absoluteFilePath()).arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch());
    if (const auto known = s_known.constFind(key); known != s_known.constEnd()) {
        return *known;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QFile contents(file.absoluteFilePath());
    if (contents.open(QIODevice::ReadOnly)) {
        hash.addData(&contents);
    }
    const auto result = QString::fromLatin1(hash.result().toHex());
    s_known.insert(key, result);
    return result;
}

bool isNative(const Mod& mod)
{
    const auto file = mod.fileinfo().fileName();
    return file.endsWith(".hnmod") || file.endsWith(".hnmod.disabled");
}

}  // namespace

HorizonModFolderModel::HorizonModFolderModel(const QDir& dir, BaseInstance* instance, bool is_indexed, bool create_dir, QObject* parent)
    : ModFolderModel(dir, instance, is_indexed, create_dir, parent)
{
    m_column_names.append("Build");
    m_column_names_translated.append(tr("Build"));
    m_column_sort_keys.append(SortType::NAME);
    m_column_resize_modes.append(QHeaderView::Interactive);
    // Always shown: it is what tells a native mod the game has from one it hasn't yet
    m_columnsHideable.append(false);

    // What Horizon's own launcher installed beside: native mods in their old folder, and its record of where it all came from
    HorizonMods::adoptNativeMods(gameDir());

    connect(this, &ResourceFolderModel::parseFinished, this, &HorizonModFolderModel::addMissingSwitches);
    connect(this, &ResourceFolderModel::updateFinished, this, &HorizonModFolderModel::refreshBuildStates);
    connect(this, &ResourceFolderModel::parseFinished, this, &HorizonModFolderModel::refreshBuildStates);

    // A native mod's files hold only its id and version: its summary, authors and icon are the registry's
    new ResourceRemoteInfo(
        this,
        [](Resource& resource, const ResourceRemoteInfo::Info& info) {
            auto& mod = static_cast<Mod&>(resource);
            auto details = mod.details();
            bool changed = false;
            const auto fill = [&changed](auto& field, const auto& value) {
                if (field.isEmpty() && !value.isEmpty()) {
                    field = value;
                    changed = true;
                }
            };
            fill(details.name, info.name);
            fill(details.description, info.summary);
            fill(details.authors, info.authors);
            fill(details.homeurl, info.homepage);
            if (details.licenses.isEmpty() && !info.license.isEmpty()) {
                details.licenses.append(ModLicense(info.license));
                changed = true;
            }
            if (changed) {
                mod.setDetails(details);
            }
        },
        [](Resource& resource, const QImage&, const QString& path) {
            // The icon the mod's own files have comes first; one kept beside it is read from there when shown
            auto& mod = static_cast<Mod&>(resource);
            if (mod.iconPath().isEmpty()) {
                auto details = mod.details();
                details.icon_file = path;
                mod.setDetails(details);
            }
        },
        &HorizonModFolderModel::projectOf);
}

std::optional<ResourceRemoteInfo::Project> HorizonModFolderModel::projectOf(const Resource& resource)
{
    if (auto project = ResourceRemoteInfo::fromMetadata(resource)) {
        return project;
    }
    // One put here by hand: the registry knows it by its id, if it is the registry's
    if (const auto id = static_cast<const Mod&>(resource).mod_id(); !id.isEmpty()) {
        return ResourceRemoteInfo::Project{ ModPlatform::ResourceProvider::HORIZON, id };
    }
    return std::nullopt;
}

QString HorizonModFolderModel::profileName(const QString& profile)
{
    if (profile == "performance") {
        return tr("Performance");
    }
    if (profile == "dev") {
        return tr("Dev");
    }
    if (profile == "release") {
        return tr("Release");
    }
    return profile;
}

void HorizonModFolderModel::refreshBuildStates()
{
    // What the game built last was built with: "<profile>|<id>=<version>=<sha256>;..." (HorizonBuildDialog)
    QMap<QString, std::pair<QString, QString>> built;
    m_builtProfile.clear();
    m_builtVersion.clear();
    if (APPLICATION) {
        auto settings = instance()->settings();
        const auto version = settings->get("HorizonNativeVersion").toString();
        if (!version.isEmpty() && HorizonBackend::isVersionInstalled(version)) {
            const auto recorded = settings->get("HorizonNativeSignature").toString();
            const auto bar = recorded.indexOf('|');
            m_builtProfile = bar < 0 ? QString() : recorded.left(bar);
            m_builtVersion = version;
            for (const auto& part : recorded.mid(bar + 1).split(';', Qt::SkipEmptyParts)) {
                const auto fields = part.split('=');
                if (fields.size() == 3) {
                    built.insert(fields.at(0), { fields.at(1), fields.at(2) });
                }
            }
        }
    }

    m_builds.clear();
    for (auto* mod : allMods()) {
        Build build;
        if (!isNative(*mod)) {
            build.state = BuildState::NotNeeded;
        } else if (!mod->enabled()) {
            build.state = BuildState::Off;
        } else if (const auto found = built.constFind(mod->mod_id()); found == built.constEnd()) {
            build.state = BuildState::NotBuilt;
        } else if (found->first == mod->version() && found->second == sha256Of(mod->fileinfo())) {
            build.state = BuildState::Built;
        } else {
            build.state = BuildState::Changed;
            build.builtVersion = found->first;
        }
        m_builds.insert(mod->internal_id(), build);
    }

    const auto rows = rowCount({});
    if (rows > 0) {
        emit dataChanged(index(0, BuildColumn), index(rows - 1, BuildColumn));
    }
}

QVariant HorizonModFolderModel::data(const QModelIndex& index, int role) const
{
    if (index.column() != BuildColumn) {
        return ModFolderModel::data(index, role);
    }
    if (!validateIndex(index)) {
        return {};
    }
    if (role == Qt::BackgroundRole) {
        return rowBackground(index.row());
    }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return {};
    }

    const auto& mod = at(index.row());
    const auto build = m_builds.value(mod.internal_id(), Build{ isNative(mod) ? BuildState::NotBuilt : BuildState::NotNeeded, {} });
    const auto profile = profileName(m_builtProfile);
    const bool tooltip = role == Qt::ToolTipRole;
    switch (build.state) {
        case BuildState::NotNeeded:
            return tooltip ? tr("A content mod: the game loads it as it is, without a build.") : tr("Not needed");
        case BuildState::Built:
            if (tooltip) {
                return tr("In the game built with the %1 profile (%2).").arg(profile, m_builtVersion);
            }
            return profile.isEmpty() ? tr("Built") : tr("Built (%1)").arg(profile);
        case BuildState::Changed:
            if (tooltip) {
                return tr("The game was built with version %1 of this mod (%2 profile). Build Horizon again to play this one.")
                    .arg(build.builtVersion, profile);
            }
            return tr("Needs a rebuild");
        case BuildState::NotBuilt:
            return tooltip ? tr("A native mod does nothing until the game is built with it: use Build Horizon.") : tr("Not built");
        case BuildState::Off:
            return tooltip ? tr("Turned off: the next build leaves it out.") : tr("Off");
    }
    return {};
}

QVariant HorizonModFolderModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (section != BuildColumn) {
        return ModFolderModel::headerData(section, orientation, role);
    }
    switch (role) {
        case Qt::DisplayRole:
            return columnNames().at(section);
        case Qt::ToolTipRole:
            return tr("Whether the game is built with the mod, and with which profile. Native mods run only in a game built with "
                      "them; content mods need no build.");
        default:
            return {};
    }
}

int HorizonModFolderModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : NUM_HORIZON_COLUMNS;
}

QString HorizonModFolderModel::gameDir() const
{
    // horizon-mods is in the game folder
    auto game = dir();
    game.cdUp();
    return game.absolutePath();
}

bool HorizonModFolderModel::setResourceEnabled(const QModelIndexList& indexes, EnableAction action)
{
    // The launcher's own switch first: it may turn other mods on or off with these (their dependencies)
    QMap<Mod*, bool> before;
    for (auto* mod : allMods()) {
        before[mod] = mod->enabled();
    }
    const bool succeeded = ModFolderModel::setResourceEnabled(indexes, action);

    // Then Horizon's, for each mod that changed
    for (auto* mod : allMods()) {
        if (before.contains(mod) && before.value(mod) != mod->enabled() && !mod->mod_id().isEmpty()) {
            HorizonMods::setEnabled(gameDir(), mod->mod_id(), mod->enabled());
        }
    }
    refreshBuildStates();
    return succeeded;
}

void HorizonModFolderModel::addMissingSwitches()
{
    const auto game = gameDir();
    for (auto* mod : allMods()) {
        const auto id = mod->mod_id();
        if (!id.isEmpty() && !HorizonMods::switchState(game, id).has_value()) {
            HorizonMods::setEnabled(game, id, mod->enabled());
        }
    }
}
