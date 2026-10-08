#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <MinHook.h>
#include <atomic>
#include <array>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include "game.hpp"
#include "bridge.hpp"
#include "observations.hpp"
#include "menu.hpp"
#include "shop.hpp"
#include "garden.hpp"
#include "continue_dialog.hpp"
#include "options_controls.hpp"
#include "profiles.hpp"
#include "zombatar.hpp"
#include "ui_controls.hpp"
#include "selector_controls.hpp"
#include "lifecycle_controls.hpp"
#include "game_input.hpp"

namespace {
using pvz::json;
constexpr uintptr_t APP_PTR=0x731c50, UPDATE_APP=0x5dd270;
constexpr uintptr_t IMAGE_BASE=0x400000, TEXT_BASE=0x401000;
constexpr size_t TEXT_SIZE=2965148;
constexpr const char* TEXT_HASH="420db8be5f6d704d10ea1fb7c7c815623807b7a2cebb55c80fd66ce70447ff64";
std::atomic<bool> enabled{false};
std::atomic<DWORD> main_thread{0};
std::atomic<uint64_t> main_calls{0};
bool paused_by_bridge=false;
std::atomic<bool> background_patch{false};
std::atomic<bool> framework_resume_pending{false};
uintptr_t paused_board=0, last_board=0;
uint64_t board_generation=0;
int last_tick=-1;
using UpdateFn=bool(__thiscall*)(void*,bool*);
UpdateFn original_update=nullptr;

void set_background_running(bool active) {
    auto address=reinterpret_cast<unsigned char*>(0x5d87c9);
    const unsigned char normal[2]={0x74,0x40},running[2]={0xeb,0x00};
    const auto expected=active?normal:running;
    const auto replacement=active?running:normal;
    if(std::memcmp(address,expected,2)!=0)throw std::runtime_error("Focus branch bytes differ from the verified game.");
    DWORD old=0;
    if(!VirtualProtect(address,2,PAGE_EXECUTE_READWRITE,&old))throw std::runtime_error("Could not enable background updates.");
    std::memcpy(address,replacement,2);
    FlushInstructionCache(GetCurrentProcess(),address,2);
    DWORD ignored=0;VirtualProtect(address,2,old,&ignored);
    background_patch=active;
}

bool readable(uintptr_t address,size_t bytes) {
    if(address<0x10000 || bytes>16*1024*1024 || address+bytes<address) return false;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info))) return false;
    return info.State==MEM_COMMIT && !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
        address+bytes<=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;
}
template<class T> T read(uintptr_t p) {
    if(!readable(p,sizeof(T))) throw std::runtime_error("Game object memory is no longer valid.");
    T value; std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(T)); return value;
}
uintptr_t app(){return read<uint32_t>(APP_PTR);}
uintptr_t board(){return read<uint32_t>(app()+0x868);}
int scene(){return read<int>(app()+0x91c);}
int mode(){return read<int>(app()+0x918);}
std::string hex(uintptr_t value){std::ostringstream out;out<<std::hex<<value;return out.str();}

std::string ui_scope() {
    static std::string previous;
    static uint64_t generation=0;
    static const uint64_t nonce=GetTickCount64();
    const auto a=app();
    std::ostringstream signature;
    signature<<a<<':'<<scene()<<':'<<mode()<<':'<<board()<<':'<<read<uint32_t>(a+0x94c);
    const auto selector=read<uint32_t>(a+0x870);
    signature<<':'<<selector;
    if(selector&&readable(selector,0x168))signature<<':'<<read<int>(selector+0x160)<<':'<<read<int>(selector+0x164);
    using GetDialog=void*(__thiscall*)(void*,int);
    const auto lookup=reinterpret_cast<GetDialog>(read<uint32_t>(read<uint32_t>(a)+0x124));
    for(int id=0;id<64;++id) {
        const auto dialog=lookup(reinterpret_cast<void*>(a),id);
        if(dialog)signature<<':'<<id<<':'<<dialog;
    }
    const auto current=signature.str();
    if(current!=previous){previous=current;++generation;}
    return std::to_string(GetCurrentProcessId())+":"+std::to_string(nonce)+":"+std::to_string(generation);
}

std::string sha256(const void* data,size_t size) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
        throw std::runtime_error("Could not initialize SHA-256.");
    std::array<unsigned char,32> hash{};
    auto status=BCryptHash(algorithm,nullptr,0,(PUCHAR)data,(ULONG)size,hash.data(),(ULONG)hash.size());
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0)throw std::runtime_error("Could not compute game fingerprint.");
    std::ostringstream out;out<<std::hex<<std::setfill('0');
    for(auto b:hash)out<<std::setw(2)<<int(b);
    return out.str();
}

