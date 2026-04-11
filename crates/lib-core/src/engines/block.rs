//! # block
//!
//! Motor de bloques para archivos grandes (>= umbral de triage).
//!
//! ## Pipeline
//!
//! ```text
//! ┌──────────────┐  crossbeam  ┌──────────────┐
//! │  BlockReader │ ──────────► │  BlockWriter │
//! │  (thread A)  │  backpress. │  (thread B)  │
//! └──────────────┘             └──────────────┘
//! ```
//!
//! ## Concurrencia
//!
//! Dos threads OS (no tokio):
//! - Thread A: `BlockReader` — lee del origen.
//! - Thread B: `BlockWriter` — escribe al destino.
//!
//! El canal crossbeam hace de cola con backpressure natural.
//! Si el destino es más lento que el origen, el reader se bloquea.
//!
//! ## Hashing
//!
//! Ambos threads calculan su propio hash incremental.
//! Al finalizar, el orquestador compara origen vs destino.
//! Si no coinciden → `CoreError::HashMismatch`.

use std::path::Path;
use std::sync::Arc;
use std::thread;

use crate::checkpoint::FlowControl;
use crate::config::EngineConfig;
use crate::error::{CoreError, Result};
use crate::pipeline::{BlockReader, BlockWriter};
use crate::telemetry::TelemetryHandle;

/// Motor de copia para archivos grandes.
pub struct BlockEngine {
    config:    Arc<EngineConfig>,
    flow:      FlowControl,
    telemetry: TelemetryHandle,
}

impl BlockEngine {
    pub fn new(
        config:    Arc<EngineConfig>,
        flow:      FlowControl,
        telemetry: TelemetryHandle,
    ) -> Self {
        Self { config, flow, telemetry }
    }

    /// Copia un archivo grande usando el pipeline de bloques.
    ///
    /// Retorna `Some(hash)` si `config.verify == true`, `None` en caso contrario.
    pub fn copy_file(&self, source: &Path, dest: &Path) -> Result<Option<String>> {
        tracing::debug!(
            "BlockEngine: iniciando copia\n  origen:  {}\n  destino: {}",
            source.display(),
            dest.display()
        );

        // ── Canal crossbeam con backpressure ──────────────────────────────────
        // Capacidad fija: máx `channel_capacity` bloques en vuelo.
        // RAM máxima = channel_capacity × block_size_bytes (validado en config).
        let (tx, rx) = crossbeam::channel::bounded(self.config.channel_capacity);

        // ── Clonar para mover a los threads ───────────────────────────────────
        let config_reader = (*self.config).clone();
        let config_writer = (*self.config).clone();
        let flow_reader   = self.flow.clone();
        let telemetry_r   = self.telemetry.clone();
        let source_path   = source.to_path_buf();
        let dest_path     = dest.to_path_buf();

        // ── Thread A: Reader ──────────────────────────────────────────────────
        let reader_handle = thread::Builder::new()
            .name(format!("block-reader:{}", source.file_name()
                .and_then(|n| n.to_str())
                .unwrap_or("?")))
            .spawn(move || {
                let reader = BlockReader::new(config_reader, flow_reader, telemetry_r);
                reader.run(&source_path, tx)
            })
            .map_err(|e| CoreError::io(source, std::io::Error::new(
                std::io::ErrorKind::Other,
                format!("No se pudo crear thread reader: {e}")
            )))?;

        // ── Thread B: Writer (en el thread actual) ────────────────────────────
        // No creamos un tercer thread: el thread que llama a copy_file()
        // actúa como writer. Esto reduce el overhead de context-switch.
        let writer = BlockWriter::new(config_writer);
        let write_result = writer.run(&dest_path, rx, None /* hash se compara abajo */);

        // ── Recoger resultado del reader ──────────────────────────────────────
        let source_hash = reader_handle
            .join()
            .map_err(|_| CoreError::PipelineDisconnected)??;

        // ── Propagar error del writer (si lo hay) ─────────────────────────────
        let write_result = write_result?;

        // ── Verificación cruzada de hashes ────────────────────────────────────
        if self.config.verify {
            match (&source_hash, &write_result.dest_hash) {
                (Some(src), Some(dst)) if src != dst => {
                    return Err(CoreError::HashMismatch {
                        path:     dest.to_path_buf(),
                        expected: src.clone(),
                        actual:   dst.clone(),
                    });
                }
                _ => {}
            }

            if let Some(ref hash) = source_hash {
                tracing::info!(
                    "Verificación OK [{}]: {}",
                    self.config.hash_algorithm,
                    hash
                );
            }
        }

        tracing::debug!(
            "BlockEngine: completado — {:.1} MB escritos",
            write_result.bytes_written as f64 / 1024.0 / 1024.0
        );

        Ok(source_hash)
    }
}