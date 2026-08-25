use eframe::egui;

use crate::annotation_tool::AnnotationMode;
use crate::image_window_geometry::WindowResizeAction;
use crate::layout::shortcut_layout_for_image_count;
use crate::viewer::{AppAction, ArrowKey};

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
        egui::Key::Space,
        ShortcutScope::GlobalWhenNotTyping,
        AppAction::NextImage,
    ),
    (
        egui::Key::Backspace,
        ShortcutScope::GlobalWhenNotTyping,
        AppAction::PreviousImage,
    ),
    (
        egui::Key::PageDown,
        ShortcutScope::GlobalWhenNotTyping,
        AppAction::NextImagePage,
    ),
    (
        egui::Key::PageUp,
        ShortcutScope::GlobalWhenNotTyping,
        AppAction::PreviousImagePage,
    ),
    (egui::Key::Q, ShortcutScope::GlobalWhenNotTyping, AppAction::Quit),
    (
        egui::Key::N,
        ShortcutScope::GlobalWhenNotTyping,
        AppAction::ResizeWindow(WindowResizeAction::Normal),
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
        for &(key, scope, ref action) in SHORTCUTS {
            push_if_pressed(input, key, scope, viewport, typing_text, action.clone(), &mut actions);
        }
        push_arrow_shortcuts(input, viewport, typing_text, &mut actions);
        push_resize_text_shortcuts(input, viewport, typing_text, &mut actions);
        push_layout_shortcuts(input, viewport, typing_text, &mut actions);
        push_annotation_shortcuts(input, viewport, typing_text, &mut actions);
        push_clipboard_shortcuts(input, viewport, typing_text, &mut actions);
        push_color_editor_shortcut(input, viewport, typing_text, &mut actions);
        push_scale_mode_shortcut(input, viewport, typing_text, &mut actions);
    });
    actions
}

fn push_color_editor_shortcut(
    input: &egui::InputState,
    viewport: ShortcutViewport,
    typing_text: bool,
    out_actions: &mut Vec<AppAction>,
) {
    if scope_allows(ShortcutScope::GlobalWhenNotTyping, viewport, typing_text)
        && input.modifiers == egui::Modifiers::NONE
        && input.key_pressed(egui::Key::E)
    {
        out_actions.push(AppAction::ShowColorEditor);
    }
}

/// `s` scales the image to the window, the dual of `a` scaling the window to
/// the image. Modifier-free only, so it stays clear of the Cmd+S save shortcut.
fn push_scale_mode_shortcut(
    input: &egui::InputState,
    viewport: ShortcutViewport,
    typing_text: bool,
    out_actions: &mut Vec<AppAction>,
) {
    if scope_allows(ShortcutScope::GlobalWhenNotTyping, viewport, typing_text)
        && input.modifiers == egui::Modifiers::NONE
        && input.key_pressed(egui::Key::S)
    {
        out_actions.push(AppAction::ScaleImageToWindow);
    }
}

/// Arrow keys are reported as-is. Whether one scrolls the view or moves to
/// the next image depends on how much scroll room is left, which is view
/// state; `Viewer` resolves that against the image window.
fn push_arrow_shortcuts(
    input: &egui::InputState,
    viewport: ShortcutViewport,
    typing_text: bool,
    out_actions: &mut Vec<AppAction>,
) {
    if !scope_allows(ShortcutScope::GlobalWhenNotTyping, viewport, typing_text) {
        return;
    }

    for (key, arrow) in [
        (egui::Key::ArrowDown, ArrowKey::Down),
        (egui::Key::ArrowUp, ArrowKey::Up),
        (egui::Key::ArrowRight, ArrowKey::Right),
        (egui::Key::ArrowLeft, ArrowKey::Left),
    ] {
        if input.key_pressed(key) {
            out_actions.push(AppAction::Arrow(arrow));
        }
    }
}

fn push_annotation_shortcuts(
    input: &egui::InputState,
    viewport: ShortcutViewport,
    typing_text: bool,
    out_actions: &mut Vec<AppAction>,
) {
    if !scope_allows(ShortcutScope::GlobalWhenNotTyping, viewport, typing_text) {
        return;
    }
    if input.key_pressed(egui::Key::L) && input.modifiers.shift {
        out_actions.push(AppAction::SetAnnotationMode(AnnotationMode::AddLine));
    }
    if input.key_pressed(egui::Key::A) && input.modifiers.shift {
        out_actions.push(AppAction::SetAnnotationMode(AnnotationMode::AddArrow));
    }
    if input.key_pressed(egui::Key::R) && input.modifiers.shift {
        out_actions.push(AppAction::SetAnnotationMode(AnnotationMode::AddRectangle));
    }
    if input.key_pressed(egui::Key::E) && input.modifiers.shift {
        out_actions.push(AppAction::SetAnnotationMode(AnnotationMode::AddEllipse));
    }
    if input.key_pressed(egui::Key::T) && input.modifiers.shift {
        out_actions.push(AppAction::SetAnnotationMode(AnnotationMode::AddText));
    }
    if input.key_pressed(egui::Key::Escape) {
        out_actions.push(AppAction::SetAnnotationMode(AnnotationMode::Select));
    }
    if input.key_pressed(egui::Key::Delete) {
        if input.modifiers.shift {
            out_actions.push(AppAction::DeleteImageOnDisk);
        } else {
            out_actions.push(AppAction::DeleteSelectedAnnotation);
        }
    }
    if input.key_pressed(egui::Key::Z) && (input.modifiers.ctrl || input.modifiers.command || input.modifiers.mac_cmd) {
        out_actions.push(AppAction::UndoImageEdit);
    }
    let cmd = input.modifiers.ctrl || input.modifiers.command || input.modifiers.mac_cmd;
    if input.key_pressed(egui::Key::O) && cmd {
        out_actions.push(AppAction::OpenImage);
    }
    if input.key_pressed(egui::Key::W) && cmd {
        out_actions.push(AppAction::CloseImage);
    }
    if input.key_pressed(egui::Key::S) && cmd && !input.modifiers.shift {
        out_actions.push(AppAction::SaveImageEdits);
    }
}

