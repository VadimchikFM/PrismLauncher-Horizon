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

#include <LoggedProcess.h>
#include <launch/LaunchStep.h>
#include <minecraft/auth/AuthSession.h>

#include "HorizonContext.h"
#include "MinecraftTarget.h"

/**
 * Starts `horizon-client` in the instance's game folder, the counterpart of LauncherPartLaunch for the Java game.
 *
 * The signed-in account goes to the game as JSON on its standard input (`--session-stdin`), never on the command
 * line, where other programs could read the token. An offline launch hands over the player name alone.
 */
class HorizonLaunch : public LaunchStep {
    Q_OBJECT
   public:
    explicit HorizonLaunch(LaunchTask* parent, HorizonContextPtr context);
    ~HorizonLaunch() override = default;

    void executeTask() override;
    bool abort() override;
    void proceed() override;
    bool canAbort() const override { return true; }

    void setWorkingDirectory(const QString& wd);
    void setAuthSession(AuthSessionPtr session) { m_session = std::move(session); }
    void setTargetToJoin(MinecraftTarget::Ptr targetToJoin) { m_targetToJoin = std::move(targetToJoin); }

   private slots:
    void on_state(LoggedProcess::State state);
    void on_log(const QStringList& lines, MessageLevel level);

   private:
    void fail(const QString& reason);

   private:
    HorizonContextPtr m_context;
    LoggedProcess m_process;
    AuthSessionPtr m_session;
    MinecraftTarget::Ptr m_targetToJoin;

    QString m_program;
    QStringList m_arguments;
    /// The account, written to the game's standard input once it runs. Empty for an offline launch.
    QByteArray m_sessionJson;
    /// The game's own word on why it stopped: its last line on stderr.
    QString m_lastErrorLine;
    QString m_crashReport;

    bool m_mayProceed = false;
};
