// Screens and state of the MonHex app
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "config.hpp"
#include "media.hpp"
#include "pkx/PKX.hpp"
#include "sav/Item.hpp"
#include "savedata.hpp"
#include "ui/gfx.hpp"
#include "ui/input.hpp"

namespace monhex
{
    class App
    {
    public:
        // Returns false (with error set) if the screen or system services can't start
        bool init(std::string& error);
        void exit(void);
        // One frame; returns false when the user wants to leave
        bool frame(void);

    private:
        enum class Screen
        {
            Users,
            Games,
            Boxes,
            Settings,
            Backups,
            Message
        };

        struct UserEntry
        {
            AccountUid uid;
            std::string name;
            SDL_Texture* avatar = nullptr;
        };

        struct Hint
        {
            u64 button;
            u32 glyph;
            std::string label;
        };

        struct HintArea
        {
            SDL_Rect rect;
            u64 button;
        };

        enum class DialogAction
        {
            None,
            LeaveBoxes,
            Quit,
            SaveThenLeave,
            SaveThenQuit,
            RestoreBackup,
            Release
        };

        // A place for a Pokemon: a party slot, or a box and slot
        struct SlotRef
        {
            bool party;
            int box;
            int slot;
            bool bank = false; // a box of the bank on the SD card
            bool operator==(const SlotRef& o) const
            {
                return party == o.party && bank == o.bank && box == o.box && slot == o.slot;
            }
        };

        // What the search needs to know about each Pokemon
        struct SearchEntry
        {
            SlotRef ref{};
            std::string name, speciesName;
            bool shiny = false;
            u8 level = 1, type1 = 0, type2 = 0;
        };

        struct Search
        {
            bool open = false;
            std::vector<SearchEntry> index;
            std::vector<int> results;
            std::string text;
            int shiny = 0;   // any, only shiny, no shiny
            int type  = -1;  // -1 any
            int minLevel = 1, maxLevel = 100;
            bool includeBank = true;
            int row = 0, cursor = 0, scroll = 0;
            bool onResults = false;
            float time = 0;
        };

        // A Pokemon picked up to move or clone, and where it came from
        struct Held
        {
            std::unique_ptr<pksm::PKX> pk;
            SlotRef from;
        };

        enum class MenuAction
        {
            Details,
            Edit,
            Move,
            Clone,
            Export,
            Release,
            ClearMarks,
            Create,
            Import
        };

        struct MenuOption
        {
            std::string label;
            MenuAction action;
        };

        // The options shown next to a slot when A is pressed on it
        struct Menu
        {
            bool open = false;
            std::vector<MenuOption> options;
            int cursor = 0;
            SDL_Rect anchor{};
            float time = 0;
        };

        struct DialogOption
        {
            u64 button;
            u32 glyph;
            std::string label;
            DialogAction action;
        };

        enum class PickerKind
        {
            Species,
            CreateSpecies, // choosing what to create in an empty slot
            Move,
            Item,
            MetLocation,
            EggLocation,
            Version,
            ImportFile,
            BagItem,
            Ball
        };

        struct PickerItem
        {
            int id;
            std::string name;
            u8 type;          // move type for a coloured dot, 255 for none
            int iconItem = -1; // item whose picture is shown, -1 for none
            int iconBall = -1; // Poke Ball whose picture is shown, -1 for none
            int iconSpecies = -1; // species whose box sprite is shown, -1 for none
            int badgeNumber = -1; // number shown in a badge on the right (Pokedex number)
        };

        // Searchable list for long choices (species, moves, items, places...)
        struct Picker
        {
            bool open = false;
            PickerKind kind = PickerKind::Species;
            int index = 0; // which move
            std::string title, query;
            std::vector<PickerItem> items;
            std::vector<int> filtered; // indices into items matching the query
            int cursor = 0, scroll = 0;
            bool allowNumber = false; // X types a number instead
            float time = 0;
            SDL_Rect area{}; // where the list is drawn
        };

        struct BagRow
        {
            int pouch; // index into the save's pouches
            int slot;  // -1 for the "add an item" row
            u16 id;
            u16 count;
            u16 max;   // most the game allows of this item
        };

        // Pouches shown under one name (battle items go with medicine)
        struct BagGroup
        {
            std::string name;
            std::vector<int> pouches;
        };

        enum class View
        {
            Boxes,
            Party,
            Bag,
            Bank
        };

        struct Dialog
        {
            bool open = false;
            std::string title, message;
            std::vector<DialogOption> options;
            std::vector<SDL_Rect> areas; // where each option was drawn, for taps
            float time = 0;
        };

        // The selection frame glides from one item to the next
        struct Highlight
        {
            float x = 0, y = 0, w = 0, h = 0;
            bool placed = false;
            void moveTo(int tx, int ty, int tw, int th);
            void snap(void) { placed = false; }
        };

