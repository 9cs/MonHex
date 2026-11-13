#include "app.hpp"
#include "lang.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <sys/stat.h>

#include "theme.hpp"
#include "ui/sprites.hpp"
#include "ui/types.hpp"
#include "utils/i18n.hpp"

using namespace monhex::ui;
using namespace monhex::layout;

namespace monhex
{
    namespace
    {
        constexpr int PER_ROW = 5;
        constexpr int AVATAR = 168, USER_GAP = 56;
        constexpr int ICON = 200, ICON_GAP = 44, ICON_ROW_H = 300, GAMES_Y = 140;

        int rowStartX(int count, int itemW, int gap)
        {
            int n = SDL_min(count, PER_ROW);
            return (SCREEN_W - (n * itemW + (n - 1) * gap)) / 2;
        }

        Result initAccounts(void)
        {
            Result rc = accountInitialize(AccountServiceType_System);
            if (R_FAILED(rc))
                rc = accountInitialize(AccountServiceType_Application);
            return rc;
        }

        bool runningAsApplication(void)
        {
            AppletType type = appletGetAppletType();
            return type == AppletType_Application || type == AppletType_SystemApplication;
        }

        std::string resultText(Result rc)
        {
            char buf[48];
            snprintf(buf, sizeof(buf), tr("código %04d-%04d"), 2000 + R_MODULE(rc), R_DESCRIPTION(rc));
            return buf;
        }

        SDL_Rect userAvatar(int i, int count)
        {
            int x0 = rowStartX(count, AVATAR, USER_GAP);
            return {x0 + (i % PER_ROW) * (AVATAR + USER_GAP), 230 + (i / PER_ROW) * 270, AVATAR, AVATAR};
        }

        SDL_Rect gameIcon(int i, int count)
        {
            int x0 = rowStartX(count, ICON, ICON_GAP);
            return {x0 + (i % PER_ROW) * (ICON + ICON_GAP), GAMES_Y + (i / PER_ROW) * ICON_ROW_H, ICON, ICON};
        }

    }

    void App::Highlight::moveTo(int tx, int ty, int tw, int th)
    {
        if (!placed)
        {
            x = tx, y = ty, w = tw, h = th;
            placed = true;
            return;
        }
        constexpr float SPEED = 0.38f;
        x += (tx - x) * SPEED;
        y += (ty - y) * SPEED;
        // Size jumps straight to the target so the frame's shape stays cached
        w = tw, h = th;
        if (std::fabs(tx - x) < 0.5f)
            x = tx;
        if (std::fabs(ty - y) < 0.5f)
            y = ty;
    }

    bool App::init(std::string& error)
    {
        Result rc;
        if (R_FAILED(rc = romfsInit()))
        {
            error = tr("Falha ao abrir os dados internos do app (romfs), ") + resultText(rc);
            return false;
        }
        mRomfsUp = true;
        if (!mGfx.init(error))
            return false;
        mGfxUp = true;
        buildBackground();
        mInput.init();
        // The app was called NXDex before: its folder (backups, bank, settings,
        // cries) moves over once
        struct stat st;
        if (stat("sdmc:/switch/nxdex", &st) == 0 && stat("sdmc:/switch/monhex", &st) != 0)
            rename("sdmc:/switch/nxdex", "sdmc:/switch/monhex");
        mConfig.load();
        setUiLanguage(uiLanguageFromCode(mConfig.language));
        mMedia.init();

        if (R_FAILED(rc = initAccounts()))
        {
            showMessage(tr("Falha ao iniciar o serviço de contas, ") + resultText(rc), Screen::Message);
            return true;
        }
        mAccountsUp = true;
        if (R_FAILED(rc = nsInitialize()))
        {
            showMessage(tr("Falha ao iniciar o serviço de jogos (ns), ") + resultText(rc), Screen::Message);
            return true;
        }
        mNsUp = true;

        i18n::init(LANG);
        loadUsers();
        if (mUsers.empty())
        {
            showMessage(tr("Nenhum usuário foi encontrado no console."), Screen::Message);
            return true;
        }

        // Opened through a game (holding R), the system already asked who's playing
        AccountUid preselected{};
        if (runningAsApplication() && R_SUCCEEDED(accountGetPreselectedUser(&preselected)) &&
            accountUidIsValid(&preselected))
        {
            for (size_t i = 0; i < mUsers.size(); i++)
            {
                if (memcmp(&mUsers[i].uid, &preselected, sizeof(AccountUid)) == 0)
                {
                    selectUser(static_cast<int>(i));
                    return true;
                }
            }
        }
        if (mUsers.size() == 1)
            selectUser(0);
        else
            mScreen = Screen::Users;
        return true;
    }

