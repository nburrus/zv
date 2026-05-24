use eframe::egui;

use crate::viewer::AppAction;

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

const SHORTCUTS: &[(egui::Key, ShortcutScope, AppAction)] = &[
    (egui::Key::ArrowDown,  ShortcutScope::GlobalWhenNotTyping, AppAction::NextImage),
    (egui::Key::Space,      ShortcutScope::GlobalWhenNotTyping, AppAction::NextImage),
    (egui::Key::ArrowUp,    ShortcutScope::GlobalWhenNotTyping, AppAction::PreviousImage),
    (egui::Key::Backspace,  ShortcutScope::GlobalWhenNotTyping, AppAction::PreviousImage),
    (egui::Key::Q,          ShortcutScope::GlobalWhenNotTyping, AppAction::Quit),
];

pub fn collect_shortcuts(
    ctx: &egui::Context,
    viewport: ShortcutViewport,
) -> Vec<AppAction> {
    let mut actions = Vec::new();
    let typing_text = ctx.wants_keyboard_input();
    ctx.input(|input| {
        for &(key, scope, action) in SHORTCUTS {
            push_if_pressed(input, key, scope, viewport, typing_text, action, &mut actions);
        }
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
