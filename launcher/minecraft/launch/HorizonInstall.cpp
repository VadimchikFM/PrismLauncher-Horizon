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

#include "HorizonInstall.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>

#include "Application.h"
#include "FileSystem.h"
#include "archive/ExtractZipTask.h"
#include "launch/LaunchTask.h"
#include "minecraft/HorizonBackend.h"
#include "minecraft/HorizonComponent.h"
#include "minecraft/HorizonMods.h"
#include "minecraft/HorizonVersionList.h"
#include "minecraft/MinecraftInstance.h"
#include "net/ChecksumValidator.h"
#include "net/NetJob.h"
#include "settings/SettingsObject.h"

namespace {

/// The archive Horizon publishes its game in for this system, or nothing when it has none.
QString platformAsset()
{
    const auto archive = Horizon::platformArchive();
    return archive.isEmpty() ? QString() : "horizon-" + archive;
}

QString installedFile()
{
    return FS::PathCombine(Horizon::rootDir(), "installed.json");
}

/// The folder of a version the player picked, by its release's tag.
QString pinnedDir(const QString& tag)
{
    return FS::PathCombine(Horizon::rootDir(), "pinned", tag);
}

/// Where the build's programs are in `dir`: at its top, or in the one folder an archive wraps them in.
QString locateBuild(const QString& dir)
{
    if (Horizon::isBuild(dir)) {
        return dir;
    }
    for (const auto& entry : QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (Horizon::isBuild(entry.absoluteFilePath())) {
            return entry.absoluteFilePath();
        }
    }
    return {};
}

}  // namespace

HorizonInstall::HorizonInstall(LaunchTask* parent, HorizonContextPtr context, LaunchMode mode)
    : LaunchStep(parent), m_context(std::move(context)), m_mode(mode)
{}

void HorizonInstall::executeTask()
{
    if (m_mode == LaunchMode::Demo) {
        fail(tr("Horizon has no demo mode. Sign in with a Microsoft account that owns Minecraft: Java Edition to play, or remove "
                "Horizon from the instance (Edit > Version)."));
        return;
    }

    // The game built with the instance's native mods is what the instance plays, when there is one. (The Horizon
    // Mods page builds it.) The mods being on is what asks for it: with all of them off, the regular build plays.
    auto* instance = m_parent->instance();
    const auto signature = HorizonMods::nativeSignature(instance->gameRoot());
    if (!signature.isEmpty()) {
        const auto version = instance->settings()->get("HorizonNativeVersion").toString();
        if (HorizonBackend::isVersionInstalled(version) &&
            useBuild(HorizonBackend::versionDir(version), tr("native build %1").arg(version), false)) {
            if (!instance->settings()->get("HorizonNativeSignature").toString().endsWith("|" + signature)) {
                emit logLine(tr("The native mods of this instance changed since its game was built: it plays without the changes. "
                                "Build it again with Build Horizon on the Mods page.\n"),
                             MessageLevel::Warning);
            }
            emitSucceeded();
            return;
        }
        emit logLine(tr("This instance has native mods turned on, but its game is not built with them: they do nothing. Build "
                        "it with Build Horizon on the Mods page. Starting the regular Horizon.\n"),
                     MessageLevel::Warning);
    }

    // A build the player chose (their own, or one another program installed) is played as it is.
    const auto custom = APPLICATION->settings()->get("HorizonBuildPath").toString().trimmed();
    if (!custom.isEmpty()) {
        const auto dir = locateBuild(QDir(custom).absolutePath());
        if (dir.isEmpty() || !useBuild(dir, tr("custom build"), false)) {
            fail(tr("The Horizon build folder \"%1\" has no %2 and %3. Choose the folder of an unpacked Horizon build, or clear the "
                    "setting to let the launcher download one.")
                     .arg(custom, Horizon::executableName("horizon-client"), Horizon::executableName("horizon-prepare")));
            return;
        }
        emitSucceeded();
        return;
    }

    m_asset = platformAsset();

    // A version the player picked for this instance (as a mod loader's version is picked) is played as it is: it
    // has a folder of its own, by its release's tag, and is never updated.
    const auto wanted = HorizonComponent::version(instance->getPackProfile());
    if (!wanted.isEmpty() && wanted != HorizonVersion::Latest) {
        if (!HorizonVersion::isValidTag(wanted)) {
            fail(tr("\"%1\" is not a Horizon version. Pick one on the instance's Version page.").arg(wanted));
            return;
        }
        m_pinnedTag = wanted;
        m_baseUrl = QString("https://github.com/horizon-rust/horizon/releases/download/%1/").arg(wanted);
        const auto dir = locateBuild(pinnedDir(wanted));
        if (!dir.isEmpty() && useBuild(dir, wanted, false)) {
            emitSucceeded();
            return;
        }
        if (m_mode == LaunchMode::Offline) {
            fail(tr("Horizon %1 is not installed yet, and an offline launch can't download it. Launch this instance online once.")
                     .arg(wanted));
            return;
        }
        if (m_asset.isEmpty()) {
            fail(tr("Horizon publishes no build for this system (%1, %2)").arg(QSysInfo::kernelType(), QSysInfo::currentCpuArchitecture()));
            return;
        }
        setStatus(tr("Looking for Horizon %1...").arg(wanted));
        fetchChecksums();
        return;
    }

    // Else the newest Nightly, which the launcher keeps up to date
    m_baseUrl = Horizon::ReleaseBaseUrl;
    readInstalled();
    const bool autoUpdate = APPLICATION->settings()->get("HorizonAutoUpdate").toBool();
    if (m_mode == LaunchMode::Offline || (!autoUpdate && Horizon::isBuild(m_installedDir))) {
        if (useInstalledBuild()) {
            emitSucceeded();
        } else {
            fail(tr("Horizon is not installed yet, and an offline launch can't download it. Launch this instance online once."));
        }
        return;
    }

    if (m_asset.isEmpty()) {
        updateUnavailable(
            tr("Horizon publishes no build for this system (%1, %2)").arg(QSysInfo::kernelType(), QSysInfo::currentCpuArchitecture()));
        return;
    }

    // The nightly release is replaced in place, so its checksums say whether the installed build is still the newest.
    setStatus(tr("Checking for a new Horizon build..."));
    fetchChecksums();
}

