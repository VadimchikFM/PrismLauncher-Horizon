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

#include <QHash>

#include "ModFolderModel.h"
#include "modplatform/ResourceRemoteInfo.h"

/**
 * The mods of an instance launched with Horizon (`horizon-mods/`, see HorizonMods), on its Mods page.
 *
 * They are the launcher's resources as any mod is: turned on and off by renaming, with metadata of where they came
 * from. Horizon also has a switch of its own for each mod, `mod.<id>` in `horizon-mods.txt`, without which the game
 * loads none: this model keeps it with the launcher's, writing it when a mod is turned on or off here and when a mod
 * the switches don't know yet arrives (a switch the game's own Mods screen set is left as it is).
 */
class HorizonModFolderModel : public ModFolderModel {
    Q_OBJECT

   public:
    /// After the columns of any mod list: whether the game is built with the mod
    enum HorizonColumns { BuildColumn = ModFolderModel::NUM_COLUMNS, NUM_HORIZON_COLUMNS };

    /// Where a mod stands with the game built last (Build Horizon on the Mods page builds it).
    enum class BuildState {
        /// A content mod: the game loads it as it is
        NotNeeded,
        /// A native mod in the game built last, as it is now
        Built,
        /// A native mod in the game built last, but another version or another file than this
        Changed,
        /// A native mod that is on, but not in the game built last (or no game is built)
        NotBuilt,
        /// A native mod that is off: the next build leaves it out
        Off,
    };

    HorizonModFolderModel(const QDir& dir, BaseInstance* instance, bool is_indexed, bool create_dir, QObject* parent = nullptr);

    QString id() const override { return "horizonmods"; }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    int columnCount(const QModelIndex& parent) const override;

    bool setResourceEnabled(const QModelIndexList& indexes, EnableAction action) override;

    /// Reads again what the game was built with, after a build.
    void refreshBuildStates();

    /// The profile a build was made with, as the build dialog names it.
    static QString profileName(const QString& profile);

   private:
    struct Build {
        BuildState state = BuildState::NotNeeded;
        /// The version of the mod the game was built with, when another
        QString builtVersion;
    };

    /// Turns on the mods the switches have no line for yet: the ones just installed.
    void addMissingSwitches();
    QString gameDir() const;
    /// The project of a mod on Horizon's registry: its metadata's, or else its own id.
    static std::optional<ResourceRemoteInfo::Project> projectOf(const Resource& resource);

    /// By the mods' internal ids
    QHash<QString, Build> m_builds;
    /// The profile and the version of the game built last, when there is one
    QString m_builtProfile;
    QString m_builtVersion;
};