    void App::exit(void)
    {
        mSlots.clear();
        mSave = {};
        mMedia.exit();
        for (auto& u : mUsers)
        {
            if (u.avatar)
                SDL_DestroyTexture(u.avatar);
        }
        for (auto* icon : mGameIcons)
        {
            if (icon)
                SDL_DestroyTexture(icon);
        }
        if (mNsUp)
        {
            i18n::exit();
            nsExit();
        }
        if (mAccountsUp)
            accountExit();
        for (auto* tex : {mBackground, mWatermark, mGlow, mWallpapers[0], mWallpapers[1],
                 mWallpapers[2], mWallpapers[3]})
        {
            if (tex)
                SDL_DestroyTexture(tex);
        }
        if (mGfxUp)
            mGfx.exit();
        if (mRomfsUp)
            romfsExit();
    }

    // ------------------------------------------------------------------ state

    void App::loadUsers(void)
    {
        AccountUid uids[ACC_USER_LIST_SIZE];
        s32 count = 0;
        if (R_FAILED(accountListAllUsers(uids, ACC_USER_LIST_SIZE, &count)))
            return;
        for (s32 i = 0; i < count; i++)
        {
            UserEntry user{uids[i], tr("Usuário ") + std::to_string(i + 1), nullptr};
            AccountProfile profile;
            if (R_SUCCEEDED(accountGetProfile(&profile, uids[i])))
            {
                AccountProfileBase base{};
                if (R_SUCCEEDED(accountProfileGet(&profile, nullptr, &base)) && base.nickname[0])
                    user.name = base.nickname;
                u32 imageSize = 0;
                if (R_SUCCEEDED(accountProfileGetImageSize(&profile, &imageSize)) && imageSize)
                {
                    std::vector<u8> jpeg(imageSize);
                    u32 read = 0;
                    if (R_SUCCEEDED(accountProfileLoadImage(&profile, jpeg.data(), imageSize, &read)))
                        user.avatar = mGfx.roundedImage(jpeg.data(), read, AVATAR, AVATAR, AVATAR / 2);
                }
                accountProfileClose(&profile);
            }
            mUsers.push_back(user);
        }
    }

    void App::runWithLoading(const std::string& text, const std::function<void()>& work)
    {
        std::atomic<bool> done{false};
        std::thread worker([&] {
            work();
            done = true;
        });
        while (!done)
        {
            appletMainLoop(); // keep the system responsive; quitting waits for the work
            drawLoading(text);
        }
        worker.join();
        mHighlight.snap();
    }

    void App::selectUser(int index)
    {
        mUser = index;
        for (auto* icon : mGameIcons)
        {
            if (icon)
                SDL_DestroyTexture(icon);
        }
        mGameIcons.clear();
        AccountUid uid = mUsers[index].uid;
        std::vector<GameEntry> games;
        runWithLoading(tr("Procurando jogos Pokémon"), [&] { games = listPokemonGames(uid); });
        mGames = std::move(games);
        // Textures can only be made on the main thread
        for (const auto& g : mGames)
        {
            mGameIcons.push_back(
                g.icon.empty() ? nullptr : mGfx.roundedImage(g.icon.data(), g.icon.size(), ICON, ICON, 18));
        }
        mGameCursor = 0;
        mScreen     = Screen::Games;
    }

    void App::openGame(int index)
    {
        const GameEntry game = mGames[index];
        std::string error;
        LoadedSave loaded;
        AccountUid uid       = mUsers[mUser].uid;
        std::string userName = mUsers[mUser].name;
        int maxBackups       = mConfig.maxBackups;
        runWithLoading(tr("Fazendo backup e abrindo o save"), [&] {
            loaded = loadGameSave(game, uid, userName, maxBackups, error);
        });
        mSave = std::move(loaded);
        if (!mSave.sav)
        {
            std::string msg = error;
            if (!mSave.backupPath.empty())
                msg += tr("\n\nBackup salvo em:\n") + mSave.backupPath;
            showMessage(msg, Screen::Games);
            return;
        }
        mOpenGame   = game;
        mDirty      = false;
        mBox        = 0;
        mPartyView  = false;
        mSlotCursor = 0;
        loadSlots();
        mScreen = Screen::Boxes;
    }

    void App::showMessage(const std::string& message, Screen returnTo)
    {
        mMessage  = message;
        mReturnTo = returnTo;
        mScreen   = Screen::Message;
    }

