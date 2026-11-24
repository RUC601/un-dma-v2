#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
mod portable;
use serde::Serialize;
use serde_json::{json, Value};
use std::{
    fs,
    io::{BufRead, BufReader, Write},
    path::PathBuf,
    process::{ChildStdin, Command, Stdio},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Mutex,
    },
    time::{Duration, Instant},
};
use tauri::{Emitter, Manager, State};
use tauri_plugin_updater::{Update, UpdaterExt};
#[cfg(windows)]
use windows::Win32::{
    Foundation::HWND,
    Graphics::Dwm::{
        DwmSetWindowAttribute, DWMSBT_NONE, DWMSBT_TRANSIENTWINDOW, DWMWA_BORDER_COLOR,
        DWMWA_COLOR_NONE, DWMWA_SYSTEMBACKDROP_TYPE, DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_ROUND,
        DWMWINDOWATTRIBUTE,
    },
};

const RUNTIME_LOG_MAX_BYTES: u64 = 4 * 1024 * 1024;

struct RuntimeLogWriter {
    path: PathBuf,
    max_bytes: u64,
    bytes_written: u64,
    file: Option<fs::File>,
}

impl RuntimeLogWriter {
    fn open(path: PathBuf, max_bytes: u64) -> std::io::Result<Self> {
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }
        let max_bytes = max_bytes.max(1);
        let existing_len = fs::metadata(&path).map(|m| m.len()).unwrap_or(0);
        if existing_len >= max_bytes {
            Self::rotate_file(&path);
        }
        let file = fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(&path)?;
        let bytes_written = file.metadata().map(|m| m.len()).unwrap_or(0);
        Ok(Self {
            path,
            max_bytes,
            bytes_written,
            file: Some(file),
        })
    }

    fn rotated_path(path: &PathBuf) -> PathBuf {
        let mut rotated = path.as_os_str().to_os_string();
        rotated.push(".1");
        PathBuf::from(rotated)
    }

    fn rotate_file(path: &PathBuf) {
        let backup = Self::rotated_path(path);
        let _ = fs::remove_file(&backup);
        let _ = fs::rename(path, &backup);
    }

    fn write_line(&mut self, line: &str) {
        let line_bytes = line.len() as u64 + 1;
        if self.bytes_written > 0 && self.bytes_written.saturating_add(line_bytes) > self.max_bytes {
            if let Some(mut f) = self.file.take() {
                let _ = f.flush();
            }
            Self::rotate_file(&self.path);
            self.file = fs::OpenOptions::new()
                .create(true)
                .write(true)
                .truncate(true)
                .open(&self.path)
                .ok();
            self.bytes_written = 0;
        }
        if let Some(f) = self.file.as_mut() {
            if writeln!(f, "{}", line).is_ok() {
                self.bytes_written = self.bytes_written.saturating_add(line_bytes);
            }
        }
    }
}

