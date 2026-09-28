#include "offline_translator/window_policy.hpp"

namespace offline_translator {

UiEffect effect_for(UiCommand command) {
    switch (command) {
    case UiCommand::window_close:
        return UiEffect::hide_to_tray;
    case UiCommand::tray_open:
        return UiEffect::restore_window;
    case UiCommand::tray_exit:
        return UiEffect::destroy_and_quit;
    case UiCommand::start_minimized:
        return UiEffect::start_hidden_in_tray;
    case UiCommand::smoke_start:
        return UiEffect::run_smoke_then_destroy;
    }
    return UiEffect::destroy_and_quit;
}

bool smoke_may_enable_autostart() {
    return false;
}

bool smoke_hides_forever() {
    return false;
}

bool tray_menu_has(TrayMenuItem item) {
    return item == TrayMenuItem::open || item == TrayMenuItem::exit;
}

}  // пространство имён offline_translator