    float App::pressGlow(void) const
    {
        float t = mGfx.time() - mPressTime;
        return t < 0.25f ? 1.0f - t / 0.25f : 0.0f;
    }

    // ------------------------------------------------------------------ input

    bool App::frame(void)
    {
        if (!appletMainLoop())
            return false;
        mInput.update();
        u64 input = mInput.all();
        // Taps on the footer hints act like pressing their button
        for (const auto& area : mHintAreas)
        {
            if (mInput.tappedIn(area.rect.x, area.rect.y, area.rect.w, area.rect.h))
                input |= area.button;
        }
        // An open dialog takes all input
        bool dialogUp = updateDialog(input);
        if (dialogUp)
            input = 0;
        bool plusOpensBag = mScreen == Screen::Boxes && !mDetail && !mSearch.open && !mMenu.open;
        if ((input & HidNpadButton_Plus) && !plusOpensBag)
        {
            if (hasUnsaved() && mSave.sav)
            {
                askUnsaved(DialogAction::Quit, DialogAction::SaveThenQuit);
                input = 0;
            }
            else
            {
                return false;
            }
        }
        for (u64 b : {HidNpadButton_A, HidNpadButton_B, HidNpadButton_X, HidNpadButton_Minus})
        {
            if (input & b)
            {
                mFlashButton = b;
                mFlashTime   = mGfx.time();
            }
        }

        Screen before = mScreen;
        switch (mScreen)
        {
            case Screen::Users:
                updateUsers(input);
                break;
            case Screen::Games:
                updateGames(input);
                break;
            case Screen::Boxes:
                updateBoxes(input);
                break;
            case Screen::Settings:
                updateSettings(input);
                break;
            case Screen::Backups:
                updateBackups(input);
                break;
            case Screen::Message:
                updateMessage(input);
                break;
        }
        if (mScreen != before)
            mHighlight.snap();
        if (mQuit)
            return false;

        mGfx.begin(theme::BG);
        drawBackground();
        mHintAreas.clear();
        switch (mScreen)
        {
            case Screen::Users:
                drawUsers();
                break;
            case Screen::Games:
                drawGames();
                break;
            case Screen::Boxes:
                drawBoxes();
                break;
            case Screen::Settings:
                drawSettings();
                break;
            case Screen::Backups:
                drawBackups();
                break;
            case Screen::Message:
                drawMessage();
                break;
        }
        drawDialog();
        drawToast();
        mGfx.end();
        return true;
    }

    void App::updateUsers(u64 pressed)
    {
        int count   = static_cast<int>(mUsers.size());
        mUserCursor = moveInGrid(mUserCursor, count, PER_ROW, pressed);
        for (int i = 0; i < count; i++)
        {
            SDL_Rect r = userAvatar(i, count);
            if (mInput.tappedIn(r.x, r.y, r.w, r.h + 50))
            {
                mUserCursor = i;
                pressed |= HidNpadButton_A;
            }
        }
        if (pressed & HidNpadButton_A)
        {
            mPressTime = mGfx.time();
            selectUser(mUserCursor);
        }
    }

    void App::updateGames(u64 pressed)
    {
        if ((pressed & HidNpadButton_B) && mUsers.size() > 1)
        {
            mScreen = Screen::Users;
            return;
        }
        if (pressed & HidNpadButton_Minus)
        {
            mScreen = Screen::Settings;
            return;
        }
        if ((pressed & HidNpadButton_Y) && !mGames.empty())
        {
            openBackups();
            return;
        }
        int count = static_cast<int>(mGames.size());
        if (count == 0)
            return;
        mGameCursor = moveInGrid(mGameCursor, count, PER_ROW, pressed);
        for (int i = 0; i < count; i++)
        {
            SDL_Rect r = gameIcon(i, count);
            if (mInput.tappedIn(r.x, r.y, r.w, r.h + 70))
            {
                mGameCursor = i;
                pressed |= HidNpadButton_A;
            }
        }
        if (pressed & HidNpadButton_A)
        {
            mPressTime = mGfx.time();
            openGame(mGameCursor);
        }
    }

    namespace
    {
        // Settings rows: the row itself, then a short description under it
        constexpr int SETTINGS_ROWS = 3, SETTINGS_ROW_H = 76, SETTINGS_STEP = 156;
        int settingsRowY(int index) { return CONTENT_Y + 30 + index * SETTINGS_STEP; }
    }