fn push_clipboard_shortcuts(
    input: &egui::InputState,
    viewport: ShortcutViewport,
    typing_text: bool,
    out_actions: &mut Vec<AppAction>,
) {
    if !scope_allows(ShortcutScope::GlobalWhenNotTyping, viewport, typing_text) {
        return;
    }

    // egui-winit consumes native Cmd/Ctrl+C and exposes this semantic event.
    let copy_requested = input.events.iter().any(|event| matches!(event, egui::Event::Copy));
    // egui's paste event is text-only, so image import follows Preview's
    // "New from Clipboard" convention instead of taking over text paste.
    let new_from_clipboard_requested = input.modifiers.command && input.key_pressed(egui::Key::N);

    if copy_requested {
        out_actions.push(AppAction::CopyImageToClipboard);
    }
    if new_from_clipboard_requested {
        out_actions.push(AppAction::PasteImageFromClipboard);
    }
}

fn push_layout_shortcuts(
    input: &egui::InputState,
    viewport: ShortcutViewport,
    typing_text: bool,
    out_actions: &mut Vec<AppAction>,
) {
    if !scope_allows(ShortcutScope::GlobalWhenNotTyping, viewport, typing_text) {
        return;
    }
    if input.modifiers.alt || input.modifiers.ctrl || input.modifiers.command || input.modifiers.mac_cmd {
        return;
    }

    if input.key_pressed(egui::Key::Num0) {
        out_actions.push(AppAction::AutoLayout);
    }

    let keys = [
        egui::Key::Num1,
        egui::Key::Num2,
        egui::Key::Num3,
        egui::Key::Num4,
        egui::Key::Num5,
        egui::Key::Num6,
        egui::Key::Num7,
        egui::Key::Num8,
        egui::Key::Num9,
    ];
    for (index, key) in keys.into_iter().enumerate() {
        if input.key_pressed(key) {
            out_actions.push(AppAction::SetLayout(shortcut_layout_for_image_count(index + 1)));
        }
    }
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

    if input.key_pressed(egui::Key::A)
        && !input.modifiers.shift
        && !input.modifiers.alt
        && !input.modifiers.ctrl
        && !input.modifiers.command
        && !input.modifiers.mac_cmd
    {
        out_actions.push(AppAction::ResizeWindow(WindowResizeAction::RestoreAspectRatio));
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
        out_actions.push(AppAction::ResizeWindow(WindowResizeAction::Increase10Percent));
    }
    if input.key_pressed(egui::Key::Comma) {
        out_actions.push(AppAction::ResizeWindow(WindowResizeAction::Decrease10Percent));
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

#[cfg(test)]
mod tests {
    use super::*;

    fn clipboard_actions(viewport: ShortcutViewport, events: Vec<egui::Event>) -> Vec<AppAction> {
        let ctx = egui::Context::default();
        let mut actions = Vec::new();
        let _ = ctx.run(
            egui::RawInput {
                events,
                modifiers: egui::Modifiers::COMMAND,
                ..Default::default()
            },
            |ctx| actions = collect_shortcuts(ctx, viewport),
        );
        actions
    }

    #[test]
    fn clipboard_shortcuts_are_global_across_viewports() {
        for viewport in [ShortcutViewport::MainImage, ShortcutViewport::Controls] {
            let actions = clipboard_actions(
                viewport,
                vec![
                    egui::Event::Copy,
                    egui::Event::Key {
                        key: egui::Key::N,
                        physical_key: None,
                        pressed: true,
                        repeat: false,
                        modifiers: egui::Modifiers::COMMAND,
                    },
                ],
            );
            assert!(actions.contains(&AppAction::CopyImageToClipboard));
            assert!(actions.contains(&AppAction::PasteImageFromClipboard));
        }
    }

    #[test]
    fn clipboard_shortcuts_are_suppressed_while_typing() {
        assert!(!scope_allows(
            ShortcutScope::GlobalWhenNotTyping,
            ShortcutViewport::MainImage,
            true
        ));
    }
}
