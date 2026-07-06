# Implementation Tracking (Checklist & Process Log)

Use checklists and process logs to track implementation work.

## Directory Structure

```
docs/
  templates/                  # All templates
    implementation_checklist_TEMPLATE.md
    process_log_TEMPLATE.md
  process_log/                # Process logs (dedicated directory)
    INDEX.md                  # Index of all process logs
    YYYY-MM-DD-feature-name-log.md
    YYYY-MM-DD-feature-name-log-02.md  # Split file (when log exceeds 100 lines)
  archive/                    # Archived old checklists & process logs
  YYYY-MM-DD-feature-name-checklist.md   # Checklists (docs/ root)
```

## Usage Rules

1. **When starting implementation**: Create files
   - Checklist: `docs/YYYY-MM-DD-feature-name-checklist.md` (see `docs/templates/implementation_checklist_TEMPLATE.md`)
   - Process log: `docs/process_log/YYYY-MM-DD-feature-name-log.md` (see `docs/templates/process_log_TEMPLATE.md`)
   - List all tasks as `- [ ]` items, add a link to the corresponding process log
   - Add the new log to `docs/process_log/INDEX.md`

2. **When executing any action** (build, test, git operation, file move, refactoring, etc.): **Always** log to the process log
   - Find the relevant existing process log in `docs/process_log/`
   - If no relevant log exists, create a new one following the naming convention `YYYY-MM-DD-feature-name-log.md` and add it to `docs/process_log/INDEX.md`
   - Append an entry recording: DateTime, Activity, Result, Evidence (command output, error messages, etc.)
   - This applies to **all** actions, not just implementation tasks — including file operations, archive moves, configuration changes, and investigations

3. **When a task is completed**: **Always** do the following
   - Update the corresponding checklist item to `- [x]`
   - Append a completion entry to the process log (record DateTime, Activity, Result, Evidence)

4. **When changing approach or adjusting scope**: Always record the rationale in the process log

5. **When all tasks are completed**: Verify all checklist items are `- [x]` and append a completion entry to the process log

6. **Archiving**: When `docs/` root becomes cluttered, move completed (all `- [x]`) checklists and their corresponding process logs to `docs/archive/`

7. **File splitting**: Process log files must be kept concise
   - When a log file exceeds **100 lines**, create a new split file with suffix `-02`, `-03`, etc.
   - Example: `2026-03-11-feature-log.md` → `2026-03-11-feature-log-02.md`
   - Add the split file to `INDEX.md`
   - Each split file should include a header linking to the previous/next parts

## Referencing Process Logs

- **When implementation intent is unclear**: Before asking the user, first check `docs/process_log/INDEX.md` and read the relevant process log to understand the rationale and context behind the implementation
- Process logs record *why* decisions were made, not just *what* was done — use them to understand design intent, scope changes, and rejected alternatives
