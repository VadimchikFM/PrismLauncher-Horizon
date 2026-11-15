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

#include <launch/LaunchStep.h>

#include "HorizonContext.h"
#include "LaunchMode.h"
#include "net/Download.h"

/**
 * Finds the Horizon build to play with: the folder named in the settings, else the newest nightly, which is
 * downloaded from the project's GitHub releases, checked against the release's SHA256SUMS and unpacked into a
 * folder of its own.
 */
class HorizonInstall : public LaunchStep {
    Q_OBJECT
   public:
    /// @param mode an offline launch must not use the network, so only a build that is already here will do; the
    /// demo is not something Horizon has.
    explicit HorizonInstall(LaunchTask* parent, HorizonContextPtr context, LaunchMode mode);
    ~HorizonInstall() override = default;

    void executeTask() override;
    bool canAbort() const override { return true; }

   public slots:
    bool abort() override;

   private slots:
    void checksumsFetched();
    void archiveDownloaded();
    void archiveExtracted();

   private:
    /// Points the context at the build in `dir`. False when there is none.
    bool useBuild(const QString& dir, const QString& label, bool managed);
    bool useInstalledBuild();
    void readInstalled();
    /// Asks the release at `m_baseUrl` for its SHA256SUMS: what its archive for this system is.
    void fetchChecksums();
    /// The update could not be had: play the installed build, or fail when there is none.
    void updateUnavailable(const QString& reason);
    void fail(const QString& reason);
    void watch(Task* task);

   private:
    HorizonContextPtr m_context;
    LaunchMode m_mode;

    Task::Ptr m_task;
    Net::Download::Ptr m_checksumsDownload;
    QByteArray* m_checksums = nullptr;

    /// The archive this system plays, e.g. horizon-windows-x64.zip.
    QString m_asset;
    /// The release the build comes from: its downloads' address, ending in a slash.
    QString m_baseUrl;
    /// The tag of the version the player picked for the instance; empty when it follows the newest Nightly.
    QString m_pinnedTag;
    /// The build the launcher installed last: its archive's SHA-256 and its folder.
    QString m_installedSha256;
    QString m_installedDir;
    /// The build being installed.
    QString m_newSha256;
    QString m_archivePath;
    QString m_extractDir;
};
