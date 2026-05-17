#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AppAction {
    NextImage,
    PreviousImage,
    Quit,
}
