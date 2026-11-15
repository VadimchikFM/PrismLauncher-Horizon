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

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

#include <FileSystem.h>
#include <archive/ArchiveWriter.h>
#include <minecraft/HorizonMods.h>
#include <minecraft/mod/Mod.h>
#include <minecraft/mod/ShaderPack.h>
#include <minecraft/mod/tasks/LocalModParseTask.h>
#include <minecraft/mod/tasks/LocalShaderPackParseTask.h>
#include <translations/BundledTranslator.h>

#include <QBuffer>
#include <QImage>
#include <modplatform/horizon/HorizonAPI.h>
#include <modplatform/packwiz/Packwiz.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

/// The mods of a Horizon game folder: Horizon's Content API (docs/api/content.md in horizon-rust/horizon).
class HorizonModsTest : public QObject {
    Q_OBJECT

    /// A zip at `path` holding `files`.
    static void makeZip(const QString& path, const QMap<QString, QByteArray>& files)
    {
        MMCZip::ArchiveWriter zip(path);
        QVERIFY(zip.open());
        for (auto file = files.begin(); file != files.end(); ++file) {
            QVERIFY(zip.addFile(file.key(), file.value()));
        }
        QVERIFY(zip.close());
    }

    static void makeContentMod(const QString& path, const QString& id, const QString& name, const QString& version)
    {
        makeZip(path, { { "horizon-mod.toml", QString("id = \"%1\"\nname = \"%2\"\nversion = \"%3\"\napi = \"0.2\"\nauthors = [\"Alex\"]\n")
                                                  .arg(id, name, version)
                                                  .toUtf8() },
                        { "data/" + id.toUtf8() + "/recipe/thing.json", "{}" } });
    }

    static void makeNativeMod(const QString& path, const QString& id, const QString& version)
    {
        makeZip(path, { { "horizon-native.json",
                          QString(R"({"schema": 1, "id": "%1", "version": "%2", "game": "26.3"})").arg(id, version).toUtf8() },
                        { "mixins/" + id.toUtf8() + "/horizon-mixin.toml", "[mod]\n" } });
    }

    static QByteArray switches(const QString& gameDir) { return FS::read(QDir(gameDir).filePath("horizon-mods.txt")); }

    static HorizonMods::Mod find(const QString& gameDir, const QString& id)
    {
        for (const auto& mod : HorizonMods::scan(gameDir)) {
            if (mod.id == id) {
                return mod;
            }
        }
        return {};
    }

   private slots:
    void test_theLauncherReadsHorizonMods()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        // The launcher's mod list reads a Horizon mod's manifest as it reads a Fabric mod's
        const auto content = QDir(temp.path()).filePath("gems-1.2.hmod");
        makeContentMod(content, "gems", "Gems", "1.2.0");
        Mod gems(content);
        QCOMPARE(gems.type(), ResourceType::ZIPFILE);
        QVERIFY(ModUtils::process(gems, ModUtils::ProcessingLevel::Full));
        QCOMPARE(gems.mod_id(), QString("gems"));
        QCOMPARE(gems.name(), QString("Gems"));
        QCOMPARE(gems.version(), QString("1.2.0"));
        QCOMPARE(gems.authors(), QStringList{ "Alex" });

