//! # filecopier CLI
//!
//! Interfaz de línea de comandos para FileCopier-Rust.

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

// ─────────────────────────────────────────────────────────────────────────────
// Argumentos CLI
// ─────────────────────────────────────────────────────────────────────────────

/// Motor de copia de alto rendimiento con verificación de integridad
#[derive(Parser, Debug)]
#[command(
    name       = "filecopier",
    version,
    about      = "Motor de copia de alto rendimiento con verificación de integridad",
    long_about = None,
    after_help = "Ejemplos:\n  filecopier C:\\src C:\\dst\n  filecopier --verify --hasher blake3 C:\\src C:\\dst\n  filecopier --resume C:\\src C:\\dst"
)]
struct Cli {
    /// Ruta de origen (archivo o directorio)
    #[arg(value_name = "ORIGEN")]
    source: PathBuf,

    /// Ruta de destino
    #[arg(value_name = "DESTINO")]
    dest: PathBuf,

    /// Habilita verificación de integridad post-copia
    #[arg(long)]
    verify: bool,

    /// Algoritmo de hashing: blake3, xxhash, sha2
    #[arg(long, default_value = "blake3", value_name = "ALGO")]
    hasher: String,

    /// Tamaño de bloque en MB para archivos grandes
    #[arg(long, default_value_t = 4, value_name = "MB")]
    block_size: u64,

    /// Umbral en MB: archivos >= umbral usan motor de bloques
    #[arg(long, default_value_t = 16, value_name = "MB")]
    threshold: u64,

    /// Máximo de bloques en vuelo simultáneamente
    #[arg(long, default_value_t = 8, value_name = "N")]
    channel_cap: usize,

    /// Máximo de tareas concurrentes para archivos pequeños
    #[arg(long, default_value_t = 128, value_name = "N")]
    swarm_limit: usize,

    /// Reanudar desde checkpoint existente
    #[arg(long, short = 'r')]
    resume: bool,

    /// Escribir directamente sin archivos .partial intermedios (no recomendado)
    #[arg(long, hide = true)]
    no_partial: bool,

    /// Ignorar detección automática de hardware
    #[arg(long)]
    no_detect: bool,

    /// Nivel de verbosidad (-v info, -vv debug, -vvv trace)
    #[arg(short = 'v', long = "verbose", action = clap::ArgAction::Count)]
    verbosity: u8,

    /// Mostrar solo errores y resumen final
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
        eprintln!("⚠  {e}. Usando blake3 por defecto.");
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

    // ── 3. Detección de hardware ──────────────────────────────────────────────
    if !cli.no_detect {
        let adapter  = lib_os::platform_adapter();
        let strategy = adapter.compute_strategy(&cli.source, &cli.dest);

        tracing::info!(
            "Hardware: origen={:?}, destino={:?}",
            strategy.source_kind,
            strategy.dest_kind
        );

        // Aplicar heurísticas solo si el usuario dejó los valores por defecto.
        if cli.swarm_limit == 128 {
            // Con --verify activo usamos concurrencia reducida para evitar
            // saturar el page cache del OS con demasiadas tareas de hash
            // concurrentes. En SSD→SSD: 128→32 tareas con verify.
            config.swarm_concurrency = if cli.verify {
                strategy.recommended_swarm_concurrency_verify
            } else {
                strategy.recommended_swarm_concurrency
            };
        }
        if cli.block_size == 4 {
            config.block_size_bytes = strategy.recommended_block_size;
        }

        if !cli.quiet {
            print_hardware_info(&strategy, cli.verify);
        }
    }

    // ── 4. Validar configuración ──────────────────────────────────────────────
    config.validate()?;

    if !cli.quiet {
        print_config_banner(&config, &cli.source, &cli.dest);
    }

    // ── 5. Control de flujo (Ctrl+C) ──────────────────────────────────────────
    let flow             = FlowControl::new();
    let flow_for_handler = flow.clone();
    let ctrl_c_count     = Arc::new(AtomicBool::new(false));
    let count_clone      = Arc::clone(&ctrl_c_count);

    ctrlc_handler(move || {
        if count_clone.load(Ordering::Relaxed) {
            eprintln!("\n⚠  Segunda interrupción: cancelando...");
            flow_for_handler.cancel();
        } else {
            count_clone.store(true, Ordering::Relaxed);
            eprintln!("\n⏸  Pausa solicitada. Presiona Ctrl+C de nuevo para cancelar.");
            flow_for_handler.pause();
        }
    });

    // ── 6. Ejecutar motor ─────────────────────────────────────────────────────
    let start = Instant::now();
    let quiet = cli.quiet;

    let on_progress: lib_core::engine::orchestrator::ProgressCallback = Box::new(
        move |progress: CopyProgress| {
            if !quiet {
                print_progress(&progress);
            }
        },
    );

    let orchestrator = Orchestrator::new(config, flow);
    let result       = orchestrator.run(&cli.source, &cli.dest, Some(on_progress))?;

    // ── 7. Resumen final ──────────────────────────────────────────────────────
    if !cli.quiet {
        println!();
    }

    print_summary(&result, start.elapsed());

    if result.failed_files > 0 {
        std::process::exit(3);
    }

    Ok(())
}

// ─────────────────────────────────────────────────────────────────────────────
// UI helpers
// ─────────────────────────────────────────────────────────────────────────────

