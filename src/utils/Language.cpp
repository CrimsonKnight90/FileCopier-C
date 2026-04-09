#include "../../include/Language.h"

namespace FileCopier {

Language& Language::Instance() {
    static Language inst;
    return inst;
}

Language::Language() { LoadES(); }

void Language::SetLanguage(Lang lang) {
    m_current = lang;
    m_strings.clear();
    switch (lang) {
        case Lang::EN: LoadEN(); break;
        default:       LoadES(); break;
    }
}

QString Language::Get(const QString& key) const {
    auto it = m_strings.find(key);
    if (it != m_strings.end()) return it->second;
    return "[" + key + "]";
}

std::vector<std::pair<Lang, QString>> Language::Available() {
    return {
        { Lang::ES, "Español" },
        { Lang::EN, "English" },
    };
}

void Language::LoadES() {
    m_strings = {
        { "app.title",          "FileCopier" },
        { "lbl.source",         "Origen:" },
        { "lbl.dest",           "Destino:" },
        { "btn.browse",         "..." },
        { "btn.start",          "Iniciar" },
        { "btn.pause",          "Pausar" },
        { "btn.resume",         "Reanudar" },
        { "btn.cancel",         "Cancelar" },
        { "lbl.speed",          "Velocidad:" },
        { "lbl.ready",          "Listo" },
        { "lbl.copying",        "Copiando..." },
        { "btn.toggle.show",    "\u25bc" },
        { "btn.toggle.hide",    "\u25b2" },
        { "tab.copylist",       "Lista de Copia" },
        { "tab.errors",         "Informe de Errores" },
        { "tab.options",        "Opciones" },
        { "col.source",         "Fuente" },
        { "col.size",           "Tama\u00f1o" },
        { "col.dest",           "Destino" },
        { "col.status",         "Estado" },
        { "btn.toTop",          "\u21c8 Al inicio" },
        { "btn.up",             "\u2191 Subir" },
        { "btn.down",           "\u2193 Bajar" },
        { "btn.toBottom",       "\u21ca Al final" },
        { "btn.addFiles",       "+ A\u00f1adir" },
        { "btn.remove",         "\u2715 Eliminar" },
        { "btn.save",           "Guardar lista" },
        { "btn.load",           "Cargar lista" },
        { "col.time",           "Hora" },
        { "col.action",         "Acci\u00f3n" },
        { "col.target",         "Archivo" },
        { "col.errtext",        "Error" },
        { "btn.clearErrors",    "Borrar informe" },
        { "btn.saveErrors",     "Guardar informe" },
        { "opt.endCopy",        "Fin de la copia" },
        { "opt.noCloseErr",     "No cerrar si hubo errores" },
        { "opt.noClose",        "No cerrar ventana" },
        { "opt.close",          "Cerrar ventana al terminar" },
        { "opt.collision",      "Colisiones de archivos" },
        { "opt.col.ask",        "Siempre preguntar" },
        { "opt.col.cancel",     "Cancelar copia entera" },
        { "opt.col.skip",       "Pasar (saltar)" },
        { "opt.col.overwrite",  "Sobreescribir" },
        { "opt.col.overwriteD", "Sobreescribir si es diferente" },
        { "opt.col.renameNew",  "Renombrar archivo nuevo" },
        { "opt.col.renameOld",  "Renombrar archivo viejo" },
        { "opt.copyErr",        "Errores de copia" },
        { "opt.err.ask",        "Siempre preguntar" },
        { "opt.err.cancel",     "Cancelar la copia entera" },
        { "opt.err.skip",       "Pasar (saltar archivo)" },
        { "opt.err.retryLog",   "Reintentar una vez, luego registrar" },
        { "opt.err.moveEnd",    "Poner al final de la lista" },
        { "opt.perf",           "Rendimiento" },
        { "opt.threads",        "Hilos de copia:" },
        { "opt.bufferKB",       "Tama\u00f1o de buffer (KB):" },
        { "opt.noBuffering",    "I/O sin cach\u00e9 (FILE_FLAG_NO_BUFFERING)" },
        { "opt.overlapped",     "I/O as\u00edncrono (overlapped)" },
        { "opt.verify",         "Verificar archivos tras copiar" },
        { "opt.language",       "Idioma / Language" },
        { "opt.apply",          "Aplicar y guardar opciones" },
        { "opt.default",        "Por defecto" },
        { "msg.selectFolders",  "Selecciona carpeta origen y destino." },
        { "msg.done",           "Completado: %1 ok, %2 errores de %3 total" },
        { "msg.cancelled",      "Cancelado por el usuario" },
        { "msg.confirmCancel",  "\u00bfCancelar la copia en curso y salir?" },
        { "status.scanning",    "Escaneando archivos..." },
        { "status.ready",       "Listo" },
        { "status.optSaved",    "Opciones guardadas." },
        { "ctx.selectAll",      "Seleccionar todos" },
        { "ctx.invertSel",      "Invertir selecci\u00f3n" },
        { "ctx.sort",           "Ordenar" },
        { "tray.newTask",       "Nueva tarea" },
        { "tray.newCopy",       "Nueva copia" },
        { "tray.newMove",       "Nuevo desplazamiento" },
        { "tray.config",        "Configuraci\u00f3n" },
        { "tray.quit",          "Salir" },
    };
}

void Language::LoadEN() {
    m_strings = {
        { "app.title",          "FileCopier" },
        { "lbl.source",         "Source:" },
        { "lbl.dest",           "Destination:" },
        { "btn.browse",         "..." },
        { "btn.start",          "Start" },
        { "btn.pause",          "Pause" },
        { "btn.resume",         "Resume" },
        { "btn.cancel",         "Cancel" },
        { "lbl.speed",          "Speed:" },
        { "lbl.ready",          "Ready" },
        { "lbl.copying",        "Copying..." },
        { "btn.toggle.show",    "\u25bc" },
        { "btn.toggle.hide",    "\u25b2" },
        { "tab.copylist",       "Copy List" },
        { "tab.errors",         "Error Report" },
        { "tab.options",        "Options" },
        { "col.source",         "Source" },
        { "col.size",           "Size" },
        { "col.dest",           "Destination" },
        { "col.status",         "Status" },
        { "btn.toTop",          "\u21c8 To Top" },
        { "btn.up",             "\u2191 Up" },
        { "btn.down",           "\u2193 Down" },
        { "btn.toBottom",       "\u21ca To Bottom" },
        { "btn.addFiles",       "+ Add Files" },
        { "btn.remove",         "\u2715 Remove" },
        { "btn.save",           "Save List" },
        { "btn.load",           "Load List" },
        { "col.time",           "Time" },
        { "col.action",         "Action" },
        { "col.target",         "File" },
        { "col.errtext",        "Error" },
        { "btn.clearErrors",    "Clear Report" },
        { "btn.saveErrors",     "Save Report" },
        { "opt.endCopy",        "End of copy" },
        { "opt.noCloseErr",     "Don't close if errors occurred" },
        { "opt.noClose",        "Don't close window" },
        { "opt.close",          "Close window when done" },
        { "opt.collision",      "File Collisions" },
        { "opt.col.ask",        "Always ask" },
        { "opt.col.cancel",     "Cancel entire copy" },
        { "opt.col.skip",       "Skip" },
        { "opt.col.overwrite",  "Overwrite" },
        { "opt.col.overwriteD", "Overwrite if different" },
        { "opt.col.renameNew",  "Rename new file" },
        { "opt.col.renameOld",  "Rename old file" },
        { "opt.copyErr",        "Copy Errors" },
        { "opt.err.ask",        "Always ask" },
        { "opt.err.cancel",     "Cancel entire copy" },
        { "opt.err.skip",       "Skip file" },
        { "opt.err.retryLog",   "Retry once, then log" },
        { "opt.err.moveEnd",    "Move to end of list" },
        { "opt.perf",           "Performance" },
        { "opt.threads",        "Copy threads:" },
        { "opt.bufferKB",       "Buffer size (KB):" },
        { "opt.noBuffering",    "No-buffering I/O (FILE_FLAG_NO_BUFFERING)" },
        { "opt.overlapped",     "Overlapped async I/O" },
        { "opt.verify",         "Verify files after copy" },
        { "opt.language",       "Language / Idioma" },
        { "opt.apply",          "Apply and save options" },
        { "opt.default",        "Defaults" },
        { "msg.selectFolders",  "Please select source and destination folders." },
        { "msg.done",           "Done: %1 ok, %2 failed of %3 total" },
        { "msg.cancelled",      "Cancelled by user" },
        { "msg.confirmCancel",  "Cancel the ongoing copy and exit?" },
        { "status.scanning",    "Scanning files..." },
        { "status.ready",       "Ready" },
        { "status.optSaved",    "Options saved." },
        { "ctx.selectAll",      "Select All" },
        { "ctx.invertSel",      "Invert Selection" },
        { "ctx.sort",           "Sort" },
        { "tray.newTask",       "New Task" },
        { "tray.newCopy",       "New Copy" },
        { "tray.newMove",       "New Move" },
        { "tray.config",        "Configuration" },
        { "tray.quit",          "Quit" },
    };
}

} // namespace FileCopier