        // Startup and screen changes
        void loadUsers(void);
        void selectUser(int index);
        void openGame(int index);
        void showMessage(const std::string& message, Screen returnTo);
        // Runs work on another thread while the loading animation plays
        void runWithLoading(const std::string& text, const std::function<void()>& work);

        // Input per screen; `pressed` includes taps on the footer hints
        void updateUsers(u64 pressed);
        void updateGames(u64 pressed);
        void updateBoxes(u64 pressed);
        void updateSettings(u64 pressed);
        void updateMessage(u64 pressed);

        // Drawing
        void drawHeader(const std::string& context, int contextWidth = 820);
        View currentView(void) const;
        void setView(View view);
        std::vector<std::pair<View, SDL_Rect>> viewTabs(void);
        void drawViewTabs(void);
        bool tapViewTabs(void);
        SDL_Rect searchButton(void);
        void drawFooter(const std::vector<Hint>& hints);
        void drawHighlight(int radius);
        void drawLoading(const std::string& text);
        void drawUsers(void);
        void drawGames(void);
        void drawBoxes(void);
        void drawSettings(void);
        void drawMessage(void);
        void drawBoxHeader(const std::string& title, bool arrows);
        void drawBoxSlots(const std::vector<std::unique_ptr<pksm::PKX>>& slots, int cursor, int box);
        void drawSlotMarks(const SDL_Rect& r, const SlotRef& ref);
        void drawParty(void);
        void drawSummary(const pksm::PKX* pk);
        void drawDetail(void);
        void drawRadar(const pksm::PKX& pk, int cx, int cy, int radius, float grow);
        void drawShinyMark(int cx, int cy, int size);
        void drawShinyStage(int x, int y, int w, int h, int radius);
        void updateDetailMedia(const pksm::PKX& pk);
        void drawDetailSprite(const pksm::PKX& pk, int x, int y, int w, int h, float swapTime);
        void playCry(void);

        // Editing (app_editor.cpp)
        void startEditing(void);
        void createPokemon(pksm::Species species);
        void finishEditing(bool keep);
        void editField(int row, int delta, bool toLimit, bool activate);
        std::string fieldValue(int row) const;
        void openPicker(PickerKind kind, int index);
        void filterPicker(void);
        void choosePicked(int id);
        void updatePicker(u64 pressed);
        void drawPicker(void);
        void updateNatureChart(u64 pressed);
        void drawNatureChart(void);
        void updateEditor(u64 pressed);
        SDL_Rect editorArea(void) const;
        std::string editorDescription(void) const;
        void drawEditor(void);
        void saveChanges(DialogAction after);
        void runDialogAction(DialogAction action);
        void askUnsaved(DialogAction leave, DialogAction saveThenLeave);
        void showDialog(const std::string& title, const std::string& message, std::vector<DialogOption> options);
        bool updateDialog(u64 pressed);
        void drawDialog(void);
        void showToast(const std::string& text);
        void drawToast(void);

        // Organising (app_storage.cpp)
        SlotRef currentSlot(void) const;
        std::unique_ptr<pksm::PKX> readSlot(const SlotRef& ref) const;
        bool writeSlot(const SlotRef& ref, const pksm::PKX& pk);
        bool fitsGame(const pksm::PKX& pk, std::string& why) const;
        void clearSlot(const SlotRef& ref);
        bool isMarked(const SlotRef& ref) const;
        void toggleMark(void);
        std::vector<SlotRef> actionSlots(void) const;
        int partyLeftAfterRemoving(const std::vector<SlotRef>& refs) const;
        void afterStorageChange(bool gameChanged);
        void pickUp(bool clone);
        bool isHeldOrigin(const SlotRef& ref) const;
        void dropHeld(void);
        void releaseSelected(void);
        void askRelease(void);
        void exportSelected(void);
        void openImport(void);
        void importFile(const std::string& path);
        void openSlotMenu(void);
        void runMenuAction(MenuAction action);
        SDL_Rect menuRect(void) const;
        void updateMenu(u64 pressed);
        void drawMenu(void);
        void drawHeld(int cx, int bottomY);

        // Bank and search (app_bank.cpp)
        std::unique_ptr<pksm::PKX> readBankSlot(int box, int slot) const;
        bool writeBankSlot(int box, int slot, const pksm::PKX& pk);
        void clearBankSlot(int box, int slot);
        void loadBankSlots(void);
        std::vector<std::unique_ptr<pksm::PKX>> bankBox(int box) const;
        bool hasUnsaved(void) const;
        bool commitBank(bool additions, std::string& error);
        void openSearch(void);
        void buildSearchIndex(void);
        void filterSearch(void);
        void jumpTo(const SlotRef& ref);
        void updateSearch(u64 pressed);
        void drawSearch(void);