    void App::updateSettings(u64 pressed)
    {
        mSettingsRow = moveInGrid(mSettingsRow, SETTINGS_ROWS, 1, pressed & (KEY_UP | KEY_DOWN));
        for (int r = 0; r < SETTINGS_ROWS; r++)
        {
            if (mInput.tappedIn(60, settingsRowY(r), SCREEN_W - 120, SETTINGS_ROW_H))
                mSettingsRow = r;
        }
        int y         = settingsRowY(mSettingsRow);
        bool tapLeft  = mInput.tappedIn(SCREEN_W - 420, y, 120, SETTINGS_ROW_H);
        bool tapRight = mInput.tappedIn(SCREEN_W - 180, y, 120, SETTINGS_ROW_H);
        bool left     = (pressed & KEY_LEFT) || tapLeft;
        bool right    = (pressed & KEY_RIGHT) || tapRight || (pressed & HidNpadButton_A);
        bool changed  = false;
        if (mSettingsRow == 0 && (left || right))
        {
            // Language: English, Portuguese, Spanish, French
            int count = int(UiLanguage::Count);
            int v     = int(uiLanguageFromCode(mConfig.language));
            v         = (v + (right ? 1 : count - 1)) % count;
            mConfig.language = uiLanguageCode(UiLanguage(v));
            setUiLanguage(UiLanguage(v));
            changed = true;
        }
        else if (mSettingsRow == 1)
        {
            // 1..20 backups, then "no limit" (0)
            int v = mConfig.maxBackups;
            if (right)
                v = v == 0 ? 0 : (v >= Config::MAX_BACKUPS_LIMIT ? 0 : v + 1);
            if (left)
                v = v == 0 ? Config::MAX_BACKUPS_LIMIT : SDL_max(1, v - 1);
            changed = v != mConfig.maxBackups;
            mConfig.maxBackups = v;
        }
        else if (mSettingsRow == 2 && (left || right))
        {
            mConfig.downloadMedia = !mConfig.downloadMedia;
            changed = true;
        }
        if (changed)
            mConfig.save();
        if (pressed & HidNpadButton_B)
            mScreen = Screen::Games;
    }

    void App::drawSettings(void)
    {
        drawHeader(tr("Configurações"));
        int x = 60, w = SCREEN_W - 120;
        int vx = SCREEN_W - 240;
        auto row = [&](int index, const std::string& title, const std::string& value, const char* description) {
            int y = settingsRowY(index);
            drawDivider(x, y, w);
            mGfx.text(title, x + 20, y + (SETTINGS_ROW_H - mGfx.textHeight(24)) / 2, 24, theme::TEXT, Align::Left, 560);
            mGfx.arrow(vx - 130, y + SETTINGS_ROW_H / 2, 16, false, theme::TEXT);
            mGfx.text(value, vx, y + (SETTINGS_ROW_H - mGfx.textHeight(24)) / 2, 24, theme::TEXT, Align::Center, 220);
            mGfx.arrow(vx + 130, y + SETTINGS_ROW_H / 2, 16, true, theme::TEXT);
            drawDivider(x, y + SETTINGS_ROW_H, w);
            if (description)
                mGfx.textWrapped(description, x + 20, y + SETTINGS_ROW_H + 10, 17, theme::DIM, Align::Left, w - 40, 2, 24);
        };
        // The language row reads in both the chosen language and English
        std::string languageTitle = std::string(tr("Idioma")) + (uiLanguage() == UiLanguage::English ? "" : " / Language");
        row(0, languageTitle, uiLanguageName(uiLanguage()),
            nullptr);
        row(1, tr("Backups guardados por jogo"),
            mConfig.maxBackups == 0 ? tr("Sem limite") : std::to_string(mConfig.maxBackups),
            tr("Ao abrir um jogo, o save é copiado para sdmc:/switch/monhex/backups. Acima desse limite, os "
               "backups mais antigos daquele jogo e usuário são apagados."));
        row(2, tr("Cry dos Pokémon"), mConfig.downloadMedia ? tr("Ligado") : tr("Desligado"),
            tr("Toque no Pokémon na tela de detalhes para ouvir o cry. Cada som é baixado da internet "
               "(Pokémon Showdown) na primeira vez e guardado no cartão SD; sem internet, só os já baixados tocam."));
        int hy = settingsRowY(mSettingsRow);
        mHighlight.moveTo(x, hy + 1, w, SETTINGS_ROW_H - 2);
        drawHighlight(6);

        drawFooter({{0, glyph::DLEFT, tr("Mudar")}, {HidNpadButton_B, glyph::B, tr("Voltar")},
            {HidNpadButton_Plus, glyph::PLUS, tr("Sair")}});
    }

    void App::updateMessage(u64 pressed)
    {
        if ((pressed & (HidNpadButton_B | HidNpadButton_A)) && mReturnTo != Screen::Message)
            mScreen = mReturnTo;
    }