// UpdateAppStep is used both by the outer loop and Dialog::WaitForResult.
// 1096 vtable +0x17c targets 0x5dd270, returns AL, and pops one bool* arg.
// Servicing only outer UpdateApp would strand synchronous purchase dialogs.
std::string board_id(uintptr_t b);
bool __fastcall update_hook(void* self,void*,bool* updated) {
    if(enabled.load() && reinterpret_cast<uintptr_t>(self)==app()) {
        main_thread.store(GetCurrentThreadId());
        ++main_calls;
        if(framework_resume_pending.exchange(false)) {
            // Same WidgetManager::GotFocus call as the game's active focus path.
            // LawnApp::GotFocus is a no-op on this verified build. Board pause is
            // deliberately left alone; menu/manual/controller pauses stay valid.
            auto wm=read<uint32_t>(reinterpret_cast<uintptr_t>(self)+0x320);
            reinterpret_cast<void(__thiscall*)(void*)>(0x54f090)(reinterpret_cast<void*>(wm));
        }
        pvz::shop::tick(reinterpret_cast<uintptr_t>(self));
        pvz::garden::tick(reinterpret_cast<uintptr_t>(self));
        pvz::menu::tick(reinterpret_cast<uintptr_t>(self));
        pvz::options_controls::tick(reinterpret_cast<uintptr_t>(self));
        pvz::profiles::tick(reinterpret_cast<uintptr_t>(self));
        pvz::zombatar::tick(reinterpret_cast<uintptr_t>(self));
        pvz::ui_controls::tick(reinterpret_cast<uintptr_t>(self),ui_scope(),main_thread.load());
        pvz::selector_controls::tick(reinterpret_cast<uintptr_t>(self),ui_scope(),main_thread.load());
        pvz::lifecycle_controls::tick(reinterpret_cast<uintptr_t>(self));
        pvz::game_input::tick(reinterpret_cast<uintptr_t>(self),board_id(board()),main_thread.load());
        bridge::process_pending();
    }
    return original_update(self,updated);
}

