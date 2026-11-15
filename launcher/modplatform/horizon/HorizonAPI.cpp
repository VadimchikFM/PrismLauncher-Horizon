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

#include "HorizonAPI.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>

#include "Application.h"
#include "FileSystem.h"
#include "net/HttpMetaCache.h"
#include "net/NetJob.h"

namespace {

/// How long an index that was read is used before it is read again.
constexpr qint64 IndexLifetimeSecs = 10 * 60;
/// What the launcher's resource lists ask for at a time.
constexpr int PageSize = 25;

QJsonObject s_index;
qint64 s_indexReadAt = 0;

QString tr(const char* text)
{
    return QCoreApplication::translate("HorizonAPI", text);
}

QStringList strings(const QJsonValue& array)
{
    QStringList list;
    const auto values = array.toArray();
    for (const auto& value : values) {
        list.append(value.toString());
    }
    return list;
}

/// `game-mechanics` as a label: `Game mechanics`.
QString label(QString slug)
{
    slug.replace('-', ' ');
    if (!slug.isEmpty()) {
        slug[0] = slug[0].toUpper();
    }
    return slug;
}

/// The ids of the projects taken down as a whole: every version of them is refused.
QSet<QString> revokedProjects(const QJsonObject& index)
{
    QSet<QString> revoked;
    const auto entries = index.value("revoked").toArray();
    for (const auto& value : entries) {
        const auto entry = value.toObject();
        if (entry.value("sha256").toString().isEmpty()) {
            revoked.insert(entry.value("id").toString());
        }
    }
    return revoked;
}

/// The files taken down one by one, by SHA-256.
QSet<QString> revokedFiles(const QJsonObject& index)
{
    QSet<QString> revoked;
    const auto entries = index.value("revoked").toArray();
    for (const auto& value : entries) {
        if (const auto sha256 = value.toObject().value("sha256").toString(); !sha256.isEmpty()) {
            revoked.insert(sha256.toLower());
        }
    }
    return revoked;
}

/// What a version was made for, after its number: the Horizon commit of a native mod's version.
QString madeFor(const QJsonObject& version)
{
    // A native version's API line is "native-1/<Horizon commit>/<horizon-mixins commit>"
    const auto api = version.value("api").toString().split('/');
    if (api.size() >= 2 && api.first().startsWith("native")) {
        const auto commit = api.at(1).left(7);
        if (const auto hooks = version.value("hooks").toInt(); hooks > 0) {
            return tr("for Horizon %1 and newer (hook points %2)").arg(commit).arg(hooks);
        }
        return tr("for Horizon %1").arg(commit);
    }
    return {};
}

/// The task behind HorizonAPI::withIndex.
class IndexTask : public Task {
   public:
    IndexTask(std::function<void(const QJsonObject&)> ready, std::function<void(const QString&)> failed)
        : m_ready(std::move(ready)), m_failed(std::move(failed))
    {}

    bool canAbort() const override { return true; }
    bool abort() override
    {
        if (m_job) {
            QObject::disconnect(m_job.get(), nullptr, this, nullptr);
            m_job->abort();
        }
        if (isRunning()) {
            emitAborted();
        }
        return true;
    }

   protected:
    void executeTask() override
    {
        if (const auto index = HorizonAPI::cachedIndex(); !index.isEmpty()) {
            m_ready(index);
            emitSucceeded();
            return;
        }

        setStatus(tr("Reading Horizon's registry..."));
        auto [download, bytes] = Net::Download::makeByteArray(QUrl(HorizonAPI::IndexUrl));
        m_download = download;
        m_bytes = bytes;
        m_job = makeShared<NetJob>(QString("Horizon::Index"), APPLICATION->network());
        m_job->setAskRetry(false);
        m_job->addNetAction(m_download);
        connect(m_job.get(), &Task::succeeded, this, [this] {
            QJsonParseError error{};
            const auto document = QJsonDocument::fromJson(*m_bytes, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) {
                fail(tr("Horizon's registry could not be read: %1").arg(error.errorString()));
                return;
            }
            s_index = document.object();
            s_indexReadAt = QDateTime::currentSecsSinceEpoch();
            // Kept on disk too, for the pages of installed mods and packs when the registry can't be reached
            try {
                FS::write(HorizonAPI::indexCachePath(), *m_bytes);
            } catch (const FS::FileSystemException& e) {
                qWarning() << "Could not keep Horizon's registry index:" << e.cause();
            }
            m_ready(s_index);
            emitSucceeded();
        });
        connect(m_job.get(), &Task::failed, this, [this](const QString& reason) { fail(reason); });
        connect(m_job.get(), &Task::progress, this, &Task::setProgress);
        m_job->start();
    }