fn print_config_banner(
    config: &EngineConfig,
    source: &std::path::Path,
    dest:   &std::path::Path,
) {
    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    println!("  FileCopier-Rust v{}", env!("CARGO_PKG_VERSION"));
    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    println!("  Origen:       {}", source.display());
    println!("  Destino:      {}", dest.display());
    println!("  Bloque:       {} MB", config.block_size_bytes / 1024 / 1024);
    println!("  Umbral:       {} MB", config.triage_threshold_bytes / 1024 / 1024);
    println!("  Enjambre:     {} tareas", config.swarm_concurrency);
    println!(
        "  Verificación: {}",
        if config.verify {
            format!("✓ ({})", config.hash_algorithm)
        } else {
            "✗  (usa --verify para activar)".into()
        }
    );
    println!("  Checkpoint:   {}", if config.resume { "reanudar" } else { "nuevo" });
    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    println!();
}

fn print_hardware_info(strategy: &lib_os::traits::CopyStrategy, verify: bool) {
    let label = |k: DriveKind| match k {
        DriveKind::Ssd     => "SSD/NVMe",
        DriveKind::Hdd     => "HDD",
        DriveKind::Network => "Red",
        DriveKind::Unknown => "Desconocido",
    };
    let concurrency = if verify {
        strategy.recommended_swarm_concurrency_verify
    } else {
        strategy.recommended_swarm_concurrency
    };
    println!(
        "  Hardware: {} → {}  |  enjambre={} bloque={}MB{}",
        label(strategy.source_kind),
        label(strategy.dest_kind),
        concurrency,
        strategy.recommended_block_size / 1024 / 1024,
        if verify { "  [verify: concurrencia reducida]" } else { "" },
    );
}

fn print_progress(p: &CopyProgress) {
    let bar_width: usize = 30;
    let filled = ((p.percent / 100.0) * bar_width as f64) as usize;
    let bar: String = "█".repeat(filled) + &"░".repeat(bar_width - filled);

    // Mostrar progreso del archivo actual si hay uno en proceso
    if let Some(ref current_file) = p.current_file {
        // Extraer solo el nombre del archivo (último componente del path)
        let file_name = std::path::Path::new(current_file)
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or(current_file.as_str());
        
        // Barra de progreso interno del archivo (10 caracteres)
        let inner_bar_width: usize = 10;
        let inner_filled = ((p.current_file_progress * 100.0 / 100.0) * inner_bar_width as f64) as usize;
        let inner_bar: String = "█".repeat(inner_filled.min(inner_bar_width)) 
            + &"░".repeat((inner_bar_width - inner_filled).saturating_sub(1).min(inner_bar_width));
        
        print!(
            "\r  [{bar}] {:.1}%  {}  {}/{}  ETA: {}  |  {}: [{}] {:.0}%",
            p.percent,
            p.throughput_human(),
            p.completed_files,
            p.total_files,
            p.eta_human(),
            file_name,
            inner_bar,
            p.current_file_progress * 100.0,
        );
    } else {
        print!(
            "\r  [{bar}] {:.1}%  {}  {}/{}  ETA: {}    ",
            p.percent,
            p.throughput_human(),
            p.completed_files,
            p.total_files,
            p.eta_human(),
        );
    }

    use std::io::Write;
    let _ = std::io::stdout().flush();
}

fn print_summary(
    result:  &lib_core::engine::orchestrator::CopyResult,
    elapsed: std::time::Duration,
) {
    let mb      = result.copied_bytes as f64 / 1024.0 / 1024.0;
    let avg_spd = if elapsed.as_secs_f64() > 0.0 {
        mb / elapsed.as_secs_f64()
    } else {
        0.0
    };

    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    println!("  Resumen de copia");
    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    println!("  Completados:  {} archivos", result.completed_files);
    if result.failed_files > 0 {
        println!("  ⚠  Fallidos: {} archivos", result.failed_files);
    }
    println!("  Copiados:     {:.1} MB", mb);
    println!("  Tiempo:       {:.2}s", elapsed.as_secs_f64());
    println!("  Velocidad:    {:.1} MB/s (promedio)", avg_spd);
    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

    if result.failed_files == 0 {
        println!("  ✓ Copia completada exitosamente");
    } else {
        println!("  ⚠  Copia completada con {} error(es)", result.failed_files);
        println!("    Revisa el checkpoint para detalles.");
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
                .unwrap_or_else(|_| EnvFilter::new(level)),
        )
        .with_target(false)
        .with_thread_ids(false)
        .compact()
        .init();
}

fn ctrlc_handler(handler: impl Fn() + Send + Sync + 'static) {
    #[cfg(unix)]
    {
        UNIX_SIGNAL_HANDLER.set(Arc::new(handler)).ok();
        unsafe {
            libc::signal(libc::SIGINT, unix_sigint_handler as libc::sighandler_t);
        }
    }

    #[cfg(windows)]
    {
        let _ = handler;
        // TODO Fase 2: SetConsoleCtrlHandler para pausa limpia en Windows.
    }
}

#[cfg(unix)]
static UNIX_SIGNAL_HANDLER: std::sync::OnceLock<Arc<dyn Fn() + Send + Sync>> =
    std::sync::OnceLock::new();

#[cfg(unix)]
extern "C" fn unix_sigint_handler(_sig: libc::c_int) {
    if let Some(handler) = UNIX_SIGNAL_HANDLER.get() {
        handler();
    }
}