std::string board_id(uintptr_t b) {
    int tick=b?read<int>(b+0x5580):0;
    if(b!=last_board || (b&&last_tick>tick)){last_board=b;++board_generation;}
    last_tick=tick;
    return b?std::to_string(GetCurrentProcessId())+":"+std::to_string(board_generation)+":"+hex(b):"";
}
int current_seed_cost(uintptr_t b,int plant_type,int imitater_type);
bool conveyor_belt(uintptr_t b);
json capabilities() {
    json c=json::array({"state","levels","pause","resume"});
    int s=scene();
    auto dialogs=pvz::menu::snapshot(app());
    const auto lifecycle=pvz::lifecycle_controls::snapshot(app());
    if(lifecycle.value("ok",false)) {
        for(const char* name:{"quit_open","quit_cancel","quit_confirm","credits_skip"})
            if(lifecycle.value(std::string(name)+"_available",false))c.push_back(std::string("action.")+name);
    }
    if(s==0 && lifecycle.value("ok",false) &&
       lifecycle.contains("controls") && !lifecycle["controls"].is_null())
        c.push_back("action.click_to_start");
    if(s==1) {
        c.push_back("action.profile_job");c.push_back("action.profile_close");
        try {
            pvz::profiles::initialize_guard(app());
            for(const char* name:{"profile_open","profile_select","profile_create","profile_rename","profile_delete","profile_delete_confirm"})c.push_back(std::string("action.")+name);
        } catch(const std::exception&) { /* Invalid profile layouts fail closed. */ }
        c.push_back("action.zombatar");
        c.push_back("action.navigate");
        c.push_back("action.achievements_back");c.push_back("action.achievements_scroll");
    }
    c.push_back("action.ui");
    if((s==1||((s==2||s==3)&&board()))&&dialogs.value("dialog_count",0)==0)c.push_back("action.open_menu");
    if(dialogs.value("dialog_count",0)==1&&!dialogs["options_dialog"].is_null())c.push_back("action.close_menu");
    const auto options=pvz::options_controls::snapshot(app());
    if(options.value("ok",false)) {
        if(options.value("almanac_dialog",uintptr_t(0))) {
            c.push_back("action.close_almanac");c.push_back("action.almanac_page");c.push_back("action.almanac_entry");
        }
        else if(options.value("options_dialog",uintptr_t(0))&&dialogs.value("dialog_count",0)==1) {
            c.push_back("action.option_set");
            if(s==1&&options["controls"].contains("credits")&&options["controls"]["credits"].value("enabled",false))c.push_back("action.open_credits");
            if(options["controls"].contains("almanac")&&options["controls"]["almanac"].value("enabled",false))c.push_back("action.open_almanac");
        }
    }
    if(pvz::continue_dialog::is_open(app()))c.push_back("action.continue");
    if((s==2||s==3)&&board()&&(dialogs.value("dialog_count",0)==0||!dialogs["options_dialog"].is_null())) {
        c.push_back("action.back_to_menu");
        if(mode()!=43)c.push_back("action.restart");
    }
    if(s==7&&dialogs.value("dialog_count",0)==0)c.push_back("action.back_to_menu");
    if(dialogs.value("dialog_count",0)==1&&pvz::shop::snapshot(app()).value("open",false)) {
        c.push_back("action.shop_buy");c.push_back("action.close_shop");c.push_back("action.shop_page");
    }
    if(s==3&&mode()==43&&pvz::garden::snapshot(app()).value("sale_confirmation_pending",false)) {
        c.push_back("action.garden_sale_confirm");c.push_back("action.garden_sale_cancel");
    }
    if(dialogs.value("dialog_count",0)>0)return c;
    if((s==2||s==3)&&board()&&mode()!=43&&mode()!=50)c.push_back("action.game_input");
    if(s==1||s==7)c.push_back("action.enter_level");
    if(s==3&&mode()==43&&board()) {
        for(const char* name:{"enter_garden","garden_care","garden_move","garden_chocolate","garden_feed_snail","garden_wake_snail","garden_cursor_cancel","garden_wheelbarrow_pick","garden_wheelbarrow_place","garden_next","garden_sell"})c.push_back(std::string("action.")+name);
    }
    if(s==3&&mode()==50&&board()) {c.push_back("action.tree_feed");c.push_back("action.garden_next");c.push_back("action.enter_garden");c.push_back("action.garden_cursor_cancel");}
    uintptr_t profile=read<uint32_t>(app()+0x94c);
    if(profile&&read<int>(profile+0x58)>0&&(s==1||(s==3&&mode()==43)))c.push_back("action.open_shop");
    if(s==2 && read<uint32_t>(app()+0x874)) {
        c.push_back("action.choose_cards");c.push_back("action.start_level");
    }
    if(s==5)c.push_back("action.continue");
    if(s==3 && board()) {
        c.push_back("action.collect");
        if(mode()>=0&&mode()<=15&&read<int>(board()+0x5564)>=0&&read<int>(board()+0x5564)<=5) {
            c.push_back("action.plant");
            c.push_back("action.shovel");
        }
    }
    return c;
}
json snapshot() {
    uintptr_t a=app(), b=board();
    int ui=scene();
    json result={{"process",{{"pid",GetCurrentProcessId()},{"version","1.2.0.1096"}}},
        {"scene",ui},{"mode_id",mode()},{"board_id",board_id(b)},{"tick",b?read<int>(b+0x5580):0},
        {"paused",b?bool(read<uint8_t>(b+0x17c)):false},
        {"sun",b?read<int>(b+0x5578):0},{"wave",b?read<int>(b+0x5594):0},
        {"plants",json::array()},{"zombies",json::array()},{"seed_packets",json::array()},
        {"collectibles",json::array()},{"capabilities",capabilities()},
        {"main_thread_id",main_thread.load()},{"bridge_ticks",main_calls.load()},
        {"result",ui==4?"lost":ui==5?"award":"in_progress"},
        {"observation_complete",false}};
    result["background_running"]=background_patch.load();
    result["window_has_focus"]=bool(read<uint8_t>(a+0x542));
    result["dialogs"]=pvz::menu::snapshot(a);
    result["options"]=pvz::options_controls::snapshot(a);
    result["lifecycle"]=pvz::lifecycle_controls::snapshot(a);
    result["game_input"]=pvz::game_input::snapshot(a,board_id(b),main_thread.load());
    result["ui"]=pvz::ui_controls::snapshot(a,ui_scope(),main_thread.load());
    if(ui==1)result["selector"]=pvz::selector_controls::snapshot(a,ui_scope(),main_thread.load());
    try { result["profiles"]=pvz::profiles::snapshot(a); }
    catch(const std::exception& e) { result["profiles"]={{"ok",false},{"error",e.what()}}; }
    try { result["zombatar"]=pvz::zombatar::snapshot(a); }
    catch(const std::exception& e) { result["zombatar"]={{"ok",false},{"error",e.what()}}; }
    result["shop"]=pvz::shop::snapshot(a);
    if(mode()==43)result["garden"]=pvz::garden::snapshot(a);
    if(mode()==50)result["tree"]=pvz::garden::tree_snapshot(a);
    static const char* scene_names[]={"loading","menu","seedchooser","playing","lost","award","credits","challenge"};
    result["scene_name"]=(ui>=0&&ui<8)?scene_names[ui]:"unknown";
    if(b) {
        result.update(pvz::observations::snapshot(b));
        int background=read<int>(b+0x5564);
        result["background_id"]=background;
        result["rows"]=(background==2||background==3)?6:5;
        result["cols"]=9;
        result["adventure_level"]=read<int>(b+0x5568);
        result["conveyor"]=conveyor_belt(b);
        for(auto& packet:result["seed_packets"]) {
            int t=packet["plant_type"].get<int>();
            if(t>=0&&t<=48)packet["cost"]=current_seed_cost(b,t,packet["imitater_type"].get<int>());
            else packet["usable"]=false;
        }
    }
    uintptr_t profile=read<uint32_t>(a+0x94c);
    if(profile && readable(profile,0x60))result["profile"]={{"adventure_level",read<int>(profile+0x50)},
        {"adventure_completions",read<int>(profile+0x58)},{"coins",read<int>(profile+0x54)*10}};
    uintptr_t chooser=read<uint32_t>(a+0x874);
    if(ui==2 && chooser && readable(chooser,0xd44)) {
        result["chooser"]={{"selected",read<int>(chooser+0xd3c)},{"in_flight",read<int>(chooser+0xd38)},
            {"cards",json::array()}};
        result["chooser"]["capacity"]=read<int>(read<uint32_t>(b+0x15c)+0x24);
        for(int type=0;type<=48;++type) {
            uintptr_t item=chooser+0xbc+type*0x3c;
            result["chooser"]["cards"].push_back({{"plant_type",type},{"state",read<int>(item+0x24)},
                {"bank_index",read<int>(item+0x28)}});
        }
    }
    return result;
}