    // ------------------------------------------------------------------ drawing

    void App::drawDivider(int x, int y, int w) { mGfx.fillRect(x, y, w, 1, theme::LINE); }

    void App::drawHeader(const std::string& context, int contextWidth)
    {
        mGfx.draw(mGfx.texture("romfs:/logo.png"), 30, 12, 64, 64);
        mGfx.text("MonHex", 98, 25, 32, theme::TEXT);
        if (!context.empty())
            mGfx.text(context, SCREEN_W - MARGIN - 14, 32, 22, theme::MUTED, Align::Right, contextWidth);
        drawDivider(MARGIN, HEADER_H - 1, SCREEN_W - 2 * MARGIN);
    }

    void App::drawFooter(const std::vector<Hint>& hints)
    {
        drawDivider(MARGIN, FOOTER_Y, SCREEN_W - 2 * MARGIN);
        int x = SCREEN_W - MARGIN - 20;
        float flash = mGfx.time() - mFlashTime;
        // Right to left, like the system menus
        for (auto it = hints.rbegin(); it != hints.rend(); ++it)
        {
            int labelW = mGfx.textWidth(it->label, 22);
            int glyphW = mGfx.buttonGlyphWidth(it->glyph, 26);
            int total  = glyphW + 8 + labelW;
            int left   = x - total;
            if (it->button == mFlashButton && flash < 0.3f)
            {
                u8 a = u8(60 * (1 - flash / 0.3f));
                mGfx.fillRoundRect(left - 12, FOOTER_Y + 14, total + 24, 44, 22, theme::TEXT.withAlpha(a));
            }
            mGfx.buttonGlyph(it->glyph, left, FOOTER_Y + 21, 26, theme::TEXT);
            mGfx.text(it->label, left + glyphW + 8, FOOTER_Y + 23, 22, theme::TEXT);
            mHintAreas.push_back({{left - 12, FOOTER_Y + 4, total + 24, SCREEN_H - FOOTER_Y - 4}, it->button});
            x = left - 40;
        }
    }

    void App::drawHighlight(int radius)
    {
        // Pulses between two cyans, like the system's selection frame
        float pulse = 0.5f + 0.5f * std::sin(mGfx.time() * 4.2f);
        Color c     = lerp(theme::CYAN_A, theme::CYAN_B, pulse);
        float glow  = pressGlow();
        int x = int(mHighlight.x + 0.5f), y = int(mHighlight.y + 0.5f);
        int w = int(mHighlight.w), h = int(mHighlight.h);
        mGfx.shadow(x - 4, y - 4, w + 8, h + 8, radius + 4, 10, c.withAlpha(u8(50 + 120 * glow)));
        mGfx.roundRect(x - 5, y - 5, w + 10, h + 10, radius + 5, c, 4);
    }

    void App::drawLoading(const std::string& text)
    {
        mGfx.begin(theme::BG);
        drawBackground();
        drawHeader("");
        float t = mGfx.time();
        // A Poke Ball shaking like it does after a throw: three wobbles, then a pause
        float cycle = std::fmod(t, 1.8f);
        double angle = 0;
        if (cycle < 1.1f)
            angle = std::sin(cycle / 1.1f * 3 * M_PI) * 24.0 * (1.0 - cycle / 1.6f);
        int cx = SCREEN_W / 2, cy = SCREEN_H / 2 - 40;
        mGfx.drawRotated(mGfx.pokeball(), cx, cy, 104, 104, angle);
        // The button glints while it waits
        if (cycle >= 1.1f)
        {
            float g = std::sin((cycle - 1.1f) / 0.7f * M_PI);
            mGfx.fillCircle(cx, cy, 8, Color{255, 255, 255, u8(160 * g)});
        }
        int dots = int(t * 3) % 4;
        std::string shown = text + std::string(dots, '.');
        int w = mGfx.textWidth(text + "...", 26);
        mGfx.text(shown, cx - w / 2, cy + 86, 26, theme::TEXT, Align::Left);
        mGfx.end();
    }

