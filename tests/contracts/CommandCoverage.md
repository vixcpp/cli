# CLI command coverage inventory

`CommandCoverageGateTest.sh` treats `src/commands/Dispatch.cpp` as the source
of truth for executable commands. A command must have one row. `run` and
`build` are covered by their existing dedicated suites; every other command is
explicitly classified until its real-effect contract is added.

| Command      | Help | Dispatcher        | Implementation       | Aliases                  | Class            | Status    | Contract                    |
| ------------ | ---- | ----------------- | -------------------- | ------------------------ | ---------------- | --------- | --------------------------- |
| `add`        | main | yes               | AddCommand           | —                        | PROJECT_MUTATION | UNCOVERED | —                           |
| `agent`      | main | yes               | AgentCommand         | —                        | LONG_RUNNING     | UNCOVERED | —                           |
| `build`      | main | yes               | BuildCommand         | —                        | LOCAL_WRITE      | PASS      | vix_cli_build_core_contract |
| `cache`      | main | yes               | CacheCommand         | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `check`      | main | yes               | CheckCommand         | —                        | READ_ONLY_LOCAL  | UNCOVERED | —                           |
| `clean`      | main | yes               | CleanCommand         | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `cloud`      | main | yes               | CloudCommand         | —                        | NETWORK          | UNCOVERED | —                           |
| `completion` | no   | yes               | CompletionCommand    | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `db`         | main | yes               | DbCommand            | —                        | PROJECT_MUTATION | UNCOVERED | —                           |
| `deploy`     | main | yes               | DeployCommand        | —                        | SYSTEM           | UNCOVERED | —                           |
| `deps`       | no   | yes               | InstallCommand       | deprecated install       | PROJECT_MUTATION | UNCOVERED | —                           |
| `desktop`    | main | yes               | DesktopCommand       | —                        | LONG_RUNNING     | UNCOVERED | —                           |
| `dev`        | main | yes               | DevCommand           | —                        | LONG_RUNNING     | UNCOVERED | —                           |
| `doctor`     | main | yes               | DoctorCommand        | `doctor --cloud` route   | READ_ONLY_LOCAL  | UNCOVERED | —                           |
| `env`        | main | yes               | EnvCommand           | —                        | READ_ONLY_LOCAL  | UNCOVERED | —                           |
| `fmt`        | main | yes               | FmtCommand           | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `game`       | main | yes               | GameCommand          | —                        | LONG_RUNNING     | UNCOVERED | —                           |
| `health`     | main | yes               | HealthCommand        | —                        | NETWORK          | UNCOVERED | —                           |
| `i`          | no   | yes               | InstallCommand       | install                  | PROJECT_MUTATION | UNCOVERED | —                           |
| `info`       | main | yes               | InfoCommand          | —                        | READ_ONLY_LOCAL  | UNCOVERED | —                           |
| `init`       | no   | yes               | InitCommand          | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `install`    | main | yes               | InstallCommand       | `i`, `deps`              | PROJECT_MUTATION | UNCOVERED | —                           |
| `list`       | main | yes               | ListCommand          | —                        | READ_ONLY_LOCAL  | UNCOVERED | —                           |
| `login`      | main | yes               | CloudCommand::login  | —                        | NETWORK          | UNCOVERED | —                           |
| `logout`     | main | yes               | CloudCommand::logout | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `logs`       | main | yes               | LogsCommand          | —                        | READ_ONLY_LOCAL  | UNCOVERED | —                           |
| `make`       | main | yes               | MakeCommand          | `make:<target>`          | LOCAL_WRITE      | UNCOVERED | —                           |
| `mobile`     | main | yes               | MobileCommand        | —                        | SYSTEM           | UNCOVERED | —                           |
| `modules`    | main | yes               | ModulesCommand       | —                        | PROJECT_MUTATION | UNCOVERED | —                           |
| `new`        | main | yes               | NewCommand           | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `note`       | main | yes               | NoteCommand          | —                        | LONG_RUNNING     | UNCOVERED | —                           |
| `orm`        | main | yes               | OrmCommand           | —                        | PROJECT_MUTATION | UNCOVERED | —                           |
| `outdated`   | main | yes               | OutdatedCommand      | —                        | NETWORK          | UNCOVERED | —                           |
| `p2p`        | main | yes               | P2PCommand           | —                        | LONG_RUNNING     | UNCOVERED | —                           |
| `pack`       | main | yes               | PackCommand          | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `production` | main | yes               | ProductionCommand    | —                        | READ_ONLY_LOCAL  | UNCOVERED | —                           |
| `proxy`      | main | yes               | ProxyCommand         | —                        | SYSTEM           | UNCOVERED | —                           |
| `publish`    | main | yes               | PublishCommand       | —                        | NETWORK          | UNCOVERED | —                           |
| `registry`   | main | yes               | RegistryCommand      | —                        | NETWORK          | UNCOVERED | —                           |
| `remove`     | main | yes               | RemoveCommand        | —                        | PROJECT_MUTATION | UNCOVERED | —                           |
| `repl`       | main | yes               | ReplCommand          | default when no args     | LONG_RUNNING     | UNCOVERED | —                           |
| `replay`     | main | yes               | ReplayCommand        | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `reset`      | main | yes               | ResetCommand         | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `run`        | main | yes               | RunCommand           | implicit `.cpp`/`.vix`   | LOCAL_WRITE      | PASS      | vix_cli_run_core_contract   |
| `search`     | main | yes               | SearchCommand        | —                        | NETWORK          | UNCOVERED | —                           |
| `service`    | main | yes               | ServiceCommand       | —                        | SYSTEM           | UNCOVERED | —                           |
| `store`      | main | yes               | StoreCommand         | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `task`       | main | yes               | TaskCommand          | —                        | LOCAL_WRITE      | UNCOVERED | —                           |
| `test`       | main | yes               | TestsCommand         | tests                    | LOCAL_WRITE      | UNCOVERED | —                           |
| `tests`      | no   | yes               | TestsCommand         | test                     | LOCAL_WRITE      | UNCOVERED | —                           |
| `uninstall`  | main | yes               | UninstallCommand     | —                        | SYSTEM           | UNCOVERED | —                           |
| `unpublish`  | main | yes               | UnpublishCommand     | registry unpublish route | NETWORK          | UNCOVERED | —                           |
| `up`         | no   | yes               | UpdateCommand        | update                   | PROJECT_MUTATION | UNCOVERED | —                           |
| `update`     | main | yes               | UpdateCommand        | up                       | PROJECT_MUTATION | UNCOVERED | —                           |
| `upgrade`    | main | yes               | UpgradeCommand       | —                        | SYSTEM           | UNCOVERED | —                           |
| `verify`     | main | yes               | VerifyCommand        | —                        | READ_ONLY_LOCAL  | UNCOVERED | —                           |
| `ws`         | main | yes               | WsCommand            | —                        | NETWORK          | UNCOVERED | —                           |
| `help`       | main | CLI special route | CLI::help            | `-h`, `--help`           | READ_ONLY_LOCAL  | PASS      | vix_cli_global_contract     |
| `version`    | main | CLI special route | CLI::version         | `-v`, `--version`        | READ_ONLY_LOCAL  | PASS      | vix_cli_global_contract     |

## Drift recorded during initial inventory

- `init` and `completion` are dispatched and implement `help()`, but are not
  displayed in the top-level help.
- `tests`, `i`, `deps`, and `up` are dispatch aliases intentionally omitted
  from the top-level help. `deps` is deprecated and has a runtime warning.
- `make:<target>` is a dispatcher-recognized command form delegated to `make`.
- `CLI::commands_` is an older direct command map; `CLI::run()` executes the
  `vix::cli::dispatch::global()` registry instead.