// These wrappers call normal game handlers on the game's own thread.
__declspec(noinline) int current_seed_cost(uintptr_t b,int plant_type,int imitater_type) {
    const uintptr_t fn=0x4216d0;
    int cost=0;
    __asm {
        mov edi,b
        mov eax,plant_type
        mov edx,imitater_type
        call fn
        mov cost,eax
    }
    return cost;
}
__declspec(noinline) bool conveyor_belt(uintptr_t b) {
    const uintptr_t fn=0x41fa00;
    unsigned char yes=0;
    __asm {
        mov eax,b
        call fn
        mov yes,al
    }
    return yes!=0;
}
__declspec(noinline) void select_packet(uintptr_t packet) {
    const uintptr_t fn=0x496c30;
    __asm {
        push packet
        call fn
    }
}
__declspec(noinline) void plant_at(uintptr_t b,int x,int y) {
    const uintptr_t fn=0x413250;
    __asm {
        push y
        push x
        push b
        mov ecx,1
        call fn
    }
}
__declspec(noinline) void tool_at(uintptr_t b,int x,int y,int tool) {
    const uintptr_t fn=0x414570;
    __asm {
        push tool
        push 1
        mov eax,b
        mov ecx,y
        mov edx,x
        call fn
    }
}
__declspec(noinline) void collect_coin(uintptr_t coin) {
    const uintptr_t fn=0x436950;
    __asm {
        push 1
        mov ecx,coin
        call fn
    }
}
__declspec(noinline) void clear_cursor(uintptr_t b) {
    // Normal seed MouseDown cancellation: ECX is signed clickCount, and
    // the negative branch at 0x413284 restores the seed packet via 0x410200.
    // 0x415120 is tool pickup (EDX=hit type), not cursor cancellation.
    const uintptr_t fn=0x413250;
    __asm {
        push 0
        push 0
        push b
        mov ecx,-1
        call fn
    }
}
__declspec(noinline) void board_pause(uintptr_t b,bool paused) {
    const uintptr_t fn=0x415ce0;
    unsigned char value=paused?1:0;
    __asm {
        mov ecx,b
        mov al,value
        call fn
    }
}
__declspec(noinline) void enter_adventure(uintptr_t selector) {
    const uintptr_t fn=0x450b90;
    __asm {
        push selector
        call fn
    }
}
__declspec(noinline) void choose_seed(uintptr_t chooser,uintptr_t seed) {
    const uintptr_t fn=0x494690;
    __asm {
        push seed
        mov eax,chooser
        call fn
    }
}
__declspec(noinline) void start_chooser(uintptr_t chooser) {
    const uintptr_t fn=0x493c70;
    __asm {
        mov esi,chooser
        call fn
    }
}
__declspec(noinline) bool seed_available(uintptr_t application,int plant_type) {
    const uintptr_t fn=0x459360;
    unsigned char allowed=0;
    __asm {
        mov esi,application
        mov edi,plant_type
        call fn
        mov allowed,al
    }
    return allowed!=0;
}
__declspec(noinline) void continue_award(uintptr_t award) {
    const uintptr_t fn=0x40a310;
    __asm {
        push award
        call fn
    }
}
int seed_type(const json& value) {
    static const std::array<const char*,49> names={"peashooter","sunflower","cherry_bomb","wall_nut","potato_mine","snow_pea","chomper","repeater","puff_shroom","sun_shroom","fume_shroom","grave_buster","hypno_shroom","scaredy_shroom","ice_shroom","doom_shroom","lily_pad","squash","threepeater","tangle_kelp","jalapeno","spikeweed","torchwood","tall_nut","sea_shroom","plantern","cactus","blover","split_pea","starfruit","pumpkin","magnet_shroom","cabbage_pult","flower_pot","kernel_pult","coffee_bean","garlic","umbrella_leaf","marigold","melon_pult","gatling_pea","twin_sunflower","gloom_shroom","cattail","winter_melon","gold_magnet","spikerock","cob_cannon","imitater"};
    if(value.is_number_integer()){int n=value.get<int>();if(n>=0&&n<49)return n;}
    if(value.is_string()) {
        std::string name=value.get<std::string>();
        for(int i=0;i<49;++i)if(name==names[i]||name==std::to_string(i))return i;
    }
    throw std::runtime_error("Unknown plant name or numeric plant id.");
}