   private:
    void fail(const QString& reason)
    {
        if (m_failed) {
            m_failed(reason);
        }
        emitFailed(reason);
    }

    std::function<void(const QJsonObject&)> m_ready;
    std::function<void(const QString&)> m_failed;
    NetJob::Ptr m_job;
    Net::Download::Ptr m_download;
    QByteArray* m_bytes = nullptr;
};

}  // namespace

QJsonObject HorizonAPI::cachedIndex()
{
    if (s_index.isEmpty() || QDateTime::currentSecsSinceEpoch() - s_indexReadAt > IndexLifetimeSecs) {
        return {};
    }
    return s_index;
}

QString HorizonAPI::indexCachePath()
{
    return FS::PathCombine(APPLICATION->metacache()->getBasePath("HorizonPacks"), "index.json");
}

QJsonObject HorizonAPI::knownIndex()
{
    if (!s_index.isEmpty()) {
        return s_index;
    }
    // The copy of the last read, from an earlier run of the launcher. It is not taken as fresh: searching still reads
    // the registry anew.
    static QJsonObject s_onDisk;
    static bool s_readDisk = false;
    if (!s_readDisk) {
        s_readDisk = true;
        QFile file(indexCachePath());
        if (file.open(QIODevice::ReadOnly)) {
            s_onDisk = QJsonDocument::fromJson(file.readAll()).object();
        }
    }
    return s_onDisk;
}

Task::Ptr HorizonAPI::withIndex(std::function<void(const QJsonObject&)> ready, std::function<void(const QString&)> failed)
{
    return makeShared<IndexTask>(std::move(ready), std::move(failed));
}

QJsonObject HorizonAPI::findProject(const QJsonObject& index, const QString& id)
{
    const auto projects = index.value("projects").toArray();
    for (const auto& value : projects) {
        if (value.toObject().value("id").toString() == id) {
            return value.toObject();
        }
    }
    return {};
}

bool HorizonAPI::isOfType(const QJsonObject& project, ModPlatform::ResourceType type)
{
    const auto kind = project.value("kind").toString();
    switch (type) {
        case ModPlatform::ResourceType::Mod:
            return kind == "mod" || kind == "native-mod";
        case ModPlatform::ResourceType::ResourcePack:
        case ModPlatform::ResourceType::TexturePack:
            return kind == "resourcepack";
        case ModPlatform::ResourceType::ShaderPack:
            return kind == "shader" || kind == "shaderpack";
        case ModPlatform::ResourceType::Modpack:
            return kind == "modpack";
        case ModPlatform::ResourceType::DataPack:
            return kind == "datapack";
        default:
            return false;
    }
}

auto HorizonAPI::getSortingMethods() const -> QList<SortingMethod>
{
    return { { ByRelevance, "relevance", tr("Sort by Relevance") },
             { ByDownloads, "downloads", tr("Sort by Downloads") },
             { ByUpdate, "updated", tr("Sort by Last Updated") },
             { ByName, "name", tr("Sort by Name") } };
}

ModPlatform::IndexedVersion HorizonAPI::loadIndexedPackVersion(QJsonObject& version, ModPlatform::ResourceType) const
{
    ModPlatform::IndexedVersion file;
    const auto number = version.value("version").toString();
    const auto made = madeFor(version);

    // A version is named by its number in the project, which is what the launcher's metadata keeps
    file.fileId = number;
    file.version_number = number;
    file.version = made.isEmpty() ? number : QString("%1 (%2)").arg(number, made);
    file.version_type = ModPlatform::IndexedVersionType::Release;
    if (const auto game = version.value("game").toString(); !game.isEmpty()) {
        file.mcVersion = QStringList{ game };
    }
    file.date = version.value("published").toString();
    file.changelog = version.value("changelog").toString();

    const auto download = version.value("file").toObject();
    file.fileName = download.value("name").toString();
    file.hash_type = "sha256";
    file.hash = download.value("sha256").toString().toLower();
    // Files are only taken from the registry's releases and the projects' own GitHub releases: https, by name
    if (const auto url = download.value("url").toString(); url.startsWith("https://")) {
        file.downloadUrl = url;
    }
    if (file.fileName.contains('/') || file.fileName.contains('\\') || file.fileName.startsWith('.')) {
        file.downloadUrl.clear();
    }
    return file;
}

