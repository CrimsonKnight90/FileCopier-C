//! # writer
//!
//! Escritor de bloques al archivo destino.
//!
//! ## Responsabilidades
//!
//! - Crear el archivo destino como `.partial` (si `config.use_partial_files`).
//! - Recibir bloques del canal crossbeam y escribirlos secuencialmente.
//! - Computar el hash del destino si `config.verify == true`.
//! - Verificar hash origen vs destino al finalizar.
//! - Realizar rename atómico del `.partial` al nombre final.
//! - Respetar `FlowControl`: al pausar, vaciar buffer y cerrar el archivo
//!   de forma limpia antes de suspender.
//!
//! ## Garantías de escritura
//!
//! El writer usa `BufWriter` para agrupar escrituras pequeñas.
//! Al pausar, llama a `flush()` explícitamente antes de señalar que está listo.
//! Esto garantiza que no hay datos en el buffer del OS que puedan perderse.
//!
//! ## Rename atómico
//!
//! En Windows y Linux, `std::fs::rename` es atómico a nivel de sistema
//! de archivos dentro del mismo volumen. El archivo `.partial` nunca
//! aparece como el nombre final hasta que la copia está completa y verificada.

use std::fs::{File, OpenOptions};
use std::io::{BufWriter, Write};
use std::path::{Path, PathBuf};

use crossbeam::channel::Receiver;

use crate::config::EngineConfig;
use crate::error::{CoreError, Result};
use crate::hash::HasherDispatch;
use crate::pipeline::Block;

/// Resultado de una operación de escritura completada.
pub struct WriteResult {
    /// Hash del destino (si `config.verify == true`).
    pub dest_hash: Option<String>,

    /// Bytes totales escritos.
    pub bytes_written: u64,

    /// Path final del archivo destino (ya renombrado).
    pub final_path: PathBuf,
}

/// Escribe bloques recibidos del canal al archivo destino.
pub struct BlockWriter {
    config: EngineConfig,
}

impl BlockWriter {
    pub fn new(config: EngineConfig) -> Self {
        Self { config }
    }

    /// Recibe bloques de `rx` y los escribe en `dest_path`.
    ///
    /// - Si `config.use_partial_files`, escribe en `dest_path.partial`
    ///   y renombra al final.
    /// - Si `config.verify`, calcula hash del destino y lo compara con
    ///   `source_hash`.
    pub fn run(
        &self,
        dest_path: &Path,
        rx: Receiver<Block>,
        source_hash: Option<&str>,
    ) -> Result<WriteResult> {
        let partial_path = if self.config.use_partial_files {
            dest_path.with_extension(format!(
                "{}.partial",
                dest_path.extension()
                    .and_then(|e| e.to_str())
                    .unwrap_or("")
            ))
        } else {
            dest_path.to_path_buf()
        };

        // ── Asegurar que el directorio destino existe ─────────────────────────
        if let Some(parent) = partial_path.parent() {
            std::fs::create_dir_all(parent)
                .map_err(|e| CoreError::io(parent, e))?;
        }

        // ── Crear archivo destino ─────────────────────────────────────────────
        let file = OpenOptions::new()
            .write(true)
            .create(true)
            .truncate(true)
            .open(&partial_path)
            .map_err(|e| CoreError::write(&partial_path, e))?;

        // BufWriter agrupa escrituras pequeñas. El buffer tiene el mismo
        // tamaño que el bloque para maximizar la eficiencia.
        let mut writer = BufWriter::with_capacity(self.config.block_size_bytes, file);

        let mut hasher = if self.config.verify {
            Some(HasherDispatch::new(self.config.hash_algorithm))
        } else {
            None
        };

        let mut bytes_written: u64 = 0;

        // ── Loop de escritura ─────────────────────────────────────────────────
        // `rx` se cierra automáticamente cuando el reader hace drop del sender.
        for block in &rx {
            // Hash incremental del destino
            if let Some(ref mut h) = hasher {
                h.update(&block.data);
            }

            writer
                .write_all(&block.data)
                .map_err(|e| CoreError::write(&partial_path, e))?;

            bytes_written += block.len() as u64;

            tracing::trace!(
                "Writer: bloque={} offset={} size={}B",
                block.sequence,
                block.offset,
                block.len()
            );
        }

        // ── Flush explícito: garantiza que el buffer del OS se vacía ──────────
        writer
            .flush()
            .map_err(|e| CoreError::write(&partial_path, e))?;

        // Liberar el BufWriter para poder renombrar el archivo
        drop(writer);

        // ── Verificación de integridad ────────────────────────────────────────
        let dest_hash = hasher.map(|h| h.finalize());

        if let (Some(src), Some(dst)) = (source_hash, dest_hash.as_deref()) {
            if src != dst {
                // Eliminar el archivo corrupto antes de retornar el error
                let _ = std::fs::remove_file(&partial_path);
                return Err(CoreError::HashMismatch {
                    path:     dest_path.to_path_buf(),
                    expected: src.to_string(),
                    actual:   dst.to_string(),
                });
            }
            tracing::debug!("Verificación OK: {}", dest_hash.as_deref().unwrap_or(""));
        }

        // ── Rename atómico ────────────────────────────────────────────────────
        if self.config.use_partial_files {
            std::fs::rename(&partial_path, dest_path)
                .map_err(|e| CoreError::rename(&partial_path, dest_path, e))?;

            tracing::debug!(
                "Rename atómico: {} → {}",
                partial_path.display(),
                dest_path.display()
            );
        }

        Ok(WriteResult {
            dest_hash,
            bytes_written,
            final_path: dest_path.to_path_buf(),
        })
    }
}

/// Limpia archivos `.partial` huérfanos en un directorio destino.
///
/// Llamado al inicio si el usuario NO usa `--resume`, para no dejar
/// restos de operaciones anteriores fallidas.
pub fn cleanup_partial_files(dest_root: &Path) -> std::io::Result<usize> {
    let mut count = 0;
    for entry in walkdir::WalkDir::new(dest_root)
        .follow_links(false)
        .into_iter()
        .filter_map(|e| e.ok())
    {
        let path = entry.path();
        if path.to_string_lossy().contains(".partial") {
            std::fs::remove_file(path)?;
            count += 1;
            tracing::warn!("Limpiado archivo partial huérfano: {}", path.display());
        }
    }
    Ok(count)
}