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

#include "HorizonPrepare.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "Application.h"
#include "FileSystem.h"
#include "launch/LaunchTask.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/SettingsObject.h"

namespace {

/// The JSON object a `--launch-api` call printed.
QJsonObject parseReply(const QByteArray& out)
{
    const auto start = out.indexOf('{');
    const auto end = out.lastIndexOf('}');
    if (start < 0 || end < start) {
        return {};
    }
    return QJsonDocument::fromJson(out.mid(start, end - start + 1)).object();
}

/// The names in a reply's `options`: `[{"name": "--output", "value": "PATH"}, ...]`.
QSet<QString> optionNames(const QJsonObject& reply)
{
    QSet<QString> names;
    const auto options = reply.value("options").toArray();
    for (const auto& option : options) {
        names.insert(option.toObject().value("name").toString());
    }
    return names;
}

QStringList lines(const QByteArray& text)
{
    return QString::fromUtf8(text).remove(QChar::CarriageReturn).split(QChar::LineFeed, Qt::SkipEmptyParts);
}

QString lastLine(const QByteArray& text)
{
    const auto all = lines(text);
    return all.isEmpty() ? QString() : all.last().trimmed();
}

}  // namespace

HorizonPrepare::HorizonPrepare(LaunchTask* parent, HorizonContextPtr context) : LaunchStep(parent), m_context(std::move(context)) {}

void HorizonPrepare::executeTask()
{
    if (!Horizon::isBuild(m_context->buildDir)) {
        fail(tr("There is no Horizon build to play with."));
        return;
    }
    askPreparer();
}

void HorizonPrepare::runProgram(const QString& program, const QStringList& arguments, Done done)
{
    auto* process = new QProcess(this);
    process->setWorkingDirectory(m_context->buildDir);
    m_process = process;

    connect(process, &QProcess::finished, this, [this, process, done](int exitCode, QProcess::ExitStatus status) {
        process->deleteLater();
        if (!isRunning()) {
            return;
        }
        const auto out = process->readAllStandardOutput();
        const auto err = process->readAllStandardError();
        if (status != QProcess::NormalExit) {
            fail(tr("%1 crashed.").arg(QFileInfo(process->program()).fileName()));
            return;
        }
        done(exitCode, out, err);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        // The others end in `finished`.
        if (error != QProcess::FailedToStart) {
            return;
        }
        process->deleteLater();
        if (isRunning()) {
            fail(tr("Could not start %1: %2").arg(process->program(), process->errorString()));
        }
    });
    process->start(program, arguments);
    // Nothing is said to these programs: one that reads its input finds it ended.
    process->closeWriteChannel();
}

void HorizonPrepare::askPreparer()
{
    runProgram(m_context->preparePath, { "--launch-api" }, [this](int exitCode, const QByteArray& out, const QByteArray& err) {
        const auto reply = parseReply(out);
        m_minecraft = reply.value("minecraft").toString();
        static const QRegularExpression s_version("^[A-Za-z0-9][A-Za-z0-9._-]*$");
        if (exitCode != 0 || !s_version.match(m_minecraft).hasMatch() || m_minecraft.contains("..")) {
            fail(tr("%1 doesn't answer --launch-api (%2). This Horizon build is too old for this launcher: use a newer one.")
                     .arg(Horizon::executableName("horizon-prepare"), lastLine(err)));
            return;
        }
        m_prepareOptions = optionNames(reply);
        // What the new-instance dialog offers Horizon for
        APPLICATION->settings()->set("HorizonMinecraftVersion", m_minecraft);

        emit logLine(tr("Horizon %1 (%2), for Minecraft %3, Launch API %4")
                         .arg(reply.value("horizon").toString(), m_context->buildLabel, m_minecraft, reply.value("launch_api").toString()),
                     MessageLevel::Launcher);

        // Horizon is one Minecraft version, and it is prepared from that version's own files.
        const auto instanceVersion = m_parent->instance()->getPackProfile()->getComponentVersion("net.minecraft");
        if (instanceVersion != m_minecraft) {
            fail(tr("This Horizon build plays Minecraft %1, and this instance is Minecraft %2. Change the instance's Minecraft "
                    "version to %1 (Edit > Version > Change Version), or remove Horizon from the instance there.")
                     .arg(m_minecraft, instanceVersion));
            return;
        }

        // The resources depend on the build and on the Minecraft version, not on the instance: every instance shares
        // them. A build that writes another layout gets a folder of its own, so two builds don't prepare over each other.
        const auto layout = QString("%1-s%2-o%3")
                                .arg(m_minecraft)
                                .arg(reply.value("schema").toInt())
                                .arg(reply.value("output_version").toInt());
        m_context->resourcesDir = FS::PathCombine(Horizon::rootDir(), "resources", layout);

        askClient();
    });
}

