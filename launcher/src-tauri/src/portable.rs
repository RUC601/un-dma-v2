use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    collections::HashSet,
    fs,
    io::Read,
    path::{Path, PathBuf},
    process::Command,
    time::{Duration, Instant},
};

#[derive(Serialize, Deserialize, Clone)]
pub struct FileEntry {
    pub path: String,
    pub sha256: String,
}
#[derive(Serialize, Deserialize, Clone)]
pub struct Manifest {
    pub version: String,
    pub files: Vec<FileEntry>,
}
#[derive(Serialize, Deserialize)]
struct Plan {
    root: PathBuf,
    staged: PathBuf,
    parent: u32,
    version: String,
}
#[link(name = "kernel32")]
extern "system" {
    fn MoveFileExW(old: *const u16, new: *const u16, flags: u32) -> i32;
    fn OpenProcess(access: u32, inherit: i32, pid: u32) -> isize;
    fn WaitForSingleObject(handle: isize, millis: u32) -> u32;
    fn CloseHandle(handle: isize) -> i32;
}
fn wide(p: &Path) -> Vec<u16> {
    use std::os::windows::ffi::OsStrExt;
    p.as_os_str().encode_wide().chain(Some(0)).collect()
}
pub fn replace_file(old: &Path, new: &Path) -> Result<(), String> {
    if unsafe { MoveFileExW(wide(old).as_ptr(), wide(new).as_ptr(), 1 | 8) } == 0 {
        return Err(std::io::Error::last_os_error().to_string());
    }
    Ok(())
}
fn safe_path(s: &str) -> Result<PathBuf, String> {
    if s.is_empty()
        || s.len() > 220
        || s.contains('\\')
        || s.contains(':')
        || s.starts_with('/')
        || s.ends_with('/')
    {
        return Err(format!("Invalid package path: {s}"));
    }
    for part in s.split('/') {
        if part.is_empty()
            || part == "."
            || part == ".."
            || part.ends_with(['.', ' '])
            || part.chars().any(|c| c < ' ' || "<>\"|?*".contains(c))
        {
            return Err(format!("Invalid package component: {s}"));
        }
        let stem = part.split('.').next().unwrap().to_ascii_uppercase();
        if ["CON", "PRN", "AUX", "NUL"].contains(&stem.as_str())
            || ((stem.starts_with("COM") || stem.starts_with("LPT")) && stem.len() == 4)
        {
            return Err(format!("Reserved package path: {s}"));
        }
    }
    let low = s.to_ascii_lowercase();
    let root_file = !s.contains('/')
        && (low == "unleashed-launcher.exe"
            || low == "undma_v2.exe"
            || low == "portable-manifest.json"
            || low == "info.db"
            || low.ends_with(".dll"));
    let voice = low.starts_with("alert-voice/kokoro-int8-multi-lang-v1_1/")
        || low.starts_with("alert-voice/sherpa-onnx-v2.0.1-win-x64-shared-md-release/")
        || [
            "alert-voice/espeak-ng-data-license.txt",
            "alert-voice/onnx-runtime-license.txt",
            "alert-voice/sherpa-onnx-license.txt",
            "alert-voice/sources.txt",
        ]
        .contains(&low.as_str());
    if !root_file && !low.starts_with("assets/") && !voice {
        return Err(format!("Package cannot manage user data: {s}"));
    }
    Ok(PathBuf::from(s))
}
fn digest(path: &Path) -> Result<String, String> {
    let mut f = fs::File::open(path).map_err(|e| e.to_string())?;
    let mut h = Sha256::new();
    let mut b = [0u8; 65536];
    loop {
        let n = f.read(&mut b).map_err(|e| e.to_string())?;
        if n == 0 {
            break;
        }
        h.update(&b[..n]);
    }
    Ok(format!("{:x}", h.finalize()))
}
fn no_reparse(root: &Path, relative: &Path) -> Result<(), String> {
    use std::os::windows::fs::MetadataExt;
    let mut p = root.to_path_buf();
    for part in relative.components() {
        p.push(part);
        match fs::symlink_metadata(&p) {
            Ok(m) if m.file_attributes() & 0x400 != 0 => {
                return Err(format!(
                    "Update path contains a junction or symlink: {}",
                    p.display()
                ))
            }
            Err(e) if e.kind() != std::io::ErrorKind::NotFound => return Err(e.to_string()),
            _ => {}
        }
    }
    Ok(())
}
fn copy_replace(source: &Path, destination: &Path) -> Result<(), String> {
    let nonce = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_err(|e| e.to_string())?
        .as_nanos();
    let temp = destination.with_file_name(format!(
        "{}.update-{}",
        destination.file_name().unwrap().to_string_lossy(),
        nonce
    ));
    let mut output = fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&temp)
        .map_err(|e| e.to_string())?;
    let mut input = fs::File::open(source).map_err(|e| e.to_string())?;
    std::io::copy(&mut input, &mut output).map_err(|e| e.to_string())?;
    output.sync_all().map_err(|e| e.to_string())?;
    drop(output);
    replace_file(&temp, destination)
}
fn manifest(dir: &Path, version: &str) -> Result<Manifest, String> {
    let bytes = fs::read(dir.join("portable-manifest.json")).map_err(|e| e.to_string())?;
    if bytes.len() > 2_000_000 {
        return Err("Manifest too large".into());
    }
    let m: Manifest = serde_json::from_slice(&bytes).map_err(|e| e.to_string())?;
    if m.version != version || m.files.is_empty() || m.files.len() > 20000 {
        return Err("Package version or file count mismatch".into());
    }
    let mut names = HashSet::new();
    for f in &m.files {
        let p = safe_path(&f.path)?;
        if f.path.eq_ignore_ascii_case("portable-manifest.json")
            || !names.insert(f.path.to_ascii_lowercase())
        {
            return Err("Duplicate manifest entry".into());
        }
        if digest(&dir.join(p))? != f.sha256.to_ascii_lowercase() {
            return Err(format!("Package hash mismatch: {}", f.path));
        }
    }
    if !names.contains("unleashed-launcher.exe") || !names.contains("undma_v2.exe") {
        return Err("Incomplete portable runtime".into());
    }
    Ok(m)
}
pub fn stage(root: &Path, version: &str, bytes: &[u8]) -> Result<PathBuf, String> {
    if version.is_empty()
        || !version
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || ".-+".contains(c))
    {
        return Err("Invalid package version".into());
    }
    let nonce = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_err(|e| e.to_string())?
        .as_nanos();
    no_reparse(root, Path::new(".updates"))?;
    let staged = root.join(".updates").join(format!("{version}-{nonce}"));
    fs::create_dir_all(&staged).map_err(|e| e.to_string())?;
    let result = (|| {
        let mut archive =
            zip::ZipArchive::new(std::io::Cursor::new(bytes)).map_err(|e| e.to_string())?;
        if archive.len() > 20001 {
            return Err("Too many archive entries".into());
        }
        let mut names = HashSet::new();
        let mut total = 0u64;
        for i in 0..archive.len() {
            let mut file = archive.by_index(i).map_err(|e| e.to_string())?;
            if file.is_dir() {
                continue;
            }
            let p = safe_path(file.name())?;
            if !names.insert(file.name().to_ascii_lowercase())
                || file.unix_mode().is_some_and(|m| m & 0o170000 == 0o120000)
            {
                return Err("Duplicate or symlink archive entry".into());
            }
            total = total
                .checked_add(file.size())
                .ok_or("Archive size overflow")?;
            if total > 2_000_000_000 || file.size() > 600_000_000 {
                return Err("Archive exceeds portable size limit".into());
            }
            let out = staged.join(p);
            if let Some(d) = out.parent() {
                fs::create_dir_all(d).map_err(|e| e.to_string())?;
            }
            let mut f = fs::File::create(out).map_err(|e| e.to_string())?;
            std::io::copy(&mut file, &mut f).map_err(|e| e.to_string())?;
            f.sync_all().map_err(|e| e.to_string())?;
        }
        let m = manifest(&staged, version)?;
        if names.len() != m.files.len() + 1 {
            return Err("Archive contains unmanaged files".into());
        }
        Ok(staged.clone())
    })();
    // Failed packages stay isolated for diagnosis and never replace program/user files.
    result
}
pub fn launch_helper(root: &Path, staged: &Path) -> Result<(), String> {
    let version: Manifest = serde_json::from_slice(
        &fs::read(staged.join("portable-manifest.json")).map_err(|e| e.to_string())?,
    )
    .map_err(|e| e.to_string())?;
    manifest(staged, &version.version)?;
    let plan = Plan {
        root: root.canonicalize().map_err(|e| e.to_string())?,
        staged: staged.canonicalize().map_err(|e| e.to_string())?,
        parent: std::process::id(),
        version: version.version,
    };
    let plan_path = staged.join("apply-plan.json");
    fs::write(
        &plan_path,
        serde_json::to_vec(&plan).map_err(|e| e.to_string())?,
    )
    .map_err(|e| e.to_string())?;
    let helper = staged.join("portable-helper.exe");
    fs::copy(std::env::current_exe().map_err(|e| e.to_string())?, &helper)
        .map_err(|e| e.to_string())?;
    use std::os::windows::process::CommandExt;
    Command::new(helper)
        .arg("--portable-apply")
        .arg(plan_path)
        .creation_flags(0x08000000)
        .spawn()
        .map_err(|e| e.to_string())?;
    Ok(())
}
fn wait_parent(pid: u32) -> Result<(), String> {
    let handle = unsafe { OpenProcess(0x00100000, 0, pid) };
    if handle == 0 {
        let e = std::io::Error::last_os_error();
        return if e.raw_os_error() == Some(87) {
            Ok(())
        } else {
            Err(format!("Cannot verify launcher exit: {e}"))
        };
    }
    let result = unsafe { WaitForSingleObject(handle, 30000) };
    unsafe { CloseHandle(handle) };
    if result != 0 {
        return Err("Launcher exit timed out; no files replaced".into());
    }
    Ok(())
}
fn apply(plan: &Plan) -> Result<(), String> {
    let root = plan.root.canonicalize().map_err(|e| e.to_string())?;
    let staged = plan.staged.canonicalize().map_err(|e| e.to_string())?;
    if !staged.starts_with(root.join(".updates")) || staged == root {
        return Err("Invalid update staging root".into());
    }
    // Re-verify the original ZIP through the official updater before trusting extracted files.
    let fresh = verify_cached(&root, &staged)?;
    let staged = fresh.canonicalize().map_err(|e| e.to_string())?;
    let m = manifest(&staged, &plan.version)?;
    wait_parent(plan.parent)?;
    let mut files: Vec<String> = m.files.iter().map(|f| f.path.clone()).collect();
    files.push("portable-manifest.json".into());
    if let Ok(old) = fs::read(root.join("portable-manifest.json")) {
        let old: Manifest = serde_json::from_slice(&old).map_err(|e| e.to_string())?;
        for f in old.files {
            safe_path(&f.path)?;
            if !files.contains(&f.path) {
                files.push(f.path);
            }
        }
    }
    let backup = staged.join("backup");
    fs::create_dir(&backup).map_err(|e| e.to_string())?;
    for name in &files {
        no_reparse(&root, &safe_path(name)?)?;
    }
    let mut existed = HashSet::new();
    // Finish every backup before touching any managed program file.
    for name in &files {
        let relative = safe_path(name)?;
        let source = root.join(&relative);
        if source.is_file() {
            let dest = backup.join(&relative);
            fs::create_dir_all(dest.parent().unwrap()).map_err(|e| e.to_string())?;
            fs::copy(source, dest).map_err(|e| e.to_string())?;
            existed.insert(name.clone());
        }
    }
    let mut touched = Vec::new();
    let result = (|| {
        for name in &files {
            let relative = safe_path(name)?;
            let source = staged.join(&relative);
            let dest = root.join(&relative);
            fs::create_dir_all(dest.parent().unwrap()).map_err(|e| e.to_string())?;
            if source.is_file() {
                copy_replace(&source, &dest)?;
            } else if dest.exists() {
                fs::remove_file(&dest).map_err(|e| e.to_string())?;
            }
            touched.push(name.clone());
        }
        manifest(&root, &plan.version)?;
        let ack = staged.join("restart-ready");
        let mut child = Command::new(root.join("unleashed-launcher.exe"))
            .current_dir(&root)
            .arg("--update-ack")
            .arg(&ack)
            .spawn()
            .map_err(|e| e.to_string())?;
        let deadline = Instant::now() + Duration::from_secs(30);
        while !ack.exists() {
            if child.try_wait().map_err(|e| e.to_string())?.is_some() {
                return Err("Updated launcher failed before ready".into());
            }
            if Instant::now() > deadline {
                return Err(
                    "Updated launcher did not acknowledge ready; backup retained for recovery"
                        .into(),
                );
            }
            std::thread::sleep(Duration::from_millis(100));
        }
        Ok(())
    })();
    if result.is_err() {
        let mut errors = Vec::new();
        for name in touched.iter().rev() {
            let relative = safe_path(name)?;
            let dest = root.join(&relative);
            let restored = if existed.contains(name) {
                copy_replace(&backup.join(&relative), &dest)
            } else {
                fs::remove_file(dest).map_err(|e| e.to_string())
            };
            if let Err(e) = restored {
                errors.push(format!("{name}: {e}"));
            }
        }
        if !errors.is_empty() {
            return Err(format!(
                "Update failed; rollback needs recovery from {}: {}",
                backup.display(),
                errors.join("; ")
            ));
        }
        let _ = Command::new(root.join("unleashed-launcher.exe"))
            .current_dir(&root)
            .spawn();
    }
    result
}
fn verify_cached(root: &Path, cached: &Path) -> Result<PathBuf, String> {
    use tauri_plugin_updater::UpdaterExt;
    let mut context = tauri::generate_context!();
    context.config_mut().app.windows.clear();
    let app = tauri::Builder::default()
        .plugin(tauri_plugin_updater::Builder::new().build())
        .build(context)
        .map_err(|e| e.to_string())?;
    let raw =
        serde_json::from_slice(&fs::read(cached.join("release.json")).map_err(|e| e.to_string())?)
            .map_err(|e| e.to_string())?;
    let update = app
        .updater()
        .map_err(|e| e.to_string())?
        .restore_update(raw)
        .map_err(|e| e.to_string())?
        .ok_or("Cached version is not newer than current version")?;
    let bytes = fs::read(cached.join("verified.zip")).map_err(|e| e.to_string())?;
    update.verify(&bytes).map_err(|e| e.to_string())?;
    stage(root, &update.version, &bytes)
}
pub fn helper_entry() -> bool {
    let args: Vec<_> = std::env::args_os().collect();
    if args.get(1).is_none_or(|a| a != "--portable-apply") {
        return false;
    }
    if let Some(path) = args.get(2) {
        let path = PathBuf::from(path);
        let result = (|| {
            let bytes = fs::read(&path).map_err(|e| e.to_string())?;
            let plan: Plan = serde_json::from_slice(&bytes).map_err(|e| e.to_string())?;
            apply(&plan)
        })();
        let record = match result {
            Ok(()) => "Update completed".to_string(),
            Err(e) => format!("Update stopped: {e}"),
        };
        let _ = fs::write(path.with_file_name("update-result.txt"), record);
    }
    true
}

#[cfg(test)]
#[path = "../../../../../04_SCENARIO/un-dma-v2/src/PortableUpdaterTests.rs"]
mod tests;
