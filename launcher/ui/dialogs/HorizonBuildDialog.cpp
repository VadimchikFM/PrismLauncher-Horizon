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

#include "HorizonBuildDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

#include "Application.h"
#include "minecraft/HorizonMods.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"

HorizonBuildDialog::HorizonBuildDialog(MinecraftInstance* instance, QString profile, QWidget* parent)
    : QDialog(parent), m_instance(instance), m_profile(std::move(profile)), m_backend(instance->gameRoot())
{
    setWindowTitle(tr("Building Horizon with mods - %1").arg(instance->name()));
    setWindowModality(Qt::WindowModal);
    resize(760, 480);

    auto* layout = new QVBoxLayout(this);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setMaximumBlockCount(5000);
    layout->addWidget(m_log);

    m_button = new QPushButton(tr("Stop"), this);
    layout->addWidget(m_button, 0, Qt::AlignRight);
    connect(m_button, &QPushButton::clicked, this, &HorizonBuildDialog::reject);

    connect(&m_backend, &HorizonBackend::line, this, [this](const QString& text, bool) { append(text); });
    connect(&m_backend, &HorizonBackend::status, this, [this](const QString& text) {
        if (!text.isEmpty()) {
            m_status->setText(text);
        }
    });
    connect(&m_backend, &HorizonBackend::finished, this, &HorizonBuildDialog::commandFinished);

    // Once the dialog shows
    QTimer::singleShot(0, this, &HorizonBuildDialog::start);
}

void HorizonBuildDialog::start()
{
    m_signature = HorizonMods::nativeSignature(m_instance->gameRoot());
    if (m_signature.isEmpty()) {
        fail(tr("No native mod is turned on in this instance."));
        return;
    }

    // Horizon's launcher builds the native mods of `horizon-native-mods`: the ones that are on, and only those
    QString error;
    if (!HorizonMods::mirrorNativeMods(m_instance->gameRoot(), &error)) {
        fail(tr("Could not get the native mods ready for the build: %1").arg(error));
        return;
    }

    // A rebuild stays on the Horizon the instance plays, so only what changed is compiled. The first build starts
    // from the newest release.
    const auto built = m_instance->settings()->get("HorizonNativeVersion").toString();
    if (HorizonBackend::isVersionInstalled(built)) {
        build(built);
    } else {
        findBase();
    }
}

void HorizonBuildDialog::findBase()
{
    // The release Horizon's launcher has installed already is the one to build on. Installing is only for when it
    // has none: `install` puts a version in anew each time, and with that its prepared game files are gone.
    m_step = Step::FindBase;
    m_backend.run({ "versions", "--installed", "--json" });
}

void HorizonBuildDialog::prepareThenBuild(const QString& base)
{
    // Prepared game files are in the version's own folder, with the list of what they hold
    const QDir resources(QDir(HorizonBackend::versionDir(base)).filePath(".local/horizon-client"));
    bool prepared = false;
    for (const auto& entry : resources.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        prepared = prepared || QFileInfo(QDir(entry.absoluteFilePath()).filePath("horizon-resources.json")).isFile();
    }
    if (prepared) {
        build(base);
    } else {
        prepareBase(base);
    }
}

void HorizonBuildDialog::installBase()
{
    m_step = Step::InstallBase;
    m_status->setText(tr("Installing the newest Horizon for the build to start from..."));
    append(m_status->text());
    m_backend.run({ "install", "latest", "--json" });
}

void HorizonBuildDialog::prepareBase(const QString& base)
{
    // A build starts from a version whose game files are prepared: its copy of them is what the built game plays
    // with. Horizon's launcher prepares them from this launcher's own Minecraft files, which it knows how to read.
    m_step = Step::PrepareBase;
    m_base = base;
    m_prepared = true;
    m_status->setText(tr("Preparing %1 from this launcher's Minecraft files. This is done once per Horizon version and takes several "
                         "minutes.")
                          .arg(base));
    append(m_status->text());
    m_backend.run({ "prepare", "--version", base, "--minecraft", QDir::toNativeSeparators(APPLICATION->dataRoot()) });
}

