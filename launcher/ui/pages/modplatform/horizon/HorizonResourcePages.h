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

#include "ui/pages/modplatform/ModPage.h"
#include "ui/pages/modplatform/ResourcePackPage.h"
#include "ui/pages/modplatform/ShaderPackPage.h"

/**
 * Horizon's registry (HorizonAPI) in the launcher's download dialogs, beside Modrinth and CurseForge: its mods for an
 * instance launched with Horizon, and its resource packs and shader packs.
 */
namespace ResourceDownload {

namespace Horizon {
static inline QString displayName()
{
    return "Horizon";
}
static inline QIcon icon()
{
    return QIcon::fromTheme("horizon");
}
static inline QString id()
{
    return "horizon";
}
static inline QString debugName()
{
    return "Horizon";
}
static inline QString metaEntryBase()
{
    return "HorizonPacks";
}
}  // namespace Horizon

class HorizonModPage : public ModPage {
    Q_OBJECT

   public:
    static HorizonModPage* create(ModDownloadDialog* dialog, BaseInstance& instance)
    {
        return ModPage::create<HorizonModPage>(dialog, instance);
    }

    HorizonModPage(ModDownloadDialog* dialog, BaseInstance& instance);
    ~HorizonModPage() override = default;

    bool shouldDisplay() const override;

    inline auto displayName() const -> QString override { return Horizon::displayName(); }
    inline auto icon() const -> QIcon override { return Horizon::icon(); }
    inline auto id() const -> QString override { return Horizon::id(); }
    inline auto debugName() const -> QString override { return Horizon::debugName(); }
    inline auto metaEntryBase() const -> QString override { return Horizon::metaEntryBase(); }

    inline auto helpPage() const -> QString override { return ""; }

    std::unique_ptr<ModFilterWidget> createFilterWidget() override;

   protected:
    void prepareProviderCategories() override;
    Task::Ptr m_categoriesTask;
};

class HorizonResourcePackPage : public ResourcePackResourcePage {
    Q_OBJECT

   public:
    static HorizonResourcePackPage* create(ResourcePackDownloadDialog* dialog, BaseInstance& instance)
    {
        return ResourcePackResourcePage::create<HorizonResourcePackPage>(dialog, instance);
    }

    HorizonResourcePackPage(ResourcePackDownloadDialog* dialog, BaseInstance& instance);
    ~HorizonResourcePackPage() override = default;

    bool shouldDisplay() const override;

    inline auto displayName() const -> QString override { return Horizon::displayName(); }
    inline auto icon() const -> QIcon override { return Horizon::icon(); }
    inline auto id() const -> QString override { return Horizon::id(); }
    inline auto debugName() const -> QString override { return Horizon::debugName(); }
    inline auto metaEntryBase() const -> QString override { return Horizon::metaEntryBase(); }

    inline auto helpPage() const -> QString override { return ""; }
};

class HorizonShaderPackPage : public ShaderPackResourcePage {
    Q_OBJECT

   public:
    static HorizonShaderPackPage* create(ShaderPackDownloadDialog* dialog, BaseInstance& instance)
    {
        return ShaderPackResourcePage::create<HorizonShaderPackPage>(dialog, instance);
    }

    HorizonShaderPackPage(ShaderPackDownloadDialog* dialog, BaseInstance& instance);
    ~HorizonShaderPackPage() override = default;

    bool shouldDisplay() const override;

    inline auto displayName() const -> QString override { return Horizon::displayName(); }
    inline auto icon() const -> QIcon override { return Horizon::icon(); }
    inline auto id() const -> QString override { return Horizon::id(); }
    inline auto debugName() const -> QString override { return Horizon::debugName(); }
    inline auto metaEntryBase() const -> QString override { return Horizon::metaEntryBase(); }

    inline auto helpPage() const -> QString override { return ""; }
};

}  // namespace ResourceDownload
