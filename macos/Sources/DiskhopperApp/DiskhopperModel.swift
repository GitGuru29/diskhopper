import AppKit
import Foundation
import DiskhopperCore

typealias DhHandle = UnsafeMutableRawPointer

struct CleanableRow: Identifiable {
    let id: Int
    let path: String
    let ruleId: String
    let ruleName: String
    let level: Int32
    let size: UInt64
}

struct CleanStats {
    var planned: Int
    var succeeded: Int
    var failed: Int
    var bytesFreed: UInt64
    var bytesFailed: UInt64
    var firstError: String
}

@MainActor
final class DiskhopperModel: ObservableObject {
    @Published var path: String = NSHomeDirectory() + "/Downloads"
    @Published var isScanning = false
    @Published var isCleaning = false
    @Published var errorMessage: String?

    @Published var scannedPath: String?
    @Published var totalApparent: UInt64 = 0
    @Published var totalAllocated: UInt64 = 0
    @Published var fileCount: UInt64 = 0
    @Published var dirCount: UInt64 = 0
    @Published var symlinkCount: UInt64 = 0
    @Published var errorCount: UInt64 = 0

    @Published var safeBytes: UInt64 = 0
    @Published var reviewBytes: UInt64 = 0
    @Published var protectedBytes: UInt64 = 0

    @Published var cleanable: [CleanableRow] = []

    @Published var includeReview = true
    @Published var includeSafe = true
    @Published var permanentSafe = false

    @Published var cleanStats: CleanStats?

    private var engine: DhHandle?
    private var pendingScan: DhHandle?

    var timeMachineAvailable: Bool {
        dh_time_machine_available() != 0
    }

    init() {
        engine = dh_engine_create(NSHomeDirectory())
    }

    func rescan(_ target: String) {
        path = target
        scanDirectory()
    }

    func scanDirectory() {
        guard engine != nil else { return }
        isScanning = true
        errorMessage = nil
        cleanStats = nil
        let target = (path as NSString).expandingTildeInPath
        let handle = engine
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let scan = dh_scan(handle, target, 0) else {
                DispatchQueue.main.async {
                    self?.errorMessage = "scanning failed"
                    self?.isScanning = false
                }
                return
            }
            DispatchQueue.main.async {
                self?.apply(scan: scan)
            }
        }
    }

    func pickFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.prompt = "Scan"
        if panel.runModal() == .OK, let url = panel.url {
            scanDirectory(url.path)
        }
    }

    private func scanDirectory(_ target: String) {
        path = target
        scanDirectory()
    }

    private func apply(scan: DhHandle) {
        if let previous = pendingScan {
            dh_scan_destroy(previous)
        }
        pendingScan = scan

        let error = String(cString: dh_scan_error(scan))
        guard error.isEmpty else {
            errorMessage = error
            isScanning = false
            return
        }

        scannedPath = String(cString: dh_scan_root(scan))
        totalApparent = dh_scan_apparent(scan)
        totalAllocated = dh_scan_allocated(scan)
        fileCount = dh_scan_files(scan)
        dirCount = dh_scan_dirs(scan)
        symlinkCount = dh_scan_symlinks(scan)
        errorCount = dh_scan_errors(scan)
        safeBytes = dh_scan_bucket_bytes(scan, DhBucketSafe)
        reviewBytes = dh_scan_bucket_bytes(scan, DhBucketReview)
        protectedBytes = dh_scan_bucket_bytes(scan, DhBucketProtected)
        cleanable = parseCleanable(scan)
        isScanning = false
    }

    private func parseCleanable(_ scan: DhHandle) -> [CleanableRow] {
        var rows: [CleanableRow] = []
        for bucket in [DhBucketReview, DhBucketSafe] {
            let count = dh_scan_cleanable_count(scan, bucket)
            for index in 0..<count {
                guard let path = dh_scan_cleanable_path(scan, bucket, index),
                      let ruleId = dh_scan_cleanable_rule_id(scan, bucket, index),
                      let ruleName = dh_scan_cleanable_rule_name(scan, bucket, index)
                else { continue }
                rows.append(CleanableRow(
                    id: rows.count,
                    path: String(cString: path),
                    ruleId: String(cString: ruleId),
                    ruleName: String(cString: ruleName),
                    level: bucket,
                    size: dh_scan_cleanable_size(scan, bucket, index)
                ))
            }
        }
        rows.sort { $0.size > $1.size }
        return rows
    }

    var cleanableBytes: UInt64 {
        cleanable.reduce(0) { $0 + $1.size }
    }

    var reviewRows: [CleanableRow] {
        cleanable.filter { $0.level == DhBucketReview }
    }

    var safeRows: [CleanableRow] {
        cleanable.filter { $0.level == DhBucketSafe }
    }

    func runClean() {
        guard let scan = pendingScan, !cleanable.isEmpty else { return }
        isCleaning = true

        let auditDirectory = NSHomeDirectory() + "/.diskhopper"
        try? FileManager.default.createDirectory(
            atPath: auditDirectory, withIntermediateDirectories: true)
        let auditPath = auditDirectory + "/audit.log"

        let wantReview = includeReview ? 1 : 0
        let wantSafe = includeSafe ? 1 : 0
        let permanent = permanentSafe ? 1 : 0

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let result = dh_clean(scan, Int32(wantSafe), Int32(wantReview),
                                        Int32(permanent), auditPath)
            else {
                DispatchQueue.main.async {
                    self?.isCleaning = false
                    self?.cleanStats = CleanStats(planned: 0, succeeded: 0,
                                                  failed: 1, bytesFreed: 0,
                                                  bytesFailed: 0,
                                                  firstError: "cleaning failed")
                }
                return
            }
            let stats = CleanStats(
                planned: Int(dh_clean_planned(result)),
                succeeded: Int(dh_clean_succeeded(result)),
                failed: Int(dh_clean_failed(result)),
                bytesFreed: dh_clean_bytes_freed(result),
                bytesFailed: dh_clean_bytes_failed(result),
                firstError: String(cString: dh_clean_first_error(result))
            )
            dh_clean_destroy(result)
            DispatchQueue.main.async {
                self?.isCleaning = false
                self?.cleanStats = stats
            }
        }
    }

    func destroyHandles() {
        if let scan = pendingScan { pendingScan = nil; dh_scan_destroy(scan) }
        if let engine = engine { self.engine = nil; dh_engine_destroy(engine) }
    }
}