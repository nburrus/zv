use std::collections::{HashMap, VecDeque};
use std::path::PathBuf;

use eframe::egui;
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug)]
pub struct DebugConfig {
    script_json: Option<PathBuf>,
    artifact_dir: Option<PathBuf>,
    wait_frames: Option<u64>,
}

#[derive(Clone, Debug)]
pub struct ViewerDebugState {
    pub image_rect: Option<egui::Rect>,
    pub controls_enabled: bool,
    pub controls_viewport_id: egui::ViewportId,
    pub controls_target_position: Option<egui::Pos2>,
    pub cursor_info: Option<crate::image_window::CursorPixelInfo>,
    pub selected_image: Option<SelectedImageDebug>,
}

#[derive(Clone, Debug, Serialize)]
pub struct SelectedImageDebug {
    pub name: String,
    pub width: u32,
    pub height: u32,
    pub bytes_per_row: usize,
}

#[derive(Clone, Debug)]
struct ScreenshotRequest {
    name: String,
    path: PathBuf,
}

#[derive(Debug)]
pub struct RuntimeDebug {
    actions: Vec<DebugAction>,
    action_index: usize,
    artifact_dir: PathBuf,
    frame_index: u64,
    events_by_viewport: HashMap<egui::ViewportId, Vec<egui::Event>>,
    pending_screenshots: usize,
    wait_until_frame: Option<u64>,
    wait_for_image_started_at_frame: Option<u64>,
    quit_sent_at_frame: Option<u64>,
    trace: Vec<TraceEntry>,
}

#[derive(Clone, Debug, Deserialize)]
struct DebugScriptFile {
    #[serde(default)]
    wait_frames_default: Option<u64>,
    actions: Vec<DebugAction>,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
enum DebugAction {
    WaitForImage {
        #[serde(default = "default_wait_for_image_timeout")]
        timeout_frames: u64,
    },
    WaitFrames {
        frames: u64,
    },
    Hover {
        target: DebugTarget,
        at: [f32; 2],
    },
    RightClick {
        target: DebugTarget,
        at: [f32; 2],
    },
    Key {
        viewport: DebugViewport,
        key: DebugKey,
    },
    Screenshot {
        name: String,
        viewport: DebugViewport,
    },
    State {
        name: String,
    },
}

#[derive(Clone, Copy, Debug, Deserialize)]
#[serde(rename_all = "snake_case")]
enum DebugTarget {
    Image,
}

#[derive(Clone, Copy, Debug, Deserialize)]
#[serde(rename_all = "snake_case")]
enum DebugViewport {
    Root,
    Controls,
}

#[derive(Clone, Copy, Debug, Deserialize)]
#[serde(rename_all = "snake_case")]
enum DebugKey {
    Q,
}

#[derive(Debug, Serialize)]
struct TraceEntry {
    frame: u64,
    action_index: usize,
    kind: &'static str,
    name: String,
    path: String,
}

#[derive(Debug, Serialize)]
struct StateSnapshot<'a> {
    frame: u64,
    selected_image: Option<&'a SelectedImageDebug>,
    image_window: ImageWindowSnapshot,
    controls_window: ControlsWindowSnapshot,
    cursor: Option<CursorSnapshot<'a>>,
}

#[derive(Debug, Serialize)]
struct ImageWindowSnapshot {
    visible: bool,
    viewport: &'static str,
    image_rect: Option<RectSnapshot>,
}

#[derive(Debug, Serialize)]
struct ControlsWindowSnapshot {
    visible: bool,
    viewport: &'static str,
    target_position: Option<PosSnapshot>,
}

#[derive(Debug, Serialize)]
struct CursorSnapshot<'a> {
    image: &'a str,
    x: u32,
    y: u32,
    rgba: [u8; 4],
}

#[derive(Clone, Copy, Debug, Serialize)]
struct RectSnapshot {
    x: f32,
    y: f32,
    w: f32,
    h: f32,
}

#[derive(Clone, Copy, Debug, Serialize)]
struct PosSnapshot {
    x: f32,
    y: f32,
}

impl DebugConfig {
    pub fn new(script_json: Option<PathBuf>, artifact_dir: Option<PathBuf>, wait_frames: Option<u64>) -> Self {
        Self {
            script_json,
            artifact_dir,
            wait_frames,
        }
    }