#[derive(Clone, Default, Serialize)]
struct Status {
    phase: String,
    version: String,
    received: u64,
    total: Option<u64>,
    error: String,
}
struct Native {
    input: Mutex<ChildStdin>,
    running: AtomicBool,
    reader_done: AtomicBool,
    saved: AtomicBool,
    save_failed: AtomicBool,
}
#[derive(Default)]
struct Runtime {
    native: Mutex<Option<Arc<Native>>>,
    update: Mutex<Option<Update>>,
    status: Mutex<Status>,
    downloaded: Mutex<Option<PathBuf>>,
    busy: AtomicBool,
    installing: AtomicBool,
}
fn root() -> Result<PathBuf, String> {
    Ok(std::env::current_exe()
        .map_err(|e| e.to_string())?
        .parent()
        .ok_or("Missing executable directory")?
        .to_owned())
}
fn prefs() -> Result<Value, String> {
    let p = root()?.join("config/ui-preferences.json");
    if !p.exists() {
        let directory = root()?.join("config");
        let read = |name: &str| -> String {
            let path = directory.join(name);
            if fs::metadata(&path).is_ok_and(|m| m.len() <= 4_000_000) {
                fs::read_to_string(path).unwrap_or_default()
            } else {
                String::new()
            }
        };
        let profile = ini_value(&read("app-state.ini"), "Application", "activeProfile");
        let profile = profile
            .filter(|p| {
                !p.is_empty()
                    && !p.contains(['/', '\\', ':'])
                    && p != ".."
                    && directory.join(p).is_file()
            })
            .unwrap_or_else(|| "config.ini".into());
        let language = ini_value(&read(&profile), "Global", "uiLanguage")
            .and_then(|v| v.parse::<u8>().ok())
            .filter(|v| *v <= 1)
            .unwrap_or(1);
        return Ok(json!({"version":1,"language":language,"loginGlass":true}));
    }
    if fs::metadata(&p).map_err(|e| e.to_string())?.len() > 65536 {
        return Err("Invalid UI preferences file size".into());
    }
    let v: Value = serde_json::from_slice(&fs::read(p).map_err(|e| e.to_string())?)
        .map_err(|e| e.to_string())?;
    if !v.is_object() {
        return Err("Invalid UI preferences JSON".into());
    }
    if v.get("version")
        .is_some_and(|version| version.as_u64() != Some(1))
    {
        return Err("Unsupported UI preferences version".into());
    }
    Ok(v)
}
fn ini_value(text: &str, section: &str, key: &str) -> Option<String> {
    let mut active = false;
    for line in text.lines() {
        let line = line.trim().trim_start_matches('\u{feff}');
        if line.starts_with('[') && line.ends_with(']') {
            active = line[1..line.len() - 1].eq_ignore_ascii_case(section);
            continue;
        }
        if active && !line.starts_with([';', '#']) {
            if let Some((name, value)) = line.split_once('=') {
                if name.trim().eq_ignore_ascii_case(key) {
                    return Some(value.trim().to_owned());
                }
            }
        }
    }
    None
}
fn save_preferences(value: &Value) -> Result<(), String> {
    let dir = root()?.join("config");
    fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let temp = dir.join("ui-preferences.launcher.tmp");
    let mut f = fs::File::create(&temp).map_err(|e| e.to_string())?;
    f.write_all(&serde_json::to_vec_pretty(value).map_err(|e| e.to_string())?)
        .map_err(|e| e.to_string())?;
    f.sync_all().map_err(|e| e.to_string())?;
    drop(f);
    // Windows rename cannot replace an existing file; use the platform's atomic replacement.
    portable::replace_file(&temp, &dir.join("ui-preferences.json"))
}
fn send(native: &Native, value: Value) -> Result<(), String> {
    let mut input = native.input.lock().map_err(|e| e.to_string())?;
    writeln!(input, "{}", value).map_err(|e| e.to_string())?;
    input.flush().map_err(|e| e.to_string())
}
fn publish(app: &tauri::AppHandle, status: Status) {
    let state = app.state::<Runtime>();
    *state.status.lock().unwrap() = status.clone();
    let _ = app.emit("update-state", &status);
    if let Some(n) = state.native.lock().unwrap().as_ref() {
        if n.running.load(Ordering::Acquire) {
            let _ = send(n, json!({"command":"update","state":status}));
        }
    };
}
#[cfg(windows)]
fn set_dwm_attribute<T>(hwnd: HWND, attribute: DWMWINDOWATTRIBUTE, value: T) -> bool {
    // DWM reads the value synchronously using the supplied byte count.
    unsafe {
        DwmSetWindowAttribute(
            hwnd,
            attribute,
            std::ptr::from_ref(&value).cast(),
            std::mem::size_of::<T>() as u32,
        )
        .is_ok()
    }
}