    void App::drawUsers(void)
    {
        drawHeader("");
        mGfx.text(tr("Selecione um usuário"), SCREEN_W / 2, 150, 30, theme::TEXT, Align::Center);
        int count = static_cast<int>(mUsers.size());
        for (int i = 0; i < count; i++)
        {
            SDL_Rect r = userAvatar(i, count);
            mGfx.shadow(r.x, r.y + 4, r.w, r.h, r.w / 2, 12, theme::SHADOW);
            if (mUsers[i].avatar)
                mGfx.draw(mUsers[i].avatar, r.x, r.y, r.w, r.h);
            else
                mGfx.fillCircle(r.x + r.w / 2, r.y + r.h / 2, r.w / 2, theme::CARD);
            mGfx.text(mUsers[i].name, r.x + r.w / 2, r.y + r.h + 22, 24,
                i == mUserCursor ? theme::TEXT : theme::MUTED, Align::Center, AVATAR + USER_GAP - 8);
        }
        if (count > 0)
        {
            SDL_Rect r = userAvatar(mUserCursor, count);
            mHighlight.moveTo(r.x, r.y, r.w, r.h);
            drawHighlight(r.w / 2);
        }
        drawFooter({{HidNpadButton_A, glyph::A, "OK"}, {HidNpadButton_Plus, glyph::PLUS, tr("Sair")}});
    }

    void App::drawGames(void)
    {
        drawHeader(mUsers[mUser].name);
        int count = static_cast<int>(mGames.size());
        if (count == 0)
        {
            mGfx.text(tr("Nenhum jogo Pokémon com save foi encontrado para este usuário."),
                SCREEN_W / 2, 330, 26, theme::MUTED, Align::Center);
        }
        for (int i = 0; i < count; i++)
        {
            SDL_Rect r = gameIcon(i, count);
            bool sel   = i == mGameCursor;
            mGfx.shadow(r.x, r.y + 6, r.w, r.h, 18, 14, theme::SHADOW);
            if (mGameIcons[i])
                mGfx.draw(mGameIcons[i], r.x, r.y, r.w, r.h);
            else
            {
                mGfx.fillRoundRect(r.x, r.y, r.w, r.h, 18, theme::CARD);
                mGfx.draw(mGfx.pokeball(), r.x + 50, r.y + 50, 100, 100);
            }
            if (!mGames[i].knownSupported)
            {
                int tw = mGfx.textWidth(tr("Não testado"), 16) + 20;
                mGfx.fillRoundRect(r.x + 10, r.y + r.h - 38, tw, 28, 14, Color{0, 0, 0, 170});
                mGfx.text(tr("Não testado"), r.x + 20, r.y + r.h - 34, 16, theme::TEXT);
            }
            mGfx.textWrapped(mGames[i].name, r.x + r.w / 2, r.y + r.h + 16, 20,
                sel ? theme::TEXT : theme::MUTED, Align::Center, ICON + ICON_GAP - 12, 2, 26);
        }
        if (count > 0)
        {
            SDL_Rect r = gameIcon(mGameCursor, count);
            mHighlight.moveTo(r.x, r.y, r.w, r.h);
            drawHighlight(18);
        }
        std::vector<Hint> hints;
        if (count > 0)
            hints.push_back({HidNpadButton_A, glyph::A, tr("Abrir")});
        if (mUsers.size() > 1)
            hints.push_back({HidNpadButton_B, glyph::B, tr("Trocar usuário")});
        if (count > 0)
            hints.push_back({HidNpadButton_Y, glyph::Y, tr("Backups")});
        hints.push_back({HidNpadButton_Minus, glyph::MINUS, tr("Configurações")});
        hints.push_back({HidNpadButton_Plus, glyph::PLUS, tr("Sair")});
        drawFooter(hints);
    }

    void App::buildBackground(void)
    {
        // Base: a dark vertical gradient, drawn small and stretched (the linear
        // filter keeps it smooth)
        if (SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, 4, 64, 32, SDL_PIXELFORMAT_RGBA32))
        {
            u32* px = static_cast<u32*>(surf->pixels);
            for (int y = 0; y < 64; y++)
            {
                Color c = lerp(Color{30, 29, 46}, Color{19, 20, 28}, (y + 0.5f) / 64);
                for (int x = 0; x < 4; x++)
                    px[y * (surf->pitch / 4) + x] = SDL_MapRGBA(surf->format, c.r, c.g, c.b, 255);
            }
            mBackground = mGfx.textureFromSurface(surf, true);
            SDL_FreeSurface(surf);
        }

