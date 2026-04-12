//! # swarm
//!
//! Motor de enjambre para archivos pequeños (< umbral de triage).
//!
//! ## Estrategia
//!
//! Lanza hasta `config.swarm_concurrency` tareas tokio en paralelo.
//! El `Semaphore` limita las tareas activas para no saturar:
//! - File descriptors del OS.
//! - Cabezal del HDD si el destino es disco mecánico.
//!
//! ## Por qué tokio y no rayon
//!
//! Los archivos pequeños tienen latencia de metadatos dominante (open,
//! stat, create, rename). Son operaciones I/O-bound, no CPU-bound.
//! tokio maneja cientos de tareas I/O con muy pocos threads OS,
//! mientras que rayon crearía un thread por tarea (ineficiente para I/O).
//!
//! ## Hashing en el enjambre
//!
//! Para archivos pequeños, el hash se calcula sobre el contenido
//! completo en memoria (single-pass). No hay pipeline reader/writer:
//! todo ocurre en la misma tarea, más eficiente para archivos < 16 MB.

use std::path::Path;
use std::sync::Arc;

use tokio::sync::Semaphore;

use crate::checkpoint::FlowControl;
use crate::config::EngineConfig;
use crate::error::{CoreError, Result};
use crate::hash::HasherDispatch;
use crate::telemetry::TelemetryHandle;

use super::orchestrator::FileEntry;

/// Resultado de copiar un archivo en el enjambre.
/// `Ok(Some(hash))` si verify, `Ok(None)` si no, `Err(...)` si falló.
type SwarmFileResult = (std::path::PathBuf, std::result::Result<Option<String>, CoreError>);

/// Motor de enjambre para archivos pequeños.
pub struct SwarmEngine {
    config:    Arc<EngineConfig>,
    flow:      FlowControl,
    telemetry: TelemetryHandle,
}

impl SwarmEngine {
    pub fn new(
        config:    Arc<EngineConfig>,
        flow:      FlowControl,
        telemetry: TelemetryHandle,
    ) -> Self {
        Self { config, flow, telemetry }
    }

    /// Copia todos los archivos de `files` en paralelo.
    ///
    /// Retorna un `Vec` con el resultado de cada archivo (incluyendo fallos).
    /// El enjambre **nunca aborta** por el fallo de un archivo individual.
    pub async fn run(
        &self,
        files:            Vec<FileEntry>,
        _checkpoint_path: &Path, // Reservado para checkpointing del enjambre (Fase 2)
    ) -> Result<Vec<SwarmFileResult>> {
        if files.is_empty() {
            return Ok(vec![]);
        }

        let semaphore = Arc::new(Semaphore::new(self.config.swarm_concurrency));
        let mut handles = Vec::with_capacity(files.len());

        for entry in files {
            // Comprobar cancelación antes de lanzar cada tarea
            if self.flow.is_cancelled() {
                break;
            }

            let sem       = Arc::clone(&semaphore);
            let config    = Arc::clone(&self.config);
            let telemetry = self.telemetry.clone();
            let flow      = self.flow.clone();

            let handle = tokio::spawn(async move {
                // Adquirir slot del semáforo (backpressure del enjambre)
                let _permit = sem
                    .acquire()
                    .await
                    .expect("Semáforo cerrado inesperadamente");

                if flow.is_cancelled() || flow.is_paused() {
                    return (entry.relative, Err(CoreError::Paused));
                }

                let result = copy_small_file(&entry, &config, &telemetry).await;
                (entry.relative, result)
            });

            handles.push(handle);
        }

        // Recoger todos los resultados
        let mut results = Vec::with_capacity(handles.len());
        for handle in handles {
            match handle.await {
                Ok(result) => results.push(result),
                Err(e) => {
                    tracing::error!("Tarea del enjambre entró en pánico: {e}");
                }
            }
        }

        Ok(results)
    }
}

/// Copia un archivo pequeño de forma async (single-pass: leer todo → escribir).
///
/// Para archivos < 16 MB, leer todo en memoria y luego escribir es más
/// eficiente que el pipeline reader/writer porque evita el overhead del
/// canal crossbeam para datos que caben en unas pocas KB/MB.
async fn copy_small_file(
    entry:     &FileEntry,
    config:    &EngineConfig,
    telemetry: &TelemetryHandle,
) -> std::result::Result<Option<String>, CoreError> {
    // Leer el archivo completo en memoria
    let data = tokio::fs::read(&entry.source)
        .await
        .map_err(|e| CoreError::read(&entry.source, e))?;

    let size = data.len() as u64;

    // Hash single-pass (los datos ya están en memoria, no hay overhead de re-lectura)
    let hash = if config.verify {
        let mut hasher = HasherDispatch::new(config.hash_algorithm);
        hasher.update(&data);
        Some(hasher.finalize())
    } else {
        None
    };

    // Asegurar que el directorio destino existe
    if let Some(parent) = entry.dest.parent() {
        tokio::fs::create_dir_all(parent)
            .await
            .map_err(|e| CoreError::io(parent, e))?;
    }

    // Construir path .partial (sufijo al nombre completo, NO reemplazamos extensión)
    let partial_dest = if config.use_partial_files {
        let file_name = entry.dest
            .file_name()
            .expect("dest debe tener nombre de archivo");
        let partial_name = format!("{}.partial", file_name.to_string_lossy());
        entry.dest
            .parent()
            .unwrap_or(std::path::Path::new("."))
            .join(partial_name)
    } else {
        entry.dest.clone()
    };

    // Escribir
    tokio::fs::write(&partial_dest, &data)
        .await
        .map_err(|e| CoreError::write(&partial_dest, e))?;

    // Rename atómico
    if config.use_partial_files {
        tokio::fs::rename(&partial_dest, &entry.dest)
            .await
            .map_err(|e| CoreError::rename(&partial_dest, &entry.dest, e))?;
    }

    // Telemetría
    telemetry.add_bytes(size);
    telemetry.complete_file();

    tracing::trace!(
        "Enjambre: copiado {} ({} bytes)",
        entry.source.display(),
        size
    );

    Ok(hash)
}