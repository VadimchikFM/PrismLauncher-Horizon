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

#include "HorizonLaunch.h"

#include <QDir>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QStandardPaths>

#include "Application.h"
#include "Commandline.h"
#include "FileSystem.h"
#include "launch/LaunchTask.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"

#ifdef Q_OS_LINUX
#include "gamemode_client.h"
#endif

namespace {
/// What the game's last line on stderr starts with when it saved a crash report (`crash_report_line` in --launch-api).
const QString CrashReportLine = QStringLiteral("Horizon: crash report saved to ");
}  // namespace

HorizonLaunch::HorizonLaunch(LaunchTask* parent, HorizonContextPtr context)
    : LaunchStep(parent), m_context(std::move(context)), m_process(QStringConverter::Utf8)
{
    connect(&m_process, &LoggedProcess::log, this, &HorizonLaunch::on_log);
    connect(&m_process, &LoggedProcess::stateChanged, this, &HorizonLaunch::on_state);
}

void HorizonLaunch::executeTask()
{
    auto* instance = m_parent->instance();
    auto* settings = instance->settings();

    if (!m_session || m_session->launchMode == LaunchMode::Demo) {
        fail(tr("Horizon has no demo mode. Sign in with a Microsoft account that owns Minecraft: Java Edition to play."));
        return;
    }
    if (!Horizon::isBuild(m_context->buildDir) || m_context->resourcesDir.isEmpty()) {
        fail(tr("Horizon is not ready to start: its build or its resources are missing."));
        return;
    }

    // Only the options the build lists: it refuses any other with exit code 2.
    const auto supports = [this](const QString& option) { return m_context->clientOptions.contains(option); };

    // One folder per instance: saves, options.txt, packs and screenshots where the Java game keeps them, and the
    // logs and crash reports beside them, where this launcher's log pages look.
    const auto gameDir = QDir(instance->gameRoot()).absolutePath();
    QStringList arguments{ "--resources", m_context->resourcesDir, "--game-dir", gameDir };
    if (supports("--home")) {
        arguments << "--home" << gameDir;
    }

    auto width = settings->get("MinecraftWinWidth").toInt();
    auto height = settings->get("MinecraftWinHeight").toInt();
    if (settings->get("LaunchMaximized").toBool()) {
        // The game takes a size, not a window state: the screen's is the nearest thing to maximized.
        if (const auto* screen = QGuiApplication::primaryScreen()) {
            const auto size = screen->availableSize();
            width = size.width();
            height = size.height();
        }
    }
    if (width > 0 && height > 0) {
        arguments << "--width" << QString::number(width) << "--height" << QString::number(height);
    }

    if (m_targetToJoin) {
        if (!m_targetToJoin->address.isEmpty()) {
            if (supports("--quickPlayMultiplayer")) {
                arguments << "--quickPlayMultiplayer" << m_targetToJoin->address + ':' + QString::number(m_targetToJoin->port);
            }
        } else if (!m_targetToJoin->world.isEmpty() && supports("--quickPlaySingleplayer")) {
            arguments << "--quickPlaySingleplayer" << m_targetToJoin->world;
        }
    }

    auto environment = instance->createLaunchEnvironment();
    // What --home says, for a build that doesn't take the option yet.
    environment.insert("HORIZON_HOME", gameDir);

    const bool signedIn = m_session->launchMode == LaunchMode::Normal && m_session->user_type == "msa" &&
                          !m_session->access_token.isEmpty() && m_session->access_token != "0";
    if (signedIn && supports("--session-stdin")) {
        QJsonObject account;
        account["name"] = m_session->player_name;
        account["uuid"] = m_session->uuid;
        account["access_token"] = m_session->access_token;
        account["user_type"] = "msa";
        m_sessionJson = QJsonDocument(account).toJson(QJsonDocument::Compact);
        arguments << "--session-stdin";
    } else {
        // Without a session the game plays offline, under the name it is given.
        environment.insert("HORIZON_PLAYER_NAME", m_session->player_name);
        emit logLine(tr("Starting Horizon offline as %1: online-mode servers will refuse the player.").arg(m_session->player_name),
                     MessageLevel::Launcher);
    }

    if (!QDir(instance->modsRoot()).entryList(QStringList{ "*.jar" }, QDir::Files).isEmpty()) {
        emit logLine(tr("This instance has Java mods. Horizon has no Java, so they are not loaded."), MessageLevel::Warning);
    }

    m_program = m_context->clientPath;
    m_arguments = arguments;

    auto wrapperCommandStr = instance->getWrapperCommand().trimmed();
    if (!wrapperCommandStr.isEmpty()) {
        wrapperCommandStr = m_parent->substituteVariables(wrapperCommandStr);
        auto wrapperArgs = Commandline::splitArgs(wrapperCommandStr);
        const auto wrapperCommand = wrapperArgs.takeFirst();
        if (QStandardPaths::findExecutable(wrapperCommand).isEmpty()) {
            fail(tr("The wrapper command \"%1\" couldn't be found.").arg(wrapperCommand));
            return;
        }
        emit logLine("Wrapper command is:\n" + wrapperCommandStr + "\n\n", MessageLevel::Launcher);
        m_arguments = wrapperArgs + QStringList{ m_program } + arguments;
        m_program = wrapperCommand;
    }

    // The token is not here: it goes through standard input.
    emit logLine(tr("Horizon command line:\n  %1 %2\n").arg(m_program, m_arguments.join(' ')), MessageLevel::Launcher);

    m_process.setProcessEnvironment(environment);
    // The game keeps running when the launcher quits.
    m_process.setDetachable(true);

    m_mayProceed = true;
    emit readyForLaunch();
}