    pub fn into_runtime(self) -> Option<RuntimeDebug> {
        let path = self.script_json?;

        let artifact_dir = self.artifact_dir.unwrap_or_else(|| PathBuf::from("debug-artifacts"));
        let contents = std::fs::read_to_string(&path)
            .unwrap_or_else(|err| panic!("failed to read debug script JSON {}: {err}", path.display()));
        let script: DebugScriptFile = serde_json::from_str(&contents)
            .unwrap_or_else(|err| panic!("failed to parse debug script JSON {}: {err}", path.display()));
        let actions = expand_default_waits(script.actions, self.wait_frames.or(script.wait_frames_default));

        Some(RuntimeDebug {
            actions,
            action_index: 0,
            artifact_dir,
            frame_index: 0,
            events_by_viewport: HashMap::new(),
            pending_screenshots: 0,
            wait_until_frame: None,
            wait_for_image_started_at_frame: None,
            quit_sent_at_frame: None,
            trace: Vec::new(),
        })
    }
}

impl RuntimeDebug {
    pub fn raw_input_hook(&mut self, raw_input: &mut egui::RawInput) {
        self.save_screenshot_events(&raw_input.events);
        raw_input.events.retain(|event| !is_user_input_event(event));

        if let Some(events) = self.events_by_viewport.remove(&raw_input.viewport_id) {
            raw_input.events.extend(events);
        }
    }

    pub fn update_after_viewer(&mut self, ctx: &egui::Context, state: &ViewerDebugState) {
        self.frame_index += 1;
        self.assert_quit_did_not_stall();

        if self.pending_screenshots > 0 {
            ctx.request_repaint();
            return;
        }

        while self.action_index < self.actions.len() {
            let action = self.actions[self.action_index].clone();
            let should_continue = self.apply_action(ctx, state, action);
            if !should_continue {
                break;
            }
        }

        if self.action_index < self.actions.len() {
            ctx.request_repaint();
        }
    }

    fn apply_action(&mut self, ctx: &egui::Context, state: &ViewerDebugState, action: DebugAction) -> bool {
        match action {
            DebugAction::WaitForImage { timeout_frames } => {
                if state.image_rect.is_some() {
                    self.wait_for_image_started_at_frame = None;
                    self.advance_action();
                    true
                } else {
                    let start = *self.wait_for_image_started_at_frame.get_or_insert(self.frame_index);
                    assert!(
                        self.frame_index.saturating_sub(start) <= timeout_frames,
                        "debug script timed out waiting for image after {timeout_frames} frames"
                    );
                    false
                }
            }
            DebugAction::WaitFrames { frames } => {
                let wait_until = *self.wait_until_frame.get_or_insert(self.frame_index + frames);
                if self.frame_index >= wait_until {
                    self.wait_until_frame = None;
                    self.advance_action();
                    true
                } else {
                    false
                }
            }
            DebugAction::Hover { target, at } => {
                let pos = resolve_target_pos(target, at, state);
                // Debug "root" input targets the main image viewport/window.
                let viewport_id = egui::ViewportId::ROOT;
                self.queue_hover(viewport_id, pos);
                request_repaint_after_scripted_input(ctx, state, viewport_id);
                self.advance_action();
                false
            }
            DebugAction::RightClick { target, at } => {
                let pos = resolve_target_pos(target, at, state);
                // Debug "root" input targets the main image viewport/window.
                let viewport_id = egui::ViewportId::ROOT;
                self.queue_right_click(viewport_id, pos);
                request_repaint_after_scripted_input(ctx, state, viewport_id);
                self.advance_action();
                false
            }
            DebugAction::Key { viewport, key } => {
                let viewport_id = resolve_viewport(viewport, state);
                self.queue_key(viewport_id, key);
                request_repaint_after_scripted_input(ctx, state, viewport_id);
                if matches!(key, DebugKey::Q) {
                    self.quit_sent_at_frame = Some(self.frame_index);
                }
                self.advance_action();
                false
            }
            DebugAction::Screenshot { name, viewport } => {
                self.request_screenshot(ctx, resolve_viewport(viewport, state), name);
                self.advance_action();
                false
            }
            DebugAction::State { name } => {
                self.write_state_snapshot(name, state);
                self.advance_action();
                true
            }
        }
    }

    fn advance_action(&mut self) {
        self.action_index += 1;
    }

    fn queue_hover(&mut self, viewport_id: egui::ViewportId, pos: egui::Pos2) {
        self.queue_events(viewport_id, [egui::Event::PointerMoved(pos)]);
    }