void HorizonInstall::fetchChecksums()
{
    auto [download, bytes] = Net::Download::makeByteArray(QUrl(m_baseUrl + "SHA256SUMS"));
    m_checksumsDownload = download;
    m_checksums = bytes;

    auto job = makeShared<NetJob>(QString("Horizon::Checksums"), APPLICATION->network());
    job->setAskRetry(false);
    job->addNetAction(m_checksumsDownload);
    connect(job.get(), &Task::succeeded, this, &HorizonInstall::checksumsFetched, Qt::QueuedConnection);
    connect(
        job.get(), &Task::failed, this, [this](const QString& reason) { updateUnavailable(reason); }, Qt::QueuedConnection);
    m_task = job;
    job->start();
}

void HorizonInstall::checksumsFetched()
{
    if (!isRunning()) {
        return;
    }

    m_newSha256 = Horizon::checksumFor(*m_checksums, m_asset);
    if (m_newSha256.isEmpty()) {
        updateUnavailable(tr("the release lists no %1").arg(m_asset));
        return;
    }
    if (m_pinnedTag.isEmpty() && m_newSha256 == m_installedSha256 && useInstalledBuild()) {
        emit logLine(tr("Horizon is up to date.\n"), MessageLevel::Launcher);
        emitSucceeded();
        return;
    }

    m_archivePath = FS::PathCombine(Horizon::rootDir(), "downloads", m_asset);
    setStatus(tr("Downloading Horizon..."));
    emit logLine(tr("Downloading %1 from %2").arg(m_asset, m_baseUrl), MessageLevel::Launcher);

    auto download = Net::Download::makeFile(QUrl(m_baseUrl + m_asset), m_archivePath);
    download->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha256, m_newSha256));
    auto job = makeShared<NetJob>(QString("Horizon::Download"), APPLICATION->network());
    job->addNetAction(download);
    connect(job.get(), &Task::succeeded, this, &HorizonInstall::archiveDownloaded, Qt::QueuedConnection);
    connect(
        job.get(), &Task::failed, this, [this](const QString& reason) { updateUnavailable(reason); }, Qt::QueuedConnection);
    watch(job.get());
    m_task = job;
    job->start();

    // Shows the progress dialog, which returns here only once this step has ended: nothing may follow.
    emit progressReportingRequest();
}

void HorizonInstall::archiveDownloaded()
{
    if (!isRunning()) {
        return;
    }

    // A build is a folder of its own: named after its archive's checksum, or after the tag of a picked version.
    m_extractDir = m_pinnedTag.isEmpty() ? FS::PathCombine(Horizon::rootDir(), "builds", m_newSha256.left(16)) : pinnedDir(m_pinnedTag);
    // What an interrupted install left there.
    FS::deletePath(m_extractDir);
    if (!FS::ensureFolderPathExists(m_extractDir)) {
        fail(tr("Could not create the folder %1").arg(m_extractDir));
        return;
    }

    setStatus(tr("Unpacking Horizon..."));
    setDetails({});
    auto task = makeShared<MMCZip::ExtractZipTask>(m_archivePath, QDir(m_extractDir));
    connect(task.get(), &Task::succeeded, this, &HorizonInstall::archiveExtracted, Qt::QueuedConnection);
    connect(
        task.get(), &Task::failed, this, [this](const QString& reason) { fail(tr("Could not unpack Horizon: %1").arg(reason)); },
        Qt::QueuedConnection);
    connect(task.get(), &Task::progress, this, &HorizonInstall::setProgress);
    m_task = task;
    task->start();
}

