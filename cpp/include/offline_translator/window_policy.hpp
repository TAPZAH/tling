#pragma once

namespace offline_translator {

// Команды, которые влияют на жизненный цикл окна и трея.
enum class UiCommand {
    window_close,      // крестик / Alt+F4
    tray_open,         // пункт «Открыть» и двойной клик по иконке
    tray_exit,         // пункт «Выход»
    start_minimized,   // аргумент --minimized
    smoke_start,       // аргумент --smoke-start
};

// Что должно произойти в ответ на команду.
enum class UiEffect {
    hide_to_tray,
    restore_window,
    destroy_and_quit,
    start_hidden_in_tray,
    run_smoke_then_destroy,
};

UiEffect effect_for(UiCommand command);

// Smoke не должен прятать окно навсегда и не включает автозагрузку.
bool smoke_may_enable_autostart();
bool smoke_hides_forever();

// Пункты меню трея, которые GUI обязан зарегистрировать.
enum class TrayMenuItem {
    open,
    exit,
};

bool tray_menu_has(TrayMenuItem item);

}  // пространство имён offline_translator
