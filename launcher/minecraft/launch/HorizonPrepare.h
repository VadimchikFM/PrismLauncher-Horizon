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

#include <QPointer>
#include <QProcess>
#include <functional>

#include <launch/LaunchStep.h>

#include "HorizonContext.h"

/**
 * Asks the Horizon build what it supports (`--launch-api`), then makes sure the resources the game reads are there
 * and current for it (`horizon-prepare --check`), preparing them from this launcher's own Minecraft files when they
 * are not (`horizon-prepare --prepare --prism-dir`). Preparing downloads nothing: the instance's libraries and assets
 * are the ones the launcher's own update steps fetched before this step runs.
 */
class HorizonPrepare : public LaunchStep {
    Q_OBJECT
   public:
    explicit HorizonPrepare(LaunchTask* parent, HorizonContextPtr context);
    ~HorizonPrepare() override = default;

    void executeTask() override;
    bool canAbort() const override { return true; }

   public slots:
    bool abort() override;

   private:
    using Done = std::function<void(int exitCode, const QByteArray& out, const QByteArray& err)>;

    /// Runs one of the build's programs to its end, then hands `done` what it printed.
    void runProgram(const QString& program, const QStringList& arguments, Done done);
    void askPreparer();
    void askClient();
    void checkResources();
    void prepareResources();
    void readPrepareOutput();
    void handlePrepareLine(const QByteArray& line);
    void prepareFinished(int exitCode, QProcess::ExitStatus status);
    void finish();
    void fail(const QString& reason);

    /// The home of the instance's Java, for the preparer's exporters.
    QString javaHome() const;
    /// What `--prism-dir` is given: the instance when it is in the launcher's own tree, else the launcher's root.
    QString prismDir() const;

   private:
    HorizonContextPtr m_context;
    QPointer<QProcess> m_process;

    /// The options `horizon-prepare --launch-api` lists.
    QSet<QString> m_prepareOptions;
    /// The Minecraft version the build plays, and prepares from.
    QString m_minecraft;

    QByteArray m_outputBuffer;
    QString m_prepareError;
};
