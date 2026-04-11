//! # engine
//!
//! Motor dual de copia: bloques grandes y enjambre asíncrono.
//!
//! ## Orquestador
//!
//! `Orchestrator` es el punto de entrada público. Recibe la lista de archivos
//! a copiar (ya resuelta por el triage) y despacha cada archivo al motor
//! correcto según su tamaño y la configuración.
//!
//! ## Separación de responsabilidades
//!
//! - `orchestrator.rs` → Triage, coordinación, checkpoint management.
//! - `block.rs`        → Motor de bloques (archivos pesados, crossbeam + rayon).
//! - `swarm.rs`        → Motor de enjambre (archivos pequeños, tokio).

pub mod block;
pub mod orchestrator;
pub mod swarm;

pub use orchestrator::Orchestrator;