fn appearance_preferences(_app: &tauri::AppHandle, mut value: Value) -> Value {
    #[cfg(windows)]
    let native_acrylic = _app
        .get_webview_window("main")
        .and_then(|window| window.hwnd().ok())
        .is_some_and(|hwnd| {
            let enabled = value["loginGlass"].as_bool().unwrap_or(true);
            let backdrop = if enabled {
                DWMSBT_TRANSIENTWINDOW
            } else {
                DWMSBT_NONE
            };
            // The documented desktop Acrylic attribute is unavailable on Windows 10.
            set_dwm_attribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, backdrop) && enabled
        });
    #[cfg(not(windows))]
    let native_acrylic = false;

    value["nativeAcrylic"] = json!(native_acrylic);
    value
}
#[tauri::command]
fn preferences(app: tauri::AppHandle) -> Result<Value, String> {
    prefs().map(|p| appearance_preferences(&app, p))
}
#[tauri::command]
fn set_language(language: u8, app: tauri::AppHandle, state: State<Runtime>) -> Result<(), String> {
    if language > 1 {
        return Err("Invalid language".into());
    }
    if state
        .native
        .lock()
        .unwrap()
        .as_ref()
        .is_some_and(|n| n.running.load(Ordering::Acquire))
    {
        return Err("Console owns preferences while running".into());
    }
    let mut p = prefs()?;
    p["language"] = json!(language);
    save_preferences(&p)?;
    app.emit("preferences", appearance_preferences(&app, p))
        .map_err(|e| e.to_string())
}
#[tauri::command]
fn close_launcher(app: tauri::AppHandle, state: State<Runtime>) -> Result<(), String> {
    if state
        .native
        .lock()
        .unwrap()
        .as_ref()
        .is_some_and(|n| n.running.load(Ordering::Acquire))
    {
        if let Some(w) = app.get_webview_window("main") {
            w.hide().map_err(|e| e.to_string())?;
        }
    } else {
        app.exit(0);
    }
    Ok(())
}
#[tauri::command]
async fn authorize(app: tauri::AppHandle, username: String) -> Result<(), String> {
    let state = app.state::<Runtime>();
    let mut slot = state.native.lock().unwrap();
    if slot
        .as_ref()
        .is_some_and(|n| n.running.load(Ordering::Acquire))
    {
        return Ok(());
    }
    let dir = root()?;
    // The launcher owns this file until the child is spawned.
    if !dir.join("config/ui-preferences.json").exists() {
        save_preferences(&prefs()?)?;
    }
    let binary = dir.join("UNDMA_V2.exe");
    if !binary.is_file() {
        return Err("UNDMA_V2.exe is missing. Rebuild the portable package.".into());
    }
    use std::os::windows::process::CommandExt;
    let mut child = Command::new(binary)
        .arg("--launcher-stdio")
        .current_dir(dir)
        .creation_flags(0x08000000)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()
        .map_err(|e| e.to_string())?;
    let output = child.stdout.take().ok_or("Missing runtime output")?;
    let native = Arc::new(Native {
        input: Mutex::new(child.stdin.take().ok_or("Missing runtime input")?),
        running: AtomicBool::new(true),
        reader_done: AtomicBool::new(false),
        saved: AtomicBool::new(false),
        save_failed: AtomicBool::new(false),
    });
    *slot = Some(native.clone());
    drop(slot);
    let read_app = app.clone();
    let read_native = native.clone();
    std::thread::spawn(move || {
        let mut log = root()
            .ok()
            .and_then(|p| RuntimeLogWriter::open(p.join("logs").join("launcher-runtime.log"), RUNTIME_LOG_MAX_BYTES).ok());
        for line in BufReader::new(output).lines().map_while(Result::ok) {
            if let Some(raw) = line.strip_prefix("UNIPC ") {
                if let Ok(v) = serde_json::from_str::<Value>(raw) {
                    match v["event"].as_str() {
                        Some("ready") => {
                            let _ = send(&read_native, json!({"command":"account","username":username}));
                            if let Some(w) = read_app.get_webview_window("main") {
                                let _ = w.hide();
                            }
                            let s = read_app.state::<Runtime>().status.lock().unwrap().clone();
                            let _ = send(&read_native, json!({"command":"update","state":s}));
                        }
                        Some("saved") => {
                            read_native.saved.store(true, Ordering::Release);
                        }
                        Some("save_failed") => {
                            read_native.save_failed.store(true, Ordering::Release);
                        }
                        Some("preferences") => {
                            let preferences = appearance_preferences(&read_app, v);
                            let _ = read_app.emit("preferences", preferences);
                        }
                        Some("download") => {
                            let a = read_app.clone();
                            tauri::async_runtime::spawn(async move {
                                let _ = download_update(a).await;
                            });
                        }
                        Some("check") => {
                            let a = read_app.clone();
                            tauri::async_runtime::spawn(async move {
                                let _ = check_update(a).await;
                            });
                        }
                        Some("install") => {
                            let a = read_app.clone();
                            tauri::async_runtime::spawn(async move {
                                let _ = install_update(a).await;
                            });
                        }
                        _ => {}
                    }
                }
            } else if let Some(writer) = log.as_mut() {
                writer.write_line(&line);
            }
        }
        read_native.reader_done.store(true, Ordering::Release);
    });
    std::thread::spawn(move || {
        let result = child.wait();
        native.running.store(false, Ordering::Release);
        let state = app.state::<Runtime>();
        if !state.installing.load(Ordering::Acquire) {
            if let Some(w) = app.get_webview_window("main") {
                let _ = w.show();
                let _ = w.set_focus();
            }
            if !result.is_ok_and(|s| s.success()) {
                let _ = app.emit(
                    "runtime-error",
                    "Runtime exited unexpectedly; see logs/launcher-runtime.log",
                );
            }
        }
    });
    Ok(())
}
#[tauri::command]
async fn check_update(app: tauri::AppHandle) -> Result<(), String> {
    let state = app.state::<Runtime>();
    if state.busy.swap(true, Ordering::AcqRel) {
        return Ok(());
    }
    publish(
        &app,
        Status {
            phase: "checking".into(),
            ..Default::default()
        },
    );
    let result = async {
        let u = app
            .updater()
            .map_err(|e| e.to_string())?
            .check()
            .await
            .map_err(|e| e.to_string())?;
        let status = if let Some(ref u) = u {
            Status {
                phase: "available".into(),
                version: u.version.clone(),
                ..Default::default()
            }
        } else {
            Status {
                phase: "current".into(),
                ..Default::default()
            }
        };
        *state.update.lock().unwrap() = u;
        *state.downloaded.lock().unwrap() = None;
        publish(&app, status);
        Ok::<(), String>(())
    }
    .await;
    state.busy.store(false, Ordering::Release);
    if let Err(ref e) = result {
        publish(
            &app,
            Status {
                phase: "error".into(),
                error: e.clone(),
                ..Default::default()
            },
        );
    }
    result
}
#[tauri::command]
async fn download_update(app: tauri::AppHandle) -> Result<(), String> {
    let state = app.state::<Runtime>();
    if state.busy.swap(true, Ordering::AcqRel) {
        return Ok(());
    }
    let result = async {
        let update = state
            .update
            .lock()
            .unwrap()
            .clone()
            .ok_or("No available update")?;
        let progress_app = app.clone();
        let version = update.version.clone();
        let progress_version = version.clone();
        let mut received = 0;
        let mut last_progress=Instant::now();
        publish(
            &app,
            Status {
                phase: "downloading".into(),
                version: version.clone(),
                ..Default::default()
            },
        );
        // Official updater verifies the complete download before returning bytes.
        let bytes = update
            .download(
                move |chunk, total| {
                    received += chunk as u64;
                    if last_progress.elapsed()<Duration::from_millis(100){return;}
                    last_progress=Instant::now();
                    publish(
                        &progress_app,
                        Status {
                            phase: "downloading".into(),
                            version: progress_version.clone(),
                            received,
                            total,
                            ..Default::default()
                        },
                    );
                },
                || {},
            )
            .await
            .map_err(|e| e.to_string())?;
        let staged = portable::stage(&root()?, &version, &bytes)?;
        fs::write(staged.join("verified.zip"), &bytes).map_err(|e| e.to_string())?;
        fs::write(
            staged.join("release.json"),
            serde_json::to_vec(&update.raw_json).map_err(|e| e.to_string())?,
        )
        .map_err(|e| e.to_string())?;
        *state.downloaded.lock().unwrap() = Some(staged);
        publish(
            &app,
            Status {
                phase: "ready".into(),
                version,
                ..Default::default()
            },
        );
        Ok::<(), String>(())
    }
    .await;
    state.busy.store(false, Ordering::Release);
    if let Err(ref e) = result {
        publish(
            &app,
            Status {
                phase: "error".into(),
                error: e.clone(),
                ..Default::default()
            },
        );
    }
    result
}
#[tauri::command]
async fn install_update(app: tauri::AppHandle) -> Result<(), String> {
    let state = app.state::<Runtime>();
    if state.installing.swap(true, Ordering::AcqRel) {
        return Ok(());
    }
    let result = async {
        if state.busy.load(Ordering::Acquire) {
            return Err("Download is still active".into());
        }
        let staged = state
            .downloaded
            .lock()
            .unwrap()
            .clone()
            .ok_or("No verified update is ready")?;
        let update = state
            .update
            .lock()
            .unwrap()
            .clone()
            .ok_or("Missing update metadata")?;
        update
            .verify(&fs::read(staged.join("verified.zip")).map_err(|e| e.to_string())?)
            .map_err(|e| e.to_string())?;
        let previous = state.status.lock().unwrap().clone();
        publish(
            &app,
            Status {
                phase: "stopping".into(),
                ..previous
            },
        );
        let native = state.native.lock().unwrap().clone();
        if let Some(n) = native {
            if n.running.load(Ordering::Acquire) {
                n.saved.store(false, Ordering::Release);
                n.save_failed.store(false, Ordering::Release);
                send(&n, json!({"command":"save_exit"}))?;
                let deadline = Instant::now() + Duration::from_secs(30);
                while n.running.load(Ordering::Acquire) || !n.reader_done.load(Ordering::Acquire) {
                    if n.save_failed.load(Ordering::Acquire) {
                        return Err("Saving failed; update was stopped".into());
                    }
                    if Instant::now() > deadline {
                        return Err("Runtime exit timed out; update was stopped".into());
                    }
                    tauri::async_runtime::spawn_blocking(|| {
                        std::thread::sleep(Duration::from_millis(100))
                    })
                    .await
                    .map_err(|e| e.to_string())?;
                }
                if !n.saved.load(Ordering::Acquire) {
                    return Err("Runtime closed without a successful save acknowledgement".into());
                }
            }
        }
        portable::launch_helper(&root()?, &staged)?;
        app.exit(0);
        Ok::<(), String>(())
    }
    .await;
    if let Err(ref e) = result {
        state.installing.store(false, Ordering::Release);
        publish(
            &app,
            Status {
                phase: "error".into(),
                error: e.clone(),
                ..Default::default()
            },
        );
        if let Some(w) = app.get_webview_window("main") {
            let _ = w.show();
        }
    }
    result
}
fn main() {
    if portable::helper_entry() {
        return;
    }
    tauri::Builder::default()
        .plugin(tauri_plugin_single_instance::init(|app, _, _| {
            if let Some(w) = app.get_webview_window("main") {
                let _ = w.show();
                let _ = w.set_focus();
            }
        }))
        .plugin(tauri_plugin_updater::Builder::new().build())
        .manage(Runtime::default())
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                let state = window.state::<Runtime>();
                if state
                    .native
                    .lock()
                    .unwrap()
                    .as_ref()
                    .is_some_and(|n| n.running.load(Ordering::Acquire))
                {
                    api.prevent_close();
                    let _ = window.hide();
                }
            }
        })
        .invoke_handler(tauri::generate_handler![
            preferences,
            set_language,
            authorize,
            close_launcher,
            check_update,
            download_update,
            install_update
        ])
        .setup(|app| {
            #[cfg(windows)]
            if let Some(hwnd) = app
                .get_webview_window("main")
                .and_then(|window| window.hwnd().ok())
            {
                set_dwm_attribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_ROUND);
                set_dwm_attribute(hwnd, DWMWA_BORDER_COLOR, DWMWA_COLOR_NONE);
            }
            if let Some(path) = std::env::args().skip_while(|a| a != "--update-ack").nth(1) {
                let _ = fs::write(path, b"ready");
            }
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("UNLEASHED launcher failed");
}