ModPlatform::IndexedPack::Ptr HorizonAPI::packFrom(const QJsonObject& project)
{
    auto pack = std::make_shared<ModPlatform::IndexedPack>();
    auto object = project;
    HorizonAPI api;
    api.loadIndexedPack(*pack, object);
    api.loadExtraPackInfo(*pack, object);
    return pack;
}

void HorizonAPI::loadIndexedPack(ModPlatform::IndexedPack& pack, QJsonObject& project) const
{
    const auto id = project.value("id").toString();
    pack.addonId = id;
    pack.provider = ModPlatform::ResourceProvider::HORIZON;
    pack.name = project.value("name").toString(id);
    pack.slug = id;
    pack.description = project.value("summary").toString();
    pack.logoName = "horizon_" + id;
    pack.logoUrl = project.value("icon").toString();
    pack.websiteUrl = "https://playhorizon.dev/mods/" + id;
    pack.side = ModPlatform::Side::NoSide;

    pack.authors.clear();
    for (const auto& author : strings(project.value("authors"))) {
        pack.authors.append({ author, {} });
    }

    // Every version is in the index: they are loaded with the project. A version the registry took down is not.
    pack.versions.clear();
    const auto revoked = revokedFiles(s_index);
    const auto versions = project.value("versions").toArray();
    for (const auto& value : versions) {
        auto object = value.toObject();
        auto version = loadIndexedPackVersion(object, ModPlatform::ResourceType::Mod);
        version.addonId = id;
        if (version.downloadUrl.isEmpty() || revoked.contains(version.hash)) {
            continue;
        }
        // The loader column of the launcher's mod list: a Horizon mod runs on Horizon
        const auto kind = project.value("kind").toString();
        if (kind == "mod" || kind == "native-mod") {
            version.loaders = ModPlatform::Horizon;
        }
        pack.versions.append(version);
    }
    pack.versionsLoaded = true;
}

void HorizonAPI::loadExtraPackInfo(ModPlatform::IndexedPack& pack, QJsonObject& project) const
{
    pack.extraDataLoaded = true;
    if (const auto source = project.value("source").toString(); source.startsWith("https://")) {
        pack.extraData.sourceUrl = source;
    }

    // The page: what the project says of itself, then what the registry knows of it
    QStringList body;
    const auto description = project.value("description").toString().trimmed();
    body.append(description.isEmpty() ? project.value("summary").toString() : description);

    QStringList facts;
    if (const auto kind = project.value("kind").toString(); kind == "native-mod") {
        facts.append(tr("**Native mod**: its code is compiled into the game, which has to be built with it (Mods > Build Horizon). It runs "
                        "with full access to your computer; its source was reviewed before it was listed."));
    } else if (kind == "mod") {
        facts.append(tr("**Content mod**: blocks, items, recipes and assets the game loads as they are. Nothing is compiled."));
    }
    if (const auto license = project.value("license").toString(); !license.isEmpty()) {
        facts.append(tr("License: %1").arg(license));
    }
    if (const auto downloads = project.value("downloads").toInteger(); downloads > 0) {
        facts.append(tr("Downloads: %1").arg(downloads));
    }
    if (const auto stars = project.value("stars").toInt(); stars > 0) {
        facts.append(tr("GitHub stars: %1").arg(stars));
    }
    QStringList tags;
    for (const auto& category : strings(project.value("categories"))) {
        tags.append(label(category));
    }
    for (const auto& tag : strings(project.value("tags"))) {
        tags.append("#" + tag);
    }
    if (!tags.isEmpty()) {
        facts.append(tags.join(", "));
    }
    if (project.value("key").toString().isEmpty()) {
        facts.append(tr("Unsigned: the launcher checks each file's SHA-256 against the registry's."));
    } else {
        facts.append(tr("Signed by its author: the registry checks the signature, the launcher each file's SHA-256."));
    }
    body.append("- " + facts.join("\n- "));

    // Every version, with the Minecraft and the Horizon it was made for
    QStringList versions;
    const auto list = project.value("versions").toArray();
    for (const auto& value : list) {
        const auto version = value.toObject();
        auto line = QString("**%1** (%2): Minecraft %3").arg(version.value("version").toString(), version.value("published").toString(),
                                                              version.value("game").toString());
        if (const auto made = madeFor(version); !made.isEmpty()) {
            line += ", " + made;
        }
        const auto broken = version.value("broken_on").toArray();
        for (const auto& on : broken) {
            line += tr(", *does not build on Horizon %1*").arg(on.toObject().value("horizon").toString().left(7));
        }
        if (const auto requires_ = version.value("requires").toArray(); !requires_.isEmpty()) {
            line += QCoreApplication::translate("HorizonAPI", ", %n mod(s)", nullptr, static_cast<int>(requires_.size()));
        }
        versions.append("- " + line);
    }
    if (!versions.isEmpty()) {
        body.append(tr("### Versions") + "\n\n" + versions.join("\n"));
    }

    pack.extraData.body = body.join("\n\n");
}

