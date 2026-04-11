//! # telemetry
//!
//! Métricas diferenciadas en tiempo real para el motor de copia.
//!
//! ## Diseño de telemetría diferenciada
//!
//! El motor dual produce dos tipos de métricas distintas:
//!
//! - **Motor de bloques** → `MB/s` (throughput de bytes)
//! - **Motor de enjambre** → `archivos/s` (IOPS de metadatos)
//!
//! Ambas métricas se consolidan en `CopyProgress`, que es la única
//! estructura que el frontend (CLI o GUI Tauri) necesita observar.
//!
//! ## Thread-safety
//!
//! `TelemetrySink` usa `Arc<Mutex<...>>` internamente para permitir
//! actualización desde múltiples threads del pipeline sin contención excesiva.
//! Las lecturas son baratas (snapshot atómico).

use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::sync::Arc;
use std::time::Instant;

/// Snapshot inmutable del progreso en un instante dado.
/// Esta es la estructura que el frontend consume.
#[derive(Debug, Clone)]
pub struct CopyProgress {
    /// Bytes totales a copiar (suma de todos los archivos).
    pub total_bytes: u64,

    /// Bytes ya copiados hasta este momento.
    pub copied_bytes: u64,

    /// Número total de archivos en la operación.
    pub total_files: usize,

    /// Archivos completados (copia + verificación si aplica).
    pub completed_files: usize,

    /// Archivos que fallaron (no bloqueantes: el motor continúa).
    pub failed_files: usize,

    /// Throughput actual del motor de bloques en bytes/segundo.
    pub throughput_bytes_per_sec: f64,

    /// Tasa actual del motor de enjambre en archivos/segundo.
    pub files_per_sec: f64,

    /// Porcentaje de progreso global [0.0, 100.0].
    pub percent: f64,

    /// Tiempo transcurrido desde el inicio de la operación.
    pub elapsed_secs: f64,

    /// Estimación de tiempo restante en segundos. `None` si no hay datos.
    pub eta_secs: Option<f64>,
}

/// Contador atómico compartido entre todos los threads del motor.
///
/// Usa `AtomicU64` y `AtomicUsize` para actualizaciones lock-free
/// en el hot path. Los snapshots se calculan bajo demanda.
pub struct TelemetrySink {
    // ── Contadores de bytes ───────────────────────────────────────────────────
    total_bytes:    u64,
    copied_bytes:   Arc<AtomicU64>,

    // ── Contadores de archivos ────────────────────────────────────────────────
    total_files:    usize,
    completed_files: Arc<AtomicUsize>,
    failed_files:    Arc<AtomicUsize>,

    // ── Medición de tiempo ────────────────────────────────────────────────────
    start:          Instant,

    // ── Ventana deslizante para throughput ────────────────────────────────────
    // Guardamos el último snapshot para calcular throughput incremental.
    last_snapshot_bytes: Arc<AtomicU64>,
    last_snapshot_time:  Arc<std::sync::Mutex<Instant>>,
}

impl TelemetrySink {
    /// Crea un nuevo sink con los totales conocidos antes de iniciar.
    pub fn new(total_bytes: u64, total_files: usize) -> Self {
        let now = Instant::now();
        Self {
            total_bytes,
            copied_bytes:        Arc::new(AtomicU64::new(0)),
            total_files,
            completed_files:     Arc::new(AtomicUsize::new(0)),
            failed_files:        Arc::new(AtomicUsize::new(0)),
            start:               now,
            last_snapshot_bytes: Arc::new(AtomicU64::new(0)),
            last_snapshot_time:  Arc::new(std::sync::Mutex::new(now)),
        }
    }

    /// Registra que se copiaron `bytes` adicionales.
    ///
    /// Llamado desde el writer thread en el hot path. Debe ser lo más
    /// barato posible → `Relaxed` ordering es suficiente aquí porque
    /// la consistencia entre threads se garantiza cuando se llama a `snapshot()`.
    #[inline]
    pub fn add_bytes(&self, bytes: u64) {
        self.copied_bytes.fetch_add(bytes, Ordering::Relaxed);
    }

    /// Registra que un archivo fue completado exitosamente.
    #[inline]
    pub fn complete_file(&self) {
        self.completed_files.fetch_add(1, Ordering::Relaxed);
    }

    /// Registra que un archivo falló.
    #[inline]
    pub fn fail_file(&self) {
        self.failed_files.fetch_add(1, Ordering::Relaxed);
    }

    /// Retorna un handle clonado para enviar a otro thread.
    ///
    /// Todos los handles comparten los mismos contadores atómicos.
    pub fn handle(&self) -> TelemetryHandle {
        TelemetryHandle {
            copied_bytes:    Arc::clone(&self.copied_bytes),
            completed_files: Arc::clone(&self.completed_files),
            failed_files:    Arc::clone(&self.failed_files),
        }
    }

