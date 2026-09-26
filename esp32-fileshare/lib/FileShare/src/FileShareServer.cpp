#include "FileShareServer.h"

namespace {

// Single-page UI. Talks to the JSON API; no external assets, works offline.
const char kUiHtml[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>File share</title>
<style>
body{font:16px system-ui,sans-serif;margin:0 auto;max-width:720px;padding:16px;background:#0f1115;color:#e6e8ec}
a{color:#5b9dd9}table{width:100%;border-collapse:collapse}td{padding:6px 4px;border-bottom:1px solid #262a33}
td.s{text-align:right;white-space:nowrap;color:#9aa0aa}button{background:#262a33;color:#e6e8ec;border:1px solid #3a3f4b;border-radius:4px;padding:4px 10px}
#bar{height:4px;background:#5b9dd9;width:0}#msg{min-height:1.4em;color:#e0a458}footer{margin-top:24px;font-size:13px;color:#9aa0aa}
</style></head><body>
<h1 id=title>File share</h1>
<p id=usage></p>
<form id=up hidden><input type=file id=pick> <button>Upload</button><div id=bar></div></form>
<p id=msg></p>
<table id=list></table>
<footer><a href="/wifi">Wi-Fi setup</a> (setup hotspot only)</footer>
<script>
const $=id=>document.getElementById(id);
const kb=n=>n<1024?n+' B':(n/1024).toFixed(1)+' KB';
let info={};
async function refresh(){
  const r=await fetch('/api/files');
  if(!r.ok){$('msg').textContent='List failed: '+r.status;return}
  info=await r.json();
  $('title').textContent=document.title=info.title;
  $('usage').textContent=kb(info.used)+' of '+kb(info.maxTotal)+' used, max '+kb(info.maxFile)+' per file';
  $('up').hidden=!info.upload;
  const t=$('list');t.innerHTML='';
  if(!info.files.length)t.innerHTML='<tr><td>No files yet.</td></tr>';
  for(const f of info.files){
    const tr=t.insertRow(),q=encodeURIComponent(f.name);
    tr.insertCell().innerHTML='<a href="/api/file?name='+q+'">'+f.name+'</a>';
    const s=tr.insertCell();s.className='s';s.textContent=kb(f.size);
    if(info.delete){const b=document.createElement('button');b.textContent='Delete';
      b.onclick=async()=>{if(!confirm('Delete '+f.name+'?'))return;
        const d=await fetch('/api/file?name='+q,{method:'DELETE'});
        $('msg').textContent=d.ok?'':'Delete failed: '+await d.text();refresh()};
      tr.insertCell().appendChild(b)}
  }
}
$('up').onsubmit=e=>{
  e.preventDefault();const f=$('pick').files[0];if(!f)return;
  if(f.size>info.maxFile){$('msg').textContent='Too big: max '+kb(info.maxFile);return}
  const fd=new FormData();fd.append('file',f,f.name);
  const x=new XMLHttpRequest();x.open('POST','/api/file');
  x.upload.onprogress=p=>{$('bar').style.width=(100*p.loaded/p.total)+'%'};
  x.onload=()=>{$('bar').style.width=0;$('msg').textContent=x.status==200?'Uploaded '+JSON.parse(x.responseText).name:'Upload failed: '+x.responseText;$('pick').value='';refresh()};
  x.onerror=()=>{$('bar').style.width=0;$('msg').textContent='Upload failed';};
  x.send(fd);
};
refresh();
</script></body></html>)HTML";

const char* contentTypeFor(const String& name) {
    const int dot = name.lastIndexOf('.');
    if (dot < 0) return "application/octet-stream";
    String ext = name.substring(dot + 1);
    ext.toLowerCase();
    if (ext == "txt" || ext == "log" || ext == "md" || ext == "csv") return "text/plain";
    if (ext == "json") return "application/json";
    if (ext == "htm" || ext == "html") return "text/html";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "pdf") return "application/pdf";
    return "application/octet-stream";
}

bool safeChar(char c) { return isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_'; }

}  // namespace

// --- Names -----------------------------------------------------------------

bool FileShareServer::isSafeName(const String& name, uint8_t maxLen) {
    if (name.isEmpty() || name.length() > maxLen) return false;
    // Leading dot hides temp files and blocks "." / "..".
    if (name[0] == '.') return false;
    for (size_t i = 0; i < name.length(); ++i) {
        if (!safeChar(name[i])) return false;
    }
    return name.indexOf("..") < 0;
}

String FileShareServer::sanitizeName(const String& raw, uint8_t maxLen) {
    // Browsers may send a full path; keep the last component only.
    int cut = max(raw.lastIndexOf('/'), raw.lastIndexOf('\\'));
    String in = raw.substring(cut + 1);

    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i) {
        const char c = in[i];
        char mapped = safeChar(c) ? c : '_';
        // No "..", and no runs of '_'.
        if ((mapped == '.' || mapped == '_') && !out.isEmpty() && out[out.length() - 1] == mapped) continue;
        out += mapped;
    }
    while (!out.isEmpty() && (out[0] == '.' || out[0] == '_')) out.remove(0, 1);

    if (out.length() > maxLen) {
        // Keep the extension if it's short, trim the stem.
        const int dot = out.lastIndexOf('.');
        String ext = (dot > 0 && out.length() - dot <= 8) ? out.substring(dot) : String();
        out = out.substring(0, maxLen - ext.length()) + ext;
    }
    return isSafeName(out, maxLen) ? out : String();
}

// --- Lifecycle ---------------------------------------------------------------

bool FileShareServer::begin(fs::FS& fs, const FileShareConfig& cfg, Print* log) {
    fs_ = &fs;
    cfg_ = cfg;
    log_ = log;

    if (!fs_->exists(cfg_.rootDir) && !fs_->mkdir(cfg_.rootDir)) {
        if (log_) log_->printf("[fileshare] cannot create %s\n", cfg_.rootDir);
        return false;
    }
    // Drop a partial upload left by a reset mid-transfer.
    if (fs_->exists(tempPath())) fs_->remove(tempPath());

    server_.reset(new WebServer(cfg_.port));
    WebServer& s = *server_;
    s.on("/", HTTP_GET, [this] { handleUi(); });
    s.on("/api/files", HTTP_GET, [this] { handleList(); });
    s.on("/api/file", HTTP_GET, [this] { handleDownload(); });
    s.on("/api/file", HTTP_DELETE, [this] { handleDelete(); });
    s.on("/api/file", HTTP_POST, [this] { handleUploadDone(); }, [this] { handleUploadChunk(); });
    s.onNotFound([this] { fail(404, "not found"); });
    s.begin();

    if (log_) log_->printf("[fileshare] serving %s on port %u\n", cfg_.rootDir, cfg_.port);
    return true;
}

void FileShareServer::end() {
    if (server_) server_->stop();
    server_.reset();
    if (up_.file) up_.file.close();
}

void FileShareServer::loop() {
    if (server_) server_->handleClient();
}

size_t FileShareServer::usedBytes() {
    size_t total = 0;
    File dir = fs_->open(cfg_.rootDir);
    if (!dir) return 0;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (!f.isDirectory()) total += f.size();
        f.close();
    }
    dir.close();
    return total;
}

// --- Helpers -------------------------------------------------------------------

bool FileShareServer::authorized() {
    if (!cfg_.user || !cfg_.password) return true;
    if (server_->authenticate(cfg_.user, cfg_.password)) return true;
    server_->requestAuthentication(BASIC_AUTH, cfg_.title);
    return false;
}

String FileShareServer::pathFor(const String& name) const { return String(cfg_.rootDir) + "/" + name; }

String FileShareServer::tempPath() const { return String(cfg_.rootDir) + "/.upload.part"; }

void FileShareServer::fail(int status, const char* msg) { server_->send(status, "text/plain", msg); }

// --- Handlers ------------------------------------------------------------------

void FileShareServer::handleUi() {
    if (!authorized()) return;
    server_->send_P(200, "text/html", kUiHtml);
}

void FileShareServer::handleList() {
    if (!authorized()) return;
    // Names are restricted to [A-Za-z0-9._-], so they need no JSON escaping.
    // Files that don't match (put there by other code) are not listed.
    String json = "{\"files\":[";
    size_t used = 0;
    bool first = true;
    File dir = fs_->open(cfg_.rootDir);
    if (dir) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            if (!f.isDirectory()) {
                used += f.size();
                // name() is the bare filename on arduino-esp32 2.x and 3.x.
                const String name = f.name();
                if (isSafeName(name, cfg_.maxNameLen)) {
                    if (!first) json += ',';
                    first = false;
                    json += "{\"name\":\"" + name + "\",\"size\":" + String(static_cast<unsigned>(f.size())) + "}";
                }
            }
            f.close();
        }
        dir.close();
    }
    String title = cfg_.title;
    title.replace("\\", "\\\\");
    title.replace("\"", "\\\"");
    json += "],\"used\":" + String(static_cast<unsigned>(used)) +
            ",\"maxFile\":" + String(static_cast<unsigned>(cfg_.maxFileBytes)) +
            ",\"maxTotal\":" + String(static_cast<unsigned>(cfg_.maxTotalBytes)) +
            ",\"upload\":" + (cfg_.allowUpload ? "true" : "false") +
            ",\"delete\":" + (cfg_.allowDelete ? "true" : "false") +
            ",\"title\":\"" + title + "\"}";
    server_->send(200, "application/json", json);
}

