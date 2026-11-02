// Native exceptions must release the recursive boundary;
// a C++ lock_guard alone is insufficient under /EHsc.
#define EFZ_AUDIO_CALLBACK(call) \
    EnterAudioBoundary(); \
    __try { return call; } \
    __finally { LeaveAudioBoundary(); }
