//! # filecopier CLI
//!
//! Interfaz de línea de comandos para FileCopier-Rust.
//!
//! ## Uso básico
//!
//! ```bash
//! # Copia simple
//! filecopier /origen /destino
//!
//! # Con verificación de integridad (blake3)
//! filecopier --verify /origen /destino
//!
//! # Con algoritmo alternativo
//! filecopier --verify --hasher xxhash /origen /destino
//!
//! # Reanudar copia interrumpida
//! filecopier --resume /origen /destino
//!
//! # Control de recursos
//! filecopier --block-size 8 --swarm-limit 64 /origen /destino
//! ```

use std::path::PathBuf;
use std::str::FromStr;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Instant;

use clap::Parser;

use lib_core::{
    checkpoint::FlowControl,
    config::EngineConfig,
    engine::Orchestrator,
    hash::Algorithm,
    telemetry::CopyProgress,
};
use lib_os::traits::DriveKind;

// Importación condicional de libc para el handler de señales Unix
#[cfg(unix)]
use libc;

// ─────────────────────────────────────────────────────────────────────────────
// Argumentos CLI
// ─────────────────────────────────────────────────────────────────────────────

/// FileCopier-Rust — Motor de copia profesional de alto rendimiento
#[derive(Parser, Debug)]
#[command(
    name    = "filecopier",
    version,
    about   = "Motor de copia de alto rendimiento con verificación de integridad",
    long_about = None,
    after_help = "Ejemplos:\n  filecopier /origen /destino\n  filecopier --verify --hasher blake3 /src /dst\n  filecopier --resume /src /dst"
)]
struct Cli {
    /// Ruta de origen (archivo o directorio)
    #[arg(value_name = "ORIGEN")]
    source: PathBuf,

    /// Ruta de destino
    #[arg(value_name = "DESTINO")]
    dest: PathBuf,

    // ── Verificación ──────────────────────────────────────────────────────────

    /// Habilita verificación de integridad mediante hashing post-copia
    #[arg(long, short = 'v')]
    verify: bool,

    /// Algoritmo de hashing: blake3 (default), xxhash, sha2
    #[arg(long, default_value = "blake3", value_name = "ALGO")]
    hasher: String,

    // ── Motor de bloques ──────────────────────────────────────────────────────

    /// Tamaño de bloque en MB para archivos grandes [default: 4]
    #[arg(long, default_value_t = 4, value_name = "MB")]
    block_size: u64,

    /// Umbral en MB para clasificar archivo como "grande" [default: 16]
    #[arg(long, default_value_t = 16, value_name = "MB")]
    threshold: u64,

    /// Capacidad del canal de bloques (máx bloques en vuelo) [default: 8]
    #[arg(long, default_value_t = 8, value_name = "N")]
    channel_cap: usize,

    // ── Motor de enjambre ─────────────────────────────────────────────────────

    /// Máximo de tareas concurrentes en el motor de enjambre [default: 128]
    #[arg(long, default_value_t = 128, value_name = "N")]
    swarm_limit: usize,

    // ── Resiliencia ───────────────────────────────────────────────────────────

    /// Reanudar desde checkpoint existente (si existe)
    #[arg(long, short = 'r')]
    resume: bool,

    /// No usar archivos .partial (peligroso: puede corromper destino en fallos)
    #[arg(long, hide = true)]
    no_partial: bool,

    // ── Heurísticas ───────────────────────────────────────────────────────────

    /// Ignorar detección automática de hardware y usar configuración manual
    #[arg(long)]
    no_detect: bool,

    // ── Diagnóstico ──────────────────────────────────────────────────────────

    /// Nivel de verbosidad: -v info, -vv debug, -vvv trace
    #[arg(long = "verbose", short, action = clap::ArgAction::Count)]
    verbosity: u8,

