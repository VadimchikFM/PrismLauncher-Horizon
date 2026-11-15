#pragma once

#include "InstanceCreationTask.h"

#include <utility>

class VanillaCreationTask final : public InstanceCreationTask {
    Q_OBJECT
   public:
    VanillaCreationTask(BaseVersion::Ptr version) : InstanceCreationTask(), m_version(std::move(version)) {}
    VanillaCreationTask(BaseVersion::Ptr version, QString loader, BaseVersion::Ptr loader_version);

    std::unique_ptr<MinecraftInstance> createInstance() override;

    /// Makes the instance one that is launched with Horizon, the game rewritten in Rust, instead of Java.
    /// @param version the Horizon it plays: a release's tag, or nothing for the newest Nightly.
    void setUseHorizon(bool useHorizon, QString version = {})
    {
        m_use_horizon = useHorizon;
        m_horizon_version = std::move(version);
    }

   private:
    bool m_use_horizon = false;
    QString m_horizon_version;

    // Version to update to / create of the instance.
    BaseVersion::Ptr m_version;

    bool m_using_loader = false;
    QString m_loader;
    BaseVersion::Ptr m_loader_version;
};
