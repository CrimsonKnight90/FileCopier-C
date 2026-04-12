//! # orchestrator
//!
//! Punto de entrada del motor. Escanea el árbol de archivos, clasifica
//! cada archivo (bloque vs enjambre), gestiona el checkpoint y coordina
//! ambos motores.
//!
//! ## Flujo de ejecución
//!
//! ```text
//! run()
//!  ├─ 1. Escanear origen con walkdir
//!  ├─ 2. Calcular totales (bytes, archivos)
//!  ├─ 3. Cargar checkpoint si --resume
//!  ├─ 4. Filtrar archivos ya completados
//!  ├─ 5. Clasificar: large_files vs small_files
//!  ├─ 6. Lanzar motor de enjambre para small_files (tokio)
//!  ├─ 7. Procesar large_files con motor de bloques (secuencial)
//!  ├─ 8. Guardar checkpoint en cada archivo completado
//!  └─ 9. Eliminar checkpoint al finalizar
//! ```

use std::path::{Path, PathBuf};
use std::sync::Arc;

use walkdir::WalkDir;

use crate::checkpoint::{CheckpointState, FlowControl};
use crate::config::EngineConfig;
use crate::engine::block::BlockEngine;
use crate::engine::swarm::SwarmEngine;
use crate::error::Result;
use crate::telemetry::{CopyProgress, TelemetrySink};

/// Resultado final de la operación de copia.
#[derive(Debug)]
pub struct CopyResult {
    pub completed_files: usize,
    pub failed_files:    usize,
    pub total_bytes:     u64,
    pub copied_bytes:    u64,
    pub elapsed_secs:    f64,
}

/// Archivo candidato a copiar, resultado del escaneo.
#[derive(Debug, Clone)]
pub struct FileEntry {
    /// Path absoluto del archivo origen.
    pub source: PathBuf,
    /// Path absoluto del archivo destino (calculado preservando estructura).
    pub dest:   PathBuf,
    /// Tamaño en bytes del archivo origen.
    pub size:   u64,
    /// Path relativo desde la raíz del origen (clave del checkpoint).
    pub relative: PathBuf,
}

/// Callback para recibir snapshots de progreso desde el caller (CLI/GUI).
pub type ProgressCallback = Box<dyn Fn(CopyProgress) + Send + Sync>;

/// Coordina el escaneo, clasificación y ejecución de la copia.
pub struct Orchestrator {
    config: Arc<EngineConfig>,
    flow:   FlowControl,
}

impl Orchestrator {
    pub fn new(config: EngineConfig, flow: FlowControl) -> Self {
        Self {
            config: Arc::new(config),
            flow,
        }
    }