#[cfg(test)]
mod tests {
    use super::RuntimeLogWriter;
    use std::{fs, time::{SystemTime, UNIX_EPOCH}};

    #[test]
    fn runtime_log_rotates_on_size_limit_and_startup() {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let dir = std::env::temp_dir().join(format!("undma_log_rotate_test_{}", nanos));
        let log_path = dir.join("launcher-runtime.log");
        let backup_path = dir.join("launcher-runtime.log.1");

        {
            let mut writer = RuntimeLogWriter::open(log_path.clone(), 32).unwrap();
            writer.write_line("first-line-1234567890");
            writer.write_line("second-line-1234567890");
            writer.write_line("third-line-1234567890");
        }

        assert!(log_path.is_file());
        assert!(backup_path.is_file());
        let current = fs::read_to_string(&log_path).unwrap();
        let backup = fs::read_to_string(&backup_path).unwrap();
        assert!(current.contains("third-line-1234567890"));
        assert!(!current.contains("first-line-1234567890"));
        assert!(backup.contains("second-line-1234567890"));
        assert!(fs::metadata(&log_path).unwrap().len() <= 32);

        // Oversized file on startup rotates before appending new lines.
        fs::write(&log_path, "0123456789012345678901234567890123456789").unwrap();
        {
            let mut writer = RuntimeLogWriter::open(log_path.clone(), 32).unwrap();
            writer.write_line("fresh-session");
        }
        let after_restart = fs::read_to_string(&log_path).unwrap();
        let rotated_after_restart = fs::read_to_string(&backup_path).unwrap();
        assert_eq!(after_restart.trim(), "fresh-session");
        assert!(rotated_after_restart.starts_with("0123456789"));

        let _ = fs::remove_dir_all(&dir);
    }
}