void HorizonPrepare::askClient()
{
    runProgram(m_context->clientPath, { "--launch-api" }, [this](int exitCode, const QByteArray& out, const QByteArray& err) {
        const auto reply = parseReply(out);
        if (exitCode != 0 || reply.isEmpty()) {
            fail(tr("%1 doesn't answer --launch-api (%2). This Horizon build is too old for this launcher: use a newer one.")
                     .arg(Horizon::executableName("horizon-client"), lastLine(err)));
            return;
        }
        m_context->clientOptions = optionNames(reply);
        checkResources();
    });
}

void HorizonPrepare::checkResources()
{
    setStatus(tr("Checking Horizon's resources..."));
    runProgram(
        m_context->preparePath, { "--check", "--output", m_context->resourcesDir },
        [this](int exitCode, const QByteArray& out, const QByteArray& err) {
            if (exitCode == 0) {
                finish();
                return;
            }
            if (exitCode != 1) {
                fail(tr("Could not check Horizon's resources: %1").arg(lastLine(err)));
                return;
            }
            // Exit code 1: each gap is a line on stdout, or stderr says why it can't check. Preparing fills both.
            emit logLine(tr("Horizon's resources have to be prepared:"), MessageLevel::Launcher);
            for (const auto& gap : lines(out.isEmpty() ? err : out)) {
                emit logLine("  " + gap, MessageLevel::Launcher);
            }
            // From the event loop: preparing shows a progress dialog, which stays until this step ends.
            QMetaObject::invokeMethod(this, [this] { prepareResources(); }, Qt::QueuedConnection);
        });
}

void HorizonPrepare::prepareResources()
{
    if (!isRunning()) {
        return;
    }

    const QStringList neededOptions{ "--prepare", "--prism-dir", "--output", "--complete", "--progress" };
    for (const auto& needed : neededOptions) {
        if (!m_prepareOptions.contains(needed)) {
            fail(tr("This Horizon build's preparer has no %1 option: it is too old for this launcher. Use a newer build.").arg(needed));
            return;
        }
    }

    QStringList arguments{ "--prepare", "--prism-dir", prismDir() };
    if (const auto home = javaHome(); !home.isEmpty() && m_prepareOptions.contains("--java-home")) {
        arguments << "--java-home" << home;
    }
    arguments << "--output" << m_context->resourcesDir << "--complete" << "--progress" << "json";

    if (!FS::ensureFolderPathExists(m_context->resourcesDir)) {
        fail(tr("Could not create the folder %1").arg(m_context->resourcesDir));
        return;
    }

    emit logLine(tr("Preparing Horizon's resources from this launcher's Minecraft %1 files. The first time takes a few minutes.")
                     .arg(m_minecraft),
                 MessageLevel::Launcher);
    emit logLine("  " + m_context->preparePath + " " + arguments.join(' ') + "\n", MessageLevel::Launcher);
    setStatus(tr("Preparing Horizon's resources..."));
    setProgress(0, 1000);

    m_outputBuffer.clear();
    m_prepareError.clear();

    auto* process = new QProcess(this);
    process->setWorkingDirectory(m_context->buildDir);
    m_process = process;
    connect(process, &QProcess::readyReadStandardOutput, this, &HorizonPrepare::readPrepareOutput);
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        // With --progress json, stderr is the text meant for people.
        emit logLines(lines(process->readAllStandardError()), MessageLevel::StdErr);
    });
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus status) {
        process->deleteLater();
        if (!isRunning()) {
            return;
        }
        readPrepareOutput();
        handlePrepareLine(m_outputBuffer);
        m_outputBuffer.clear();
        prepareFinished(exitCode, status);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        process->deleteLater();
        if (isRunning()) {
            fail(tr("Could not start %1: %2").arg(process->program(), process->errorString()));
        }
    });
    process->start(m_context->preparePath, arguments);
    process->closeWriteChannel();

    // Shows the progress dialog, which returns here only once this step has ended: nothing may follow.
    if (isRunning()) {
        emit progressReportingRequest();
    }
}