    /// Ejecuta la copia completa de `source_root` a `dest_root`.
    ///
    /// `on_progress` se llama aproximadamente cada segundo con el estado actual.
    pub fn run(
        &self,
        source_root: &Path,
        dest_root:   &Path,
        on_progress: Option<ProgressCallback>,
    ) -> Result<CopyResult> {
        let start = std::time::Instant::now();

        // ── 1. Escanear origen ────────────────────────────────────────────────
        tracing::info!("Escaneando origen: {}", source_root.display());
        let all_files = self.scan_files(source_root, dest_root)?;
        tracing::info!("Archivos encontrados: {}", all_files.len());

        // ── 2. Calcular totales ───────────────────────────────────────────────
        let total_bytes: u64 = all_files.iter().map(|f| f.size).sum();
        let total_files = all_files.len();

        // ── 3. Cargar checkpoint si --resume ──────────────────────────────────
        let job_id = generate_job_id(source_root, dest_root);
        let checkpoint_path = CheckpointState::default_path(dest_root, &job_id);

        let mut checkpoint = if self.config.resume && checkpoint_path.exists() {
            tracing::info!("Cargando checkpoint: {}", checkpoint_path.display());
            CheckpointState::load(&checkpoint_path)?
        } else {
            CheckpointState::new(
                &job_id,
                source_root.to_path_buf(),
                dest_root.to_path_buf(),
                all_files.iter().map(|f| f.relative.clone()),
            )
        };

        // ── 4. Filtrar ya completados ─────────────────────────────────────────
        let pending: Vec<FileEntry> = all_files
            .into_iter()
            .filter(|f| !checkpoint.completed.contains_key(&f.relative))
            .collect();

        tracing::info!(
            "Pendientes: {}/{} archivos ({} ya completados)",
            pending.len(),
            total_files,
            total_files - pending.len()
        );

        // ── 5. Clasificar por tamaño ──────────────────────────────────────────
        let (large_files, small_files): (Vec<FileEntry>, Vec<FileEntry>) = pending
            .into_iter()
            .partition(|f| self.config.is_large_file(f.size));

        tracing::info!(
            "Triage: {} archivos grandes (>= {}MB), {} archivos pequeños",
            large_files.len(),
            self.config.triage_threshold_bytes / 1024 / 1024,
            small_files.len(),
        );

        // ── 6. Telemetría ─────────────────────────────────────────────────────
        let telemetry = Arc::new(TelemetrySink::new(total_bytes, total_files));

        // Thread de reporte de progreso (~1 Hz)
        let telemetry_reporter = if on_progress.is_some() {
            let t = Arc::clone(&telemetry);
            let cb = on_progress.unwrap();
            let flow = self.flow.clone();
            Some(std::thread::spawn(move || {
                while !flow.is_cancelled() {
                    let progress = t.snapshot();
                    let done = progress.completed_files + progress.failed_files
                        >= progress.total_files;
                    cb(progress);
                    if done { break; }
                    std::thread::sleep(std::time::Duration::from_millis(500));
                }
            }))
        } else {
            None
        };

        // ── 7. Motor de enjambre (archivos pequeños) ──────────────────────────
        // Se lanza primero para comenzar a saturar IOPS mientras el motor
        // de bloques procesa los archivos grandes secuencialmente.
        let swarm_results = if !small_files.is_empty() {
            let swarm = SwarmEngine::new(
                Arc::clone(&self.config),
                self.flow.clone(),
                telemetry.handle(),
            );
            // El enjambre es async: lo ejecutamos en un runtime tokio.
            let rt = tokio::runtime::Builder::new_multi_thread()
                .worker_threads(2)
                .enable_io()
                .build()
                .expect("No se pudo crear el runtime tokio");

            let checkpoint_path_clone = checkpoint_path.clone();
            rt.block_on(async {
                swarm.run(small_files, &checkpoint_path_clone).await
            })?
        } else {
            vec![]
        };

        // Actualizar checkpoint con resultados del enjambre.
        // IMPORTANTE: el enjambre ya actualizó la telemetría internamente
        // (add_bytes + complete_file / fail_file en copy_small_file).
        // El orquestador solo actualiza el checkpoint, NO la telemetría.
        for (relative, result) in swarm_results {
            match result {
                Ok(hash) => checkpoint.mark_completed(relative, hash),
                Err(e)   => {
                    tracing::warn!("Enjambre: falló {}: {e}", relative.display());
                    checkpoint.mark_failed(relative, e.to_string());
                }
            }
        }

        // Persistir checkpoint tras completar todos los archivos pequeños.
        if !checkpoint.completed.is_empty() || !checkpoint.failed.is_empty() {
            if let Err(e) = checkpoint.save(&checkpoint_path) {
                tracing::warn!("No se pudo guardar checkpoint post-enjambre: {e}");
            }
        }

        // ── 8. Motor de bloques (archivos grandes) ────────────────────────────
        let block_engine = BlockEngine::new(
            Arc::clone(&self.config),
            self.flow.clone(),
            telemetry.handle(),
        );

        for entry in large_files {
            tracing::info!(
                "Copiando (bloques): {} ({:.1} MB)",
                entry.source.display(),
                entry.size as f64 / 1024.0 / 1024.0
            );

            match block_engine.copy_file(&entry.source, &entry.dest) {
                Ok(hash) => {
                    // El motor de bloques NO actualiza telemetría internamente
                    // (el reader solo cuenta bytes leídos). El orquestador es
                    // responsable de marcar el archivo como completado.
                    telemetry.complete_file();
                    checkpoint.mark_completed(entry.relative.clone(), hash);
                }
                Err(e) => {
                    telemetry.fail_file();
                    tracing::error!(
                        "Error copiando {}: {}",
                        entry.source.display(),
                        e
                    );
                    checkpoint.mark_failed(entry.relative.clone(), e.to_string());
                }
            }

            // Guardar checkpoint tras cada archivo grande completado
            if let Err(e) = checkpoint.save(&checkpoint_path) {
                tracing::warn!("No se pudo guardar checkpoint: {e}");
            }
        }

        // ── 9. Cleanup ────────────────────────────────────────────────────────
        if let Some(handle) = telemetry_reporter {
            let _ = handle.join();
        }

        // Eliminar checkpoint si todo completó exitosamente
        if checkpoint.failed.is_empty() && checkpoint.is_complete() {
            CheckpointState::delete(&checkpoint_path)?;
        } else if !checkpoint.is_complete() || !checkpoint.failed.is_empty() {
            // Guardar estado final con fallos para inspección
            let _ = checkpoint.save(&checkpoint_path);
        }

        let snapshot = telemetry.snapshot();

        Ok(CopyResult {
            completed_files: snapshot.completed_files,
            failed_files:    snapshot.failed_files,
            total_bytes:     snapshot.total_bytes,
            copied_bytes:    snapshot.copied_bytes,
            elapsed_secs:    snapshot.elapsed_secs,
        })
    }

    /// Escanea el árbol de archivos del origen y calcula los paths de destino.
    fn scan_files(&self, source_root: &Path, dest_root: &Path) -> Result<Vec<FileEntry>> {
        let mut files = Vec::new();

        for entry in WalkDir::new(source_root)
            .follow_links(false)
            .into_iter()
            .filter_map(|e| {
                match e {
                    Ok(e) if e.file_type().is_file() => Some(e),
                    Ok(_)  => None, // Directorios: los crea el writer
                    Err(e) => {
                        tracing::warn!("Error escaneando entrada: {e}");
                        None
                    }
                }
            })
        {
            let source = entry.path().to_path_buf();
            let size = entry.metadata()
                .map(|m| m.len())
                .unwrap_or(0);

            // Path relativo desde la raíz del origen
            let relative = source
                .strip_prefix(source_root)
                .unwrap_or(&source)
                .to_path_buf();

            let dest = dest_root.join(&relative);

            files.push(FileEntry { source, dest, size, relative });
        }

        Ok(files)
    }
}

/// Genera un ID de job reproducible basado en origen y destino.
/// Mismo origen + destino → mismo job_id → checkpoint reutilizable.
fn generate_job_id(source: &Path, dest: &Path) -> String {
    use std::hash::{Hash, Hasher};
    use std::collections::hash_map::DefaultHasher;

    let mut h = DefaultHasher::new();
    source.hash(&mut h);
    dest.hash(&mut h);
    format!("{:016x}", h.finish())
}