void HorizonBuildDialog::build(const QString& base)
{
    m_step = Step::Build;
    m_base = base;
    m_status->setText(tr("Building Horizon with this instance's native mods (%1). The first build downloads Rust and takes 20 to 30 "
                         "minutes; later ones compile only what changed.")
                          .arg(m_profile));
    append(tr("Building on %1, profile %2").arg(base, m_profile));
    // --trust-native-code: the player said so on the page that opened this dialog, for exactly these mods
    m_backend.run({ "native-build", "--version", base, "--profile", m_profile, "--trust-native-code" });
}

void HorizonBuildDialog::commandFinished(bool succeeded, const QByteArray& standardOutput, const QString& error)
{
    const auto step = m_step;
    m_step = Step::Idle;
    if (m_stopping) {
        fail(tr("The build was stopped."));
        return;
    }

    switch (step) {
        case Step::FindBase: {
            // {"installed": ["nightly-2026-11-13-9f42064", ...], "selected": "nightly-2026-11-13-9f42064"}
            const auto versions = QJsonDocument::fromJson(standardOutput).object();
            QStringList releases;
            const auto installed = versions.value("installed").toArray();
            for (const auto& value : installed) {
                // Not a native build, which is some instance's game: a release or a Nightly
                if (const auto tag = value.toString(); !tag.startsWith("native-") && HorizonBackend::isVersionInstalled(tag)) {
                    releases.append(tag);
                }
            }
            const auto selected = versions.value("selected").toString();
            if (!succeeded || releases.isEmpty()) {
                installBase();
            } else {
                prepareThenBuild(releases.contains(selected) ? selected : releases.first());
            }
            return;
        }
        case Step::InstallBase: {
            // {"installed": "<tag>", "prepared": false}
            const auto installed = QJsonDocument::fromJson(standardOutput).object();
            const auto tag = installed.value("installed").toString();
            if (!succeeded || tag.isEmpty()) {
                fail(tr("Could not install Horizon: %1").arg(error));
                return;
            }
            if (installed.value("prepared").toBool()) {
                build(tag);
            } else {
                prepareThenBuild(tag);
            }
            return;
        }
        case Step::PrepareBase: {
            if (!succeeded) {
                fail(tr("Could not prepare %1: %2").arg(m_base, error));
                return;
            }
            build(m_base);
            return;
        }
        case Step::Build: {
            if (!succeeded && !m_prepared && error.contains("not prepared") && !m_base.startsWith("native-")) {
                // The launcher's own word on it: prepare, once, and build again
                prepareBase(m_base);
                return;
            }
            if (!succeeded) {
                fail(error);
                return;
            }
            // Its last line: "<version> built and selected for its instance (42 s)."
            static const QRegularExpression s_built("^(native-\\S+) built and selected", QRegularExpression::MultilineOption);
            const auto match = s_built.match(QString::fromUtf8(standardOutput));
            if (match.hasMatch()) {
                succeed(match.captured(1));
                return;
            }
            // Said another way: the version its default instance (this game folder) now plays
            m_step = Step::FindVersion;
            m_backend.run({ "instance", "list", "--json" });
            return;
        }
        case Step::FindVersion: {
            QString version;
            const auto instances = QJsonDocument::fromJson(standardOutput).array();
            for (const auto& value : instances) {
                const auto instance = value.toObject();
                if (instance.value("id").toString() == "horizon") {
                    version = instance.value("version").toString();
                }
            }
            if (!succeeded || version.isEmpty()) {
                fail(tr("The build ended, but which version it made could not be read."));
                return;
            }
            succeed(version);
            return;
        }
        case Step::Idle:
            return;
    }
}

void HorizonBuildDialog::succeed(const QString& version)
{
    if (!HorizonBackend::isVersionInstalled(version)) {
        fail(tr("The build says it made %1, which is not in %2.").arg(version, HorizonBackend::dataDir()));
        return;
    }
    // What the instance plays from now on, and what it was built with: the Mods page tells from these
    // whether the mods have changed since
    auto* settings = m_instance->settings();
    settings->set("HorizonNativeVersion", version);
    settings->set("HorizonNativeSignature", builtWith(m_profile, m_signature));

    m_succeeded = true;
    m_status->setText(tr("Built: this instance now plays %1.").arg(version));
    append(m_status->text());
    m_button->setText(tr("Close"));
}

void HorizonBuildDialog::fail(const QString& reason)
{
    m_status->setText(tr("<span style=\"color:#e01b24;\">The build failed:</span> %1").arg(reason.toHtmlEscaped()));
    append(reason);
    append(tr("The full log of a build is %1").arg(HorizonBackend::buildLogPath()));
    m_button->setText(tr("Close"));
}