void HorizonLaunch::proceed()
{
    if (!m_mayProceed) {
        return;
    }
    m_mayProceed = false;
    m_process.start(m_program, m_arguments);
}

void HorizonLaunch::on_log(const QStringList& lines, MessageLevel level)
{
    if (level == MessageLevel::StdErr) {
        for (const auto& line : lines) {
            const auto text = line.trimmed();
            if (text.isEmpty()) {
                continue;
            }
            m_lastErrorLine = text;
            if (text.startsWith(CrashReportLine)) {
                m_crashReport = text.mid(CrashReportLine.size()).trimmed();
            }
        }
    }
    emit logLines(lines, level);
}

void HorizonLaunch::on_state(LoggedProcess::State state)
{
    auto* instance = m_parent->instance();
    switch (state) {
        case LoggedProcess::FailedToStart: {
            fail(tr("Could not launch Horizon: %1").arg(m_process.errorString()));
            return;
        }
        case LoggedProcess::Running: {
            emit logLine(QString("Horizon process ID: %1\n\n").arg(m_process.processId()), MessageLevel::Launcher);
            m_parent->setPid(m_process.processId());
            instance->setMinecraftRunning(true);

            if (!m_sessionJson.isEmpty()) {
                m_process.write(m_sessionJson);
                m_sessionJson.fill('\0');
                m_sessionJson.clear();
            }
            // The game reads its standard input to the end: closing it is what hands the session over.
            m_process.closeWriteChannel();

#ifdef Q_OS_LINUX
            if (instance->settings()->get("EnableFeralGamemode").toBool() && APPLICATION->capabilities() & Application::SupportsGameMode) {
                auto pid = m_process.processId();
                if (pid) {
                    gamemode_request_start_for(pid);
                }
            }
#endif
            if (instance->settings()->get("CloseAfterLaunch").toBool()) {
                APPLICATION->closeAllWindows();
            }
            return;
        }
        case LoggedProcess::Aborted:
        case LoggedProcess::Crashed: {
            m_parent->setPid(-1);
            instance->setMinecraftRunning(false);
            emitFailed(tr("Game crashed."));
            return;
        }
        case LoggedProcess::Finished: {
            if (instance->settings()->get("CloseAfterLaunch").toBool()) {
                APPLICATION->showMainWindow();
            }
            m_parent->setPid(-1);
            instance->setMinecraftRunning(false);

            // 0: the player quit. 2: the command line was refused, and stderr names the option. 1 and 101: the game
            // stopped on an error or panicked, and its last line on stderr names the crash report.
            const auto exitCode = m_process.exitCode();
            if (exitCode == 0) {
                emitSucceeded();
            } else if (exitCode == 2) {
                fail(tr("Horizon refused its command line: %1").arg(m_lastErrorLine));
            } else {
                if (!m_crashReport.isEmpty()) {
                    emit logLine(tr("Horizon saved a crash report:\n  %1").arg(m_crashReport), MessageLevel::Error);
                }
                emitFailed(tr("Game crashed."));
            }
            return;
        }
        default:
            break;
    }
}

void HorizonLaunch::setWorkingDirectory(const QString& wd)
{
    m_process.setWorkingDirectory(wd);
}

void HorizonLaunch::fail(const QString& reason)
{
    emit logLine(reason, MessageLevel::Fatal);
    emitFailed(reason);
}

bool HorizonLaunch::abort()
{
    if (m_mayProceed) {
        // Nothing runs yet: the step just ends.
        m_mayProceed = false;
        if (isRunning()) {
            emitAborted();
        }
        return true;
    }
    const auto state = m_process.state();
    if (state == LoggedProcess::Running || state == LoggedProcess::Starting) {
        m_process.kill();
    }
    return true;
}
