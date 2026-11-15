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

#include "HorizonPackCreationTask.h"

#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>

#include "FileSystem.h"
#include "minecraft/HorizonBackend.h"
#include "minecraft/HorizonComponent.h"
#include "minecraft/HorizonMods.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/INISettingsObject.h"

bool HorizonPackCreationTask::abort()
{
    // What installs the pack's mods stops too; the task itself ends as any instance creation does
    if (m_backend != nullptr) {
        m_backend->abort();
    }
    return InstanceCreationTask::abort();
}

std::unique_ptr<MinecraftInstance> HorizonPackCreationTask::createInstance()
{
    setStatus(tr("Creating an instance for the modpack %1").arg(m_pack));

    auto inst = std::make_unique<MinecraftInstance>(m_globalSettings, std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg")),
                                                    m_stagingPath);
    {
        SettingsObject::Lock lock(inst->settings());
        auto components = inst->getPackProfile();
        components->buildingFromScratch();
        components->setComponentVersion("net.minecraft", m_minecraft, true);
        HorizonComponent::install(components, {});
        inst->setName(name());
        inst->setIconKey(m_instIcon);
    }

    const auto gameDir = QDir(inst->gameRoot()).absolutePath();
    if (!FS::ensureFolderPathExists(gameDir)) {
        setError(tr("Could not create the folder %1").arg(gameDir));
        return nullptr;
    }

    // Horizon's launcher installs the pack: its mods, from the registry, each one checked
    bool succeeded = false;
    QByteArray output;
    QString error;
    {
        HorizonBackend backend(gameDir);
        QEventLoop loop;
        connect(&backend, &HorizonBackend::status, this, [this](const QString& text) {
            if (!text.isEmpty()) {
                setStatus(text);
            }
        });
        connect(&backend, &HorizonBackend::line, this, [this](const QString& text, bool) { setDetails(text.trimmed()); });
        connect(&backend, &HorizonBackend::finished, &loop, [&](bool ok, const QByteArray& standardOutput, const QString& reason) {
            succeeded = ok;
            output = standardOutput;
            error = reason;
            loop.quit();
        });
        m_backend = &backend;
        setStatus(tr("Installing the mods of %1...").arg(m_pack));
        backend.run({ "mods", "install", m_pack, "--source", "horizon", "--json" });
        loop.exec();
        m_backend = nullptr;
    }
    setDetails({});
    if (m_abort) {
        return nullptr;
    }
    if (!succeeded) {
        setError(tr("Horizon's launcher could not install the modpack: %1").arg(error));
        return nullptr;
    }

    // {"pack": "atomic_industry", "version": "1.0.0", "mods": [...], "home": "<game folder>/horizon-packs/<id>/home"}
    // The pack's own game folder holds its mods and their switches: its contents are the instance's game folder
    const QDir game(gameDir);
    const auto home = QDir(QJsonDocument::fromJson(output).object().value("home").toString()).absolutePath();
    const auto relative = game.relativeFilePath(home);
    if (!QFileInfo(home).isDir() || relative.startsWith("..") || QDir::isAbsolutePath(relative)) {
        setError(tr("Horizon's launcher installed the modpack, but not where it could be found (%1).").arg(home));
        return nullptr;
    }
    setStatus(tr("Moving the modpack into the instance..."));
    for (const auto& entry : QDir(home).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) {
        const auto target = game.filePath(entry.fileName());
        // What the launcher wrote beside the pack (its browser's list) gives way to the pack's own
        FS::deletePath(target);
        if (!QDir().rename(entry.absoluteFilePath(), target)) {
            setError(tr("Could not move %1 into the instance.").arg(entry.fileName()));
            return nullptr;
        }
    }
    FS::deletePath(game.filePath("horizon-packs"));
    // Its native mods, where the instance's Mods page keeps them
    HorizonMods::adoptNativeMods(gameDir);

    return inst;
}
