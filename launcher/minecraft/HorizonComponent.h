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

#include <QString>

class PackProfile;

/**
 * Horizon (https://github.com/horizon-rust/horizon), Minecraft rewritten in Rust, as a component of an instance, as a
 * mod loader is: it shows on the Version page with the version it plays, which "Change version" picks from Horizon's
 * releases, and removing it makes the instance a Java one again.
 *
 * It is a component of the instance's own (`patches/dev.playhorizon.horizon.json`): Horizon has no entry in the
 * launcher's metadata, and adds nothing to the Java launch. Its version is a release's tag, or "latest" for the newest
 * Nightly, which the launcher keeps up to date.
 */
namespace HorizonComponent {

inline const QString Uid = QStringLiteral("dev.playhorizon.horizon");
inline const QString Name = QStringLiteral("Horizon");

/// The Horizon `profile` plays: a release's tag or "latest"; empty when it plays none (or the component is off).
QString version(PackProfile* profile);

/// Makes `profile` play Horizon `version` (a release's tag; empty or "latest" for the newest Nightly).
bool install(PackProfile* profile, const QString& version);

}  // namespace HorizonComponent
