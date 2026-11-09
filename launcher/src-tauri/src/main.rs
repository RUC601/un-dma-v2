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
        let profile = ini_value(&read("app-state.ini"), "Application", "activeProfile")
            .or_else(|| ini_value(&read("config.ini"), "Global", "lastConfigProfile"));
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
fn glass(app: &tauri::AppHandle, value: &Value) {
    if let Some(w) = app.get_webview_window("main") {
        if value["loginGlass"].as_bool().unwrap_or(true) {
            if window_vibrancy::apply_acrylic(&w, Some((21, 22, 28, 190))).is_err() {
                let _ = w.set_background_color(Some(tauri::window::Color(21, 22, 28, 255)));
            }
        } else {
            let _ = window_vibrancy::clear_acrylic(&w);
            let _ = w.set_background_color(Some(tauri::window::Color(21, 22, 28, 255)));
        }
    }
}
#[tauri::command]
fn preferences() -> Result<Value, String> {
    prefs()
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
    app.emit("preferences", p).map_err(|e| e.to_string())
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
async fn authorize(app: tauri::AppHandle) -> Result<(), String> {
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
        saved: AtomicBool::new(false),
        save_failed: AtomicBool::new(false),
    });
    *slot = Some(native.clone());
    drop(slot);
    let read_app = app.clone();
    let read_native = native.clone();
    std::thread::spawn(move || {
        let log = root().ok().and_then(|p| {
            let d = p.join("logs");
            fs::create_dir_all(&d).ok()?;
            fs::OpenOptions::new()
                .create(true)
                .append(true)
                .open(d.join("launcher-runtime.log"))
                .ok()
        });
        let mut log = log;
        for line in BufReader::new(output).lines().map_while(Result::ok) {
            if let Some(raw) = line.strip_prefix("UNIPC ") {
                if let Ok(v) = serde_json::from_str::<Value>(raw) {
                    match v["event"].as_str() {
                        Some("ready") => {
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
                            let _ = read_app.emit("preferences", &v);
                            glass(&read_app, &v);
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
            } else if let Some(f) = log.as_mut() {
                let _ = writeln!(f, "{}", line);
            }
        }
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
                while n.running.load(Ordering::Acquire) {
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
            if let Ok(p) = prefs() {
                glass(app.handle(), &p);
            }
            if let Some(path) = std::env::args().skip_while(|a| a != "--update-ack").nth(1) {
                let _ = fs::write(path, b"ready");
            }
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("UNLEASHED launcher failed");
}