void HorizonPrepare::readPrepareOutput()
{
    if (!m_process) {
        return;
    }
    m_outputBuffer += m_process->readAllStandardOutput();
    for (auto end = m_outputBuffer.indexOf('\n'); end >= 0; end = m_outputBuffer.indexOf('\n')) {
        const auto line = m_outputBuffer.left(end);
        m_outputBuffer.remove(0, end + 1);
        handlePrepareLine(line);
    }
}

void HorizonPrepare::handlePrepareLine(const QByteArray& rawLine)
{
    // `--progress json`: every line is one object. {"message"} is a log line, {"fraction"} is how far the work is
    // (0 to 1), {"error"} is why it failed. Keys this launcher doesn't know are for newer ones.
    const auto line = rawLine.trimmed();
    if (line.isEmpty()) {
        return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        emit logLine(QString::fromUtf8(line), MessageLevel::Launcher);
        return;
    }
    const auto object = document.object();
    if (const auto message = object.value("message"); message.isString()) {
        emit logLine(message.toString(), MessageLevel::Launcher);
        setDetails(message.toString().section('\n', 0, 0));
    }
    if (const auto fraction = object.value("fraction"); fraction.isDouble()) {
        setProgress(qRound(qBound(0.0, fraction.toDouble(), 1.0) * 1000), 1000);
    }
    if (const auto failure = object.value("error"); failure.isString()) {
        m_prepareError = failure.toString();
    }
}

void HorizonPrepare::prepareFinished(int exitCode, QProcess::ExitStatus status)
{
    if (status == QProcess::NormalExit && exitCode == 0) {
        emit logLine(tr("Horizon's resources are ready.\n"), MessageLevel::Launcher);
        finish();
        return;
    }
    auto reason = m_prepareError;
    if (reason.isEmpty()) {
        reason = status == QProcess::NormalExit ? tr("horizon-prepare ended with exit code %1").arg(exitCode) : tr("horizon-prepare crashed");
    }
    fail(tr("Could not prepare Horizon's resources: %1").arg(reason));
}

void HorizonPrepare::finish()
{
    // Resources of a layout the installed build no longer writes are several hundred megabytes nothing reads.
    if (m_context->managedBuild) {
        const QDir current(m_context->resourcesDir);
        QDir all = current;
        if (all.cdUp()) {
            for (const auto& entry : all.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (entry.fileName() != current.dirName()) {
                    FS::deletePath(entry.absoluteFilePath());
                }
            }
        }
    }
    emitSucceeded();
}

void HorizonPrepare::fail(const QString& reason)
{
    emit logLine(reason, MessageLevel::Fatal);
    emitFailed(reason);
}

QString HorizonPrepare::javaHome() const
{
    const auto java = FS::ResolveExecutable(m_parent->instance()->settings()->get("JavaPath").toString());
    if (java.isEmpty()) {
        return {};
    }
    auto dir = QFileInfo(java).absoluteDir();
    if (dir.dirName() != "bin" || !dir.cdUp()) {
        return {};
    }
    return dir.absolutePath();
}

QString HorizonPrepare::prismDir() const
{
    const QDir root(APPLICATION->dataRoot());
    // Named itself, an instance gives the preparer its own components (the LWJGL it really uses). That only works
    // for an instance in the launcher's own tree: the preparer finds the libraries and assets above `instances`.
    const QDir instance(m_parent->instance()->instanceRoot());
    auto instances = instance;
    if (instances.cdUp() && instances.dirName() == "instances") {
        auto above = instances;
        if (above.cdUp() && above.canonicalPath() == root.canonicalPath()) {
            return instance.absolutePath();
        }
    }
    return root.absolutePath();
}

bool HorizonPrepare::abort()
{
    // First, so that what the killed program still reports is ignored.
    if (isRunning()) {
        emitAborted();
    }
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
    return true;
}
