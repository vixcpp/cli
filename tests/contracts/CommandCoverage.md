# CLI command coverage inventory

`CommandCoverageGateTest.sh` treats `src/commands/Dispatch.cpp` as the source
of truth for executable commands. A command must have one row. `run` and
`build` are covered by their existing dedicated suites; every other command is
explicitly classified until its real-effect contract is added.

| Command      | Help | Dispatcher        | Implementation       | Aliases                  | Class            | Status    | Contract                    |
| ------------ | ---- | ----------------- | -------------------- | ------------------------ | ---------------- | --------- | --------------------------- |
| `add`        | main | yes               | AddCommand           | —                        | PROJECT_MUTATION | UNCOVERED | —                           |
| `build`      | main | yes               | BuildCommand         | —                        | LOCAL_WRITE      | PASS      | vix_cli_build_core_contract |
| `cache`      | main | yes               | CacheCommand         | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `check`      | main | yes               | CheckCommand         | —                        | READ_ONLY_LOCAL  | UNCOVERED | —                           |
| `clean`      | main | yes               | CleanCommand         | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `completion` | no   | yes               | CompletionCommand    | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `deps`       | no   | yes               | InstallCommand       | deprecated install       | PROJECT_MUTATION | UNCOVERED | —                           |
| `doctor`     | main | yes               | DoctorCommand        | —                        | READ_ONLY_LOCAL  | UNCOVERED | —                           |
| `dev`        | main | yes               | DevCommand           | —                        | LONG_RUNNING     | PASS      | vix_cli_dev_command_forwarding_tests |
| `fmt`        | main | yes               | FmtCommand           | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `i`          | no   | yes               | InstallCommand       | install                  | PROJECT_MUTATION | UNCOVERED | —                           |
| `info`       | main | yes               | InfoCommand          | —                        | READ_ONLY_LOCAL  | UNCOVERED | —                           |
| `init`       | no   | yes               | InitCommand          | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `install`    | main | yes               | InstallCommand       | `i`, `deps`              | PROJECT_MUTATION | UNCOVERED | —                           |
| `list`       | main | yes               | ListCommand          | —                        | READ_ONLY_LOCAL  | UNCOVERED | —                           |
| `login`      | main | yes               | CloudCommand::login  | —                        | NETWORK          | UNCOVERED | —                           |
| `logout`     | main | yes               | CloudCommand::logout | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `new`        | main | yes               | NewCommand           | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `outdated`   | main | yes               | OutdatedCommand      | —                        | NETWORK          | UNCOVERED | —                           |
| `pack`       | main | yes               | PackCommand          | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `publish`    | main | yes               | PublishCommand       | —                        | NETWORK          | UNCOVERED | —                           |
| `registry`   | main | yes               | RegistryCommand      | —                        | NETWORK          | UNCOVERED | —                           |
| `remove`     | main | yes               | RemoveCommand        | —                        | PROJECT_MUTATION | UNCOVERED | —                           |
| `repl`       | main | yes               | ReplCommand          | default when no args     | LONG_RUNNING     | UNCOVERED | —                           |
| `reset`      | main | yes               | ResetCommand         | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `run`        | main | yes               | RunCommand           | implicit `.cpp`/`.vix`   | LOCAL_WRITE      | PASS      | vix_cli_run_core_contract   |
| `search`     | main | yes               | SearchCommand        | —                        | NETWORK          | UNCOVERED | —                           |
| `store`      | main | yes               | StoreCommand         | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `test`       | main | yes               | TestsCommand         | tests                    | LOCAL_WRITE      | UNCOVERED | —                           |
| `tests`      | no   | yes               | TestsCommand         | test                     | LOCAL_WRITE      | UNCOVERED | —                           |
| `uninstall`  | main | yes               | UninstallCommand     | —                        | SYSTEM           | UNCOVERED | —                           |
| `unpublish`  | main | yes               | UnpublishCommand     | registry unpublish route | NETWORK          | UNCOVERED | —                           |
| `up`         | no   | yes               | UpdateCommand        | update                   | PROJECT_MUTATION | UNCOVERED | —                           |
| `update`     | main | yes               | UpdateCommand        | up                       | PROJECT_MUTATION | UNCOVERED | —                           |
| `upgrade`    | main | yes               | UpgradeCommand       | —                        | SYSTEM           | UNCOVERED | —                           |
| `verify`     | main | yes               | VerifyCommand        | —                        | READ_ONLY_LOCAL  | UNCOVERED | —                           |
| `help`       | main | CLI special route | CLI::help            | `-h`, `--help`           | READ_ONLY_LOCAL  | PASS      | vix_cli_global_contract     |
| `version`    | main | CLI special route | CLI::version         | `-v`, `--version`        | READ_ONLY_LOCAL  | PASS      | vix_cli_global_contract     |

## Drift recorded during initial inventory

- `init` and `completion` are dispatched and implement `help()`, but are not
  displayed in the top-level help.
- `tests`, `i`, `deps`, and `up` are dispatch aliases intentionally omitted
  from the top-level help. `deps` is deprecated and has a runtime warning.
- Application-domain and rejected generic-looking commands are intentionally
  absent from the active dispatcher; `CommandCoverageGateTest.sh` guards that
  negative surface explicitly.
- `CLI::run()` executes the `vix::cli::dispatch::global()` registry. Its help
  output is maintained separately and covered by the same surface gate.
