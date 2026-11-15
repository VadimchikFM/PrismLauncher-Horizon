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

#include "HorizonVersionList.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "Application.h"
#include "minecraft/launch/HorizonContext.h"
#include "net/NetJob.h"

const QString HorizonVersion::Latest = QStringLiteral("latest");

bool HorizonVersion::isValidTag(const QString& tag)
{
    static const QRegularExpression s_tag("^[A-Za-z0-9][A-Za-z0-9._-]*$");
    return s_tag.match(tag).hasMatch() && !tag.contains("..");
}

QString HorizonVersion::displayName(const QString& tag)
{
    if (tag.isEmpty() || tag == Latest) {
        return QCoreApplication::translate("HorizonVersionList", "Newest Nightly (updates itself)");
    }
    return tag;
}

namespace {

/// GitHub's list of the repository's releases, a hundred a page, newest first.
const QString ReleasesUrl = QStringLiteral("https://api.github.com/repos/horizon-rust/horizon/releases?per_page=100&page=%1");
/// The repository has a few hundred releases, most of them the launcher's.
constexpr int MaxPages = 8;

/// Reads the releases page by page and hands the game's ones to the list.
class LoadTask : public Task {
    Q_OBJECT
   public:
    explicit LoadTask(HorizonVersionList* list) : m_list(list) {}

    bool canAbort() const override { return true; }
    bool abort() override
    {
        if (m_job) {
            disconnect(m_job.get(), nullptr, this, nullptr);
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
        m_asset = Horizon::platformArchive();
        if (m_asset.isEmpty()) {
            emitFailed(tr("Horizon publishes no build for this system."));
            return;
        }
        m_asset.prepend("horizon-");
        m_versions.append(std::make_shared<HorizonVersion>(HorizonVersion::Latest, HorizonVersion::displayName(HorizonVersion::Latest),
                                                           tr("nightly")));
        fetch(1);
    }

   private:
    void fetch(int page)
    {
        m_page = page;
        setStatus(tr("Reading Horizon's releases..."));
        setProgress(page - 1, MaxPages);
        auto [download, bytes] = Net::Download::makeByteArray(QUrl(ReleasesUrl.arg(page)));
        m_download = download;
        m_bytes = bytes;
        m_job = makeShared<NetJob>(QString("Horizon::Releases"), APPLICATION->network());
        m_job->setAskRetry(false);
        m_job->addNetAction(m_download);
        connect(m_job.get(), &Task::succeeded, this, &LoadTask::pageFetched, Qt::QueuedConnection);
        connect(m_job.get(), &Task::failed, this, &LoadTask::pageFailed, Qt::QueuedConnection);
        m_job->start();
    }

    void pageFailed(const QString& reason)
    {
        if (!isRunning()) {
            return;
        }
        // The pages read so far are still a list
        if (m_versions.size() > 1) {
            finish();
        } else {
            emitFailed(reason);
        }
    }

    void pageFetched()
    {
        if (!isRunning()) {
            return;
        }
        const auto releases = QJsonDocument::fromJson(*m_bytes).array();
        for (const auto& value : releases) {
            const auto release = value.toObject();
            const auto tag = release.value("tag_name").toString();
            // "nightly" is the newest Nightly's moving tag: the first entry stands for it
            if (tag == "nightly" || release.value("draft").toBool() || !HorizonVersion::isValidTag(tag)) {
                continue;
            }
            // The game's releases are the ones with the game's archive (the launcher's have the launcher's)
            const auto assets = release.value("assets").toArray();
            bool here = false;
            for (const auto& asset : assets) {
                here = here || asset.toObject().value("name").toString() == m_asset;
            }
            if (!here) {
                continue;
            }
            const auto published = release.value("published_at").toString().left(10);
            const auto type = tag.startsWith("nightly") ? tr("nightly") : tr("release");
            m_versions.append(std::make_shared<HorizonVersion>(tag, tag, published.isEmpty() ? type : QString("%1, %2").arg(type, published)));
        }
        if (releases.size() == 100 && m_page < MaxPages) {
            fetch(m_page + 1);
        } else {
            finish();
        }
    }

    void finish()
    {
        m_list->updateListData(m_versions);
        emitSucceeded();
    }

   private:
    HorizonVersionList* m_list;
    QString m_asset;
    int m_page = 0;
    QList<BaseVersion::Ptr> m_versions;
    NetJob::Ptr m_job;
    Net::Download::Ptr m_download;
    QByteArray* m_bytes = nullptr;
};

}  // namespace

HorizonVersionList::HorizonVersionList(QObject* parent) : BaseVersionList(parent) {}

HorizonVersionList* HorizonVersionList::shared()
{
    static auto* s_list = new HorizonVersionList(QCoreApplication::instance());
    return s_list;
}

Task::Ptr HorizonVersionList::getLoadTask([[maybe_unused]] bool forceReload)
{
    // One read at a time: whoever asks while it runs waits for the same one
    if (!m_loadTask || m_loadTask->isFinished()) {
        m_loadTask.reset(new LoadTask(this));
    }
    return m_loadTask;
}

const BaseVersion::Ptr HorizonVersionList::at(int i) const
{
    return m_versions.at(i);
}

int HorizonVersionList::count() const
{
    return m_versions.size();
}

QVariant HorizonVersionList::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_versions.size()) {
        return {};
    }
    switch (role) {
        // The newest Nightly is what an instance plays unless the player picks another
        case RecommendedRole:
        case LatestRole:
            return m_versions.at(index.row())->descriptor() == HorizonVersion::Latest;
        default:
            return BaseVersionList::data(index, role);
    }
}

BaseVersionList::RoleList HorizonVersionList::providesRoles() const
{
    return { VersionPointerRole, VersionRole, VersionIdRole, RecommendedRole, LatestRole, TypeRole };
}

void HorizonVersionList::updateListData(QList<BaseVersion::Ptr> versions)
{
    beginResetModel();
    m_versions = std::move(versions);
    m_loaded = true;
    endResetModel();
}

#include "HorizonVersionList.moc"
