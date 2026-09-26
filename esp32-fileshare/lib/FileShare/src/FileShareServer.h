// HTTP file share over any Arduino fs::FS (LittleFS, SD, SD_MMC, FFat).
//
// Serves a small browser UI plus a JSON API from one flat directory: list,
// download, upload, delete. It does not manage WiFi; bring the network up
// yourself (or with FileShareWifi) and call loop() often.
//
//   GET    /                      browser UI
//   GET    /api/files             {"files":[{"name":..,"size":..}],"used":..,...}
//   GET    /api/file?name=X       download X
//   POST   /api/file              multipart upload, form field "file"
//   DELETE /api/file?name=X       delete X
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <WebServer.h>

#include <functional>
#include <memory>

struct FileShareConfig {
    // Directory on the filesystem that is shared. Created if missing.
    const char* rootDir = "/share";
    uint16_t port = 80;
    // HTTP Basic auth. Both null = open to anyone on the network. Basic auth
    // over plain HTTP is not encrypted; it only keeps casual visitors out.
    const char* user = nullptr;
    const char* password = nullptr;
    // Limits for "small files" sharing. Uploads over either are rejected.
    size_t maxFileBytes = 256 * 1024;
    size_t maxTotalBytes = 1024 * 1024;
    // Stored filenames are sanitised and capped at this length. Keep
    // strlen(rootDir) + 1 + maxNameLen under the filesystem's path limit
    // (LittleFS: 64 by default, SPIFFS: 32).
    uint8_t maxNameLen = 48;
    bool allowUpload = true;
    bool allowDelete = true;
    // Shown in the browser UI.
    const char* title = "File share";
};

class FileShareServer {
public:
    enum class Event { Uploaded, Deleted };
    // name is the stored (sanitised) filename; size is 0 for Deleted.
    using EventHandler = std::function<void(Event, const String& name, size_t size)>;

    // Starts the HTTP server. The filesystem must already be mounted.
    // Returns false if the share directory can't be created.
    bool begin(fs::FS& fs, const FileShareConfig& cfg = FileShareConfig(),
               Print* log = &Serial);
    void end();
    // Call from the main loop (or a periodic task). Handles at most one request.
    void loop();

    // Called after a file is stored or removed, e.g. to announce it elsewhere.
    void onEvent(EventHandler handler) { onEvent_ = std::move(handler); }

    // The underlying server, to register extra routes (FileShareWifi uses it
    // for its setup page). Null before begin().
    WebServer* server() { return server_.get(); }

    // Bytes used by files in the share directory.
    size_t usedBytes();

    // Turns an arbitrary client filename into a safe flat name, or "" if
    // nothing usable is left. Exposed so host code can store files that the
    // share will list.
    static String sanitizeName(const String& raw, uint8_t maxLen);
    static bool isSafeName(const String& name, uint8_t maxLen);

private:
    struct Upload {
        File file;
        String name;
        size_t written = 0;
        size_t budget = 0;  // bytes this upload may still use
        int status = 0;     // 0 = none seen yet, 200 = ok, else HTTP error
        String error;
    };

    bool authorized();
    String pathFor(const String& name) const;
    String tempPath() const;
    void fail(int status, const char* msg);
    void abortUpload(int status, const char* msg);

    void handleUi();
    void handleList();
    void handleDownload();
    void handleDelete();
    void handleUploadChunk();
    void handleUploadDone();

    fs::FS* fs_ = nullptr;
    FileShareConfig cfg_;
    Print* log_ = nullptr;
    std::unique_ptr<WebServer> server_;
    Upload up_;
    EventHandler onEvent_;
};