QJsonArray HorizonAPI::documentToArray(QJsonDocument& document) const
{
    return document.object().value("projects").toArray();
}

Task::Ptr HorizonAPI::searchProjects(SearchArgs&& args, Callback<QList<ModPlatform::IndexedPack::Ptr>>&& callbacks) const
{
    auto ready = [args, callbacks](const QJsonObject& index) {
        const auto revoked = revokedProjects(index);
        const auto words = args.search.value_or(QString()).toLower().split(' ', Qt::SkipEmptyParts);
        const auto categories = args.categoryIds.value_or(QStringList());

        QSet<QString> games;
        for (const auto& version : args.versions.value_or(std::vector<Version>())) {
            games.insert(version.toString());
        }

        // How well a project answers the search: 0 when a word is nowhere in it
        auto relevance = [&words](const QJsonObject& project) {
            const auto name = project.value("name").toString().toLower();
            const auto id = project.value("id").toString();
            const auto tags = strings(project.value("tags")) + strings(project.value("categories"));
            int score = 1;
            for (const auto& word : words) {
                if (name.startsWith(word)) {
                    score += 8;
                } else if (name.contains(word) || id.contains(word)) {
                    score += 4;
                } else if (tags.contains(word)) {
                    score += 2;
                } else if (project.value("summary").toString().toLower().contains(word) ||
                           strings(project.value("authors")).join(' ').toLower().contains(word)) {
                    score += 1;
                } else {
                    return 0;
                }
            }
            return score;
        };

        struct Match {
            int score;
            QJsonObject project;
        };
        QList<Match> matches;
        const auto projects = index.value("projects").toArray();
        for (const auto& value : projects) {
            const auto project = value.toObject();
            const auto id = project.value("id").toString();
            if (id.isEmpty() || revoked.contains(id) || !isOfType(project, args.type)) {
                continue;
            }
            const auto projectCategories = strings(project.value("categories"));
            if (!std::all_of(categories.begin(), categories.end(), [&](const QString& c) { return projectCategories.contains(c); })) {
                continue;
            }
            if (!games.isEmpty()) {
                const auto versions = project.value("versions").toArray();
                const bool forGame = std::any_of(versions.begin(), versions.end(),
                                                 [&games](const QJsonValue& v) { return games.contains(v.toObject().value("game").toString()); });
                if (!forGame) {
                    continue;
                }
            }
            if (const auto score = relevance(project); score > 0) {
                matches.append({ score, project });
            }
        }

        const auto newest = [](const QJsonObject& project) {
            const auto versions = project.value("versions").toArray();
            return versions.isEmpty() ? QString() : versions.first().toObject().value("published").toString();
        };
        const auto order = args.sorting ? args.sorting->index : static_cast<unsigned int>(ByRelevance);
        std::stable_sort(matches.begin(), matches.end(), [&](const Match& a, const Match& b) {
            switch (order) {
                case ByUpdate:
                    return newest(a.project) > newest(b.project);
                case ByName:
                    return a.project.value("name").toString().localeAwareCompare(b.project.value("name").toString()) < 0;
                case ByRelevance:
                    if (a.score != b.score) {
                        return a.score > b.score;
                    }
                    [[fallthrough]];
                default:
                    return a.project.value("downloads").toInteger() > b.project.value("downloads").toInteger();
            }
        });

        QList<ModPlatform::IndexedPack::Ptr> page;
        for (auto i = args.offset; i < matches.size() && page.size() < PageSize; i++) {
            page.append(packFrom(matches.at(i).project));
        }
        callbacks.on_succeed(page);
    };
    auto failed = [callbacks](const QString& reason) {
        if (callbacks.on_fail) {
            callbacks.on_fail(reason, -1);
        }
    };
    return withIndex(ready, failed);
}