json action(const json& args) {
    const std::string type=args.at("type").get<std::string>();
    uintptr_t a=app();int ui=scene();
    if(type=="game_input") {
        if(args.at("board_id").get<std::string>()!=board_id(board()))
            throw std::runtime_error("The requested board is no longer current.");
        auto request=args;request.erase("type");request.erase("board_id");
        return pvz::game_input::action(a,request,board_id(board()),main_thread.load());
    }
    if(type=="click_to_start"||type=="quit_open"||type=="quit_cancel"||type=="quit_confirm"||type=="credits_skip")
        return pvz::lifecycle_controls::action(a,type,args.value("confirm",false));
    if(type=="open_menu")return pvz::menu::open_options(a);
    if(type=="close_menu")return pvz::menu::close_options(a);
    if(type=="open_almanac")return pvz::options_controls::press(a,"almanac");
    if(type=="close_almanac")return pvz::options_controls::press(a,"almanac_close");
    if(type=="open_credits")return pvz::options_controls::press(a,"credits");
    if(type=="option_set")return pvz::options_controls::set(a,args.at("name").get<std::string>(),args.at("value"));
    if(type=="almanac_page")return pvz::options_controls::press(a,"almanac_"+args.at("page").get<std::string>());
    if(type=="almanac_entry")return pvz::options_controls::select_entry(a,args.at("kind").get<std::string>(),args.at("index").get<int>());
    if(type=="navigate"||type=="achievements_back"||type=="achievements_scroll")return pvz::selector_controls::action(a,args,ui_scope(),main_thread.load());
    if(type=="ui") {
        auto request=args;request.erase("type");
        return pvz::ui_controls::dispatch(a,request,ui_scope(),main_thread.load());
    }
    if(type.rfind("profile_",0)==0) {
        if(type!="profile_job"&&type!="profile_close")pvz::profiles::initialize_guard(a);
        auto reply=pvz::profiles::dispatch(a,type,args);
        if(!reply.contains("accepted"))reply["accepted"]=reply.value("state",std::string{})!="failed";
        return reply;
    }
    if(type=="zombatar") {
        auto request=args;request["type"]=args.at("operation");request.erase("operation");
        if(request["type"]=="selector_button")throw std::runtime_error("Use the guarded selector navigation API.");
        return pvz::zombatar::action(a,request);
    }
    if(type=="back_to_menu"||type=="restart") {
        auto dialog=pvz::menu::snapshot(a);
        if(ui!=7&&mode()!=43&&mode()!=50 && dialog["options_dialog"].is_null())pvz::menu::open_options(a);
        json reply=type=="back_to_menu"?pvz::menu::back_to_menu(a):pvz::menu::restart_level(a);
        if(reply.value("confirmation_required",false))reply=type=="back_to_menu"?pvz::menu::confirm_back_to_menu(a):pvz::menu::confirm_restart(a);
        paused_by_bridge=false;paused_board=0;
        return reply;
    }
    if(type=="garden_care") {
        if(pvz::menu::snapshot(a).value("dialog_count",0)!=0)throw std::runtime_error("Close garden dialogs before care.");
        std::string tool=args.at("tool").get<std::string>();
        pvz::garden::CareKind care;
        if(tool=="water")care=pvz::garden::CareKind::water;
        else if(tool=="fertilizer")care=pvz::garden::CareKind::fertilize;
        else if(tool=="bug_spray")care=pvz::garden::CareKind::bug_spray;
        else if(tool=="phonograph")care=pvz::garden::CareKind::phonograph;
        else throw std::runtime_error("Unknown garden care tool.");
        return pvz::garden::care(a,args.at("plant_id").get<uint32_t>(),care);
    }
    if(type=="garden_sale_cancel")return pvz::garden::cancel_sale(a,args.at("plant_id").get<uint32_t>());
    if(type=="garden_sale_confirm") {
        if(!args.value("confirm",false))throw std::runtime_error("Sale requires explicit confirmation.");
        const auto profile=read<uint32_t>(a+0x94c);
        pvz::profiles::authorize_test_profile(a,read<uint32_t>(profile+0x20),"delete");
        return pvz::garden::confirm_sale(a,args.at("plant_id").get<uint32_t>());
    }
    if(type=="garden_cursor_cancel")return pvz::garden::cursor_cancel(a);
    if(type=="enter_garden")return pvz::garden::enter_garden(a,args.at("garden_id").get<int>());
    if(type=="garden_move"||type=="garden_chocolate"||type=="garden_feed_snail"||type=="garden_wake_snail"||type=="garden_wheelbarrow_pick"||type=="garden_wheelbarrow_place"||type=="garden_next"||type=="tree_feed"||type=="garden_sell") {
        if(pvz::menu::snapshot(a).value("dialog_count",0)!=0)throw std::runtime_error("Close dialogs before garden operations.");
        if(type=="garden_next")return pvz::garden::next_garden(a);
        if(type=="tree_feed")return pvz::garden::feed_tree(a);
        if(type=="garden_wheelbarrow_place")return pvz::garden::place_wheelbarrow_plant(a,args.at("row").get<int>(),args.at("col").get<int>());
        if(type=="garden_feed_snail")return pvz::garden::feed_snail(a,args.at("stinky_id").get<uint32_t>());
        if(type=="garden_wake_snail")return pvz::garden::wake_snail(a,args.at("stinky_id").get<uint32_t>());
        const auto id=args.at("plant_id").get<uint32_t>();
        if(type=="garden_move")return pvz::garden::move_plant(a,id,args.at("row").get<int>(),args.at("col").get<int>());
        if(type=="garden_chocolate")return pvz::garden::feed_plant_chocolate(a,id);
        if(type=="garden_wheelbarrow_pick")return pvz::garden::put_in_wheelbarrow(a,id);
        // Original profiles' potted plants are protected from sale. A test
        // profile is authorized only while its identity remains tracked.
        const auto profile=read<uint32_t>(a+0x94c);
        pvz::profiles::authorize_test_profile(a,read<uint32_t>(profile+0x20),"delete");
        return pvz::garden::sell_plant(a,id);
    }
    if(type=="open_shop"||type=="shop_buy"||type=="close_shop"||type=="shop_page") {
        if(type=="open_shop") {
            uintptr_t profile=read<uint32_t>(a+0x94c);
            if(!profile||read<int>(profile+0x58)<1)throw std::runtime_error("Shop unlock check is currently supported for completed Adventure profiles only.");
        }
        auto params=args;params.erase("type");
        if(type=="shop_buy") {
            if(params.value("quantity",1)!=1)throw std::runtime_error("One purchase per action is supported; refresh the quote before buying again.");
            params.erase("quantity");
        }
        return pvz::shop::dispatch(a,type,params);
    }
    if(type=="plant"||type=="shovel"||type=="collect") {
        uintptr_t b=board();
        if(ui!=3||!b)throw std::runtime_error("A playing board is required.");
        if(type!="collect"&&(mode()<0||mode()>15))throw std::runtime_error("This mode requires separately verified gameplay actions.");
        if(read<uint8_t>(b+0x17c))throw std::runtime_error("Resume the game before acting.");
        uintptr_t cursor=read<uint32_t>(b+0x150);
        if(read<int>(cursor+0x30)!=0)throw std::runtime_error("The player already has a plant or tool selected.");
        if(type=="collect") {
            uint32_t id=args.at("entity_id").is_string()?static_cast<uint32_t>(std::stoul(args.at("entity_id").get<std::string>())):args.at("entity_id").get<uint32_t>();
            uint32_t index=id&0xffff;
            uint32_t count=read<uint32_t>(b+0x100);
            if(count>4096||index>=count)throw std::runtime_error("Collectible ID is no longer present.");
            uintptr_t coin=read<uint32_t>(b+0xfc)+index*0xd8;
            if(read<uint32_t>(coin+0xd0)!=id||read<uint8_t>(coin+0x38)||read<uint8_t>(coin+0x50))
                throw std::runtime_error("Collectible is stale, dead, or already being collected.");
            if(read<int>(coin+0x58)==16)throw std::runtime_error("Loose seed packets need the pickup/placement action, not collection.");
            collect_coin(coin);
            return {{"accepted",true},{"entity_id",id},{"settles_during_normal_updates",true}};
        }
        int row=args.at("row").get<int>(),col=args.at("col").get<int>();
        int background=read<int>(b+0x5564),rows=(background==2||background==3)?6:5;
        if(row<1||row>rows||col<1||col>9||background<0||background>5)throw std::runtime_error("Invalid lawn grid coordinate.");
        // Cell centres for the normal game grid; roof columns include their slope.
        // PixelToGridX at 0x420070 computes (x - 40) / 80.
        int x=40+(col-1)*80+40;
        int y=(rows==6?80+(row-1)*85+42:80+(row-1)*100+50);
        if(background==4||background==5)y=80+(row-1)*85+42+std::max(0,5-col)*20;
        if(type=="shovel") {
            if(args.value("layer",std::string("plant"))!="plant")throw std::runtime_error("Specific overlay shoveling is not yet supported.");
            auto entities=pvz::observations::snapshot(b)["plants"];
            bool found=false;
            for(const auto& p:entities)if(p["row"]==row&&p["col"]==col)found=true;
            if(!found)throw std::runtime_error("There is no plant at this cell.");
            tool_at(b,x,y,6);
            auto after=pvz::observations::snapshot(b)["plants"];
            bool changed=after.size()<entities.size();
            return {{"accepted",changed},{"row",row},{"col",col},{"reason",changed?"shoveled":"game_rejected"}};
        }
        uintptr_t bank=read<uint32_t>(b+0x15c);
        int slot=args.at("slot").get<int>(),count=read<int>(bank+0x24);
        if(slot<1||slot>count||count>10)throw std::runtime_error("Invalid seed-bank slot.");
        uintptr_t packet=bank+0x28+(slot-1)*0x50;
        int plant_type=read<int>(packet+0x34),imitater=read<int>(packet+0x38);
        if(plant_type<0||plant_type>48||!read<uint8_t>(packet+0x48))throw std::runtime_error("Seed packet is empty or cooling down.");
        int before_sun=read<int>(b+0x5578),cost=current_seed_cost(b,plant_type,imitater);
        if(!conveyor_belt(b)&&before_sun<cost)throw std::runtime_error("Not enough sun for the selected seed.");
        select_packet(packet);
        if(read<int>(cursor+0x30)!=1)throw std::runtime_error("The game refused to pick up this seed packet.");
        auto plants_before=pvz::observations::snapshot(b)["plants"];
        plant_at(b,x,y);
        bool placed=read<int>(cursor+0x30)==0;
        if(!placed)clear_cursor(b);
        bool at_requested_cell=false;
        const auto plants_after=pvz::observations::snapshot(b)["plants"];
        for(const auto& p:plants_after) {
            if(p["row"]!=row||p["col"]!=col)continue;
            bool existed=false;
            for(const auto& old:plants_before)if(old["id"]==p["id"])existed=true;
            if(!existed)at_requested_cell=true;
        }
        // Instant-use seeds may disappear inside the handler. Never describe an
        // operation that may already have spent resources as a safe-to-retry error.
        bool effect_observed=read<int>(b+0x5578)!=before_sun||!read<uint8_t>(packet+0x48);
        return {{"accepted",placed},{"slot",slot},{"row",row},{"col",col},
            {"sun_before",before_sun},{"sun_after",read<int>(b+0x5578)},
            {"cell_verified",at_requested_cell},{"resource_or_cooldown_changed",effect_observed},
            {"verification_pending",placed&&!at_requested_cell},
            {"reason",placed?"planted":"game_rejected_cell_or_requirements"}};
    }
    if(type=="enter_level") {
        int requested=args.at("mode_id").is_number_integer()?args.at("mode_id").get<int>():std::stoi(args.at("mode_id").get<std::string>());
        if(ui!=1&&ui!=7)throw std::runtime_error("Return to a normal selection menu before entering a level.");
        if(requested>=1&&requested<=72)return pvz::menu::enter_mode(a,requested);
        if(requested!=0||ui!=1)throw std::runtime_error("Adventure starts from the main menu.");
        uintptr_t selector=read<uint32_t>(a+0x870);
        if(!readable(selector,0xc0))throw std::runtime_error("Main menu is not ready.");
        enter_adventure(selector);
        return {{"accepted",true},{"transition_pending",true},{"mode_id",0}};
    }
    if(type=="choose_cards") {
        uintptr_t chooser=read<uint32_t>(a+0x874);
        if(ui!=2||!readable(chooser,0xd44))throw std::runtime_error("Seed chooser is not active.");
        if(read<int>(chooser+0xd38)!=0)throw std::runtime_error("Wait until card selection animation finishes.");
        uintptr_t b=board(),bank=read<uint32_t>(b+0x15c);
        int capacity=read<int>(bank+0x24),selected=read<int>(chooser+0xd3c);
        auto values=args.at("plants");
        if(!values.is_array()||values.empty()||values.size()>10||selected+(int)values.size()>capacity)
            throw std::runtime_error("Selected cards would exceed available slots.");
        std::vector<int> types;
        for(const auto& value:values) {
            int t=seed_type(value);
            if(t==48)throw std::runtime_error("Imitater target selection is not implemented yet.");
            if(std::find(types.begin(),types.end(),t)!=types.end())throw std::runtime_error("Duplicate card in request.");
            if(read<int>(chooser+0xbc+t*0x3c+0x24)!=3 || !seed_available(a,t))
                throw std::runtime_error("Card is already selected, unavailable, or locked.");
            types.push_back(t);
        }
        for(int t:types)choose_seed(chooser,chooser+0xbc+t*0x3c);
        return {{"accepted",true},{"transition_pending",true},{"selected_types",types}};
    }
    if(type=="start_level") {
        uintptr_t chooser=read<uint32_t>(a+0x874),b=board();
        if(ui!=2||!readable(chooser,0xd44)||!b)throw std::runtime_error("Seed chooser is not active.");
        int count=read<int>(read<uint32_t>(b+0x15c)+0x24);
        if(read<int>(chooser+0xd38)!=0||read<int>(chooser+0xd3c)!=count)
            throw std::runtime_error("Fill every seed slot and wait for selection animation.");
        for(int slot=0;slot<count;++slot) {
            bool found=false;
            for(int t=0;t<49;++t){uintptr_t s=chooser+0xbc+t*0x3c;if(read<int>(s+0x24)==1&&read<int>(s+0x28)==slot)found=true;}
            if(!found)throw std::runtime_error("Seed bank selection is not settled.");
        }
        start_chooser(chooser);return {{"accepted",true},{"transition_pending",true}};
    }
    if(type=="continue") {
        if(pvz::continue_dialog::is_open(a))return pvz::continue_dialog::continue_saved_game(a);
        uintptr_t award=read<uint32_t>(a+0x878);
        if(ui!=5||!readable(award,0x30))throw std::runtime_error("No level award is ready.");
        continue_award(award);return {{"accepted",true},{"transition_pending",true}};
    }
    throw std::runtime_error("Unsupported native action: "+type);
}
}

