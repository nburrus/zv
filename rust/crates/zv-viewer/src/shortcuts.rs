use eframe::egui;

use crate::geometry::WindowResizeAction;
use crate::viewer::AppAction;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ShortcutViewport {
    MainImage, // Root viewport in egui.
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
    (
        egui::Key::ArrowDown,
        ShortcutScope::GlobalWhenNotTyping,
        AppAction::NextImage,
    ),
    (
        egui::Key::Space,
        ShortcutScope::GlobalWhenNotTyping,
        AppAction::NextImage,
    ),
    (
        egui::Key::ArrowUp,
        ShortcutScope::GlobalWhenNotTyping,
        AppAction::PreviousImage,
    ),
    (
        egui::Key::Backspace,
        ShortcutScope::GlobalWhenNotTyping,
        AppAction::PreviousImage,
    ),
    (
        egui::Key::Q,
        ShortcutScope::GlobalWhenNotTyping,
        AppAction::Quit,
    ),
    (
        egui::Key::N,
        ShortcutScope::GlobalWhenNotTyping,
        AppAction::ResizeWindow(WindowResizeAction::Normal),
    ),
    (
        egui::Key::A,
        ShortcutScope::GlobalWhenNotTyping,
        AppAction::ResizeWindow(WindowResizeAction::RestoreAspectRatio),
    ),
    (
        egui::Key::M,
        ShortcutScope::GlobalWhenNotTyping,
        AppAction::ResizeWindow(WindowResizeAction::Maxspect),
    ),
];

pub fn collect_shortcuts(ctx: &egui::Context, viewport: ShortcutViewport) -> Vec<AppAction> {
    let mut actions = Vec::new();
    let typing_text = ctx.wants_keyboard_input();
    ctx.input(|input| {
        for &(key, scope, action) in SHORTCUTS {
            push_if_pressed(
                input,
                key,
                scope,
                viewport,
                typing_text,
                action,
                &mut actions,
            );
        }
        push_resize_text_shortcuts(input, viewport, typing_text, &mut actions);
    });
    actions
}

fn push_resize_text_shortcuts(
    input: &egui::InputState,
    viewport: ShortcutViewport,
    typing_text: bool,
    out_actions: &mut Vec<AppAction>,
) {
    if !scope_allows(ShortcutScope::GlobalWhenNotTyping, viewport, typing_text) {
        return;
    }

    let mut typed_angle = false;
    for event in &input.events {
        let egui::Event::Text(text) = event else {
            continue;
        };
        if text.contains('<') {
            typed_angle = true;
            out_actions.push(AppAction::ResizeWindow(WindowResizeAction::Half));
        }
        if text.contains('>') {
            typed_angle = true;
            out_actions.push(AppAction::ResizeWindow(WindowResizeAction::Double));
        }
    }

    if typed_angle || input.modifiers.shift {
        return;
    }

    if input.key_pressed(egui::Key::Period) {
        out_actions.push(AppAction::ResizeWindow(
            WindowResizeAction::Increase10Percent,
        ));
    }
    if input.key_pressed(egui::Key::Comma) {
        out_actions.push(AppAction::ResizeWindow(
            WindowResizeAction::Decrease10Percent,
        ));
    }
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
