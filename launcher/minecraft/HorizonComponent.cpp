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

#include "HorizonComponent.h"

#include "minecraft/HorizonVersionList.h"
#include "minecraft/PackProfile.h"

namespace HorizonComponent {

QString version(PackProfile* profile)
{
    if (profile == nullptr) {
        return {};
    }
    auto component = profile->getComponent(Uid);
    if (!component || !component->isEnabled()) {
        return {};
    }
    const auto tag = component->getVersion();
    return tag.isEmpty() ? HorizonVersion::Latest : tag;
}

bool install(PackProfile* profile, const QString& version)
{
    if (profile == nullptr) {
        return false;
    }
    const auto tag = version.isEmpty() || !HorizonVersion::isValidTag(version) ? HorizonVersion::Latest : version;
    // Its file says the version: another version is another file
    if (profile->getComponent(Uid) && !profile->remove(Uid)) {
        return false;
    }
    return profile->installEmpty(Uid, Name, tag);
}

}  // namespace HorizonComponent
