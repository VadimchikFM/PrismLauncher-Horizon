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

#include "HorizonMods.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>

#include <toml++/toml.h>

#include "FileSystem.h"
#include "archive/ArchiveReader.h"
#include "modplatform/ModIndex.h"
#include "modplatform/packwiz/Packwiz.h"

namespace {

const QString ContentManifest = QStringLiteral("horizon-mod.toml");
const QString NativeManifest = QStringLiteral("horizon-native.json");
const QString SwitchesFile = QStringLiteral("horizon-mods.txt");
/// Marks `horizon-native-mods` as the launcher's copy for the build (adoptNativeMods).
const QString MirrorMarker = QStringLiteral(".prismlauncher");

QString tr(const char* text)
{
    return QCoreApplication::translate("HorizonMods", text);
}

bool validId(const QString& id)
{
    static const QRegularExpression s_id("^[a-z0-9_]{1,64}$");
    return s_id.match(id).hasMatch();
}

/// `id`, `name` and `version` of a content mod's manifest.
void readContentManifest(const QByteArray& contents, HorizonMods::Mod& mod)
{
    toml::table table;
#if TOML_EXCEPTIONS
    try {
        table = toml::parse(contents.toStdString());
    } catch ([[maybe_unused]] const toml::parse_error& err) {
        return;
    }
#else
    toml::parse_result result = toml::parse(contents.toStdString());
    if (!result) {
        return;
    }
    table = result.table();
#endif
    const auto text = [&table](const char* key) {
        const auto value = table[key].value<std::string>();
        return value ? QString::fromStdString(*value) : QString();
    };
    if (mod.id.isEmpty()) {
        mod.id = text("id");
    }
    if (mod.version.isEmpty()) {
        mod.version = text("version");
    }
    mod.name = text("name");
}

/// `id` and `version` of a native mod's manifest.
void readNativeManifest(const QByteArray& contents, HorizonMods::Mod& mod)
{
    const auto object = QJsonDocument::fromJson(contents).object();
    mod.id = object.value("id").toString();
    mod.version = object.value("version").toString();
    mod.native = !mod.id.isEmpty();
}

/// What the mod file (a zip) or folder at `path` says it is.
HorizonMods::Mod readMod(const QString& path)
{
    HorizonMods::Mod mod;
    mod.path = path;

    const QFileInfo info(path);
    if (info.isDir()) {
        QFile manifest(QDir(path).filePath(ContentManifest));
        if (manifest.open(QIODevice::ReadOnly)) {
            readContentManifest(manifest.readAll(), mod);
        }
    } else {
        // What a file holds decides what it is: code (horizon-native.json), content (horizon-mod.toml), or both.
        MMCZip::ArchiveReader zip(path);
        if (auto file = zip.goToFile(NativeManifest); file) {
            readNativeManifest(file->readAll(), mod);
        }
        if (auto file = zip.goToFile(ContentManifest); file) {
            readContentManifest(file->readAll(), mod);
        }
    }

    if (!validId(mod.id)) {
        mod.error = tr("It has no readable manifest (horizon-mod.toml or horizon-native.json).");
        mod.id.clear();
    }
    if (mod.name.isEmpty()) {
        mod.name = mod.id.isEmpty() ? info.fileName() : mod.id;
    }
    return mod;
}

/// The lines of `horizon-mods.txt`.
QStringList readSwitches(const QString& gameDir)
{
    QFile file(QDir(gameDir).filePath(SwitchesFile));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).remove(QChar::CarriageReturn).split(QChar::LineFeed, Qt::SkipEmptyParts);
}

bool writeSwitches(const QString& gameDir, const QStringList& lines, QString* error)
{
    try {
        FS::write(QDir(gameDir).filePath(SwitchesFile), (lines.join('\n') + '\n').toUtf8());
    } catch (const Exception& e) {
        if (error != nullptr) {
            *error = e.cause();
        }
        return false;
    }
    return true;
}