void FileShareServer::handleDownload() {
    if (!authorized()) return;
    const String name = server_->arg("name");
    if (!isSafeName(name, cfg_.maxNameLen)) return fail(400, "bad filename");
    File f = fs_->open(pathFor(name), FILE_READ);
    if (!f || f.isDirectory()) return fail(404, "not found");
    server_->sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    server_->streamFile(f, contentTypeFor(name));
    f.close();
}

void FileShareServer::handleDelete() {
    if (!authorized()) return;
    if (!cfg_.allowDelete) return fail(403, "delete disabled");
    const String name = server_->arg("name");
    if (!isSafeName(name, cfg_.maxNameLen)) return fail(400, "bad filename");
    if (!fs_->exists(pathFor(name))) return fail(404, "not found");
    if (!fs_->remove(pathFor(name))) return fail(500, "delete failed");
    if (log_) log_->printf("[fileshare] deleted %s\n", name.c_str());
    if (onEvent_) onEvent_(Event::Deleted, name, 0);
    server_->send(200, "text/plain", "deleted");
}

// The server keeps feeding chunks after an error; once status is set they are
// ignored and the final handler reports it.
void FileShareServer::abortUpload(int status, const char* msg) {
    if (up_.file) up_.file.close();
    fs_->remove(tempPath());
    up_.status = status;
    up_.error = msg;
}