    /// Salida en formato silencioso (solo errores y resumen final)
    #[arg(long, short = 'q')]
    quiet: bool,
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

fn main() {
    let cli = Cli::parse();
    init_logging(cli.verbosity, cli.quiet);

    if let Err(e) = run(cli) {
        eprintln!("\n❌ Error fatal: {e}");
        std::process::exit(1);
    }
}

fn run(cli: Cli) -> lib_core::error::Result<()> {
    // ── 1. Validar paths ──────────────────────────────────────────────────────
    if !cli.source.exists() {
        eprintln!("❌ El origen no existe: {}", cli.source.display());
        std::process::exit(2);
    }

    // ── 2. Construir configuración ────────────────────────────────────────────
    let algorithm = Algorithm::from_str(&cli.hasher).unwrap_or_else(|e| {
        eprintln!("⚠ {e}. Usando blake3 por defecto.");
        Algorithm::Blake3
    });

    let mut config = EngineConfig {
        triage_threshold_bytes: cli.threshold  * 1024 * 1024,
        block_size_bytes:        cli.block_size as usize * 1024 * 1024,
        channel_capacity:        cli.channel_cap,
        swarm_concurrency:       cli.swarm_limit,
        verify:                  cli.verify,
        hash_algorithm:          algorithm,
        resume:                  cli.resume,
        use_partial_files:       !cli.no_partial,
    };

    // ── 3. Detección de hardware y ajuste de heurísticas ─────────────────────
    if !cli.no_detect {
        let adapter = lib_os::platform_adapter();
        let strategy = adapter.compute_strategy(&cli.source, &cli.dest);

        tracing::info!(
            "Hardware detectado: origen={:?}, destino={:?}",
            strategy.source_kind,
            strategy.dest_kind
        );

        // Aplicar heurísticas solo si el usuario no sobreescribió los defaults
        // (los defaults de clap son los valores exactos que ponemos aquí)
        if cli.swarm_limit == 128 {
            config.swarm_concurrency = strategy.recommended_swarm_concurrency;
        }
        if cli.block_size == 4 {
            config.block_size_bytes = strategy.recommended_block_size;
        }

        print_hardware_info(&strategy);
    }

    // ── 4. Validar configuración ──────────────────────────────────────────────
    config.validate()?;

    if !cli.quiet {
        print_config_banner(&config, &cli.source, &cli.dest);
    }

    // ── 5. Control de flujo (Ctrl+C) ──────────────────────────────────────────
    let flow = FlowControl::new();
    let flow_for_handler = flow.clone();

    // Handler de Ctrl+C: pausa limpia en lugar de aborto brusco.
    // En una segunda señal, cancela definitivamente.
    let ctrl_c_count = Arc::new(AtomicBool::new(false));
    let count_clone  = Arc::clone(&ctrl_c_count);

    ctrlc_handler(move || {
        if count_clone.load(Ordering::Relaxed) {
            // Segunda señal: cancelar
            eprintln!("\n⚠ Segunda interrupción: cancelando...");
            flow_for_handler.cancel();
        } else {
            count_clone.store(true, Ordering::Relaxed);
            eprintln!("\n⏸ Pausa solicitada. Presiona Ctrl+C de nuevo para cancelar.");
            flow_for_handler.pause();
        }
    });

    // ── 6. Ejecutar motor ─────────────────────────────────────────────────────
    let start    = Instant::now();
    let quiet    = cli.quiet;

    // Callback de progreso para el thread de UI
    let on_progress: lib_core::engine::orchestrator::ProgressCallback = Box::new(
        move |progress: CopyProgress| {
            if !quiet {
                print_progress(&progress);
            }
        }
    );

    let orchestrator = Orchestrator::new(config, flow);
    let result = orchestrator.run(&cli.source, &cli.dest, Some(on_progress))?;

    // ── 7. Resumen final ──────────────────────────────────────────────────────
    let total_elapsed = start.elapsed();

    if !cli.quiet {
        println!(); // Nueva línea después de la barra de progreso
    }

    print_summary(&result, total_elapsed);

    if result.failed_files > 0 {
        std::process::exit(3); // Exit code 3: completado con fallos parciales
    }

    Ok(())
}

// ─────────────────────────────────────────────────────────────────────────────
// UI helpers
// ─────────────────────────────────────────────────────────────────────────────

fn print_config_banner(config: &EngineConfig, source: &std::path::Path, dest: &std::path::Path) {
    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    println!("  FileCopier-Rust v{}", env!("CARGO_PKG_VERSION"));
    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    println!("  Origen:       {}", source.display());
    println!("  Destino:      {}", dest.display());
    println!("  Bloque:       {} MB", config.block_size_bytes / 1024 / 1024);
    println!("  Umbral:       {} MB", config.triage_threshold_bytes / 1024 / 1024);
    println!("  Enjambre:     {} tareas", config.swarm_concurrency);
    println!("  Verificación: {}", if config.verify {
        format!("✓ ({})", config.hash_algorithm)
    } else {
        "✗ (--verify para activar)".into()
    });
    println!("  Checkpoint:   {}", if config.resume { "reanudar" } else { "nuevo" });
    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    println!();
}

fn print_hardware_info(strategy: &lib_os::traits::CopyStrategy) {
    let kind_label = |k: DriveKind| match k {
        DriveKind::Ssd     => "SSD/NVMe",
        DriveKind::Hdd     => "HDD",
        DriveKind::Network => "RED",
        DriveKind::Unknown => "Desconocido",
    };

    println!(
        "  Hardware: {} → {}  |  enjambre={} bloques={}MB",
        kind_label(strategy.source_kind),
        kind_label(strategy.dest_kind),
        strategy.recommended_swarm_concurrency,
        strategy.recommended_block_size / 1024 / 1024
    );
}

fn print_progress(p: &CopyProgress) {
    // Barra de progreso inline (sobreescribe la línea anterior)
    let bar_width: usize = 30;
    let filled = ((p.percent / 100.0) * bar_width as f64) as usize;
    let bar: String = "█".repeat(filled) + &"░".repeat(bar_width - filled);

    print!(
        "\r  [{bar}] {:.1}%  {}  {}/{}  ETA: {}    ",
        p.percent,
        p.throughput_human(),
        p.completed_files,
        p.total_files,
        p.eta_human(),
    );

    // Flush inmediato para que la terminal muestre el update
    use std::io::Write;
    let _ = std::io::stdout().flush();
}

fn print_summary(result: &lib_core::engine::orchestrator::CopyResult, elapsed: std::time::Duration) {
    let mb_copied = result.copied_bytes as f64 / 1024.0 / 1024.0;
    let avg_speed = if elapsed.as_secs_f64() > 0.0 {
        mb_copied / elapsed.as_secs_f64()
    } else {
        0.0
    };

    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    println!("  Resumen de copia");
    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    println!("  Completados:  {} archivos", result.completed_files);

    if result.failed_files > 0 {
        println!("  ⚠ Fallidos:  {} archivos", result.failed_files);
    }

    println!("  Copiados:     {:.1} MB", mb_copied);
    println!("  Tiempo:       {:.1}s", elapsed.as_secs_f64());
    println!("  Velocidad:    {:.1} MB/s (promedio)", avg_speed);
    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

    if result.failed_files == 0 {
        println!("  ✓ Copia completada exitosamente");
    } else {
        println!("  ⚠ Copia completada con {} error(es)", result.failed_files);
        println!("    Revisa el checkpoint para detalles de los archivos fallidos.");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Logging y señales
// ─────────────────────────────────────────────────────────────────────────────

fn init_logging(verbosity: u8, quiet: bool) {
    use tracing_subscriber::EnvFilter;

    let level = if quiet {
        "error"
    } else {
        match verbosity {
            0 => "warn",
            1 => "info",
            2 => "debug",
            _ => "trace",
        }
    };

    tracing_subscriber::fmt()
        .with_env_filter(
            EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| EnvFilter::new(level))
        )
        .with_target(false)
        .with_thread_ids(false)
        .compact()
        .init();
}

/// Handler de Ctrl+C portable (Windows + Unix).
///
/// ## Diseño
///
/// El problema central del handler de señales en Unix es que `signal()`
/// solo acepta un puntero a función `extern "C"`, no un closure.
/// La solución estándar es un `static` global que el handler de señal
/// puede leer sin capturar nada del stack.
///
/// La clave del bug anterior: había DOS `static HANDLER` en scopes
/// distintos. El `sigint_handler` veía su propio static (siempre vacío),
/// no el del scope externo. La corrección: un único `static` en nivel
/// de módulo, visible para ambos lados.
fn ctrlc_handler(handler: impl Fn() + Send + Sync + 'static) {
    // ── Unix: handler de SIGINT via static global ────────────────────────────
    #[cfg(unix)]
    {
        // Static en nivel de módulo: el único punto de verdad compartido
        // entre el código de setup y el `extern "C" fn sigint_handler`.
        // Safety: se escribe exactamente una vez antes de instalar la señal.
        UNIX_SIGNAL_HANDLER
            .set(Arc::new(handler))
            .ok(); // Ignorar error si ya fue inicializado (no debería ocurrir)

        unsafe {
            libc::signal(libc::SIGINT, unix_sigint_handler as libc::sighandler_t);
        }
    }

    // ── Windows: sin handler personalizado en MVP ────────────────────────────
    // SetConsoleCtrlHandler requiere una función estática también.
    // Para MVP dejamos el comportamiento por defecto de Windows (terminar proceso).
    // TODO Fase 2: implementar SetConsoleCtrlHandler con static global análogo.
    #[cfg(windows)]
    {
        let _ = handler;
    }
}

// Static global para Unix: único punto de comunicación entre
// el código de setup y el handler de señal `extern "C"`.
#[cfg(unix)]
static UNIX_SIGNAL_HANDLER: std::sync::OnceLock<Arc<dyn Fn() + Send + Sync>> =
    std::sync::OnceLock::new();

/// Handler de señal SIGINT para Unix.
///
/// # Safety
/// Esta función es invocada por el kernel en contexto de señal.
/// Solo operaciones async-signal-safe están permitidas aquí.
/// Llamar a un closure de Rust no es formalmente async-signal-safe,
/// pero es la práctica estándar en aplicaciones CLI Rust (tokio, rayon, etc.).
/// Para un handler 100% correcto, usar `signalfd` o `pipe` trick (Fase 2).
#[cfg(unix)]
extern "C" fn unix_sigint_handler(_sig: libc::c_int) {
    // Acceso al static global: no hay captura, no hay allocation.
    if let Some(handler) = UNIX_SIGNAL_HANDLER.get() {
        handler();
    }
}