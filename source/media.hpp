// Pokemon cries, downloaded from Pokemon Showdown the first time they are needed
// (only when the console is online) and kept in sdmc:/switch/monhex/cache.
// Everything else the app shows is bundled with it.
#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include <SDL_mixer.h>

namespace monhex
{
    class Media
    {
    public:
        enum class State
        {
            Pending, // downloading
            Ready,   // on the SD card
            Failed   // not available (no internet, or Showdown has none)
        };

        bool init(void);
        void exit(void);

        // Starts fetching the file if it isn't cached; returns its state now.
        // `path` receives where the file is (or will be) on the SD card.
        State cry(const std::string& id, std::string& path);
        // Whether the console has an internet connection right now
        bool online(void);

        // Plays a cached cry file
        void play(const std::string& path);

        // Showdown's name for a species: lower case letters and digits only
        static std::string showdownId(const std::string& englishName);

    private:
        State fetch(const std::string& url, const std::string& path);
        void worker(void);

        bool mNetwork = false, mAudio = false;
        std::thread mThread;
        std::mutex mMutex;
        std::condition_variable mWake;
        std::deque<std::pair<std::string, std::string>> mQueue; // url, path
        std::map<std::string, State> mStates;                   // by path
        bool mStop = false;
        Mix_Chunk* mChunk = nullptr;
        std::string mChunkPath;
    };
}