        const auto native = QDir(temp.path()).filePath("zoom-2.0.0.hnmod.disabled");
        makeNativeMod(native, "zoom", "2.0.0");
        Mod zoom(native);
        QCOMPARE(zoom.type(), ResourceType::ZIPFILE);
        QVERIFY(!zoom.enabled());
        QVERIFY(ModUtils::process(zoom, ModUtils::ProcessingLevel::Full));
        QCOMPARE(zoom.mod_id(), QString("zoom"));
        QCOMPARE(zoom.version(), QString("2.0.0"));
    }

    void test_modsAreSwitchedAndMirroredForTheBuild()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto gameDir = QDir(temp.path()).filePath("minecraft");
        const QDir mods(HorizonMods::modsDir(gameDir));
        QVERIFY(FS::ensureFolderPathExists(mods.absolutePath()));

        // An empty game folder has no mods and no native mods on
        QVERIFY(HorizonMods::scan(gameDir).isEmpty());
        QVERIFY(HorizonMods::nativeSignature(gameDir).isEmpty());

        // Every mod is in horizon-mods, as the launcher downloaded it; a turned off one is <file>.disabled
        makeContentMod(mods.filePath("gems-1.2.0.hmod"), "gems", "Gems", "1.2.0");
        makeNativeMod(mods.filePath("zoom-2.0.0.hnmod"), "zoom", "2.0.0");
        makeNativeMod(mods.filePath("minimap-1.0.0.hnmod.disabled"), "minimap", "1.0.0");
        FS::write(mods.filePath("lamps/horizon-mod.toml"), "id = \"lamps\"\nversion = \"0.1.0\"\n");

        QCOMPARE(HorizonMods::scan(gameDir).size(), qsizetype(4));
        QCOMPARE(find(gameDir, "gems").name, QString("Gems"));
        QVERIFY(!find(gameDir, "gems").native);
        QVERIFY(find(gameDir, "zoom").native);
        QVERIFY(find(gameDir, "zoom").enabled);
        QVERIFY(!find(gameDir, "minimap").enabled);
        QCOMPARE(find(gameDir, "lamps").version, QString("0.1.0"));

        // Horizon's switches: a line per mod, the game's other lines left alone
        FS::write(QDir(gameDir).filePath("horizon-mods.txt"), "showTips:true\nmod.other:true\n");
        QVERIFY(!HorizonMods::switchState(gameDir, "gems").has_value());
        QString error;
        QVERIFY(HorizonMods::setEnabled(gameDir, "gems", true, &error));
        QVERIFY(HorizonMods::setEnabled(gameDir, "gems", false, &error));
        QCOMPARE(switches(gameDir), QByteArray("showTips:true\nmod.other:true\nmod.gems:false\n"));
        QCOMPARE(HorizonMods::switchState(gameDir, "gems"), std::optional<bool>(false));
        // Turned off in the game: off, though its file is on
        QVERIFY(!find(gameDir, "gems").enabled);

        // The signature is of the native mods that are on: content mods and turned off mods don't change it
        const auto signature = HorizonMods::nativeSignature(gameDir);
        QVERIFY(signature.startsWith("zoom=2.0.0="));
        QVERIFY(!signature.contains("minimap"));
        QVERIFY(HorizonMods::setEnabled(gameDir, "gems", true, &error));
        QCOMPARE(HorizonMods::nativeSignature(gameDir), signature);
        QVERIFY(HorizonMods::setEnabled(gameDir, "zoom", false, &error));
        QVERIFY(HorizonMods::nativeSignature(gameDir).isEmpty());
        QVERIFY(HorizonMods::setEnabled(gameDir, "zoom", true, &error));
        QCOMPARE(HorizonMods::nativeSignature(gameDir), signature);

        // The build's folder gets the native mods that are on, and only those, each turned on
        const QDir mirror(HorizonMods::nativeDir(gameDir));
        QVERIFY(FS::ensureFolderPathExists(mirror.absolutePath()));
        makeNativeMod(mirror.filePath("old.hnmod"), "old", "0.1.0");
        QVERIFY2(HorizonMods::mirrorNativeMods(gameDir, &error), qPrintable(error));
        QCOMPARE(mirror.entryList(QStringList{ "*.hnmod" }, QDir::Files), QStringList{ "zoom.hnmod" });
        QCOMPARE(HorizonMods::switchState(gameDir, "zoom"), std::optional<bool>(true));
        // The mods themselves stay where they are
        QVERIFY(QFileInfo(mods.filePath("zoom-2.0.0.hnmod")).isFile());

        // From then on, the build's folder is the launcher's copy: nothing is adopted from it
        HorizonMods::adoptNativeMods(gameDir);
        QCOMPARE(HorizonMods::scan(gameDir).size(), qsizetype(4));
    }

    void test_nativeModsOfHorizonsLauncherAreAdopted()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto gameDir = QDir(temp.path()).filePath("minecraft");
        const QDir mods(HorizonMods::modsDir(gameDir));
        const QDir native(HorizonMods::nativeDir(gameDir));
        QVERIFY(FS::ensureFolderPathExists(mods.absolutePath()));
        QVERIFY(FS::ensureFolderPathExists(native.absolutePath()));

        // What Horizon's own launcher installed: native mods in horizon-native-mods, and its record of them
        makeNativeMod(native.filePath("zoom.hnmod"), "zoom", "2.0.0");
        makeNativeMod(native.filePath("fps_overlay.hnmod"), "fps_overlay", "2.0.0");
        // A mod the launcher already has stays where it is, once
        makeNativeMod(mods.filePath("fps_overlay-2.0.0.hnmod"), "fps_overlay", "2.0.0");
        FS::write(QDir(gameDir).filePath("mod-browser.json"),
                  R"({"entries": [{"source": "horizon", "project": "zoom", "slug": "zoom", "title": "Zoom", "kind": "mod",
                      "version": "2.0.0", "number": "2.0.0", "file": "horizon-native-mods/zoom.hnmod",
                      "checksum": "SHA-256:49fa2d7d635225c53304fbf1477606c0677eb23d94dac75b730f6c3ba065f127"}]})");

        HorizonMods::adoptNativeMods(gameDir);

        QVERIFY(QFileInfo(mods.filePath("zoom.hnmod")).isFile());
        QVERIFY(!QFileInfo(native.filePath("zoom.hnmod")).exists());
        QVERIFY(QFileInfo(native.filePath("fps_overlay.hnmod")).isFile());
        QCOMPARE(HorizonMods::scan(gameDir).size(), qsizetype(2));

        // Where it came from: the launcher's metadata, so the Mods page shows it as installed from Horizon's registry
        const auto metadata = QString::fromUtf8(FS::read(mods.filePath(".index/zoom.pw.toml")));
        QVERIFY2(metadata.contains("[update.horizon]"), qPrintable(metadata));
        QVERIFY(metadata.contains("project-id = 'zoom'") || metadata.contains("project-id = \"zoom\""));
        QVERIFY(metadata.contains("zoom.hnmod"));
    }

    void test_registryProjectsAreReadAsTheLauncherShowsThem()
    {
        // A project of the registry's index (docs/api/mod-index.md), as the index lists it: newest version first
        // "\/" is JSON's escape for "/": moc would read "//" as a comment, even in a raw string
        const auto project = QJsonDocument::fromJson(R"({
            "id": "zoom", "kind": "native-mod", "name": "Zoom", "summary": "Hold a key to zoom in.",
            "description": "The zoom of Horizon.", "authors": ["Horizon"], "license": "MIT",
            "source": "https:\/\/github.com/horizon-rust/horizon-mods/tree/main/native/zoom",
            "icon": "https:\/\/horizon-rust.github.io/horizon-mods/icons/zoom.png", "categories": ["utility"], "tags": ["camera"],
            "downloads": 84114, "versions": [
                {"version": "2.0.0", "published": "2026-10-29", "game": "26.3", "hooks": 1,
                 "api": "native-1/6f8c7a0e5d531d3834c4d0be4659a1db8e7e5c2a/0d6b103c70633d8fd9443d8d0acd3aadaacedb06",
                 "file": {"name": "zoom-2.0.0.hnmod", "size": 5886, "sha256": "49FA2D7D635225C53304FBF1477606C0677EB23D94DAC75B730F6C3BA065F127",
                          "url": "https:\/\/github.com/horizon-rust/horizon-mods/releases/download/zoom-2.0.0/zoom-2.0.0.hnmod"}},
                {"version": "1.0.0", "published": "2026-10-20", "game": "26.3",
                 "api": "native-1/1111111aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/0d6b103c70633d8fd9443d8d0acd3aadaacedb06",
                 "file": {"name": "zoom-1.0.0.hnmod", "size": 5000, "sha256": "aa", "url": "http:\/\/example.com/zoom-1.0.0.hnmod"}}
            ]})").object();

        const auto pack = HorizonAPI::packFrom(project);
        QCOMPARE(pack->provider, ModPlatform::ResourceProvider::HORIZON);
        QCOMPARE(pack->addonId.toString(), QString("zoom"));
        QCOMPARE(pack->name, QString("Zoom"));
        QCOMPARE(pack->websiteUrl, QString("https://playhorizon.dev/mods/zoom"));
        QVERIFY(pack->versionsLoaded);
        QVERIFY(pack->extraDataLoaded);

        // A file not on https is never offered
        QCOMPARE(pack->versions.size(), qsizetype(1));
        const auto& version = pack->versions.first();
        // The version is named by its number, and says the Horizon it was made for
        QCOMPARE(version.fileId.toString(), QString("2.0.0"));
        QCOMPARE(version.version_number, QString("2.0.0"));
        QVERIFY2(version.version.startsWith("2.0.0 (") && version.version.contains("6f8c7a0"), qPrintable(version.version));
        QCOMPARE(version.mcVersion, QStringList{ "26.3" });
        QCOMPARE(version.hash_type, QString("sha256"));
        QCOMPARE(version.hash, QString("49fa2d7d635225c53304fbf1477606c0677eb23d94dac75b730f6c3ba065f127"));
        QCOMPARE(version.fileName, QString("zoom-2.0.0.hnmod"));
        QVERIFY(version.loaders & ModPlatform::Horizon);

        // Its page: its own words, what kind it is, and every version
        QVERIFY(pack->extraData.body.startsWith("The zoom of Horizon."));
        QVERIFY(pack->extraData.body.contains("Native mod"));
        QVERIFY(pack->extraData.body.contains("1.0.0"));
        QCOMPARE(pack->extraData.sourceUrl, QString("https://github.com/horizon-rust/horizon-mods/tree/main/native/zoom"));

        QJsonObject index;
        index["projects"] = QJsonArray{ project };
        QCOMPARE(HorizonAPI::findProject(index, "zoom").value("name").toString(), QString("Zoom"));
        QVERIFY(HorizonAPI::isOfType(project, ModPlatform::ResourceType::Mod));
        QVERIFY(!HorizonAPI::isOfType(project, ModPlatform::ResourceType::ShaderPack));
        QCOMPARE(HorizonAPI::categories(index, ModPlatform::ResourceType::Mod).first().id, QString("utility"));

        // The launcher's metadata of a download from it says where it came from, and reads back
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QDir indexDir(temp.path());
        auto versionCopy = version;
        auto metadata = Packwiz::V1::createModFormat(indexDir, *pack, versionCopy);
        Packwiz::V1::updateModIndex(indexDir, metadata);
        auto read = Packwiz::V1::getIndexForMod(indexDir, QString("zoom"));
        QVERIFY(read.isValid());
        QCOMPARE(read.provider, ModPlatform::ResourceProvider::HORIZON);
        QCOMPARE(read.project_id.toString(), QString("zoom"));
        QCOMPARE(read.file_id.toString(), QString("2.0.0"));
        QCOMPARE(read.filename, QString("zoom-2.0.0.hnmod"));
    }

    void test_horizonShaderPacksSayWhatTheyAre()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        // A pack's manifest and icon (docs/shaders.md in horizon-rust/horizon): the shader list shows both
        QImage icon(16, 16, QImage::Format_ARGB32);
        icon.fill(Qt::magenta);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(icon.save(&buffer, "PNG"));

        const auto path = QDir(temp.path()).filePath("lumen-2.6.0.hshader");
        makeZip(path, { { "horizon-shaders.toml", "schema = 1\nid = \"lumen\"\nname = \"Lumen\"\nversion = \"2.6.0\"\napi = \"0.1\"\n"
                                                  "description = \"A cozy shader.\"\nauthors = [\"Lutziano888\"]\nlicense = \"MIT\"\n"
                                                  "[sky]\nfile = \"sky.wgsl\"\n" },
                        { "sky.wgsl", "fn sky() {}" },
                        { "icon.png", png } });
        ShaderPack pack{ QFileInfo(path) };
        QCOMPARE(pack.type(), ResourceType::ZIPFILE);
        QVERIFY(ShaderPackUtils::process(pack));
        QVERIFY(pack.valid());
        QCOMPARE(pack.name(), QString("Lumen"));
        QCOMPARE(pack.description(), QString("A cozy shader."));
        QCOMPARE(pack.authors(), QStringList{ "Lutziano888" });
        QCOMPARE(pack.license(), QString("MIT"));
        QVERIFY(pack.hasImage());
        QVERIFY(pack.imageSource().isEmpty());
    }

    void test_bundledTranslationsHavePluralForms()
    {
        BundledTranslator russian("ru_RU");
        QVERIFY(!russian.isEmpty());
        QCOMPARE(russian.translate("HorizonModFolderModel", "Build Horizon", nullptr, -1), QString("Собрать Horizon"));
        // Russian has three forms: 1 / 2-4 / 5-20, and 21 is as 1
        QCOMPARE(russian.translate("HorizonRegistryBrowser", "%n project(s)", nullptr, 1), QString("%n проект"));
        QCOMPARE(russian.translate("HorizonRegistryBrowser", "%n project(s)", nullptr, 3), QString("%n проекта"));
        QCOMPARE(russian.translate("HorizonRegistryBrowser", "%n project(s)", nullptr, 11), QString("%n проектов"));
        QCOMPARE(russian.translate("HorizonRegistryBrowser", "%n project(s)", nullptr, 21), QString("%n проект"));
        // What it hasn't is left to the next catalog
        QVERIFY(russian.translate("ModFolderPage", "A string no one wrote", nullptr, -1).isNull());

        BundledTranslator english("en_US");
        QCOMPARE(english.translate("HorizonRegistryBrowser", "%n project(s)", nullptr, 1), QString("%n project"));
        QCOMPARE(english.translate("HorizonRegistryBrowser", "%n project(s)", nullptr, 2), QString("%n projects"));
        QVERIFY(english.translate("HorizonModFolderModel", "Build Horizon", nullptr, -1).isNull());
    }
};

QTEST_GUILESS_MAIN(HorizonModsTest)

#include "HorizonMods_test.moc"