        // A soft round glow, tinted and moved around when drawn
        constexpr int G = 64;
        if (SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, G, G, 32, SDL_PIXELFORMAT_RGBA32))
        {
            u32* px = static_cast<u32*>(surf->pixels);
            for (int y = 0; y < G; y++)
            {
                for (int x = 0; x < G; x++)
                {
                    float dx = (x + 0.5f) / G * 2 - 1, dy = (y + 0.5f) / G * 2 - 1;
                    float d  = std::sqrt(dx * dx + dy * dy);
                    float a  = d < 1 ? (1 - d) * (1 - d) : 0;
                    px[y * (surf->pitch / 4) + x] = SDL_MapRGBA(surf->format, 255, 255, 255, u8(a * 255));
                }
            }
            mGlow = mGfx.textureFromSurface(surf, true);
            SDL_FreeSurface(surf);
        }

        // A faint Poke Ball outline used as a watermark
        constexpr int S = 320;
        if (SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, S, S, 32, SDL_PIXELFORMAT_RGBA32))
        {
            u32* px = static_cast<u32*>(surf->pixels);
            float c = S / 2.0f, R = S / 2.0f - 6, t = 9;
            auto edge = [](float v) { return v < 0 ? 0.0f : (v > 1 ? 1.0f : v); };
            for (int y = 0; y < S; y++)
            {
                for (int x = 0; x < S; x++)
                {
                    float dx = x + 0.5f - c, dy = y + 0.5f - c;
                    float d  = std::sqrt(dx * dx + dy * dy);
                    float ring   = edge(t - std::fabs(d - (R - t)) + 0.5f);
                    float band   = edge(t * 0.8f - std::fabs(dy) + 0.5f) * edge(R - d + 0.5f) *
                                   edge(std::fabs(dx) - 52 + 0.5f);
                    float button = edge(t - std::fabs(d - 40) + 0.5f);
                    float a      = std::max({ring, band, button});
                    px[y * (surf->pitch / 4) + x] = SDL_MapRGBA(surf->format, 255, 255, 255, u8(a * 255));
                }
            }
            mWatermark = mGfx.textureFromSurface(surf, true);
            SDL_FreeSurface(surf);
        }
        buildWallpapers();
    }

    void App::drawBackground(void)
    {
        // Slow and cheap: two glows drifting and the watermark turning, all with
        // textures made once at startup
        float t = mGfx.time();
        mGfx.draw(mBackground, 0, 0, SCREEN_W, SCREEN_H);
        auto glow = [&](float cx, float cy, int w, int h, Color c) {
            if (!mGlow)
                return;
            SDL_SetTextureColorMod(mGlow, c.r, c.g, c.b);
            SDL_SetTextureAlphaMod(mGlow, c.a);
            SDL_Rect dst{int(cx - w / 2), int(cy - h / 2), w, h};
            SDL_RenderCopy(mGfx.renderer(), mGlow, nullptr, &dst);
        };
        glow(1150 + 90 * std::sin(t * 0.21f), -30 + 60 * std::sin(t * 0.17f + 1.0f), 1300, 900,
            theme::RED.withAlpha(58));
        glow(90 + 110 * std::sin(t * 0.15f + 2.0f), 760 + 50 * std::sin(t * 0.19f), 1400, 900,
            theme::BLUE.withAlpha(50));
        glow(640 + 260 * std::sin(t * 0.09f), 380 + 120 * std::sin(t * 0.13f + 0.5f), 900, 700,
            Color{150, 80, 220, 18});
        mGfx.drawRotated(mWatermark, SCREEN_W - 120, SCREEN_H - 90, 620, 620, -18 + t * 2.5, 9);
    }

    void App::drawMessage(void)
    {
        drawHeader("");
        int w = 860, h = 340;
        int x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2 - 10;
        mGfx.shadow(x, y + 6, w, h, 20, 18, theme::SHADOW);
        mGfx.fillRoundRect(x, y, w, h, 20, theme::CARD);
        int ly = y + 50;
        size_t start = 0;
        while (start <= mMessage.size())
        {
            size_t end = mMessage.find('\n', start);
            std::string line =
                mMessage.substr(start, end == std::string::npos ? std::string::npos : end - start);
            mGfx.text(line, SCREEN_W / 2, ly, 24, theme::TEXT, Align::Center, w - 60);
            ly += 36;
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        if (mReturnTo != Screen::Message)
            drawFooter({{HidNpadButton_B, glyph::B, tr("Voltar")}, {HidNpadButton_Plus, glyph::PLUS, tr("Sair")}});
        else
            drawFooter({{HidNpadButton_Plus, glyph::PLUS, tr("Sair")}});
    }

    // ------------------------------------------------------------------ backups

    namespace
    {
        // "20261112-012345" -> "12/11/2026  01:23:45"
        std::string prettyStamp(const std::string& s)
        {
            if (s.size() != 15)
                return s;
            return s.substr(6, 2) + "/" + s.substr(4, 2) + "/" + s.substr(0, 4) + "   " + s.substr(9, 2) + ":" +
                   s.substr(11, 2) + ":" + s.substr(13, 2);
        }

        constexpr int BACKUP_ROW_H = 64, BACKUP_TOP = CONTENT_Y + 70, BACKUP_VISIBLE = 7;
    }

    void App::openBackups(void)
    {
        if (mGames.empty())
            return;
        mBackupList   = listBackups(mGames[mGameCursor], mUsers[mUser].uid, mUsers[mUser].name);
        mBackupCursor = 0;
        mScreen       = Screen::Backups;
    }

    void App::updateBackups(u64 pressed)
    {
        if (pressed & HidNpadButton_B)
        {
            mScreen = Screen::Games;
            return;
        }
        int count     = int(mBackupList.size());
        mBackupCursor = moveInGrid(mBackupCursor, count, 1, pressed & (KEY_UP | KEY_DOWN));
        int first     = std::max(0, std::min(mBackupCursor - BACKUP_VISIBLE / 2, count - BACKUP_VISIBLE));
        for (int i = first; i < count && i < first + BACKUP_VISIBLE; i++)
        {
            if (mInput.tappedIn(60, BACKUP_TOP + (i - first) * BACKUP_ROW_H, SCREEN_W - 120, BACKUP_ROW_H))
            {
                if (i == mBackupCursor)
                    pressed |= HidNpadButton_A;
                mBackupCursor = i;
            }
        }
        if ((pressed & HidNpadButton_A) && count > 0)
        {
            showDialog(tr("Restaurar backup?"),
                tr("O save de ") + mGames[mGameCursor].name + tr(" volta a ser o de\n") +
                    prettyStamp(mBackupList[mBackupCursor]) + tr(". O save atual será guardado antes."),
                {{HidNpadButton_A, glyph::A, tr("Restaurar"), DialogAction::RestoreBackup},
                    {HidNpadButton_B, glyph::B, tr("Cancelar"), DialogAction::None}});
        }
    }

    void App::restoreSelectedBackup(void)
    {
        if (mBackupCursor >= int(mBackupList.size()))
            return;
        GameEntry game       = mGames[mGameCursor];
        AccountUid uid       = mUsers[mUser].uid;
        std::string userName = mUsers[mUser].name;
        std::string stamp    = mBackupList[mBackupCursor];
        std::string error;
        bool ok = false;
        runWithLoading(tr("Restaurando o backup"), [&] { ok = restoreBackup(game, uid, userName, stamp, error); });
        if (!ok)
        {
            showDialog(tr("Não foi possível restaurar"), error, {{HidNpadButton_B, glyph::B, "OK", DialogAction::None}});
            return;
        }
        mBackupList = listBackups(game, uid, userName);
        showToast(tr("Backup de ") + prettyStamp(stamp) + tr(" restaurado."));
    }

    void App::drawBackups(void)
    {
        drawHeader(mGames.empty() ? "" : mGames[mGameCursor].name);
        int x = 60, w = SCREEN_W - 120;
        mGfx.text(tr("Backups deste jogo e usuário"), x, CONTENT_Y + 16, 20, theme::MUTED);
        int count = int(mBackupList.size());
        if (count == 0)
        {
            mGfx.text(tr("Nenhum backup ainda. Um é criado sempre que o jogo é aberto aqui."), SCREEN_W / 2, 330, 22,
                theme::DIM, Align::Center);
        }
        int first = std::max(0, std::min(mBackupCursor - BACKUP_VISIBLE / 2, count - BACKUP_VISIBLE));
        for (int i = first; i < count && i < first + BACKUP_VISIBLE; i++)
        {
            int y = BACKUP_TOP + (i - first) * BACKUP_ROW_H;
            drawDivider(x, y, w);
            mGfx.text(prettyStamp(mBackupList[i]), x + 24, y + (BACKUP_ROW_H - mGfx.textHeight(24)) / 2, 24,
                theme::TEXT);
            if (i == 0)
                mGfx.text(tr("mais recente"), x + w - 24, y + (BACKUP_ROW_H - mGfx.textHeight(18)) / 2, 18, theme::DIM,
                    Align::Right);
            if (i == mBackupCursor)
            {
                mHighlight.moveTo(x, y + 1, w, BACKUP_ROW_H - 2);
                drawHighlight(6);
            }
        }
        if (count > 0)
            drawDivider(x, BACKUP_TOP + std::min(count - first, BACKUP_VISIBLE) * BACKUP_ROW_H, w);
        std::vector<Hint> hints;
        if (count > 0)
            hints.push_back({HidNpadButton_A, glyph::A, tr("Restaurar")});
        hints.push_back({HidNpadButton_B, glyph::B, tr("Voltar")});
        drawFooter(hints);
    }
}
