#pragma once

#include <string>

// The worker never touches game memory, never reads Config after Start(), and swallows every
// exception. One successful check per session; a failure only opens a retry cooldown.
namespace UpdateCheck {

enum class Status {
    Disabled,
    Idle,
    Checking,
    UpToDate,
    UpdateAvailable,
    Failed,
};

// Idempotent and non-blocking. The CheckForUpdates and netplay gates run before any attempt starts,
// so a gated call uses up neither the session's check nor the retry cooldown.
void Start();

std::string LatestVersion();

bool HasNewerRelease();

bool IsUpdateAvailable();

const char* BadgeText();

void AcknowledgeLatest();

Status GetStatus();

const char* ReleasesPageUrl();

}
