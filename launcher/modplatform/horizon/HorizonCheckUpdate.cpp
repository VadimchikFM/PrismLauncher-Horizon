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

#include "HorizonCheckUpdate.h"

#include <QSet>

#include "ResourceDownloadTask.h"
#include "minecraft/mod/Resource.h"
#include "modplatform/horizon/HorizonAPI.h"

bool HorizonCheckUpdate::abort()
{
    if (m_job && m_job->isRunning()) {
        m_job->abort();
    }
    if (isRunning()) {
        emitAborted();
    }
    return true;
}

void HorizonCheckUpdate::executeTask()
{
    setStatus(tr("Reading Horizon's registry..."));

    QSet<QString> games;
    for (const auto& version : m_gameVersions) {
        games.insert(version.toString());
    }

    m_job = HorizonAPI::withIndex(
        [this, games](const QJsonObject& index) {
            for (auto* resource : m_resources) {
                const auto meta = resource->metadata();
                if (!meta) {
                    continue;
                }
                const auto project = HorizonAPI::findProject(index, meta->project_id.toString());
                if (project.isEmpty()) {
                    emit checkFailed(resource, tr("Horizon's registry no longer lists it."));
                    continue;
                }
                auto pack = HorizonAPI::packFrom(project);

                // Its newest version made for this instance's Minecraft (the index lists versions newest first)
                const auto newest = std::find_if(pack->versions.begin(), pack->versions.end(), [&games](const ModPlatform::IndexedVersion& v) {
                    return games.isEmpty() || v.mcVersion.isEmpty() || games.contains(v.mcVersion.first());
                });
                if (newest == pack->versions.end()) {
                    continue;
                }
                const auto installed = meta->version().toString();
                if (newest->fileId.toString() == installed && resource->status() != ResourceStatus::NOT_INSTALLED) {
                    continue;
                }

                auto version = *newest;
                auto download = makeShared<ResourceDownloadTask>(pack, version, m_resourceModel, true, "update");
                const auto oldVersion = installed.isEmpty() ? tr("Unknown") : installed;
                m_updates.emplace_back(pack->name, meta->hash, oldVersion, version.version_number, version.version_type, version.changelog,
                                       ModPlatform::ResourceProvider::HORIZON, download, resource->enabled());
            }
        },
        [](const QString&) {});

    connect(m_job.get(), &Task::succeeded, this, &HorizonCheckUpdate::emitSucceeded);
    connect(m_job.get(), &Task::failed, this, &HorizonCheckUpdate::emitFailed);
    connect(m_job.get(), &Task::aborted, this, &HorizonCheckUpdate::emitAborted);
    m_job->start();
}
