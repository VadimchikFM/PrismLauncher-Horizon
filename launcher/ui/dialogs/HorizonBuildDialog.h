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

#include <QDialog>

#include "minecraft/HorizonBackend.h"

class MinecraftInstance;
class QLabel;
class QPlainTextEdit;
class QPushButton;

/**
 * Builds Horizon with the native mods turned on in an instance, and shows the build as it goes.
 *
 * The work is Horizon's own launcher's (`horizon-launcher native-build`, see HorizonBackend): it downloads its Rust
 * toolchain once, fetches the game's source at the commit the mods pin, applies the mods and compiles the game as a
 * version of its own. The instance then remembers that version and which mods it was built with, and plays it.
 *
 * Opening this dialog is the player's decision to trust the mods' code: confirmAndBuild asks first.
 */
class HorizonBuildDialog : public QDialog {
    Q_OBJECT
   public:
    /// @param profile `performance`, `dev` or `release`: how the game is compiled.
    explicit HorizonBuildDialog(MinecraftInstance* instance, QString profile, QWidget* parent = nullptr);
    ~HorizonBuildDialog() override = default;

    /// The settings key of what a native build was built with: its profile and the mods' signature.
    static QString builtWith(const QString& profile, const QString& signature) { return profile + "|" + signature; }

    /**
     * Names the native mods that are on and asks the player whether to trust them, with the build profile to compile
     * with; on their word, builds. Returns whether a build ran and succeeded.
     */
    static bool confirmAndBuild(MinecraftInstance* instance, QWidget* parent);

    /// What the instance's build is: none, one to redo (the mods or the profile changed), or up to date.
    static QString buildStatus(MinecraftInstance* instance);

   public slots:
    /// Closing while it builds asks first, and stops the build.
    void reject() override;

   private:
    enum class Step { Idle, FindBase, InstallBase, PrepareBase, Build, FindVersion };

    void start();
    void findBase();
    void installBase();
    /// Prepares `base` when its game files are not prepared yet, then builds on it.
    void prepareThenBuild(const QString& base);
    void prepareBase(const QString& base);
    void build(const QString& base);
    void commandFinished(bool succeeded, const QByteArray& standardOutput, const QString& error);
    void succeed(const QString& version);
    void fail(const QString& reason);
    void append(const QString& text);

   private:
    MinecraftInstance* m_instance;
    QString m_profile;
    bool m_succeeded = false;
    /// The native mods the build is for, as they were when it started.
    QString m_signature;
    /// The installed version the build starts from.
    QString m_base;
    /// Whether this run has prepared `m_base` already: it is done once.
    bool m_prepared = false;
    HorizonBackend m_backend;
    Step m_step = Step::Idle;
    bool m_stopping = false;

    QLabel* m_status;
    QPlainTextEdit* m_log;
    QPushButton* m_button;
};
