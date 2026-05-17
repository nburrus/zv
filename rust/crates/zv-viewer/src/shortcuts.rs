use eframe::egui;

use crate::actions::AppAction;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ShortcutViewport {
    MainImage,  // Root viewport in egui.
    Controls,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[allow(dead_code)]
enum ShortcutScope {
    GlobalAlways,
    GlobalWhenNotTyping,
    ViewportOnly(ShortcutViewport),
}

pub fn collect_shortcuts(
    ctx: &egui::Context,
    viewport: ShortcutViewport,
) -> Vec<AppAction> {
    let mut actions = Vec::new();
    let typing_text = ctx.wants_keyboard_input();
    ctx.input(|input| {
        push_if_pressed(
            input,
            egui::Key::ArrowDown,
            ShortcutScope::GlobalWhenNotTyping,
            viewport,
            typing_text,
            AppAction::NextImage,
            &mut actions,
        );
        push_if_pressed(
            input,
            egui::Key::Space,
            ShortcutScope::GlobalWhenNotTyping,
            viewport,
            typing_text,
            AppAction::NextImage,
            &mut actions,
        );
        push_if_pressed(
            input,
            egui::Key::ArrowUp,
            ShortcutScope::GlobalWhenNotTyping,
            viewport,
            typing_text,
            AppAction::PreviousImage,
            &mut actions,
        );
        push_if_pressed(
            input,
            egui::Key::Backspace,
            ShortcutScope::GlobalWhenNotTyping,
            viewport,
            typing_text,
            AppAction::PreviousImage,
            &mut actions,
        );
        push_if_pressed(
            input,
            egui::Key::Q,
            ShortcutScope::GlobalWhenNotTyping,
            viewport,
            typing_text,
            AppAction::Quit,
            &mut actions,
        );
    });
    actions
}

fn push_if_pressed(
    input: &egui::InputState,
    key: egui::Key,
    scope: ShortcutScope,
    viewport: ShortcutViewport,
    typing_text: bool,
    action: AppAction,
    out_actions: &mut Vec<AppAction>,
) {
    if !input.key_pressed(key) {
        return;
    }
    if !scope_allows(scope, viewport, typing_text) {
        return;
    }
    out_actions.push(action);
}

fn scope_allows(scope: ShortcutScope, viewport: ShortcutViewport, typing_text: bool) -> bool {
    match scope {
        ShortcutScope::GlobalAlways => true,
        ShortcutScope::GlobalWhenNotTyping => !typing_text,
        ShortcutScope::ViewportOnly(shortcut_viewport) => shortcut_viewport == viewport,
    }
}
