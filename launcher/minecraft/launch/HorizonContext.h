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

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>
#include <QString>
#include <memory>

#include "Application.h"
#include "FileSystem.h"

/**
 * What the launch steps that start Horizon share.
 *
 * Horizon (https://github.com/horizon-rust/horizon) is Minecraft: Java Edition rewritten in Rust. It is started
 * through its Launch API (docs/api/launch.md in that repository): a build is one folder holding `horizon-client`,
 * `horizon-prepare`, `research-lock.json` and `exporters/`; `horizon-prepare` turns the player's own Minecraft files
 * into the resources `horizon-client --resources` reads.
 *
 * HorizonInstall fills in the build, HorizonPrepare the resources and the options, HorizonLaunch starts the game.
 */
struct HorizonContext {
    /// The unpacked build, kept whole.
    QString buildDir;
    QString clientPath;
    QString preparePath;
    /// What the log calls this build.
    QString buildLabel;
    /// Whether the launcher downloaded this build itself (as opposed to a folder the player chose).
    bool managedBuild = false;
    /// `horizon-prepare`'s output, which `horizon-client --resources` reads.
    QString resourcesDir;
    /// The options `horizon-client --launch-api` lists. Any other is refused with exit code 2.
    QSet<QString> clientOptions;
};

using HorizonContextPtr = std::shared_ptr<HorizonContext>;

namespace Horizon {

/// The release every platform's newest build is published under.
inline const QString ReleaseBaseUrl = QStringLiteral("https://github.com/horizon-rust/horizon/releases/download/nightly/");

/// `name`, as the file is called on this system.
inline QString executableName(const QString& name)
{
#ifdef Q_OS_WIN
    return name + ".exe";
#else
    return name;
#endif
}

/// Where the launcher keeps what it downloads and prepares for Horizon.
inline QString rootDir()
{
    return QDir(APPLICATION->dataRoot()).absoluteFilePath("horizon");
}

/// How Horizon's release archives end for this system (`horizon-<this>`, `horizon-launcher-<this>`), or nothing
/// when it publishes none for it.
inline QString platformArchive()
{
    const auto arch = QSysInfo::currentCpuArchitecture();
    [[maybe_unused]] const bool x64 = arch == "x86_64";
    [[maybe_unused]] const bool arm64 = arch == "arm64";
#if defined(Q_OS_WIN)
    if (x64) {
        return QStringLiteral("windows-x64.zip");
    }
    if (arm64) {
        return QStringLiteral("windows-arm64.zip");
    }
#elif defined(Q_OS_MACOS)
    if (arm64) {
        return QStringLiteral("macos-arm64.zip");
    }
    if (x64) {
        return QStringLiteral("macos-x64.zip");
    }
#elif defined(Q_OS_LINUX)
    if (x64) {
        return QStringLiteral("linux-x86_64.tar.gz");
    }
#endif
    return {};
}

/// The SHA-256 that a release's SHA256SUMS lists for `asset`: its lines are `<hex>  <name>` (or `<hex> *<name>`).
inline QString checksumFor(const QByteArray& sums, const QString& asset)
{
    static const QRegularExpression s_sha256("^[0-9a-f]{64}$");
    for (const auto& rawLine : sums.split('\n')) {
        const auto line = QString::fromUtf8(rawLine).trimmed();
        const auto space = line.indexOf(' ');
        if (space <= 0) {
            continue;
        }
        auto name = line.mid(space).trimmed();
        if (name.startsWith('*')) {
            name.remove(0, 1);
        }
        if (name == asset) {
            const auto hex = line.left(space).toLower();
            return s_sha256.match(hex).hasMatch() ? hex : QString();
        }
    }
    return {};
}

/// Whether `dir` holds a build's two programs.
inline bool isBuild(const QString& dir)
{
    return !dir.isEmpty() && QFileInfo(FS::PathCombine(dir, executableName("horizon-client"))).isFile() &&
           QFileInfo(FS::PathCombine(dir, executableName("horizon-prepare"))).isFile();
}

}  // namespace Horizon
