//! # lib-core
//!
//! Motor principal de FileCopier-Rust.
//!
//! ## Módulos
//!
//! - [`hash`]       — Trait `ChecksumAlgorithm` e implementaciones (blake3, xxhash, sha2).
//! - [`pipeline`]   — Componentes del pipeline: lector, escritor, buffer.
//! - [`engine`]     — Motor dual: bloques grandes y enjambre asíncrono.
//! - [`checkpoint`] — Persistencia de estado para pausa/reanudar.
//! - [`telemetry`]  — Métricas diferenciadas en tiempo real.
//! - [`error`]      — Tipo de error unificado del crate.
//! - [`config`]     — Configuración centralizada del motor.

pub mod checkpoint;
pub mod config;
pub mod engine;
pub mod error;
pub mod hash;
pub mod pipeline;
pub mod telemetry;

// Re-exportaciones convenientes para usuarios del crate
pub use config::EngineConfig;
pub use error::{CoreError, Result};