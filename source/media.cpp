#include "media.hpp"

#include <cctype>
#include <cstdio>
#include <sys/stat.h>

#include <SDL.h>
#include <curl/curl.h>
#include <switch.h>

namespace monhex
{
    namespace
    {
        constexpr const char* CACHE    = "sdmc:/switch/monhex/cache";
        constexpr const char* SHOWDOWN = "https://play.pokemonshowdown.com";

        bool fileExists(const std::string& path)
        {
            struct stat st;
            return stat(path.c_str(), &st) == 0 && st.st_size > 0;
        }

        size_t writeToFile(void* data, size_t size, size_t count, void* file)
        {
            return fwrite(data, size, count, static_cast<FILE*>(file));
        }

        // Downloads to a temporary name, renamed only when complete
        bool download(const std::string& url, const std::string& path)
        {
            std::string temp = path + ".part";
            FILE* f          = fopen(temp.c_str(), "wb");
            if (!f)
                return false;
            CURL* curl = curl_easy_init();
            long status = 0;
            CURLcode rc = CURLE_FAILED_INIT;
            if (curl)
            {
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "MonHex/0.1 (Nintendo Switch homebrew)");
                curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
                rc = curl_easy_perform(curl);
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
                curl_easy_cleanup(curl);
            }
            fclose(f);
            if (rc != CURLE_OK || status != 200)
            {
                remove(temp.c_str());
                return false;
            }
            remove(path.c_str());
            return rename(temp.c_str(), path.c_str()) == 0;
        }
    }

    bool Media::init(void)
    {
        mkdir("sdmc:/switch", 0777);
        mkdir("sdmc:/switch/monhex", 0777);
        mkdir(CACHE, 0777);
        mkdir((std::string(CACHE) + "/cries").c_str(), 0777);

        // main() brings up sockets; nifm tells whether there is a connection
        mNetwork = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
        if (mNetwork)
        {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            mThread = std::thread(&Media::worker, this);
        }

        if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0 &&
            Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 2048) == 0)
        {
            Mix_Init(MIX_INIT_OGG);
            mAudio = true;
        }
        return mNetwork;
    }

    void Media::exit(void)
    {
        if (mThread.joinable())
        {
            {
                std::lock_guard<std::mutex> lock(mMutex);
                mStop = true;
            }
            mWake.notify_all();
            mThread.join();
            curl_global_cleanup();
        }
        if (mNetwork)
            nifmExit();
        if (mChunk)
            Mix_FreeChunk(mChunk);
        mChunk = nullptr;
        if (mAudio)
        {
            Mix_CloseAudio();
            Mix_Quit();
        }
        // Sockets are left to main() (nxlink may be using them)
    }

    std::string Media::showdownId(const std::string& name)
    {
        std::string id;
        for (size_t i = 0; i < name.size(); i++)
        {
            unsigned char c = name[i];
            if (std::isalnum(c))
                id += char(std::tolower(c));
            else if (c == 0xC3 && i + 1 < name.size()) // é and friends in UTF-8
            {
                unsigned char n = name[++i];
                if (n >= 0xA8 && n <= 0xAB)
                    id += 'e';
            }
            else if (c == 0xE2 && i + 2 < name.size() && (unsigned char)name[i + 1] == 0x99)
            {
                // ♀ and ♂ (Nidoran)
                id += (unsigned char)name[i + 2] == 0x80 ? 'f' : 'm';
                i += 2;
            }
        }
        return id;
    }

    Media::State Media::fetch(const std::string& url, const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mStates.find(path);
        if (it != mStates.end())
            return it->second;
        if (fileExists(path))
            return mStates[path] = State::Ready;
        if (!mNetwork || !online())
            return State::Failed; // not remembered: it may work once online
        mQueue.emplace_back(url, path);
        mWake.notify_one();
        return mStates[path] = State::Pending;
    }

    void Media::worker(void)
    {
        for (;;)
        {
            std::pair<std::string, std::string> job;
            {
                std::unique_lock<std::mutex> lock(mMutex);
                mWake.wait(lock, [&] { return mStop || !mQueue.empty(); });
                if (mStop)
                    return;
                // Newest request first: that's what is on screen
                job = mQueue.back();
                mQueue.pop_back();
            }
            bool ok = download(job.first, job.second);
            std::lock_guard<std::mutex> lock(mMutex);
            mStates[job.second] = ok ? State::Ready : State::Failed;
        }
    }

    bool Media::online(void)
    {
        NifmInternetConnectionType type;
        u32 strength = 0;
        NifmInternetConnectionStatus status;
        return mNetwork && R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status)) &&
               status == NifmInternetConnectionStatus_Connected;
    }

    Media::State Media::cry(const std::string& id, std::string& path)
    {
        path = std::string(CACHE) + "/cries/" + id + ".ogg";
        return fetch(std::string(SHOWDOWN) + "/audio/cries/" + id + ".ogg", path);
    }

    void Media::play(const std::string& path)
    {
        if (!mAudio)
            return;
        if (path != mChunkPath)
        {
            if (mChunk)
                Mix_FreeChunk(mChunk);
            mChunk     = Mix_LoadWAV(path.c_str());
            mChunkPath = mChunk ? path : "";
        }
        if (mChunk)
            Mix_PlayChannel(-1, mChunk, 0);
    }
}