void HorizonInstall::archiveExtracted()
{
    if (!isRunning()) {
        return;
    }

    const auto buildDir = locateBuild(m_extractDir);
    if (buildDir.isEmpty()) {
        FS::deletePath(m_extractDir);
        fail(tr("%1 holds no %2: Horizon's releases may have changed their layout.")
                 .arg(m_asset, Horizon::executableName("horizon-client")));
        return;
    }
#ifndef Q_OS_WIN
    // An archive that kept no permissions still has to leave two programs.
    for (const auto* name : { "horizon-client", "horizon-prepare" }) {
        QFile program(FS::PathCombine(buildDir, name));
        program.setPermissions(program.permissions() | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
    }
#endif
    QFile::remove(m_archivePath);

    if (!m_pinnedTag.isEmpty()) {
        // A picked version is nobody's "installed build": no update replaces it, and nothing removes it
        if (!useBuild(buildDir, m_pinnedTag, false)) {
            fail(tr("The Horizon build unpacked to %1 can't be used.").arg(buildDir));
            return;
        }
        emit logLine(tr("Installed Horizon %1.\n").arg(m_pinnedTag), MessageLevel::Launcher);
        emitSucceeded();
        return;
    }

    const QDir root(Horizon::rootDir());
    const QDir builds(root.absoluteFilePath("builds"));
    // The build played until now stays, for going back to; older ones go.
    const auto keepNew = QDir(m_extractDir).dirName();
    const auto keepOld = m_installedDir.isEmpty() ? QString() : builds.relativeFilePath(m_installedDir).section('/', 0, 0);

    QJsonObject installed;
    installed["asset"] = m_asset;
    installed["sha256"] = m_newSha256;
    installed["dir"] = root.relativeFilePath(buildDir);
    if (!keepOld.isEmpty() && keepOld != keepNew) {
        installed["previous"] = root.relativeFilePath(m_installedDir);
    }
    try {
        FS::write(installedFile(), QJsonDocument(installed).toJson());
    } catch (const Exception& e) {
        fail(tr("Could not record the installed Horizon build: %1").arg(e.cause()));
        return;
    }

    for (const auto& entry : builds.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.fileName() != keepNew && entry.fileName() != keepOld) {
            FS::deletePath(entry.absoluteFilePath());
        }
    }

    m_installedSha256 = m_newSha256;
    m_installedDir = buildDir;
    if (!useInstalledBuild()) {
        fail(tr("The Horizon build unpacked to %1 can't be used.").arg(buildDir));
        return;
    }
    emit logLine(tr("Installed a new Horizon build.\n"), MessageLevel::Launcher);
    emitSucceeded();
}

bool HorizonInstall::useBuild(const QString& dir, const QString& label, bool managed)
{
    if (!Horizon::isBuild(dir)) {
        return false;
    }
    m_context->buildDir = dir;
    m_context->clientPath = FS::PathCombine(dir, Horizon::executableName("horizon-client"));
    m_context->preparePath = FS::PathCombine(dir, Horizon::executableName("horizon-prepare"));
    m_context->buildLabel = label;
    m_context->managedBuild = managed;
    emit logLine(tr("Horizon build folder is:\n  %1\n").arg(dir), MessageLevel::Launcher);
    return true;
}

bool HorizonInstall::useInstalledBuild()
{
    return useBuild(m_installedDir, tr("nightly %1").arg(m_installedSha256.left(12)), true);
}

void HorizonInstall::readInstalled()
{
    m_installedSha256.clear();
    m_installedDir.clear();

    QFile file(installedFile());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    const auto dir = object.value("dir").toString();
    // The folder is one of ours: nothing else is ever deleted or run from this file's say-so.
    if (dir.isEmpty() || QDir::isAbsolutePath(dir) || dir.contains("..")) {
        return;
    }
    m_installedSha256 = object.value("sha256").toString();
    m_installedDir = QDir(Horizon::rootDir()).absoluteFilePath(dir);
}

void HorizonInstall::updateUnavailable(const QString& reason)
{
    if (!isRunning()) {
        return;
    }
    if (useInstalledBuild()) {
        emit logLine(tr("Could not get the newest Horizon build (%1). Playing the installed one.\n").arg(reason), MessageLevel::Warning);
        emitSucceeded();
    } else {
        fail(tr("Could not download Horizon: %1").arg(reason));
    }
}

void HorizonInstall::fail(const QString& reason)
{
    emit logLine(reason, MessageLevel::Fatal);
    emitFailed(reason);
}

void HorizonInstall::watch(Task* task)
{
    connect(task, &Task::progress, this, &HorizonInstall::setProgress);
    connect(task, &Task::details, this, &HorizonInstall::setDetails);
}

bool HorizonInstall::abort()
{
    if (m_task) {
        // Whatever it still reports is no longer this step's business.
        disconnect(m_task.get(), nullptr, this, nullptr);
        if (m_task->isRunning() && m_task->canAbort()) {
            m_task->abort();
        }
    }
    if (isRunning()) {
        emitAborted();
    }
    return true;
}
