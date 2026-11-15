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

#include "HorizonBackend.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "Application.h"
#include "FileSystem.h"
#include "archive/ExtractZipTask.h"
#include "minecraft/launch/HorizonContext.h"
#include "net/ChecksumValidator.h"
#include "net/NetJob.h"

namespace {

const QString ReleasesUrl = QStringLiteral("https://api.github.com/repos/horizon-rust/horizon/releases?per_page=40");
const QString DownloadUrl = QStringLiteral("https://github.com/horizon-rust/horizon/releases/download/%1/%2");
/// The launcher's releases, among the game's in the same repository.
const QString TagPrefix = QStringLiteral("launcher@");
/// How long an installed program is used before a newer one is looked for.
constexpr qint64 CheckEverySecs = 24 * 60 * 60;

/// Where this launcher keeps Horizon's launcher: the program, and the data folder it works in.
QString rootDir()
{
    return FS::PathCombine(Horizon::rootDir(), "launcher");
}

QString installedFile()
{
    return FS::PathCombine(rootDir(), "installed.json");
}

QString programName()
{
    return Horizon::executableName("horizon-launcher");
}

/// The program in an unpacked release: at its top on Windows, inside a folder or an application bundle elsewhere.
QString locateProgram(const QString& dir)
{
    QDirIterator files(dir, QStringList{ programName() }, QDir::Files, QDirIterator::Subdirectories);
    return files.hasNext() ? files.next() : QString();
}

}  // namespace

HorizonBackend::HorizonBackend(QString gameDir, QObject* parent) : QObject(parent), m_gameDir(std::move(gameDir)) {}

HorizonBackend::~HorizonBackend()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        // Nothing listens any more: don't leave a build running behind a closed window
        disconnect(m_process, nullptr, this, nullptr);
        abort();
    }
}

QString HorizonBackend::dataDir()
{
    return FS::PathCombine(rootDir(), "home");
}

QString HorizonBackend::versionDir(const QString& version)
{
    return FS::PathCombine(dataDir(), "versions", version);
}

bool HorizonBackend::isVersionInstalled(const QString& version)
{
    // No path may come out of a version's name
    static const QRegularExpression s_name("^[A-Za-z0-9][A-Za-z0-9._@+-]*$");
    return s_name.match(version).hasMatch() && !version.contains("..") && Horizon::isBuild(versionDir(version));
}

QString HorizonBackend::buildLogPath()
{
    return FS::PathCombine(dataDir(), "native", "build.log");
}

void HorizonBackend::run(const QStringList& arguments)
{
    if (m_busy) {
        return;
    }
    m_busy = true;
    m_arguments = arguments;
    m_stdout.clear();
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_lastError.clear();
    m_installedProgram.clear();
    m_installedTag.clear();

    qint64 checked = 0;
    QFile file(installedFile());
    if (file.open(QIODevice::ReadOnly)) {
        const auto object = QJsonDocument::fromJson(file.readAll()).object();
        const auto program = object.value("program").toString();
        // The program is one of ours: nothing else is ever run from this file's say-so
        if (!program.isEmpty() && !QDir::isAbsolutePath(program) && !program.contains("..")) {
            const auto path = QDir(rootDir()).absoluteFilePath(program);
            if (QFileInfo(path).isFile()) {
                m_installedProgram = path;
                m_installedTag = object.value("tag").toString();
                checked = object.value("checked").toInteger();
            }
        }
    }

    if (!m_installedProgram.isEmpty() && QDateTime::currentSecsSinceEpoch() - checked < CheckEverySecs) {
        start(m_installedProgram);
        return;
    }
    fetchReleases();
}

void HorizonBackend::fetchReleases()
{
    m_asset = Horizon::platformArchive();
    if (m_asset.isEmpty()) {
        downloadFailed(tr("Horizon publishes no launcher for this system"));
        return;
    }
    m_asset.prepend("horizon-launcher-");

    emit status(tr("Looking for Horizon's tools..."));
    auto [download, bytes] = Net::Download::makeByteArray(QUrl(ReleasesUrl));
    m_download = download;
    m_downloaded = bytes;
    auto job = makeShared<NetJob>(QString("Horizon::LauncherReleases"), APPLICATION->network());
    job->setAskRetry(false);
    job->addNetAction(m_download);
    connect(job.get(), &Task::succeeded, this, &HorizonBackend::releasesFetched, Qt::QueuedConnection);
    connect(
        job.get(), &Task::failed, this, [this](const QString& reason) { downloadFailed(reason); }, Qt::QueuedConnection);
    m_task = job;
    job->start();
}