    #[allow(dead_code)]
    fn queue_drag(
        &mut self,
        viewport_id: egui::ViewportId,
        button: egui::PointerButton,
        positions: impl IntoIterator<Item = egui::Pos2>,
    ) {
        let mut positions = positions.into_iter();
        let Some(first) = positions.next() else {
            return;
        };

        let mut events = vec![
            egui::Event::PointerMoved(first),
            egui::Event::PointerButton {
                pos: first,
                button,
                pressed: true,
                modifiers: egui::Modifiers::NONE,
            },
        ];
        let mut last = first;
        for pos in positions {
            last = pos;
            events.push(egui::Event::PointerMoved(pos));
        }
        events.push(egui::Event::PointerButton {
            pos: last,
            button,
            pressed: false,
            modifiers: egui::Modifiers::NONE,
        });
        self.queue_events(viewport_id, events);
    }

    fn queue_right_click(&mut self, viewport_id: egui::ViewportId, pos: egui::Pos2) {
        self.queue_events(
            viewport_id,
            [
                egui::Event::PointerMoved(pos),
                egui::Event::PointerButton {
                    pos,
                    button: egui::PointerButton::Secondary,
                    pressed: true,
                    modifiers: egui::Modifiers::NONE,
                },
                egui::Event::PointerButton {
                    pos,
                    button: egui::PointerButton::Secondary,
                    pressed: false,
                    modifiers: egui::Modifiers::NONE,
                },
            ],
        );
    }

    fn queue_key(&mut self, viewport_id: egui::ViewportId, key: DebugKey) {
        let key = match key {
            DebugKey::Q => egui::Key::Q,
        };
        self.queue_events(
            viewport_id,
            [
                egui::Event::Key {
                    key,
                    physical_key: Some(key),
                    pressed: true,
                    repeat: false,
                    modifiers: egui::Modifiers::NONE,
                },
                egui::Event::Key {
                    key,
                    physical_key: Some(key),
                    pressed: false,
                    repeat: false,
                    modifiers: egui::Modifiers::NONE,
                },
            ],
        );
    }

    fn queue_events(&mut self, viewport_id: egui::ViewportId, events: impl IntoIterator<Item = egui::Event>) {
        self.events_by_viewport.entry(viewport_id).or_default().extend(events);
    }

    fn request_screenshot(&mut self, ctx: &egui::Context, viewport_id: egui::ViewportId, name: String) {
        let path = self.artifact_dir.join(format!("{name}.png"));
        let request = ScreenshotRequest {
            name: name.clone(),
            path: path.clone(),
        };
        ctx.send_viewport_cmd_to(
            viewport_id,
            egui::ViewportCommand::Screenshot(egui::UserData::new(request)),
        );
        self.pending_screenshots += 1;
        self.trace.push(TraceEntry {
            frame: self.frame_index,
            action_index: self.action_index,
            kind: "screenshot",
            name,
            path: path.display().to_string(),
        });
        self.write_trace();
    }

    fn save_screenshot_events(&mut self, events: &[egui::Event]) {
        let mut saved = VecDeque::new();
        for event in events {
            let egui::Event::Screenshot { user_data, image, .. } = event else {
                continue;
            };

            let Some(data) = user_data.data.as_ref() else {
                continue;
            };
            let Some(request) = data.downcast_ref::<ScreenshotRequest>() else {
                continue;
            };

            ensure_parent_dir(&request.path);
            image::save_buffer(
                &request.path,
                image.as_raw(),
                image.size[0] as u32,
                image.size[1] as u32,
                image::ColorType::Rgba8,
            )
            .unwrap_or_else(|err| panic!("failed to save debug screenshot {}: {err}", request.path.display()));
            saved.push_back(request.name.clone());
        }

        while let Some(name) = saved.pop_front() {
            self.pending_screenshots = self.pending_screenshots.saturating_sub(1);
            tracing::info!("saved debug screenshot {name}");
        }
    }

