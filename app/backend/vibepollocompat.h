#pragma once

// Compatibility layer for Vibepollo 2.0 synthetic session controls.

#include "nvapp.h"

#include <QVector>

namespace VibepolloCompat
{
inline bool isRunningGameControl(const NvApp& app)
{
    return app.uuid.compare(QStringLiteral("9a1c5a25-58fe-40e0-b9aa-7d3f00000007"),
                            Qt::CaseInsensitive) == 0;
}

inline bool isTerminateControl(const NvApp& app)
{
    return app.uuid.compare(QStringLiteral("9a1c5a25-58fe-40e0-b9aa-7d3f00000004"),
                            Qt::CaseInsensitive) == 0;
}

inline int effectiveRunningAppId(int currentGameId, const QVector<NvApp>& apps)
{
    if (currentGameId != 0) {
        return currentGameId;
    }

    // Vibepollo 2.0 deliberately reports SERVER_FREE/currentgame=0 to a
    // secondary client while exposing a resume-only copy of the active game
    // in /applist. Treat that synthetic copy as the running app for UI purposes.
    for (const NvApp& app : apps) {
        if (isRunningGameControl(app)) {
            return app.id;
        }
    }

    return 0;
}

inline bool findTerminateControl(const QVector<NvApp>& apps, NvApp* result = nullptr)
{
    for (const NvApp& app : apps) {
        if (isTerminateControl(app)) {
            if (result != nullptr) {
                *result = app;
            }
            return true;
        }
    }

    return false;
}
}