namespace pvz {
bool initialize(std::string& error) {
    try {
        if(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))!=IMAGE_BASE){error="Unexpected main module base.";return false;}
        auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(IMAGE_BASE);
        auto nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(IMAGE_BASE+dos->e_lfanew);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE||nt->Signature!=IMAGE_NT_SIGNATURE||
            nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt->FileHeader.TimeDateStamp!=0x4d02b058||
            nt->OptionalHeader.SizeOfImage!=0x41e000){error="Unsupported runtime PE identity.";return false;}
        if(sha256(reinterpret_cast<void*>(TEXT_BASE),TEXT_SIZE)!=TEXT_HASH){error="Runtime code fingerprint does not match verified Steam 1096.";return false;}
        uintptr_t a=app();
        if(!readable(a,0x960)||read<uint32_t>(read<uint32_t>(a)+0x17c)!=UPDATE_APP){error="Unexpected app/update layout.";return false;}
        if(MH_Initialize()!=MH_OK){error="MinHook initialization failed.";return false;}
        auto status=MH_CreateHook(reinterpret_cast<void*>(UPDATE_APP),&update_hook,reinterpret_cast<void**>(&original_update));
        if(status!=MH_OK){error="Could not create validated main-loop hook.";return false;}
        set_background_running(true);
        framework_resume_pending=true;
        enabled.store(true);
        if(MH_EnableHook(reinterpret_cast<void*>(UPDATE_APP))!=MH_OK){enabled.store(false);set_background_running(false);error="Could not enable game update hook.";return false;}
        return true;
    }catch(const std::exception& e){
        enabled.store(false);
        MH_DisableHook(reinterpret_cast<void*>(UPDATE_APP));
        if(background_patch.load()) {try {set_background_running(false);}catch(...) {}}
        error=e.what();return false;
    }
}
json level_catalog() {
    json entries=json::array({{{"mode_id",0},{"name","Adventure"},{"name_token","ADVENTURE"},
        {"entry_action","enter_level"},{"entry_implemented",true},{"entry_live_verified",true},{"ai_win_verified",false}}});
    for(int id=1;id<=72;++id) {
        const uintptr_t definition=0x72a370+(id-1)*0x18;
        if(read<int>(definition)!=id)throw std::runtime_error("Unexpected challenge definition ordering.");
        const int page=read<int>(definition+8);
        const auto token=read<uint32_t>(definition+0x14);
        std::string name;
        for(int i=0;i<128;++i){char c=read<char>(token+i);if(!c)break;name.push_back(c);}
        if(name.empty()||name.size()==128)throw std::runtime_error("Invalid challenge name token.");
        const bool ordinary=page==0||page==1||page==3;
        const int selector=id==43?109:page==0?110:page==1?101:page==3?102:-1;
        entries.push_back({{"mode_id",id},{"name",name},{"name_token",name},
            {"definition_page",page},{"page_position",{{"row",read<int>(definition+12)},{"column",read<int>(definition+16)}}},
            {"selector_button_id",selector<0?json(nullptr):json(selector)},
            {"entry_action",id==50?json("garden_next"):ordinary||id==43?json("enter_level"):json(nullptr)},
            {"entry_implemented",ordinary||id==43||id==50},{"requires_owned_tree",id==50},
            {"hidden_selector_page",page==2},{"requires_visible_enabled_button",ordinary},
            {"entry_live_verified",id==13||id==43},{"ai_win_verified",false}});
    }
    return entries;
}
json dispatch(const std::string& op,const json& args) {
    if(GetCurrentThreadId()!=main_thread.load())throw std::runtime_error("Game call refused outside the main update thread.");
    if(op=="hello")return {{"version","1.2.0.1096"},{"bridge_version","0.1.0"},{"ready",enabled.load()},
        {"process",{{"pid",GetCurrentProcessId()}}},{"capabilities",capabilities()},
        {"main_thread_id",main_thread.load()},{"bridge_ticks",main_calls.load()},
        {"control_backend","game_main_thread"},{"implementation_stage","verified_core_actions"}};
    if(op=="state")return snapshot();
    if(op=="levels")return level_catalog();
    if(op=="action")return action(args);
    if(op=="pause") {
        uintptr_t b=board();if(!b) return {{"paused",false},{"reason","No battle board."}};
        if(!read<uint8_t>(b+0x17c)){board_pause(b,true);paused_by_bridge=true;paused_board=b;}
        bool actual=bool(read<uint8_t>(b+0x17c));
        if(!actual){paused_by_bridge=false;paused_board=0;}
        return {{"paused",actual},{"owned",actual&&paused_by_bridge&&paused_board==b}};
    }
    if(op=="resume") {
        uintptr_t b=board();
        if(args.value("dismiss_focus_pause",false) && !read<uint8_t>(app()+0x542)) {
            // Explicit recovery of the existing focus-pause dialog (ID 19) uses
            // LawnApp::KillDialog, which applies normal audio/board resume rules.
            reinterpret_cast<bool(__thiscall*)(void*,int)>(0x4568d0)(reinterpret_cast<void*>(app()),19);
        }
        if(paused_by_bridge&&b&&paused_board==b&&pvz::menu::snapshot(app()).value("dialog_count",0)==0)board_pause(b,false);
        paused_by_bridge=false;paused_board=0;
        return {{"paused",b?bool(read<uint8_t>(b+0x17c)):false}};
    }
    if(op=="detach")return {{"detached",true},{"requires_restart",false},{"module_retained_until_exit",true}};
    throw std::runtime_error("Unsupported bridge operation: "+op);
}
void shutdown(){
    enabled.store(false);
    if(background_patch)set_background_running(false);
    // Disable restores the entry bytes. Keep this DLL and its trampoline allocated:
    // this callback still returns through update_hook on the current game thread.
    MH_DisableHook(reinterpret_cast<void*>(UPDATE_APP));
}
}
