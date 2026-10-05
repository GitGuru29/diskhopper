import SwiftUI
import DiskhopperCore

struct ContentView: View {
    @EnvironmentObject private var model: DiskhopperModel
    @State private var confirmClean = false
    @State private var showCleanResult = false

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            scanHeader
                .padding(.bottom, 2)
            if model.isScanning {
                scanningView
            } else if let error = model.errorMessage {
                errorView(error)
            } else if model.scannedPath != nil {
                reportView
            } else {
                emptyView
            }
        }
        .padding(18)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .onDisappear { model.destroyHandles() }
        .confirmationDialog(
            "Move to Trash?",
            isPresented: $confirmClean,
            titleVisibility: .visible
        ) {
            Button(cleanConfirmLabel, role: model.permanentSafe ? .destructive : nil) {
                model.runClean()
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text(cleanConfirmMessage)
        }
        .alert("Cleaning complete", isPresented: $showCleanResult) {
            Button("OK") { model.scanDirectory() }
        } message: {
            if let stats = model.cleanStats {
                Text(cleanResultMessage(stats))
            }
        }
    }

    private var scanHeader: some View {
        HStack(spacing: 8) {
            TextField("Path", text: $model.path)
                .textFieldStyle(.roundedBorder)
                .frame(maxWidth: .infinity)
                .onSubmit { model.scanDirectory() }
            Button { model.pickFolder() } label: {
                Label("Browse…", systemImage: "folder")
            }
            Button {
                model.scanDirectory()
            } label: {
                Label("Scan", systemImage: "arrow.clockwise")
            }
            .keyboardShortcut("r", modifiers: .command)
        }
    }

    private var scanningView: some View {
        VStack(spacing: 12) {
            ProgressView()
                .controlSize(.large)
            Text("Scanning \(model.path)…")
                .foregroundStyle(.secondary)
            if model.timeMachineAvailable {
                Label("Time Machine backup detected — permanent delete enabled",
                      systemImage: "externaldrive.fill")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func errorView(_ message: String) -> some View {
        VStack(spacing: 10) {
            Label("Scan failed", systemImage: "exclamationmark.triangle")
                .font(.headline)
                .foregroundStyle(.red)
            Text(message)
                .font(.callout)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var emptyView: some View {
        VStack(spacing: 10) {
            Image(systemName: "externaldrive.badge.questionmark")
                .font(.system(size: 44))
                .foregroundStyle(.secondary)
            Text("Scan a directory to see where space goes.")
                .foregroundStyle(.secondary)
            Text("Nothing is deleted automatically. Clean moves REVIEW items to "
                 + "Trash; SAFE items stay in Trash too unless permanent "
                 + "deletion is enabled.")
                .font(.caption)
                .foregroundStyle(.tertiary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 420)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var reportView: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                summarySection
                divisionBar
                cleanableSection
                safetyFooter
            }
        }
    }

    private var summarySection: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(model.scannedPath ?? "").font(.headline)
                Spacer()
                Text(sizeText(model.totalAllocated))
                    .font(.title3.monospacedDigit())
            }
            HStack(spacing: 16) {
                stat("\(model.fileCount.formatted()) files",
                     "\(model.dirCount.formatted()) folders")
                if model.symlinkCount > 0 {
                    stat("\(model.symlinkCount.formatted()) symlinks",
                         "\(model.errorCount.formatted()) unreadable")
                }
                stat(sizeText(model.totalApparent), "apparent size")
            }
            .font(.callout)
            .foregroundStyle(.secondary)
        }
    }

    private func stat(_ primary: String, _ secondary: String) -> some View {
        VStack(alignment: .leading) {
            Text(primary).fontWeight(.medium).foregroundStyle(.primary)
            Text(secondary).foregroundStyle(.tertiary)
        }
    }

    private var divisionBar: some View {
        VStack(alignment: .leading, spacing: 6) {
            let total = max(model.safeBytes + model.reviewBytes + model.protectedBytes, 1)
            GeometryReader { proxy in
                HStack(spacing: 2) {
                    barSegment(color: .green,
                               width: CGFloat(Double(model.safeBytes) / Double(total)) * proxy.size.width,
                               count: model.safeBytes)
                    barSegment(color: .orange,
                               width: CGFloat(Double(model.reviewBytes) / Double(total)) * proxy.size.width,
                               count: model.reviewBytes)
                    barSegment(color: .gray,
                               width: CGFloat(Double(model.protectedBytes) / Double(total)) * proxy.size.width,
                               count: model.protectedBytes)
                }
                .frame(height: 12, alignment: .leading)
            }
            .frame(height: 12)
            HStack(spacing: 14) {
                legend("SAFE", sizeText(model.safeBytes), .green)
                legend("REVIEW", sizeText(model.reviewBytes), .orange)
                legend("PROTECTED", sizeText(model.protectedBytes), .gray)
            }
            .font(.caption)
        }
    }

    private func barSegment(color: Color, width: CGFloat, count: UInt64) -> some View {
        Group {
            if count > 0 {
                RoundedRectangle(cornerRadius: 4)
                    .fill(color)
                    .frame(width: max(width, 2), height: 12)
            } else {
                EmptyView()
            }
        }
    }

    private func legend(_ name: String, _ size: String, _ color: Color) -> some View {
        HStack(spacing: 4) {
            Circle().fill(color).frame(width: 8, height: 8)
            Text("\(name) \(size)").monospacedDigit()
        }
    }

    private var cleanableSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("Cleanable")
                    .font(.headline)
                Spacer()
                Text("\(model.cleanable.count) items · \(sizeText(model.cleanableBytes))")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
            if model.cleanable.isEmpty {
                Text("Nothing cleanable under this path.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            } else {
                let displayed = model.cleanable.prefix(60)
                LazyVStack(alignment: .leading, spacing: 2) {
                    ForEach(Array(displayed)) { row in
                        cleanableRow(row)
                    }
                }
                if model.cleanable.count > 60 {
                    Text("… and \(model.cleanable.count - 60) more")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                cleanControls
            }
        }
        .padding(12)
        .background(Color(nsColor: .controlBackgroundColor))
        .cornerRadius(10)
    }

    private func cleanableRow(_ row: CleanableRow) -> some View {
        HStack(spacing: 8) {
            Text(sizeText(row.size))
                .font(.callout.monospacedDigit())
                .frame(width: 80, alignment: .trailing)
            Circle()
                .fill(row.level == DhBucketSafe ? Color.green : Color.orange)
                .frame(width: 8, height: 8)
            Text(row.ruleName)
                .font(.callout)
                .fontWeight(.medium)
            Text(row.ruleId)
                .font(.caption)
                .foregroundStyle(.tertiary)
            Spacer()
            Text(abbreviate(row.path))
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
        }
        .padding(.vertical, 3)
    }

    private var cleanControls: some View {
        HStack(spacing: 14) {
            Toggle("REVIEW to Trash (\(model.reviewRows.count))",
                   isOn: $model.includeReview)
                .toggleStyle(.checkbox)
            Toggle("SAFE to Trash (\(model.safeRows.count))",
                   isOn: $model.includeSafe)
                .toggleStyle(.checkbox)
            if model.safeRows.count > 0 {
                Toggle("Permanently delete SAFE",
                       isOn: $model.permanentSafe)
                    .toggleStyle(.checkbox)
                    .disabled(!model.timeMachineAvailable)
            }
            Spacer()
            Button(cleanButtonLabel) {
                confirmClean = true
            }
            .disabled(model.isCleaning || !hasSelection)
        }
    }

    private var safetyFooter: some View {
        VStack(alignment: .leading, spacing: 3) {
            Label("REVIEW → always moved to Trash (recoverable).",
                  systemImage: "checkmark.shield")
            Label("SAFE → Trash by default; permanent delete requires a "
                  + "Time Machine backup destination.",
                  systemImage: "externaldrive.fill.badge.checkmark")
            Label("Protected paths and locked system data are refused at "
                  + "delete time.",
                  systemImage: "lock.shield")
        }
        .font(.caption)
        .foregroundStyle(.secondary)
    }

    private var hasSelection: Bool {
        (model.includeReview && !model.reviewRows.isEmpty)
            || (model.includeSafe && !model.safeRows.isEmpty)
    }

    private var selectedCount: Int {
        (model.includeReview ? model.reviewRows.count : 0)
            + (model.includeSafe ? model.safeRows.count : 0)
    }

    private var selectedBytes: UInt64 {
        let review = model.includeReview
            ? model.reviewRows.reduce(0) { $0 + $1.size } : 0
        let safe = model.includeSafe
            ? model.safeRows.reduce(0) { $0 + $1.size } : 0
        return review + safe
    }

    private var cleanButtonLabel: String {
        "Move \(selectedCount) items (\(sizeText(selectedBytes))) to Trash"
    }

    private var cleanConfirmLabel: String {
        if model.permanentSafe && model.includeSafe {
            return "Move to Trash (permanently delete SAFE)"
        }
        return "Move to Trash"
    }

    private var cleanConfirmMessage: String {
        var parts: [String] = []
        if model.includeReview {
            parts.append("\(model.reviewRows.count) REVIEW items "
                         + "(\(sizeText(selectedBytesOf(model.reviewRows)))) to Trash")
        }
        if model.includeSafe {
            let action = model.permanentSafe ? "permanently delete" : "move to Trash"
            parts.append("\(model.safeRows.count) SAFE items "
                         + "(\(sizeText(selectedBytesOf(model.safeRows)))) "
                         + "\(action)")
        }
        return parts.joined(separator: "; ") + ". This cannot be undone."
    }

    private func cleanResultMessage(_ stats: CleanStats) -> String {
        var text: String
        if stats.succeeded == stats.planned {
            text = "Moved \(stats.succeeded) items "
                + "(\(sizeText(stats.bytesFreed))) to Trash."
        } else {
            text = "\(stats.succeeded) of \(stats.planned) moved "
                + "(\(sizeText(stats.bytesFreed)))."
            if stats.failed > 0 {
                text += "\n\(stats.failed) failed (\(sizeText(stats.bytesFailed)))"
                if !stats.firstError.isEmpty {
                    text += "\nFirst error: \(stats.firstError)"
                }
            }
        }
        return text
    }

    private func selectedBytesOf(_ rows: [CleanableRow]) -> UInt64 {
        rows.reduce(0) { $0 + $1.size }
    }

    private func sizeText(_ bytes: UInt64) -> String {
        bytes.formatted(.byteCount(style: .file))
    }

    private func abbreviate(_ path: String) -> String {
        let home = NSHomeDirectory()
        if path.hasPrefix(home) {
            return "~" + path.dropFirst(home.count)
        }
        return path
    }
}