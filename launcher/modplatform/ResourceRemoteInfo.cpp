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

#include "ResourceRemoteInfo.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include "Application.h"
#include "minecraft/mod/Resource.h"
#include "minecraft/mod/ResourceFolderModel.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/flame/FlameModIndex.h"
#include "modplatform/horizon/HorizonAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "modplatform/modrinth/ModrinthPackIndex.h"
#include "net/ApiDownload.h"
#include "net/HttpMetaCache.h"

namespace {

using Info = ResourceRemoteInfo::Info;

QString keyOf(ModPlatform::ResourceProvider provider, const QString& id)
{
    return QString::number(static_cast<int>(provider)) + QChar(':') + id;
}

/// Where the download pages of each platform keep their icons: the same files serve both
QString cacheBaseOf(ModPlatform::ResourceProvider provider)
{
    switch (provider) {
        case ModPlatform::ResourceProvider::HORIZON:
            return "HorizonPacks";
        case ModPlatform::ResourceProvider::MODRINTH:
            return "ModrinthPacks";
        case ModPlatform::ResourceProvider::FLAME:
            return "FlameMods";
    }
    return {};
}

// What each project is, once its platform said; and the projects asked for, answered or not. For the whole run of the
// launcher: a project's summary and icon don't change while it runs.
QHash<QString, Info> s_info;
QSet<QString> s_asked;
// Icons by their address: loaded from the cache, and asked for
QHash<QString, QImage> s_icons;
QHash<QString, QString> s_iconPaths;
QSet<QString> s_iconsAsked;
// Every model's, so what one asked for reaches the others too
QList<QPointer<ResourceRemoteInfo>> s_all;
// The requests under way, kept until they end
QList<Task::Ptr> s_running;

void run(const Task::Ptr& task)
{
    s_running.append(task);
    QObject::connect(task.get(), &Task::finished, task.get(), [raw = task.get()] {
        s_running.removeIf([raw](const Task::Ptr& running) { return running.get() == raw; });
    });
    task->start();
}

void loadIcon(const QUrl& url, const QString& path)
{
    QImage image(path);
    if (image.isNull()) {
        qWarning() << "The icon" << url << "could not be read from" << path;
        return;
    }
    s_icons.insert(url.toString(), image.scaled({ 64, 64 }, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    s_iconPaths.insert(url.toString(), path);
}

Info horizonInfo(const QJsonObject& project)
{
    Info info;
    info.name = project.value("name").toString();
    info.summary = project.value("summary").toString();
    for (const auto& author : project.value("authors").toArray()) {
        info.authors.append(author.toString());
    }
    info.homepage = ModPlatform::getMetaURL(ModPlatform::ResourceProvider::HORIZON, project.value("id").toString());
    info.license = project.value("license").toString();
    if (const QUrl icon(project.value("icon").toString()); icon.scheme() == "https") {
        info.icon = icon;
    }
    return info;
}

Info packInfo(const ModPlatform::IndexedPack& pack)
{
    Info info;
    info.name = pack.name;
    info.summary = pack.description;
    for (const auto& author : pack.authors) {
        if (!author.name.isEmpty()) {
            info.authors.append(author.name);
        }
    }
    info.homepage = pack.websiteUrl;
    if (const QUrl icon(pack.logoUrl); icon.scheme() == "https") {
        info.icon = icon;
    }
    return info;
}

}  // namespace

ResourceRemoteInfo::ResourceRemoteInfo(ResourceFolderModel* model, ApplyInfo applyInfo, ApplyIcon applyIcon, ProjectOf projectOf)
    : QObject(model)
    , m_model(model)
    , m_applyInfo(std::move(applyInfo))
    , m_applyIcon(std::move(applyIcon))
    , m_projectOf(std::move(projectOf))
{
    s_all.removeIf([](const QPointer<ResourceRemoteInfo>& other) { return other.isNull(); });
    s_all.append(this);

    // A resource parsed anew has only what its files say again
    connect(model, &ResourceFolderModel::parseFinished, this, &ResourceRemoteInfo::schedule);
    connect(model, &ResourceFolderModel::updateFinished, this, &ResourceRemoteInfo::schedule);
}

std::optional<ResourceRemoteInfo::Project> ResourceRemoteInfo::fromMetadata(const Resource& resource)
{
    const auto metadata = resource.metadata();
    if (!metadata) {
        return std::nullopt;
    }
    if (cacheBaseOf(metadata->provider).isEmpty()) {
        return std::nullopt;
    }
    const auto id = metadata->project_id.toString();
    if (id.isEmpty()) {
        return std::nullopt;
    }
    return Project{ metadata->provider, id };
}

void ResourceRemoteInfo::schedule()
{
    if (m_scheduled) {
        return;
    }
    m_scheduled = true;
    QTimer::singleShot(0, this, &ResourceRemoteInfo::apply);
}

void ResourceRemoteInfo::apply()
{
    m_scheduled = false;

    QMap<ModPlatform::ResourceProvider, QStringList> missing;
    QList<std::pair<QUrl, QString>> icons;
    bool changed = false;
    for (auto* resource : m_model->allResources()) {
        // What a parse in the background is writing is not to be changed under it
        if (resource->isResolving()) {
            continue;
        }
        const auto project = m_projectOf(*resource);
        if (!project) {
            continue;
        }
        const auto key = keyOf(project->provider, project->id);
        const auto found = s_info.constFind(key);
        if (found == s_info.constEnd()) {
            if (!s_asked.contains(key)) {
                s_asked.insert(key);
                missing[project->provider].append(project->id);
            }
            continue;
        }

        m_applyInfo(*resource, *found);
        const auto url = found->icon.toString();
        if (url.isEmpty()) {
            continue;
        }
        if (s_icons.contains(url)) {
            m_applyIcon(*resource, s_icons.value(url), s_iconPaths.value(url));
        } else if (!s_iconsAsked.contains(url)) {
            s_iconsAsked.insert(url);
            icons.append({ found->icon, cacheBaseOf(project->provider) });
        }
        changed = true;
    }
    if (changed) {
        refresh();
    }

    for (auto it = missing.constBegin(); it != missing.constEnd(); ++it) {
        if (it.key() == ModPlatform::ResourceProvider::HORIZON) {
            fetchHorizon(it.value());
        } else {
            fetchPlatform(it.key(), it.value());
        }
    }
    if (!icons.isEmpty()) {
        fetchIcons(icons);
    }
}

void ResourceRemoteInfo::refresh()
{
    const auto rows = m_model->rowCount({});
    const auto columns = m_model->columnCount({});
    if (rows > 0 && columns > 0) {
        emit m_model->dataChanged(m_model->index(0, 0), m_model->index(rows - 1, columns - 1));
    }
}

void ResourceRemoteInfo::scheduleAll()
{
    for (const auto& other : s_all) {
        if (other) {
            other->schedule();
        }
    }
}

void ResourceRemoteInfo::fetchHorizon(const QStringList& ids)
{
    if (!APPLICATION) {
        return;
    }
    // The copy of the registry at hand first (it may be from an earlier run); the registry itself is read only for
    // projects that copy doesn't have
    const auto known = HorizonAPI::knownIndex();
    QStringList rest;
    for (const auto& id : ids) {
        const auto project = HorizonAPI::findProject(known, id);
        if (project.isEmpty()) {
            rest.append(id);
        } else {
            s_info.insert(keyOf(ModPlatform::ResourceProvider::HORIZON, id), horizonInfo(project));
        }
    }
    if (rest.size() < ids.size()) {
        scheduleAll();
    }
    if (rest.isEmpty()) {
        return;
    }
    qDebug() << "Reading Horizon's registry for" << rest.size() << "installed projects";
    run(HorizonAPI::withIndex([rest](const QJsonObject& index) {
        for (const auto& id : rest) {
            if (const auto project = HorizonAPI::findProject(index, id); !project.isEmpty()) {
                s_info.insert(keyOf(ModPlatform::ResourceProvider::HORIZON, id), horizonInfo(project));
            }
        }
        scheduleAll();
    }));
}

void ResourceRemoteInfo::fetchPlatform(ModPlatform::ResourceProvider provider, const QStringList& ids)
{
    if (!APPLICATION) {
        return;
    }
    std::pair<Task::Ptr, QByteArray*> request;
    if (provider == ModPlatform::ResourceProvider::MODRINTH) {
        request = ModrinthAPI().getProjects(ids);
    } else if (provider == ModPlatform::ResourceProvider::FLAME) {
        if (APPLICATION->getFlameAPIKey().isEmpty()) {
            return;
        }
        request = FlameAPI().getProjects(ids);
    } else {
        return;
    }
    const Task::Ptr task = request.first;
    QByteArray* response = request.second;
    if (!task) {
        return;
    }
    if (auto* job = dynamic_cast<NetJob*>(task.get())) {
        job->setAskRetry(false);
    }

    qDebug() << "Asking" << ModPlatform::ProviderCapabilities::readableName(provider) << "about" << ids.size() << "installed projects";
    QObject::connect(task.get(), &Task::failed, task.get(), [provider](const QString& reason) {
        qWarning() << "What" << ModPlatform::ProviderCapabilities::readableName(provider) << "says of installed projects could not be read:" << reason;
    });
    QObject::connect(task.get(), &Task::succeeded, task.get(), [provider, response] {
        const auto document = QJsonDocument::fromJson(*response);
        // Modrinth answers with a list of projects, CurseForge with an object whose `data` is that list
        QJsonArray entries;
        if (document.isArray()) {
            entries = document.array();
        } else if (const auto data = document.object().value("data"); data.isArray()) {
            entries = data.toArray();
        } else if (data.isObject()) {
            entries.append(data);
        }
        for (const auto& entry : entries) {
            auto object = entry.toObject();
            ModPlatform::IndexedPack pack;
            try {
                if (provider == ModPlatform::ResourceProvider::MODRINTH) {
                    Modrinth::loadIndexedPack(pack, object);
                } else {
                    FlameMod::loadIndexedPack(pack, object);
                }
            } catch (const std::exception& e) {
                qWarning() << "A project of" << ModPlatform::ProviderCapabilities::readableName(provider) << "could not be read:" << e.what();
                continue;
            }
            auto info = packInfo(pack);
            // Modrinth names a project's licence by its SPDX id, or "LicenseRef-<name>" for one SPDX has not
            auto license = object.value("license").toObject().value("id").toString();
            if (license.startsWith("LicenseRef-")) {
                license = license.mid(11).replace('-', ' ');
            }
            info.license = license;
            s_info.insert(keyOf(provider, pack.addonId.toString()), info);
        }
        scheduleAll();
    });
    run(task);
}

void ResourceRemoteInfo::fetchIcons(const QList<std::pair<QUrl, QString>>& icons)
{
    if (!APPLICATION) {
        return;
    }
    auto job = makeShared<NetJob>(QString("ResourceRemoteInfo::Icons"), APPLICATION->network());
    job->setAskRetry(false);
    bool loaded = false;
    for (const auto& icon : icons) {
        const QUrl url = icon.first;
        auto entry = APPLICATION->metacache()->resolveEntry(
            icon.second, QString("logos/%1").arg(QString(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha1).toHex())));
        const auto path = entry->getFullPath();
        // Kept from an earlier run, or by the download pages
        if (QFileInfo(path).isFile()) {
            loadIcon(url, path);
            loaded = true;
            continue;
        }
        auto download = Net::ApiDownload::makeCached(url, entry);
        QObject::connect(download.get(), &Task::succeeded, job.get(), [url, path] {
            loadIcon(url, path);
            scheduleAll();
        });
        job->addNetAction(download);
    }
    if (loaded) {
        scheduleAll();
    }
    if (job->size() > 0) {
        qDebug() << "Downloading" << job->size() << "icons of installed projects";
        run(job);
    }
}