void HorizonBackend::releasesFetched()
{
    if (!m_busy) {
        return;
    }

    // The newest release of the launcher that has an archive for this system
    m_tag.clear();
    const auto releases = QJsonDocument::fromJson(*m_downloaded).array();
    for (const auto& value : releases) {
        const auto release = value.toObject();
        const auto tag = release.value("tag_name").toString();
        if (!tag.startsWith(TagPrefix) || release.value("draft").toBool()) {
            continue;
        }
        const auto assets = release.value("assets").toArray();
        const bool here =
            std::any_of(assets.begin(), assets.end(), [this](const auto& asset) { return asset.toObject().value("name").toString() == m_asset; });
        if (here) {
            m_tag = tag;
            break;
        }
    }
    static const QRegularExpression s_tag("^launcher@[A-Za-z0-9][A-Za-z0-9._-]*$");
    if (!s_tag.match(m_tag).hasMatch()) {
        downloadFailed(tr("no release of Horizon's launcher has %1").arg(m_asset));
        return;
    }

    if (m_tag == m_installedTag && !m_installedProgram.isEmpty()) {
        // Still the newest: remember that it was looked for
        QFile file(installedFile());
        if (file.open(QIODevice::ReadOnly)) {
            auto object = QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            object["checked"] = QDateTime::currentSecsSinceEpoch();
            try {
                FS::write(installedFile(), QJsonDocument(object).toJson());
            } catch (const Exception&) {
                // It is looked for again next time
            }
        }
        start(m_installedProgram);
        return;
    }

    auto [download, bytes] = Net::Download::makeByteArray(QUrl(DownloadUrl.arg(m_tag, QStringLiteral("SHA256SUMS"))));
    m_download = download;
    m_downloaded = bytes;
    auto job = makeShared<NetJob>(QString("Horizon::LauncherChecksums"), APPLICATION->network());
    job->setAskRetry(false);
    job->addNetAction(m_download);
    connect(job.get(), &Task::succeeded, this, &HorizonBackend::checksumsFetched, Qt::QueuedConnection);
    connect(
        job.get(), &Task::failed, this, [this](const QString& reason) { downloadFailed(reason); }, Qt::QueuedConnection);
    m_task = job;
    job->start();
}

void HorizonBackend::checksumsFetched()
{
    if (!m_busy) {
        return;
    }
    m_sha256 = Horizon::checksumFor(*m_downloaded, m_asset);
    if (m_sha256.isEmpty()) {
        downloadFailed(tr("the release %1 lists no checksum for %2").arg(m_tag, m_asset));
        return;
    }

    m_archivePath = FS::PathCombine(rootDir(), "downloads", m_asset);
    emit status(tr("Downloading Horizon's tools (%1)...").arg(m_tag));
    auto download = Net::Download::makeFile(QUrl(DownloadUrl.arg(m_tag, m_asset)), m_archivePath);
    download->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha256, m_sha256));
    auto job = makeShared<NetJob>(QString("Horizon::LauncherDownload"), APPLICATION->network());
    job->setAskRetry(false);
    job->addNetAction(download);
    connect(job.get(), &Task::succeeded, this, &HorizonBackend::archiveDownloaded, Qt::QueuedConnection);
    connect(
        job.get(), &Task::failed, this, [this](const QString& reason) { downloadFailed(reason); }, Qt::QueuedConnection);
    connect(job.get(), &Task::progress, this, [this](qint64 current, qint64 total) {
        if (total > 0) {
            emit status(tr("Downloading Horizon's tools (%1): %2%").arg(m_tag).arg(current * 100 / total));
        }
    });
    m_task = job;
    job->start();
}

void HorizonBackend::archiveDownloaded()
{
    if (!m_busy) {
        return;
    }
    auto folder = m_tag;
    folder.replace(QRegularExpression("[^A-Za-z0-9._-]"), "_");
    m_extractDir = FS::PathCombine(rootDir(), "bin", folder);
    FS::deletePath(m_extractDir);
    if (!FS::ensureFolderPathExists(m_extractDir)) {
        downloadFailed(tr("could not create the folder %1").arg(m_extractDir));
        return;
    }

    emit status(tr("Unpacking Horizon's tools..."));
    auto task = makeShared<MMCZip::ExtractZipTask>(m_archivePath, QDir(m_extractDir));
    connect(task.get(), &Task::succeeded, this, &HorizonBackend::archiveExtracted, Qt::QueuedConnection);
    connect(
        task.get(), &Task::failed, this, [this](const QString& reason) { downloadFailed(reason); }, Qt::QueuedConnection);
    m_task = task;
    task->start();
}

