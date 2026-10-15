# CLI command coverage inventory

`CommandCoverageGateTest.sh` treats `src/commands/Dispatch.cpp` as the source
of truth for executable commands and checks that the dispatcher contains
exactly the frozen release surface. A command must have one row.

| Command      | Help | Dispatcher        | Implementation       | Aliases                  | Class            | Status    | Contract                    |
| ------------ | ---- | ----------------- | -------------------- | ------------------------ | ---------------- | --------- | --------------------------- |
| `add`        | main | yes               | AddCommand           | —                        | PROJECT_MUTATION | UNCOVERED | —                           |
| `build`      | main | yes               | BuildCommand         | —                        | LOCAL_WRITE      | PASS      | vix_cli_build_core_contract |
| `dev`        | main | yes               | DevCommand           | —                        | LONG_RUNNING     | PASS      | vix_cli_dev_command_forwarding_tests |
| `init`       | no   | yes               | InitCommand          | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `install`    | main | yes               | InstallCommand       | —                        | PROJECT_MUTATION | UNCOVERED | —                           |
| `new`        | main | yes               | NewCommand           | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `publish`    | main | yes               | PublishCommand       | —                        | NETWORK          | UNCOVERED | —                           |
| `remove`     | main | yes               | RemoveCommand        | —                        | PROJECT_MUTATION | UNCOVERED | —                           |
| `repl`       | main | yes               | ReplCommand          | default when no args     | LONG_RUNNING     | UNCOVERED | —                           |
| `run`        | main | yes               | RunCommand           | implicit `.cpp`          | LOCAL_WRITE      | PASS      | vix_cli_run_core_contract   |
| `test`       | main | yes               | TestsCommand         | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `update`     | main | yes               | UpdateCommand        | —                        | PROJECT_MUTATION | UNCOVERED | —                           |
| `help`       | main | CLI special route | CLI::help            | `-h`, `--help`           | READ_ONLY_LOCAL  | PASS      | vix_cli_global_contract     |
| `version`    | main | CLI special route | CLI::version         | `-v`, `--version`        | READ_ONLY_LOCAL  | PASS      | vix_cli_global_contract     |

The command gate also checks that removed commands are absent from registration,
help routing, and top-level help. `help` and `version` remain CLI infrastructure
routes rather than entries in the canonical command dispatcher.