Task::Ptr HorizonAPI::getProjectInfo(ProjectInfoArgs&& args, Callback<ModPlatform::IndexedPack::Ptr>&& callbacks, bool) const
{
    const auto id = args.pack->addonId.toString();
    auto ready = [id, callbacks](const QJsonObject& index) {
        const auto project = findProject(index, id);
        if (project.isEmpty() || revokedProjects(index).contains(id)) {
            if (callbacks.on_fail) {
                callbacks.on_fail(tr("Horizon's registry has no project %1").arg(id), 404);
            }
            return;
        }
        auto pack = packFrom(project);
        callbacks.on_succeed(pack);
    };
    auto failed = [callbacks](const QString& reason) {
        if (callbacks.on_fail) {
            callbacks.on_fail(reason, -1);
        }
    };
    return withIndex(ready, failed);
}

Task::Ptr HorizonAPI::getProjectVersions(VersionSearchArgs&& args, Callback<QVector<ModPlatform::IndexedVersion>>&& callbacks) const
{
    const auto id = args.pack->addonId.toString();
    auto ready = [id, callbacks](const QJsonObject& index) {
        QVector<ModPlatform::IndexedVersion> versions;
        if (const auto project = findProject(index, id); !project.isEmpty()) {
            const auto pack = packFrom(project);
            versions = QVector<ModPlatform::IndexedVersion>(pack->versions.begin(), pack->versions.end());
        }
        callbacks.on_succeed(versions);
    };
    auto failed = [callbacks](const QString& reason) {
        if (callbacks.on_fail) {
            callbacks.on_fail(reason, -1);
        }
    };
    return withIndex(ready, failed);
}

Task::Ptr HorizonAPI::getDependencyVersion(DependencySearchArgs&&, Callback<ModPlatform::IndexedVersion>&&) const
{
    // Horizon's mods name no dependencies the launcher resolves
    return nullptr;
}

std::pair<Task::Ptr, QByteArray*> HorizonAPI::getProject(QString, bool askRetry) const
{
    auto [download, bytes] = Net::Download::makeByteArray(QUrl(IndexUrl));
    auto job = makeShared<NetJob>(QString("Horizon::Project"), APPLICATION->network());
    job->setAskRetry(askRetry);
    job->addNetAction(download);
    return { job, bytes };
}

std::pair<Task::Ptr, QByteArray*> HorizonAPI::getProjects(QStringList) const
{
    return getProject({}, true);
}

QList<ModPlatform::Category> HorizonAPI::categories(const QJsonObject& index, ModPlatform::ResourceType type)
{
    QMap<QString, int> uses;
    const auto projects = index.value("projects").toArray();
    for (const auto& value : projects) {
        const auto project = value.toObject();
        if (!isOfType(project, type)) {
            continue;
        }
        for (const auto& category : strings(project.value("categories"))) {
            uses[category]++;
        }
    }
    auto slugs = uses.keys();
    std::sort(slugs.begin(), slugs.end(), [&uses](const QString& a, const QString& b) { return uses[a] != uses[b] ? uses[a] > uses[b] : a < b; });
    QList<ModPlatform::Category> list;
    for (const auto& slug : slugs) {
        list.append({ label(slug), slug });
    }
    return list;
}
