# Public command option contract matrix

`PublicOptionCoverageTest.sh` extracts every long option from the live help
output and requires a row below. `PASS` means the linked contract test passes;
`FAIL` is a deliberately visible product regression; `UNAVAILABLE` requires an
external service/toolchain and is not relabelled as a passing behavior.

| Command | Option                   | Contract test                     | Level | Status      |
| ------- | ------------------------ | --------------------------------- | ----- | ----------- |
| run     | `--dir`                  | RunCore + existing project tests  | B     | PASS        |
| run     | `--preset`               | existing project tests            | B     | PASS        |
| run     | `--run-preset`           | existing project tests            | B     | PASS        |
| run     | `--jobs`                 | existing project tests            | B     | PASS        |
| run     | `--clean`                | existing project tests            | C     | PASS        |
| run     | `--check`                | existing project tests            | C     | PASS        |
| run     | `--replay`               | existing replay tests             | C     | PASS        |
| run     | `--cwd`                  | RunCore                           | C     | PASS        |
| run     | `--env`                  | RunCore                           | C     | PASS        |
| run     | `--args`                 | RunCore                           | C     | PASS        |
| run     | `--run`                  | RunCore                           | C     | PASS        |
| run     | `--watch`                | existing watch tests              | C     | PASS        |
| run     | `--reload`               | existing watch tests              | B     | PASS        |
| run     | `--force-server`         | existing run tests                | B     | PASS        |
| run     | `--force-script`         | RunCore                           | B     | PASS        |
| run     | `--ui`                   | existing run tests                | B     | PASS        |
| run     | `--no-ui`                | existing run tests                | B     | PASS        |
| run     | `--env-hint`             | existing run tests                | C     | PASS        |
| run     | `--no-env-hint`          | existing run tests                | C     | PASS        |
| run     | `--trace-cache`          | RunCore, RunSingleCppCacheCliTest | C     | PASS        |
| run     | `--no-trace-cache`       | RunSingleCppCacheCliTest          | B     | PASS        |
| run     | `--compiler-fingerprint` | RunCore                           | A/C   | PASS        |
| run     | `--dep`                  | RunScriptDependencyPathTest       | C     | PASS        |
| run     | `--save`                 | RunScriptDependencyPathTest       | C     | PASS        |
| run     | `--auto-deps`            | RunCore                           | A     | PASS        |
| run     | `--san`                  | existing script tests             | B     | PASS        |
| run     | `--no-san`               | RunCore                           | C     | PASS        |
| run     | `--ubsan`                | existing script tests             | B     | PASS        |
| run     | `--tsan`                 | existing script tests             | B     | PASS        |
| run     | `--with-sqlite`          | existing dependency tests         | B     | PASS        |
| run     | `--with-mysql`           | existing dependency tests         | B     | PASS        |
| run     | `--local-cache`          | RunSingleCppCacheCliTest          | C     | PASS        |
| run     | `--docs`                 | RunCore                           | A     | PASS        |
| run     | `--no-docs`              | RunCore                           | A     | PASS        |
| run     | `--clear`                | RunCore                           | A     | PASS        |
| run     | `--no-clear`             | RunCore                           | B     | PASS        |
| run     | `--log-level`            | RunCore                           | A     | PASS        |
| run     | `--verbose`              | RunCore                           | B     | PASS        |
| run     | `--quiet`                | RunCore                           | B     | PASS        |
| run     | `--log-format`           | RunCore                           | A     | PASS        |
| run     | `--log-color`            | RunCore                           | A     | PASS        |
| run     | `--no-color`             | RunCore                           | B     | PASS        |
| run     | `--help`                 | PublicOptionCoverageTest          | A     | PASS        |
| build   | `--dir`                  | BuildCore                         | C     | PASS        |
| build   | `--preset`               | BuildCore                         | C     | PASS        |
| build   | `--build-target`         | BuildCore                         | C     | PASS        |
| build   | `--jobs`                 | existing planning tests           | B     | PASS        |
| build   | `--clean`                | BuildCore                         | C     | PASS        |
| build   | `--watch`                | BuildWatchCliTest                 | C     | PASS        |
| build   | `--fast`                 | BuildCore                         | B     | PASS        |
| build   | `--explain`              | BuildPlanningCompatTest           | B     | PASS        |
| build   | `--warnings`             | BuildWatchCliTest                 | C     | PASS        |
| build   | `--warning-check`        | BuildCore                         | C     | PASS        |
| build   | `--sanitize`             | BuildCore                         | C     | PASS        |
| build   | `--san`                  | BuildCore                         | B     | PASS        |
| build   | `--asan`                 | BuildCore                         | B     | PASS        |
| build   | `--ubsan`                | BuildCore                         | B     | PASS        |
| build   | `--tsan`                 | BuildCore                         | B     | PASS        |
| build   | `--report`               | cloud endpoint required           | C     | UNAVAILABLE |
| build   | `--page`                 | BuildCore                         | A     | PASS        |
| build   | `--limit`                | BuildCore                         | A     | PASS        |
| build   | `--no-cache`             | BuildCore                         | C     | PASS        |
| build   | `--no-status`            | BuildProgressCliTest              | C     | PASS        |
| build   | `--no-up-to-date`        | BuildPlanningCompatTest           | C     | PASS        |
| build   | `--bin`                  | existing single-file tests        | C     | PASS        |
| build   | `--out`                  | existing single-file tests        | C     | PASS        |
| build   | `--launcher`             | BuildToolCliCompatTest            | A/C   | PASS        |
| build   | `--linker`               | BuildToolCliCompatTest            | A/C   | PASS        |
| build   | `--target`               | BuildCore, BuildToolCliCompatTest | C     | PASS        |
| build   | `--sysroot`              | BuildToolCliCompatTest            | B     | PASS        |
| build   | `--targets`              | BuildCore                         | C     | PASS        |
| build   | `--static`               | existing planning tests           | B     | PASS        |
| build   | `--with-sqlite`          | existing dependency tests         | B     | PASS        |
| build   | `--with-mysql`           | existing dependency tests         | B     | PASS        |
| build   | `--managed-sdk`          | SDK profile tests                 | C     | PASS        |
| build   | `--verbose`              | BuildProgressCliTest              | C     | PASS        |
| build   | `--debug`                | BuildToolCliCompatTest            | C     | PASS        |
| build   | `--debug-log`            | BuildToolCliCompatTest            | A/C   | PASS        |
| build   | `--log`                  | BuildCore                         | C     | PASS        |
| build   | `--cmake-verbose`        | BuildProgressCliTest              | C     | PASS        |
| build   | `--quiet`                | BuildProgressCliTest              | C     | PASS        |
| build   | `--help`                 | PublicOptionCoverageTest          | A     | PASS        |
| build   | `--graph-executor`       | BuildCore                         | A/C   | PASS        |
| build   | `--heartbeat`            | BuildCore                         | B     | PASS        |
| build   | `--no-heartbeat`         | BuildCore                         | B     | PASS        |
| dev     | `--dir`                  | DevProjectContractTest            | C     | PASS        |
| dev     | `--preset`               | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--run-preset`           | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--jobs`                 | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--clean`                | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--replay`               | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--cwd`                  | DevProjectContractTest            | C     | PASS        |
| dev     | `--env`                  | DevProjectContractTest            | C     | PASS        |
| dev     | `--args`                 | DevProjectContractTest            | C     | PASS        |
| dev     | `--run`                  | Dev single-C++ matrix             | C     | UNCOVERED   |
| dev     | `--watch`                | DevProjectContractTest            | C     | PASS        |
| dev     | `--reload`               | DevProjectContractTest            | C     | UNCOVERED   |
| dev     | `--force-server`         | Dev single-C++ matrix             | C     | UNCOVERED   |
| dev     | `--force-script`         | Dev single-C++ matrix             | C     | UNCOVERED   |
| dev     | `--auto-deps`            | Dev single-C++ matrix             | C     | UNCOVERED   |
| dev     | `--san`                  | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--ubsan`                | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--tsan`                 | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--with-sqlite`          | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--with-mysql`           | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--local-cache`          | Dev single-C++ matrix             | C     | UNCOVERED   |
| dev     | `--docs`                 | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--no-docs`              | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--clear`                | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--no-clear`             | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--log-level`            | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--verbose`              | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--quiet`                | DevProjectContractTest            | C     | PASS        |
| dev     | `--log-format`           | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--log-color`            | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--no-color`             | Dev option matrix                 | C     | UNCOVERED   |
| dev     | `--help`                 | DevOptionCoverageTest             | A     | PASS        |

## Parser-only public aliases and undocumented parser surface

Short aliases covered by their long-option tests: run `-d`, `-j`, `-q`, `-h`;
build `-d`, `-j`, `-v`, `-q`, `-h`. Equal-sign forms are covered where their
long form accepts a value. `run --dev-mode` and `run --loglevel` are parsed
but absent from help; they are intentionally listed as undocumented until the
public contract is decided.

The `RunVixAppContractTest` is covered but currently **FAIL** by design: it is
a success contract for a real `vix::App` program, not an expected-failure test.