void HorizonBuildDialog::append(const QString& text)
{
    m_log->appendPlainText(text);
}

QString HorizonBuildDialog::buildStatus(MinecraftInstance* instance)
{
    auto* settings = instance->settings();
    const auto signature = HorizonMods::nativeSignature(instance->gameRoot());
    const auto version = settings->get("HorizonNativeVersion").toString();
    const auto profile = settings->get("HorizonNativeProfile").toString();

    if (signature.isEmpty()) {
        return tr("No native mod is on: the instance plays the regular Horizon.");
    }
    if (!HorizonBackend::isVersionInstalled(version)) {
        return tr("Not built yet: the native mods that are on do nothing until the game is built with them.");
    }
    if (settings->get("HorizonNativeSignature").toString() != builtWith(profile, signature)) {
        return tr("Needs a rebuild: the native mods or the profile changed since %1 was built. It still plays, without the changes.")
            .arg(version);
    }
    return tr("Up to date: the instance plays %1.").arg(version);
}

bool HorizonBuildDialog::confirmAndBuild(MinecraftInstance* instance, QWidget* parent)
{
    QStringList names;
    for (const auto& mod : HorizonMods::scan(instance->gameRoot())) {
        if (mod.native && mod.enabled) {
            names.append(QString("%1 %2").arg(mod.name, mod.version).toHtmlEscaped());
        }
    }
    if (names.isEmpty()) {
        QMessageBox::information(parent, tr("Build Horizon"),
                                 tr("No native mod is on. Native mods are code compiled into the game: download one (Download Mods > "
                                    "Horizon), then build."));
        return false;
    }

    // Building is trusting: the mods' code becomes part of the game. The player says so each time, for these mods.
    QDialog confirm(parent);
    confirm.setWindowTitle(tr("Build Horizon with native mods"));
    auto* layout = new QVBoxLayout(&confirm);

    auto* text = new QLabel(&confirm);
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    text->setText(tr("<p>%1</p><p><span style=\"color:#e01b24;\"><b>Native mods are not sandboxed.</b></span> Their code is compiled "
                     "into the game and runs with full access to this computer, like any program. Build only mods whose source you "
                     "have read or whose authors you trust.</p><p>The game will be built with:<br>%2</p><p>The first build downloads "
                     "Rust (about 2 GB with its tools) and takes 20 to 30 minutes. Later builds compile only what changed.</p>")
                      .arg(buildStatus(instance).toHtmlEscaped(), names.join("<br>")));
    layout->addWidget(text);

    auto* profileRow = new QHBoxLayout();
    profileRow->addWidget(new QLabel(tr("Build profile:"), &confirm));
    auto* profile = new QComboBox(&confirm);
    // The names are horizon-launcher's own (--profile)
    profile->addItem(tr("Performance: the everyday build, quick to rebuild"), "performance");
    profile->addItem(tr("Dev: quickest to rebuild a mod, slower game"), "dev");
    profile->addItem(tr("Release: slowest to build, slightly faster game"), "release");
    const auto saved = profile->findData(instance->settings()->get("HorizonNativeProfile").toString());
    profile->setCurrentIndex(saved < 0 ? 0 : saved);
    profileRow->addWidget(profile, 1);
    layout->addLayout(profileRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &confirm);
    auto* trust = buttons->addButton(tr("I trust these mods: build"), QDialogButtonBox::AcceptRole);
    trust->setDefault(false);
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, &confirm, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &confirm, &QDialog::reject);
    layout->addWidget(buttons);

    if (confirm.exec() != QDialog::Accepted) {
        return false;
    }
    const auto chosen = profile->currentData().toString();
    instance->settings()->set("HorizonNativeProfile", chosen);

    HorizonBuildDialog dialog(instance, chosen, parent);
    dialog.exec();
    return dialog.m_succeeded;
}

void HorizonBuildDialog::reject()
{
    if (!m_backend.isBusy()) {
        QDialog::reject();
        return;
    }
    const auto answer = QMessageBox::question(this, tr("Stop the build?"),
                                              tr("The game is still being built. Stop it? What was compiled so far is kept for the next build."),
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes && m_backend.isBusy()) {
        m_stopping = true;
        m_status->setText(tr("Stopping..."));
        m_backend.abort();
    }
}
