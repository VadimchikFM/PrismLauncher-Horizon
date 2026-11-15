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

#include <QList>
#include <QString>
#include <optional>

/**
 * The mods of a Horizon game folder (Horizon's Content API, docs/api/content.md in https://github.com/horizon-rust/horizon).
 *
 * The launcher keeps every Horizon mod in `horizon-mods/`, as its Mods page lists them: content mods (`.hmod`, or a
 * folder holding `horizon-mod.toml`), which the game loads from there, and native mods (`.hnmod`), which the game
 * leaves alone there (it loads only `.hmod`s) and which are built into it. A mod turned off in the launcher is renamed
 * `<file>.disabled`, which the game leaves alone too.
 *
 * Horizon's own switch for each mod is `mod.<id>:true` in `horizon-mods.txt`: the game loads no content mod without it,
 * and Horizon's launcher builds the native mods that have it from `horizon-native-mods/`. That folder is the build's:
 * it is filled from `horizon-mods/` before each build (mirrorNativeMods).
 */
namespace HorizonMods {

struct Mod {
    QString id;
    QString name;
    QString version;
    /// It has code: it runs only in a game built with it.
    bool native = false;
    /// The launcher has it on (its file is not `.disabled`) and so has the game (no `mod.<id>:false`).
    bool enabled = false;
    /// Its file, or its folder.
    QString path;
    /// Why it can't be read, when it can't.
    QString error;
};

/// `horizon-mods` in the game folder: every mod.
QString modsDir(const QString& gameDir);
/// `horizon-native-mods` in the game folder: the native mods of the next build.
QString nativeDir(const QString& gameDir);

/// Every mod in `horizon-mods`, by name.
QList<Mod> scan(const QString& gameDir);

/// What `horizon-mods.txt` says of mod `id`: true or false, or nothing when it has no line for it.
std::optional<bool> switchState(const QString& gameDir, const QString& id);

/// Turns a mod on or off in `horizon-mods.txt`, leaving the file's other lines as they are.
bool setEnabled(const QString& gameDir, const QString& id, bool enabled, QString* error = nullptr);

/// What the native mods turned on are (ids, versions and their files' checksums), for telling whether a game built
/// earlier was built with exactly these. Empty when none is on.
QString nativeSignature(const QString& gameDir);

/// Puts the native mods that are on, and only those, in `horizon-native-mods`, each turned on: what Horizon's
/// launcher builds the game with.
bool mirrorNativeMods(const QString& gameDir, QString* error = nullptr);

/// Moves native mods that Horizon's launcher installed into `horizon-native-mods` (before the launcher kept them in
/// `horizon-mods`, or with a modpack) to `horizon-mods`, unless a mod of the same id is there. Once per game folder:
/// from then on `horizon-native-mods` is the build's copy.
void adoptNativeMods(const QString& gameDir);

}  // namespace HorizonMods
