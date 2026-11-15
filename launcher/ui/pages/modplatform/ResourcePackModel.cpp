// SPDX-FileCopyrightText: 2023 flowln <flowlnlnln@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "ResourcePackModel.h"

#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/ResourcePackFolderModel.h"

#include <QMessageBox>
#include <utility>

namespace ResourceDownload {

ResourcePackResourceModel::ResourcePackResourceModel(const BaseInstance& base_inst,
                                                     ResourceAPI* api,
                                                     const QString& debugName,
                                                     QString metaEntryBase)
    : ResourceModel(api), m_base_instance(base_inst), m_debugName(debugName + " (Model)"), m_metaEntryBase(std::move(metaEntryBase))
{}

/******** Make data requests ********/

ResourceAPI::SearchArgs ResourcePackResourceModel::createSearchArguments()
{
    auto sort = getCurrentSortingMethodByIndex();
    return {
        .type = ModPlatform::ResourceType::ResourcePack,
        .offset = m_next_search_offset,
        .search = m_search_term,
        .sorting = sort,
        .loaders = {},
        .versions = {},
        .side = {},
        .categoryIds = {},
        .openSource = {},
    };
}

ResourceAPI::VersionSearchArgs ResourcePackResourceModel::createVersionsArguments(const QModelIndex& entry)
{
    auto pack = m_packs[entry.row()];
    return { .pack = pack, .mcVersions = {}, .loaders = {}, .resourceType = ModPlatform::ResourceType::ResourcePack };
}

ResourceAPI::ProjectInfoArgs ResourcePackResourceModel::createInfoArguments(const QModelIndex& entry)
{
    auto pack = m_packs[entry.row()];
    return { .pack = pack };
}

void ResourcePackResourceModel::searchWithTerm(const QString& term, unsigned int sort)
{
    if (m_search_term == term && m_search_term.isNull() == term.isNull() && m_current_sort_index == sort) {
        return;
    }

    setSearchTerm(term);
    m_current_sort_index = sort;

    refresh();
}


/// The installed resource of `pack`, by the metadata kept with it: what it was downloaded from.
static Resource* installedResource(const BaseInstance& instance, ModPlatform::IndexedPack::Ptr pack)
{
    auto* minecraft = dynamic_cast<MinecraftInstance*>(const_cast<BaseInstance*>(&instance));
    if (minecraft == nullptr || minecraft->resourcePackList() == nullptr) {
        return nullptr;
    }
    for (auto* resource : minecraft->resourcePackList()->allResources()) {
        if (auto meta = resource->metadata(); meta && meta->provider == pack->provider && meta->project_id == pack->addonId) {
            return resource;
        }
    }
    return nullptr;
}

bool ResourcePackResourceModel::isPackInstalled(ModPlatform::IndexedPack::Ptr pack) const
{
    return installedResource(m_base_instance, pack) != nullptr;
}

QVariant ResourcePackResourceModel::getInstalledPackVersion(ModPlatform::IndexedPack::Ptr pack) const
{
    if (auto* resource = installedResource(m_base_instance, pack)) {
        return resource->metadata()->version();
    }
    return {};
}

}  // namespace ResourceDownload