/// Which mods `horizon-mods.txt` turns on and off: its `mod.<id>:true|false` lines.
QMap<QString, bool> switches(const QString& gameDir)
{
    QMap<QString, bool> result;
    for (const auto& line : readSwitches(gameDir)) {
        const auto colon = line.lastIndexOf(':');
        if (!line.startsWith("mod.") || colon < 0) {
            continue;
        }
        result[line.mid(4, colon - 4)] = line.mid(colon + 1).trimmed() == "true";
    }
    return result;
}

/// The SHA-256 of a file, for telling a changed mod from the one built.
QString sha256Of(const QString& path)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        hash.addData(&file);
    }
    return QString::fromLatin1(hash.result().toHex());
}

/**
 * Writes the launcher's metadata (where it was downloaded from) for what Horizon's own launcher installed: its record
 * of that is `mod-browser.json`. The Mods, Resource Packs and Shader Packs pages then show these as installed from
 * Horizon's registry, and can update them.
 */
void adoptBrowserRecords(const QString& gameDir)
{
    QFile file(QDir(gameDir).filePath("mod-browser.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const auto entries = QJsonDocument::fromJson(file.readAll()).object().value("entries").toArray();
    for (const auto& value : entries) {
        const auto entry = value.toObject();
        if (entry.value("source").toString() != "horizon") {
            continue;
        }
        const auto project = entry.value("project").toString();
        const auto fileName = QFileInfo(entry.value("file").toString()).fileName();
        const auto kind = entry.value("kind").toString();
        if (!validId(project) || fileName.isEmpty()) {
            continue;
        }

        // Where the launcher keeps that kind now
        QString folder;
        if (kind == "mod") {
            folder = HorizonMods::modsDir(gameDir);
        } else if (kind == "resource-pack") {
            folder = QDir(gameDir).filePath("resourcepacks");
        } else if (kind == "shader") {
            folder = QDir(gameDir).filePath("shaderpacks");
        } else {
            continue;
        }
        if (!QFileInfo(QDir(folder).filePath(fileName)).isFile()) {
            continue;
        }
        const QDir indexDir(QDir(folder).filePath(".index"));
        const auto slug = entry.value("slug").toString(project);
        if (QFileInfo(indexDir.filePath(slug + ".pw.toml")).exists()) {
            continue;
        }

        ModPlatform::IndexedPack pack;
        pack.addonId = project;
        pack.provider = ModPlatform::ResourceProvider::HORIZON;
        pack.name = entry.value("title").toString(project);
        pack.slug = slug;

        ModPlatform::IndexedVersion version;
        version.addonId = project;
        version.fileId = entry.value("version").toString();
        version.version = version.fileId.toString();
        version.version_number = entry.value("number").toString(version.version);
        version.version_type = ModPlatform::IndexedVersionType::Release;
        version.fileName = fileName;
        version.hash_type = "sha256";
        version.hash = entry.value("checksum").toString().section(':', -1).toLower();
        if (kind == "mod") {
            version.loaders = ModPlatform::Horizon;
        }

        FS::ensureFolderPathExists(indexDir.absolutePath());
        auto metadata = Packwiz::V1::createModFormat(indexDir, pack, version);
        Packwiz::V1::updateModIndex(indexDir, metadata);
    }
}

}  // namespace

namespace HorizonMods {

QString modsDir(const QString& gameDir)
{
    return QDir(gameDir).filePath("horizon-mods");
}

QString nativeDir(const QString& gameDir)
{
    return QDir(gameDir).filePath("horizon-native-mods");
}

QList<Mod> scan(const QString& gameDir)
{
    const auto on = switches(gameDir);
    QList<Mod> mods;

    const QDir dir(modsDir(gameDir));
    const auto files =
        dir.entryInfoList(QStringList{ "*.hmod", "*.hnmod", "*.hmod.disabled", "*.hnmod.disabled" }, QDir::Files, QDir::Name);
    for (const auto& file : files) {
        auto mod = readMod(file.absoluteFilePath());
        mod.enabled = !mod.id.isEmpty() && !file.fileName().endsWith(".disabled") && on.value(mod.id, true);
        mods.append(mod);
    }
    // An unpacked content mod: a folder holding its manifest
    for (const auto& folder : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (QFileInfo(QDir(folder.absoluteFilePath()).filePath(ContentManifest)).isFile()) {
            auto mod = readMod(folder.absoluteFilePath());
            mod.enabled = !mod.id.isEmpty() && on.value(mod.id, true);
            mods.append(mod);
        }
    }
    std::sort(mods.begin(), mods.end(), [](const Mod& a, const Mod& b) { return a.name.localeAwareCompare(b.name) < 0; });
    return mods;
}

std::optional<bool> switchState(const QString& gameDir, const QString& id)
{
    const auto all = switches(gameDir);
    if (!all.contains(id)) {
        return std::nullopt;
    }
    return all.value(id);
}

bool setEnabled(const QString& gameDir, const QString& id, bool enabled, QString* error)
{
    if (!validId(id)) {
        if (error != nullptr) {
            *error = tr("The mod has no id.");
        }
        return false;
    }

    const auto prefix = QString("mod.%1:").arg(id);
    const auto wanted = prefix + (enabled ? "true" : "false");
    auto lines = readSwitches(gameDir);
    bool found = false;
    for (auto& line : lines) {
        if (line.startsWith(prefix)) {
            line = wanted;
            found = true;
        }
    }
    if (!found) {
        lines.append(wanted);
    }
    return writeSwitches(gameDir, lines, error);
}

QString nativeSignature(const QString& gameDir)
{
    QStringList parts;
    for (const auto& mod : scan(gameDir)) {
        if (mod.native && mod.enabled) {
            parts.append(QString("%1=%2=%3").arg(mod.id, mod.version, sha256Of(mod.path)));
        }
    }
    parts.sort();
    return parts.join(';');
}

bool mirrorNativeMods(const QString& gameDir, QString* error)
{
    const auto fail = [error](const QString& reason) {
        if (error != nullptr) {
            *error = reason;
        }
        return false;
    };

    const QDir mirror(nativeDir(gameDir));
    if (!FS::ensureFolderPathExists(mirror.absolutePath())) {
        return fail(tr("Could not create the folder %1").arg(mirror.absolutePath()));
    }
    // What the previous build had
    for (const auto& file : mirror.entryInfoList(QStringList{ "*.hnmod" }, QDir::Files)) {
        if (!QFile::remove(file.absoluteFilePath())) {
            return fail(tr("Could not remove %1").arg(file.absoluteFilePath()));
        }
    }
    for (const auto& mod : scan(gameDir)) {
        if (!mod.native || !mod.enabled) {
            continue;
        }
        const auto target = mirror.filePath(mod.id + ".hnmod");
        if (!QFile::copy(mod.path, target)) {
            return fail(tr("Could not copy %1 to %2").arg(mod.path, target));
        }
        if (!setEnabled(gameDir, mod.id, true, error)) {
            return false;
        }
    }
    try {
        FS::write(mirror.filePath(MirrorMarker), {});
    } catch (const Exception& e) {
        return fail(e.cause());
    }
    return true;
}

void adoptNativeMods(const QString& gameDir)
{
    const QDir mirror(nativeDir(gameDir));
    if (mirror.exists() && !QFileInfo(mirror.filePath(MirrorMarker)).exists()) {
        FS::ensureFolderPathExists(modsDir(gameDir));
        QStringList present;
        for (const auto& mod : scan(gameDir)) {
            present.append(mod.id);
        }
        for (const auto& file : mirror.entryInfoList(QStringList{ "*.hnmod" }, QDir::Files)) {
            const auto mod = readMod(file.absoluteFilePath());
            if (mod.id.isEmpty() || present.contains(mod.id)) {
                continue;
            }
            const auto target = QDir(modsDir(gameDir)).filePath(file.fileName());
            if (QFile::rename(file.absoluteFilePath(), target)) {
                present.append(mod.id);
            }
        }
        try {
            FS::write(mirror.filePath(MirrorMarker), {});
        } catch (const Exception&) {
            // Tried again next time
        }
    }
    adoptBrowserRecords(gameDir);
}

}  // namespace HorizonMods
