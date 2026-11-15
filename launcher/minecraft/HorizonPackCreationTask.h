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

#include "InstanceCreationTask.h"

class HorizonBackend;

/**
 * Makes an instance out of a modpack of Horizon's registry: an instance of the pack's Minecraft version that is
 * launched with Horizon, with the pack's mods in its game folder.
 *
 * The mods are installed by Horizon's own launcher (`mods install <pack>`, see HorizonBackend), which reads the
 * pack's `horizon-modpack.toml`, downloads each mod it lists from the registry with its checks, turns them on and
 * copies the pack's `overrides/`. It does so into a game folder of the pack's own (`horizon-packs/<id>/home`), whose
 * contents then become the new instance's game folder.
 */
class HorizonPackCreationTask final : public InstanceCreationTask {
    Q_OBJECT
   public:
    /// @param pack the pack's id in the registry
    /// @param minecraft the Minecraft version the pack is for
    HorizonPackCreationTask(QString pack, QString minecraft) : m_pack(std::move(pack)), m_minecraft(std::move(minecraft)) {}

    bool abort() override;

   protected:
    std::unique_ptr<MinecraftInstance> createInstance() override;

   private:
    QString m_pack;
    QString m_minecraft;
    /// Set while the pack's mods are being installed.
    HorizonBackend* m_backend = nullptr;
};
