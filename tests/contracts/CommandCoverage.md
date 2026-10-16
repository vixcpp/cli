# Current Vix command coverage

`REGISTERED` means an executable contract is registered in CTest; it does not
claim that the test passed in a particular build. `HELP_ONLY` means the command
has dispatch/help coverage but no dedicated operational contract yet. The
command gate also invokes `vix help <command>` for every canonical command.

| Command | Help | Dispatcher | Implementation | Aliases | Coverage | Contract |
| --- | --- | --- | --- | --- | --- | --- |
| `new` | main | yes | NewCommand | — | REGISTERED | vix_cli_native_new_project_tests |
| `init` | main | yes | InitCommand | — | REGISTERED | vix_cli_native_new_project_tests |
| `build` | main | yes | BuildCommand | — | REGISTERED | vix_cli_build_core_contract |
| `run` | main | yes | RunCommand | — | REGISTERED | vix_cli_run_core_contract |
| `dev` | main | yes | DevCommand | — | REGISTERED | vix_cli_dev_command_forwarding_tests |
| `test` | main | yes | TestsCommand | — | REGISTERED | vix_cli_tests_command_contract |
| `repl` | main | yes | ReplCommand | — | HELP_ONLY | vix_cli_command_coverage |
| `add` | main | yes | AddCommand | — | REGISTERED | vix_cli_mutation_command_contracts |
| `remove` | main | yes | RemoveCommand | — | REGISTERED | vix_cli_mutation_command_contracts |
| `update` | main | yes | UpdateCommand | — | REGISTERED | vix_cli_mutation_command_contracts |
| `install` | main | yes | InstallCommand | — | REGISTERED | vix_cli_install_reconciliation_contract |
| `publish` | main | yes | PublishCommand | — | REGISTERED | vix_cli_native_package_publication_tests |
| `upgrade` | main | yes | UpgradeCommand | — | REGISTERED | vix_cli_distribution_runtime_tests |

`help` and `version` are CLI infrastructure routes, not public commands. No
other dispatcher route is part of this checkpoint. `repl` remains the only
canonical command without a dedicated operation-level contract; its help and
dispatch route are checked by `vix_cli_command_coverage`.

The mutation CLI contract currently exercises failure boundaries; successful
dependency mutation is covered by the native package command/runtime tests.
Publication is covered at the package runtime layer, not with a live remote
publication smoke. Remote publication remains an explicit human/release check.
