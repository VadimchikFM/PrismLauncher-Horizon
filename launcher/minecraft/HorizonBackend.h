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

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>

#include "net/Download.h"
#include "tasks/Task.h"

/**
 * Runs the command line of Horizon's own launcher (`horizon-launcher`, docs/launcher-commands.md in
 * https://github.com/horizon-rust/horizon) for one game folder.
 *
 * Horizon's mod registry and, above all, building the game with native mods (its own Rust toolchain, horizon-mixins,
 * the source of the pinned commit, the build cache) are the work of that program. It is the reference
 * implementation and it follows the game from one Nightly to the next, so this launcher drives it instead of
 * repeating it:
 *
 *  - `mods search`, `mods install`: the registry, with its checksums, signatures and revocations checked;
 *  - `install`: a release of the game in the launcher's own data folder, for builds to start from;
 *  - `native-build`: the game compiled with the folder's native mods, as a version of its own.
 *
 * The program is downloaded from the project's GitHub releases on first use and checked against the release's
 * SHA256SUMS. It works in a data folder of its own inside this launcher's (`HORIZON_LAUNCHER_HOME`), with the
 * instance's game folder as its game home (`HORIZON_HOME`), where its default instance plays: so what it installs
 * lands in the instance. It never starts the game: this launcher does (HorizonLaunch).
 */
class HorizonBackend : public QObject {
    Q_OBJECT
   public:
    explicit HorizonBackend(QString gameDir, QObject* parent = nullptr);
    ~HorizonBackend() override;

    /// Horizon's launcher's data folder: installed versions, Rust, the native builds' cache.
    static QString dataDir();
    /// The folder of an installed version (a release, or a native build): a whole build of the game.
    static QString versionDir(const QString& version);
    static bool isVersionInstalled(const QString& version);
    /// The log of the last native build.
    static QString buildLogPath();

    bool isBusy() const { return m_busy; }

    /// Runs `horizon-launcher <arguments>`, after downloading the program when it is not here yet. One at a time.
    void run(const QStringList& arguments);
    /// Stops what runs, and what it started.
    void abort();

   signals:
    /// A line the program printed.
    void line(const QString& text, bool isError);
    /// What is being done before the program runs: its download.
    void status(const QString& text);
    /// The command ended. `standardOutput` is all it printed on stdout (JSON, with --json).
    void finished(bool succeeded, const QByteArray& standardOutput, const QString& error);

   private:
    void fetchReleases();
    void releasesFetched();
    void checksumsFetched();
    void archiveDownloaded();
    void archiveExtracted();
    /// The newer program could not be had: run the installed one, or fail when there is none.
    void downloadFailed(const QString& reason);
    void start(const QString& program);
    void finish(bool succeeded, const QString& error);
    void emitLines(QByteArray& buffer, const QByteArray& data, bool isError);

   private:
    QString m_gameDir;
    QStringList m_arguments;
    bool m_busy = false;

    Task::Ptr m_task;
    Net::Download::Ptr m_download;
    QByteArray* m_downloaded = nullptr;

    /// The program installed before this run, if any.
    QString m_installedProgram;
    QString m_installedTag;
    /// The release being installed.
    QString m_tag;
    QString m_asset;
    QString m_sha256;
    QString m_archivePath;
    QString m_extractDir;

    QPointer<QProcess> m_process;
    QByteArray m_stdout;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    QString m_lastError;
};