    fn write_state_snapshot(&mut self, name: String, state: &ViewerDebugState) {
        let path = self.artifact_dir.join(format!("{name}.json"));
        ensure_parent_dir(&path);
        let snapshot = StateSnapshot {
            frame: self.frame_index,
            selected_image: state.selected_image.as_ref(),
            image_window: ImageWindowSnapshot {
                visible: state.image_rect.is_some(),
                viewport: "root",
                image_rect: state.image_rect.map(RectSnapshot::from),
            },
            controls_window: ControlsWindowSnapshot {
                visible: state.controls_enabled,
                viewport: "controls",
                target_position: state.controls_target_position.map(PosSnapshot::from),
            },
            cursor: state.cursor_info.as_ref().map(|cursor| CursorSnapshot {
                image: cursor.image_name.as_str(),
                x: cursor.x,
                y: cursor.y,
                rgba: cursor.rgba,
            }),
        };
        let json = serde_json::to_string_pretty(&snapshot).expect("debug state snapshot should serialize");
        std::fs::write(&path, json)
            .unwrap_or_else(|err| panic!("failed to write debug state {}: {err}", path.display()));

        self.trace.push(TraceEntry {
            frame: self.frame_index,
            action_index: self.action_index,
            kind: "state",
            name,
            path: path.display().to_string(),
        });
        self.write_trace();
    }

    fn write_trace(&self) {
        let path = self.artifact_dir.join("trace.json");
        ensure_parent_dir(&path);
        let json = serde_json::to_string_pretty(&self.trace).expect("debug trace should serialize");
        std::fs::write(&path, json)
            .unwrap_or_else(|err| panic!("failed to write debug trace {}: {err}", path.display()));
    }

    fn assert_quit_did_not_stall(&self) {
        if let Some(quit_frame) = self.quit_sent_at_frame {
            assert!(
                self.frame_index.saturating_sub(quit_frame) <= 30,
                "debug script sent q, but the app did not exit within 30 frames"
            );
        }
    }
}

impl From<egui::Rect> for RectSnapshot {
    fn from(rect: egui::Rect) -> Self {
        Self {
            x: rect.min.x,
            y: rect.min.y,
            w: rect.width(),
            h: rect.height(),
        }
    }
}

impl From<egui::Pos2> for PosSnapshot {
    fn from(pos: egui::Pos2) -> Self {
        Self { x: pos.x, y: pos.y }
    }
}

fn expand_default_waits(actions: Vec<DebugAction>, wait_frames_default: Option<u64>) -> Vec<DebugAction> {
    let Some(default_frames) = wait_frames_default else {
        return actions;
    };

    actions
        .into_iter()
        .map(|action| match action {
            DebugAction::WaitFrames { frames: 0 } => DebugAction::WaitFrames { frames: default_frames },
            action => action,
        })
        .collect()
}

fn resolve_target_pos(target: DebugTarget, at: [f32; 2], state: &ViewerDebugState) -> egui::Pos2 {
    match target {
        DebugTarget::Image => {
            let rect = state.image_rect.expect("debug script target 'image' is unavailable");
            rect.min + rect.size() * egui::vec2(at[0], at[1])
        }
    }
}

fn resolve_viewport(viewport: DebugViewport, state: &ViewerDebugState) -> egui::ViewportId {
    match viewport {
        // ROOT corresponds to the main image viewport/window.
        DebugViewport::Root => egui::ViewportId::ROOT,
        DebugViewport::Controls => state.controls_viewport_id,
    }
}

fn ensure_parent_dir(path: &std::path::Path) {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)
            .unwrap_or_else(|err| panic!("failed to create debug artifact directory {}: {err}", parent.display()));
    }
}

fn default_wait_for_image_timeout() -> u64 {
    120
}

fn request_repaint_after_scripted_input(
    ctx: &egui::Context,
    state: &ViewerDebugState,
    source_viewport: egui::ViewportId,
) {
    // Scripted input can mutate state read by another viewport (e.g. controls
    // consuming cursor info updated by image hover), so repaint both.
    ctx.request_repaint_of(source_viewport);
    ctx.request_repaint_of(state.controls_viewport_id);
}

fn is_user_input_event(event: &egui::Event) -> bool {
    matches!(
        event,
        egui::Event::Copy
            | egui::Event::Cut
            | egui::Event::Paste(_)
            | egui::Event::Text(_)
            | egui::Event::Key { .. }
            | egui::Event::PointerMoved(_)
            | egui::Event::MouseMoved(_)
            | egui::Event::PointerButton { .. }
            | egui::Event::PointerGone
            | egui::Event::Zoom(_)
            | egui::Event::Rotate(_)
            | egui::Event::Touch { .. }
            | egui::Event::MouseWheel { .. }
    )
}