void HorizonBackend::archiveExtracted()
{
    if (!m_busy) {
        return;
    }
    const auto program = locateProgram(m_extractDir);
    if (program.isEmpty()) {
        FS::deletePath(m_extractDir);
        downloadFailed(tr("%1 holds no %2").arg(m_asset, programName()));
        return;
    }
#ifndef Q_OS_WIN
    QFile file(program);
    file.setPermissions(file.permissions() | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
#endif
    QFile::remove(m_archivePath);

    const QDir root(rootDir());
    QJsonObject installed;
    installed["tag"] = m_tag;
    installed["sha256"] = m_sha256;
    installed["program"] = root.relativeFilePath(program);
    installed["checked"] = QDateTime::currentSecsSinceEpoch();
    try {
        FS::write(installedFile(), QJsonDocument(installed).toJson());
    } catch (const Exception& e) {
        downloadFailed(e.cause());
        return;
    }

    // The programs of releases before this one
    const QDir bin(root.absoluteFilePath("bin"));
    const auto keep = QDir(m_extractDir).dirName();
    for (const auto& entry : bin.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.fileName() != keep) {
            FS::deletePath(entry.absoluteFilePath());
        }
    }

    start(program);
}

void HorizonBackend::downloadFailed(const QString& reason)
{
    if (!m_busy) {
        return;
    }
    if (!m_installedProgram.isEmpty()) {
        // An older program still does the work
        emit line(tr("Could not get the newest Horizon tools (%1). Using the installed ones.").arg(reason), true);
        start(m_installedProgram);
    } else {
        finish(false, tr("Could not download Horizon's tools: %1").arg(reason));
    }
}

void HorizonBackend::start(const QString& program)
{
    m_task.reset();
    emit status({});

    FS::ensureFolderPathExists(dataDir());
    FS::ensureFolderPathExists(m_gameDir);

    auto environment = QProcessEnvironment::systemEnvironment();
    // Its own data folder, and the instance's game folder as the folder its default instance plays in
    environment.insert("HORIZON_LAUNCHER_HOME", QDir::toNativeSeparators(dataDir()));
    environment.insert("HORIZON_HOME", QDir::toNativeSeparators(m_gameDir));
    // This launcher installs it, and replaces it
    environment.insert("HORIZON_LAUNCHER_NO_SELF_UPDATE", "1");
    // No account of its own is used: the game is started by this launcher, with this launcher's account
    environment.insert("HORIZON_NO_KEYCHAIN", "1");

    auto* process = new QProcess(this);
    process->setProcessEnvironment(environment);
    process->setWorkingDirectory(dataDir());
    m_process = process;

    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        const auto data = process->readAllStandardOutput();
        m_stdout += data;
        emitLines(m_stdoutBuffer, data, false);
    });
    connect(process, &QProcess::readyReadStandardError, this,
            [this, process] { emitLines(m_stderrBuffer, process->readAllStandardError(), true); });
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        process->deleteLater();
        const auto rest = process->readAllStandardOutput();
        m_stdout += rest;
        emitLines(m_stdoutBuffer, rest + '\n', false);
        emitLines(m_stderrBuffer, process->readAllStandardError() + '\n', true);

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            finish(true, {});
        } else if (exitStatus != QProcess::NormalExit) {
            finish(false, tr("Stopped."));
        } else {
            finish(false, m_lastError.isEmpty() ? tr("%1 ended with exit code %2").arg(programName()).arg(exitCode) : m_lastError);
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        // The others end in `finished`
        if (error != QProcess::FailedToStart) {
            return;
        }
        process->deleteLater();
        finish(false, tr("Could not start %1: %2").arg(process->program(), process->errorString()));
    });

    process->start(program, m_arguments);
    process->closeWriteChannel();
}

void HorizonBackend::emitLines(QByteArray& buffer, const QByteArray& data, bool isError)
{
    buffer += data;
    for (auto end = buffer.indexOf('\n'); end >= 0; end = buffer.indexOf('\n')) {
        const auto text = QString::fromUtf8(buffer.left(end)).remove(QChar::CarriageReturn);
        buffer.remove(0, end + 1);
        if (text.trimmed().isEmpty()) {
            continue;
        }
        if (isError) {
            m_lastError = text.trimmed();
        }
        emit line(text, isError);
    }
}

void HorizonBackend::finish(bool succeeded, const QString& error)
{
    if (!m_busy) {
        return;
    }
    m_busy = false;
    m_task.reset();
    emit status({});
    emit finished(succeeded, m_stdout, error);
}

void HorizonBackend::abort()
{
    if (m_task) {
        disconnect(m_task.get(), nullptr, this, nullptr);
        if (m_task->isRunning() && m_task->canAbort()) {
            m_task->abort();
        }
        finish(false, tr("Stopped."));
        return;
    }
    if (m_process && m_process->state() != QProcess::NotRunning) {
#ifdef Q_OS_WIN
        // A build is a tree of programs (Cargo, the compiler): all of it has to go
        QProcess::startDetached("taskkill", { "/PID", QString::number(m_process->processId()), "/T", "/F" });
#else
        m_process->kill();
#endif
    }
}