void FileShareServer::handleUploadChunk() {
    HTTPUpload& u = server_->upload();
    switch (u.status) {
        case UPLOAD_FILE_START: {
            up_ = Upload();
            if (cfg_.user && cfg_.password && !server_->authenticate(cfg_.user, cfg_.password)) {
                up_.status = 401;
                return;
            }
            if (!cfg_.allowUpload) return abortUpload(403, "upload disabled");
            up_.name = sanitizeName(u.filename, cfg_.maxNameLen);
            if (up_.name.isEmpty()) return abortUpload(400, "bad filename");
            if (fs_->exists(pathFor(up_.name))) return abortUpload(409, "a file with that name already exists");
            const size_t used = usedBytes();
            if (used >= cfg_.maxTotalBytes) return abortUpload(507, "share is full");
            up_.budget = min(cfg_.maxFileBytes, cfg_.maxTotalBytes - used);
            up_.file = fs_->open(tempPath(), FILE_WRITE);
            if (!up_.file) return abortUpload(500, "cannot open file");
            up_.status = 200;
            break;
        }
        case UPLOAD_FILE_WRITE:
            if (up_.status != 200) return;
            if (up_.written + u.currentSize > up_.budget) {
                return abortUpload(413, up_.budget < cfg_.maxFileBytes ? "not enough space left in share"
                                                                     : "file too large");
            }
            if (up_.file.write(u.buf, u.currentSize) != u.currentSize) return abortUpload(507, "storage full");
            up_.written += u.currentSize;
            break;
        case UPLOAD_FILE_END:
            if (up_.status != 200) return;
            up_.file.close();
            if (!fs_->rename(tempPath(), pathFor(up_.name))) return abortUpload(500, "cannot store file");
            if (log_) log_->printf("[fileshare] stored %s (%u bytes)\n", up_.name.c_str(), static_cast<unsigned>(up_.written));
            if (onEvent_) onEvent_(Event::Uploaded, up_.name, up_.written);
            break;
        case UPLOAD_FILE_ABORTED:
            abortUpload(400, "upload aborted");
            break;
    }
}

void FileShareServer::handleUploadDone() {
    if (up_.status == 401) {
        server_->requestAuthentication(BASIC_AUTH, cfg_.title);
    } else if (up_.status == 0) {
        fail(400, "expected multipart form field \"file\"");
    } else if (up_.status != 200) {
        fail(up_.status, up_.error.c_str());
    } else {
        server_->send(200, "application/json",
                      "{\"name\":\"" + up_.name + "\",\"size\":" + String(static_cast<unsigned>(up_.written)) + "}");
    }
    up_ = Upload();
}
