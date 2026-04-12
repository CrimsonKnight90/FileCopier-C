//! # writer
//!
//! Escritor de bloques al archivo destino.
//!
//! ## Responsabilidades
//!
//! - Crear el archivo destino como `.partial` (si `config.use_partial_files`).
//! - Recibir bloques del canal crossbeam y escribirlos secuencialmente.
//! - Computar el hash del destino si `config.verify == true`.
//! - Realizar rename atómico del `.partial` al nombre final.
//!
//! ## Garantías de escritura
//!
//! El writer usa `BufWriter` para agrupar escrituras pequeñas.
//! Al finalizar llama a `flush()` explícitamente antes del rename.
//! Esto garantiza que no hay datos en el buffer del OS que puedan perderse.
//!
//! ## Convención de nombre `.partial`
//!
//! Se añade `.partial` como **sufijo al nombre completo**, NO se reemplaza
//! la extensión existente.
//!
//!   foto.jpg  → foto.jpg.partial   ✓
//!   Makefile  → Makefile.partial   ✓
//!   foto.jpg  → foto.partial       ✗  (with_extension reemplaza)
//!
//! ## Rename atómico
//!
//! En Windows y Linux, `std::fs::rename` es atómico dentro del mismo
//! volumen. El archivo `.partial` nunca aparece con el nombre final
//! hasta que la copia está completa y verificada.

use std::fs::OpenOptions;
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

    /// Path final del archivo destino (ya renombrado desde `.partial`).
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
    /// `source_hash` es el hash del origen calculado por el `BlockReader`.
    /// Si se pasa `Some`, se verifica contra el hash calculado del destino.
    pub fn run(
        &self,
        dest_path:   &Path,
        rx:          Receiver<Block>,
        source_hash: Option<&str>,
    ) -> Result<WriteResult> {
        // ── Calcular path del archivo .partial ────────────────────────────
        let partial_path = if self.config.use_partial_files {
            let file_name = dest_path
                .file_name()
                .expect("dest_path debe tener nombre de archivo");
            let partial_name = format!("{}.partial", file_name.to_string_lossy());
            dest_path
                .parent()
                .unwrap_or(Path::new("."))
                .join(partial_name)
        } else {
            dest_path.to_path_buf()
        };

        // ── Asegurar que el directorio destino existe ─────────────────────
        if let Some(parent) = partial_path.parent() {
            std::fs::create_dir_all(parent)
                .map_err(|e| CoreError::io(parent, e))?;
        }

        // ── Crear archivo destino ─────────────────────────────────────────
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

        // ── Loop de escritura ─────────────────────────────────────────────
        // `rx` se cierra automáticamente cuando el reader hace drop del Sender.
        for block in &rx {
            if let Some(ref mut h) = hasher {
                h.update(&block.data);
            }

            writer
                .write_all(&block.data)
                .map_err(|e| CoreError::write(&partial_path, e))?;

            bytes_written += block.len() as u64;

            tracing::trace!(
                "Writer: seq={} offset={} size={}B",
                block.sequence, block.offset, block.len()
            );
        }

        // ── Flush explícito ───────────────────────────────────────────────
        writer
            .flush()
            .map_err(|e| CoreError::write(&partial_path, e))?;

        // Liberar el BufWriter antes del rename
        drop(writer);

        // ── Verificación de integridad ────────────────────────────────────
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
            tracing::debug!("Verificación OK: {}", dst);
        }

        // ── Rename atómico ────────────────────────────────────────────────
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
        if path
            .file_name()
            .and_then(|n| n.to_str())
            .map(|n| n.ends_with(".partial"))
            .unwrap_or(false)
        {
            std::fs::remove_file(path)?;
            count += 1;
            tracing::warn!("Limpiado archivo partial huérfano: {}", path.display());
        }
    }
    Ok(count)
}