    /// Calcula un snapshot inmutable del progreso actual.
    ///
    /// Este método es más caro (calcula throughput incremental con lock),
    /// pero se llama solo desde el thread de UI (~1 Hz o menos).
    pub fn snapshot(&self) -> CopyProgress {
        // Leer contadores con ordering más fuerte para snapshot consistente
        let copied   = self.copied_bytes.load(Ordering::Acquire);
        let completed = self.completed_files.load(Ordering::Acquire);
        let failed    = self.failed_files.load(Ordering::Acquire);

        let elapsed = self.start.elapsed();
        let elapsed_secs = elapsed.as_secs_f64();

        // Throughput global (bytes/s desde el inicio)
        let global_bps = if elapsed_secs > 0.0 {
            copied as f64 / elapsed_secs
        } else {
            0.0
        };

        // Throughput incremental (ventana deslizante)
        let throughput_bytes_per_sec = {
            let mut last_time = self.last_snapshot_time.lock().unwrap();
            let last_bytes    = self.last_snapshot_bytes.load(Ordering::Relaxed);
            let delta_bytes   = copied.saturating_sub(last_bytes);
            let delta_secs    = last_time.elapsed().as_secs_f64();

            let rate = if delta_secs > 0.01 {
                delta_bytes as f64 / delta_secs
            } else {
                global_bps
            };

            // Actualizar ventana
            self.last_snapshot_bytes.store(copied, Ordering::Relaxed);
            *last_time = Instant::now();

            rate
        };

        // Files/s: archivos completados / tiempo transcurrido
        let files_per_sec = if elapsed_secs > 0.0 {
            completed as f64 / elapsed_secs
        } else {
            0.0
        };

        // Porcentaje
        let percent = if self.total_bytes > 0 {
            (copied as f64 / self.total_bytes as f64) * 100.0
        } else {
            100.0
        };

        // ETA
        let eta_secs = if throughput_bytes_per_sec > 0.0 && copied < self.total_bytes {
            let remaining = self.total_bytes - copied;
            Some(remaining as f64 / throughput_bytes_per_sec)
        } else {
            None
        };

        CopyProgress {
            total_bytes:             self.total_bytes,
            copied_bytes:            copied,
            total_files:             self.total_files,
            completed_files:         completed,
            failed_files:            failed,
            throughput_bytes_per_sec,
            files_per_sec,
            percent,
            elapsed_secs,
            eta_secs,
        }
    }
}

/// Handle ligero que puede clonarse y enviarse a threads del motor.
///
/// Solo expone las operaciones de escritura (add_bytes, complete_file, fail_file).
/// El thread de UI mantiene el `TelemetrySink` original para leer snapshots.
#[derive(Clone)]
pub struct TelemetryHandle {
    copied_bytes:    Arc<AtomicU64>,
    completed_files: Arc<AtomicUsize>,
    failed_files:    Arc<AtomicUsize>,
}

impl TelemetryHandle {
    #[inline]
    pub fn add_bytes(&self, bytes: u64) {
        self.copied_bytes.fetch_add(bytes, Ordering::Relaxed);
    }

    #[inline]
    pub fn complete_file(&self) {
        self.completed_files.fetch_add(1, Ordering::Relaxed);
    }

    #[inline]
    pub fn fail_file(&self) {
        self.failed_files.fetch_add(1, Ordering::Relaxed);
    }
}

impl CopyProgress {
    /// Formatea `throughput_bytes_per_sec` en formato humano (KB/s, MB/s, GB/s).
    pub fn throughput_human(&self) -> String {
        format_bytes_per_sec(self.throughput_bytes_per_sec)
    }

    /// Formatea el ETA en formato humano (e.g. "2m 30s").
    pub fn eta_human(&self) -> String {
        match self.eta_secs {
            None => "—".into(),
            Some(s) => format_duration(s),
        }
    }
}

fn format_bytes_per_sec(bps: f64) -> String {
    const KB: f64 = 1024.0;
    const MB: f64 = KB * 1024.0;
    const GB: f64 = MB * 1024.0;

    if bps >= GB {
        format!("{:.2} GB/s", bps / GB)
    } else if bps >= MB {
        format!("{:.1} MB/s", bps / MB)
    } else if bps >= KB {
        format!("{:.0} KB/s", bps / KB)
    } else {
        format!("{:.0} B/s", bps)
    }
}

fn format_duration(secs: f64) -> String {
    let s = secs as u64;
    if s < 60 {
        format!("{s}s")
    } else if s < 3600 {
        format!("{}m {}s", s / 60, s % 60)
    } else {
        format!("{}h {}m", s / 3600, (s % 3600) / 60)
    }
}