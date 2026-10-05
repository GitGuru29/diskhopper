## v0.6: Scheduled auto-clean (macOS launchd)

Diskhopper enforces the same safety gates non-interactively: REVIEW → Trash, SAFE requires `--force` plus a Time Machine backup destination; protections are re-checked at delete time. Audit logs are appended when executing.

### CLI flags for automation
- `--json` — machine-readable output for scan/report/clean (dry-run emits a planned session)
- `--quiet` / `-q` — suppress human-readable output (errors still go to stderr)
- `--dry-run` — plan only; no changes
- `--yes` — required to actually execute

Example dry-run as JSON: `diskhopper clean --review --top 20 --dry-run --json ~`

### launchd (user agent) example
Create `~/Library/LaunchAgents/com.diskhopper.auto.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>com.diskhopper.auto</string>
  <key>ProgramArguments</key>
  <array>
    <string>/path/to/diskhopper/build/cli/diskhopper</string>
    <string>clean</string>
    <string>--review</string>
    <string>--top</string>
    <string>50</string>
    <string>--yes</string>
    <string>/Users/YOUR_USER</string>
  </array>
  <key>StartInterval</key>
  <integer>86400</integer> <!-- every 24h -->
  <key>RunAtLoad</key>
  <false/>
  <key>StandardOutPath</key>
  <string>/Users/YOUR_USER/Library/Logs/diskhopper-auto.log</string>
  <key>StandardErrorPath</key>
  <string>/Users/YOUR_USER/Library/Logs/diskhopper-auto.err</string>
</dict>
</plist>
```

Load/unload:
```bash
launchctl load ~/Library/LaunchAgents/com.diskhopper.auto.plist
launchctl unload ~/Library/LaunchAgents/com.diskhopper.auto.plist
```

Notes:
- Never include `--safe --force` in an automatic schedule unless you have a verified, always-present Time Machine backup and fully understand the impact.
- Keep the job non-interactive; gates still apply.
- JSON output is ideal for log ingestion/monitoring.