#pragma once
#include <string>
#include <vector>

namespace Mission { struct Mission; }

namespace Mission::Tutorial {

// `requires` is exhaustive: unsupported entries make the row UNAVAILABLE, never approximated.
bool IsCapabilitySupported(const std::string& cap);
std::vector<std::string> MissingCapabilities(const std::vector<std::string>& requires);
std::vector<std::string> RuntimeSupportIssues(const ::Mission::Mission& lesson);

struct LessonProgress {
    bool cleared = false;
    int  attempts = 0;
    long long firstClearAt = 0;
    long long lastClearAt = 0;
    int  revisionCleared = 0;
};

bool ProgressLoad(std::string& warning);
LessonProgress ProgressGet(const std::string& packId, const std::string& lessonId);
bool ProgressRecordClear(const std::string& packId, const std::string& lessonId,
                         int revision, int attemptsThisRun, std::string& warning);
void ProgressNoteAttempt(const std::string& packId, const std::string& lessonId);
std::string ProgressLastLesson(const std::string& packId);
void ProgressSetLastLesson(const std::string& packId, const std::string& lessonId);

// Tokens: {dir:4}/{btn:A}/{input:236A}/{term:plain|jargon}/{ui:confirm}. Unknown tokens render
// their raw inner value; escaped braces render literally.
std::string RenderRichText(const std::string& text);

}
