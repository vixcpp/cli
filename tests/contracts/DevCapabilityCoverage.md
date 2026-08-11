# Dev capability coverage

| Dimension | Capability | Contract | Status |
| --- | --- | --- | --- |
| target | project / DevSession | DevProjectContractTest | PASS |
| target | single C++ / separate watcher | — | UNCOVERED |
| change | source/header including `.inl` | DevProjectContractTest | PASS |
| change | ignored documentation | DevProjectContractTest | PASS |
| lifecycle | initial build, restart, SIGTERM | DevProjectContractTest | PASS |
| runtime | args, env, cwd persist over restart | DevProjectContractTest | PASS |
| performance | one `vix build` per logical reload | DevProjectContractTest | PASS |
| platform | project watcher on Windows | DevSession implementation | UNAVAILABLE |
