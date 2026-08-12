set dotenv-load := false

default:
    just --list

check:
    cargo check

build:
    cargo build

release:
    cargo build --release

release-small:
    cargo build --profile release-small

fmt:
    cargo fmt

fmt-check:
    cargo fmt --check

clippy:
    cargo clippy --locked --all-targets

run *args:
    cargo run -- {{args}}

run-client *args:
    cargo run -- --client {{args}}
    
run-server *args:
    cargo run -- --server

run-release *args:
    cargo run --release -- {{args}}

test:
    cargo test --locked

smoke:
    cargo run -- --version

clean:
    cargo clean