        // Bag (app_bag.cpp)
        std::vector<BagGroup> bagGroups(void) const;
        u16 bagMax(int pouch, const pksm::Item& item) const;
        std::vector<BagRow> bagRows(void) const;
        void setBagCount(const BagRow& row, int count);
        void openBagPicker(const BagRow& row);
        void pickBagItem(int id);
        void updateBag(u64 pressed);
        void drawBag(void);

        // Backups screen
        void openBackups(void);
        void updateBackups(u64 pressed);
        void drawBackups(void);
        void restoreSelectedBackup(void);
        void drawDivider(int x, int y, int w);
        void buildBackground(void);
        void drawBackground(void);
        void buildWallpapers(void);
        void drawWallpaper(int box, bool party);
        void drawPointer(int cx, int bottomY);

        void loadSlots(void);
        const pksm::PKX* selectedPokemon(void) const;
        std::string boxTitle(int box) const;
        void changeBox(int delta);
        void openDetail(void);
        float pressGlow(void) const;

        ui::Gfx mGfx;
        ui::Input mInput;
        Config mConfig;
        Media mMedia;
        int mSettingsRow = 0;
        bool mGfxUp = false, mAccountsUp = false, mNsUp = false, mRomfsUp = false;

        Screen mScreen   = Screen::Users;
        Screen mReturnTo = Screen::Users;
        std::string mMessage;

        std::vector<UserEntry> mUsers;
        int mUserCursor = 0;
        int mUser       = -1;

        std::vector<GameEntry> mGames;
        std::vector<SDL_Texture*> mGameIcons;
        int mGameCursor = 0;

        LoadedSave mSave;
        GameEntry mOpenGame{};
        bool mDirty = false; // edits not yet written to the console

        bool mEditing = false;
        std::unique_ptr<pksm::PKX> mEditPk; // the copy being edited
        bool mEditChanged = false;
        bool mCreating = false; // the copy being edited is new, for an empty slot
        Picker mPicker;
        bool mNatureChart = false; // the 5x5 nature chart, opened with A on the nature
        u8 mNatureCursor  = 0;
        float mNatureChartTime = 0;
        int mEditRow = 0, mEditScroll = 0;

        Dialog mDialog;
        std::string mToast;
        float mToastTime = -10;
        bool mQuit = false;

        bool mBankView = false; // showing the bank instead of the game's boxes
        // Bank slots changed but not yet written to the SD card (nullptr = emptied)
        std::map<std::pair<int, int>, std::unique_ptr<pksm::PKX>> mBankPending;
        int mBankBox   = 0;
        Search mSearch;
        bool mBagView = false;
        int mBagPouch = 0, mBagCursor = 0, mBagScroll = 0;
        int mBagFocus = 0; // 0 pouches, 1 items
        int mBagPickPouch = -1, mBagPickSlot = -1; // what the item picker replaces
        std::vector<Held> mHeld; // Pokemon being moved or cloned
        bool mHeldClone = false;
        float mHoldTime = 0;
        std::vector<SlotRef> mMarked; // chosen with ZR for actions on many
        Menu mMenu;
        std::vector<std::string> mImportFiles;

        std::vector<std::string> mBackupList;
        int mBackupCursor = 0;
        int mBox        = 0;
        bool mPartyView = false;
        int mSlotCursor = 0;
        std::vector<std::unique_ptr<pksm::PKX>> mSlots;
        // Box slide animation: the box being left and which way it goes
        std::vector<std::unique_ptr<pksm::PKX>> mPrevSlots;
        int mPrevBox       = 0;
        int mBoxAnimDir    = 1;
        float mBoxAnimTime = -10;
        // Summary entrance animation, restarted when the selection changes
        int mSummaryKey    = -1;
        float mSummaryTime = -10;
        // Detail view (A on a Pokemon) over a blurred copy of the screen
        bool mDetail          = false;
        float mDetailTime     = -10;
        float mDetailSwapTime = -10;
        SDL_Texture* mBlurred = nullptr; // owned by Gfx
        // Cry of the Pokemon in the detail view
        std::string mCryId;
        float mCryTime = -10;
        bool mCryWanted = false;

        SDL_Texture* mBackground = nullptr;
        SDL_Texture* mWatermark  = nullptr;
        SDL_Texture* mGlow       = nullptr;
        SDL_Texture* mWallpapers[4]{};
        Highlight mHighlight;
        float mPressTime = -10; // when A was last pressed, for the flash
        std::vector<HintArea> mHintAreas; // from the previous frame, for taps
        u64 mFlashButton     = 0;
        float mFlashTime     = -10;
    };
}
