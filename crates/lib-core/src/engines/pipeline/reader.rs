//! # reader
//!
//! Lector de bloques del archivo origen.
//!
//! ## Responsabilidades
//!
//! - Abrir el archivo origen con buffers optimizados para lectura secuencial.
//! - Leer en bloques de tamaño fijo (`config.block_size_bytes`).
//! - Enviar cada bloque al canal crossbeam (bloqueante — backpressure natural).
//! - Computar el hash del origen si `config.verify == true`.
//! - Respetar `FlowControl`: pausar limpiamente entre bloques.
//!
//! ## Thread model
//!
//! `BlockReader` corre en un thread OS dedicado (no tokio) porque usa
//! I/O síncrono. Vive en el thread del pipeline, no en el runtime async.

use std::fs::File;
use std::io::{BufReader, Read};
use std::path::Path;

use crossbeam::channel::Sender;

use crate::checkpoint::FlowControl;
use crate::config::EngineConfig;
use crate::error::{CoreError, Result};
use crate::hash::HasherDispatch;
use crate::pipeline::Block;
use crate::telemetry::TelemetryHandle;

/// Lee el archivo origen y envía bloques al canal del pipeline.
pub struct BlockReader {
    config:    EngineConfig,
    flow:      FlowControl,
    telemetry: TelemetryHandle,
}

impl BlockReader {
    pub fn new(
        config: EngineConfig,
        flow: FlowControl,
        telemetry: TelemetryHandle,
    ) -> Self {
        Self { config, flow, telemetry }
    }

    /// Lee `source_path` en bloques y los envía por `tx`.
    ///
    /// Retorna el hash del origen si `config.verify == true`.
    /// El canal se cierra automáticamente cuando este método retorna (drop de `tx`).
    pub fn run(
        &self,
        source_path: &Path,
        tx: Sender<Block>,
    ) -> Result<Option<String>> {
        let file = File::open(source_path)
            .map_err(|e| CoreError::read(source_path, e))?;

        // BufReader con el mismo tamaño que el bloque: la syscall de read()
        // ya pide exactamente un bloque, así que el buffer interno de BufReader
        // no añade overhead pero sí evita llamadas parciales del OS.
        let mut reader = BufReader::with_capacity(self.config.block_size_bytes, file);

        let mut hasher = if self.config.verify {
            Some(HasherDispatch::new(self.config.hash_algorithm))
        } else {
            None
        };

        let mut offset: u64 = 0;
        let mut sequence: u64 = 0;

        loop {
            // ── Comprobar pausa/cancelación entre bloques ─────────────────────
            // No interrumpimos a mitad de bloque: terminamos el read actual
            // y luego comprobamos. Garantiza que el escritor reciba bloques completos.
            match self.flow.check() {
                Ok(()) => {},
                Err(CoreError::Paused) => {
                    tracing::debug!("Reader: pausa detectada en bloque {sequence}");
                    // Esperar hasta reanudar o cancelar
                    self.flow.wait_for_resume()?;
                    tracing::debug!("Reader: reanudado en bloque {sequence}");
                },
                Err(e) => return Err(e),
            }

            // ── Leer siguiente bloque ─────────────────────────────────────────
            let mut buf = vec![0u8; self.config.block_size_bytes];

            // `read` puede retornar menos bytes que el buffer (fin de archivo,
            // fragmentación del OS). Usamos `read` en lugar de `read_exact`
            // para manejar el último bloque correctamente.
            let n = reader
                .read(&mut buf)
                .map_err(|e| CoreError::read(source_path, e))?;

            if n == 0 {
                // EOF: fin del archivo. El canal se cierra al hacer drop del tx.
                tracing::debug!(
                    "Reader: EOF en offset={offset}, bloques enviados={sequence}"
                );
                break;
            }

            // Truncar al tamaño real leído (importante para el último bloque)
            buf.truncate(n);

            // ── Hash incremental del origen ───────────────────────────────────
            if let Some(ref mut h) = hasher {
                h.update(&buf);
            }

            // ── Telemetría: bytes leídos ──────────────────────────────────────
            // Nota: aquí registramos bytes LEÍDOS, no escritos.
            // El escritor también actualizará para bytes escritos.
            // Para MVP, el reader es quien lleva la cuenta de progreso.
            self.telemetry.add_bytes(n as u64);

            let block = Block::new(buf, offset, sequence);
            offset   += n as u64;
            sequence += 1;

            // ── Enviar al canal (bloqueante = backpressure) ───────────────────
            // Si el canal está lleno, esta llamada bloquea el thread del reader
            // hasta que el writer consuma un slot. Esto es el backpressure.
            if tx.send(block).is_err() {
                // El receiver (writer) se cayó: error fatal del pipeline.
                return Err(CoreError::PipelineDisconnected);
            }
        }

        // Finalizar hash y retornar
        let hash = hasher.map(|h| h.finalize());
        Ok(hash)
    }
}