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

#include "HorizonResourcePages.h"
#include "ui_ResourcePage.h"

#include "minecraft/MinecraftInstance.h"
#include "modplatform/horizon/HorizonAPI.h"
#include "ui/dialogs/ResourceDownloadDialog.h"

namespace ResourceDownload {

HorizonModPage::HorizonModPage(ModDownloadDialog* dialog, BaseInstance& instance) : ModPage(dialog, instance)
{
    m_model = new ModModel(instance, new HorizonAPI(), Horizon::debugName(), Horizon::metaEntryBase());
    m_ui->packView->setModel(m_model);

    addSortings();

    // As the other platforms' pages do: connected here rather than in the parent's constructor
    connect(m_ui->sortByBox, &QComboBox::currentIndexChanged, this, &HorizonModPage::triggerSearch);
    connect(m_ui->packView->selectionModel(), &QItemSelectionModel::currentChanged, this, &HorizonModPage::onSelectionChanged);
    connect(m_ui->versionSelectionBox, &QComboBox::currentIndexChanged, this, &HorizonModPage::onVersionSelectionChanged);
    connect(m_ui->resourceSelectionButton, &QPushButton::clicked, this, &HorizonModPage::onResourceSelected);

    m_ui->packDescription->setMetaEntry(metaEntryBase());
}

HorizonResourcePackPage::HorizonResourcePackPage(ResourcePackDownloadDialog* dialog, BaseInstance& instance)
    : ResourcePackResourcePage(dialog, instance)
{
    m_model = new ResourcePackResourceModel(instance, new HorizonAPI(), Horizon::debugName(), Horizon::metaEntryBase());
    m_ui->packView->setModel(m_model);

    addSortings();

    connect(m_ui->sortByBox, &QComboBox::currentIndexChanged, this, &HorizonResourcePackPage::triggerSearch);
    connect(m_ui->packView->selectionModel(), &QItemSelectionModel::currentChanged, this, &HorizonResourcePackPage::onSelectionChanged);
    connect(m_ui->versionSelectionBox, &QComboBox::currentIndexChanged, this, &HorizonResourcePackPage::onVersionSelectionChanged);
    connect(m_ui->resourceSelectionButton, &QPushButton::clicked, this, &HorizonResourcePackPage::onResourceSelected);

    m_ui->packDescription->setMetaEntry(metaEntryBase());
}

HorizonShaderPackPage::HorizonShaderPackPage(ShaderPackDownloadDialog* dialog, BaseInstance& instance)
    : ShaderPackResourcePage(dialog, instance)
{
    m_model = new ShaderPackResourceModel(instance, new HorizonAPI(), Horizon::debugName(), Horizon::metaEntryBase());
    m_ui->packView->setModel(m_model);

    addSortings();

    connect(m_ui->sortByBox, &QComboBox::currentIndexChanged, this, &HorizonShaderPackPage::triggerSearch);
    connect(m_ui->packView->selectionModel(), &QItemSelectionModel::currentChanged, this, &HorizonShaderPackPage::onSelectionChanged);
    connect(m_ui->versionSelectionBox, &QComboBox::currentIndexChanged, this, &HorizonShaderPackPage::onVersionSelectionChanged);
    connect(m_ui->resourceSelectionButton, &QPushButton::clicked, this, &HorizonShaderPackPage::onResourceSelected);

    m_ui->packDescription->setMetaEntry(metaEntryBase());
}

// As on the other platforms' pages: implemented in each class, so that a page loads only once it is selected
auto HorizonModPage::shouldDisplay() const -> bool
{
    return true;
}
auto HorizonResourcePackPage::shouldDisplay() const -> bool
{
    return true;
}
auto HorizonShaderPackPage::shouldDisplay() const -> bool
{
    return true;
}

std::unique_ptr<ModFilterWidget> HorizonModPage::createFilterWidget()
{
    auto widget = ModFilterWidget::create(&static_cast<MinecraftInstance&>(m_baseInstance), true);
    // A Horizon mod runs on Horizon, on both sides: there is no loader or side to choose
    widget->hideLoaderAndSideFilters();
    return widget;
}

void HorizonModPage::prepareProviderCategories()
{
    m_categoriesTask = HorizonAPI::withIndex([this](const QJsonObject& index) {
        m_filter_widget->setCategories(HorizonAPI::categories(index, ModPlatform::ResourceType::Mod));
    });
    m_categoriesTask->start();
}

}  // namespace ResourceDownload
