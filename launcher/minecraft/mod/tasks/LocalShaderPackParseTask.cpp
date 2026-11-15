// SPDX-FileCopyrightText: 2022 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Rachel Powers <508861+Ryex@users.noreply.github.com>
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

#include "LocalShaderPackParseTask.h"

#include <QFile>
#include <QImage>

#include <toml++/toml.h>

#include "FileSystem.h"
#include "archive/ArchiveReader.h"

namespace ShaderPackUtils {

ShaderPack::Details readHorizonManifest(const QByteArray& contents)
{
    // docs/shaders.md in horizon-rust/horizon: name, and optionally description, authors and license
    toml::table table;
#if TOML_EXCEPTIONS
    try {
        table = toml::parse(contents.toStdString());
    } catch ([[maybe_unused]] const toml::parse_error& err) {
        return {};
    }
#else
    toml::parse_result result = toml::parse(contents.toStdString());
    if (!result) {
        return {};
    }
    table = result.table();
#endif

    auto text = [&table](const char* key) {
        const auto value = table[key].value<std::string>();
        return value ? QString::fromStdString(*value) : QString();
    };
    ShaderPack::Details details;
    details.name = text("name");
    details.description = text("description");
    details.license = text("license");
    if (auto* authors = table["authors"].as_array()) {
        for (auto&& author : *authors) {
            if (const auto name = author.value<std::string>()) {
                details.authors.append(QString::fromStdString(*name));
            }
        }
    }
    return details;
}

bool process(ShaderPack& pack, ProcessingLevel level)
{
    switch (pack.type()) {
        case ResourceType::FOLDER:
            return ShaderPackUtils::processFolder(pack, level);
        case ResourceType::ZIPFILE:
            return ShaderPackUtils::processZIP(pack, level);
        default:
            qWarning() << "Invalid type for shader pack parse task!";
            return false;
    }
}

bool processFolder(ShaderPack& pack, ProcessingLevel level)
{
    Q_ASSERT(pack.type() == ResourceType::FOLDER);

    QFileInfo shaders_dir_info(FS::PathCombine(pack.fileinfo().filePath(), "shaders"));
    // Horizon's own shader packs have a manifest instead (docs/shaders.md in horizon-rust/horizon)
    QFileInfo horizon_manifest(FS::PathCombine(pack.fileinfo().filePath(), "horizon-shaders.toml"));
    if (!horizon_manifest.isFile() && (!shaders_dir_info.exists() || !shaders_dir_info.isDir())) {
        return false;  // assets dir does not exists or isn't valid
    }
    pack.setPackFormat(ShaderPackFormat::VALID);

    if (level == ProcessingLevel::BasicInfoOnly) {
        return true;  // only need basic info already checked
    }

    if (horizon_manifest.isFile()) {
        QFile manifest(horizon_manifest.filePath());
        if (manifest.open(QIODevice::ReadOnly)) {
            pack.setDetails(readHorizonManifest(manifest.readAll()));
        }
        // A Horizon pack may have an icon beside its manifest
        if (QImage icon(FS::PathCombine(pack.fileinfo().filePath(), "icon.png")); !icon.isNull()) {
            pack.setImage(icon);
        }
    }

    return true;  // all tests passed
}

bool processZIP(ShaderPack& pack, ProcessingLevel level)
{
    Q_ASSERT(pack.type() == ResourceType::ZIPFILE);

    MMCZip::ArchiveReader zip(pack.fileinfo().filePath());
    if (!zip.collectFiles(false))
        return false;  // can't open zip file

    if (!zip.exists("/shaders") && !zip.exists("horizon-shaders.toml")) {
        // assets dir does not exists at zip root, but shader packs
        // will sometimes be a zip file containing a folder with the
        // actual contents in it. This happens
        // e.g. when the shader pack is downloaded as code
        // from Github. so other than "/shaders", we
        // could also check for a "shaders" folder one level deep.

        QStringList files = zip.getFiles();

        // the assumption here is that there is just one
        // folder with the "shader" subfolder. In case
        // there are multiple, the first one is picked.
        bool isShaderPresent = false;
        for (QString f : files) {
            if (f.contains("/shaders/", Qt::CaseInsensitive)) {
                isShaderPresent = true;
                break;
            }
        }

        if (!isShaderPresent)
            // assets dir does not exist.
            return false;
    }
    pack.setPackFormat(ShaderPackFormat::VALID);

    if (level == ProcessingLevel::BasicInfoOnly) {
        return true;  // only need basic info already checked
    }

    if (zip.exists("horizon-shaders.toml")) {
        if (auto manifest = zip.goToFile("horizon-shaders.toml")) {
            pack.setDetails(readHorizonManifest(manifest->readAll()));
        }
        if (zip.exists("icon.png")) {
            if (auto icon = zip.goToFile("icon.png")) {
                if (const auto image = QImage::fromData(icon->readAll()); !image.isNull()) {
                    pack.setImage(image);
                }
            }
        }
    }

    return true;
}

bool validate(QFileInfo file)
{
    ShaderPack sp{ file };
    return ShaderPackUtils::process(sp, ProcessingLevel::BasicInfoOnly) && sp.valid();
}

}  // namespace ShaderPackUtils

LocalShaderPackParseTask::LocalShaderPackParseTask(int token, ShaderPack& sp) : Task(false), m_token(token), m_shader_pack(sp) {}

bool LocalShaderPackParseTask::abort()
{
    m_aborted = true;
    return true;
}

void LocalShaderPackParseTask::executeTask()
{
    if (!ShaderPackUtils::process(m_shader_pack)) {
        emitFailed("this is not a shader pack");
        return;
    }

    if (m_aborted)
        emitAborted();
    else
        emitSucceeded();
}
