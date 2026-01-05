# =============================================================
# Vix App — Cross-platform build helper
# =============================================================
# Usage:
#   make build               → configure + build (ALL)
#   make run                 → build + run (target 'run')
#   make clean               → delete build folders
#   make rebuild             → full rebuild
#   make preset=name run     → override configure preset (ex: dev-msvc)
#   make BUILD_PRESET=name   → override build preset (ex: build-msvc)
# =============================================================

# ---------------- Base Shell ----------------
SHELL := /bin/bash
.ONESHELL:
.SHELLFLAGS := -eu -o pipefail -c

VERSION       ?= v0.1.0
BRANCH_DEV    ?= dev
BRANCH_MAIN   ?= main
REMOTE        ?= origin

PRESET        ?= dev-ninja
BUILD_PRESET  ?= $(PRESET)
ifeq ($(PRESET),dev-ninja)
  BUILD_PRESET := build-ninja
endif
ifeq ($(PRESET),dev-msvc)
  BUILD_PRESET := build-msvc
endif
RUN_PRESET ?= $(BUILD_PRESET)
ifeq ($(PRESET),dev-ninja)
  RUN_PRESET := run-ninja
endif
ifeq ($(PRESET),dev-msvc)
  RUN_PRESET := run-msvc
endif

CMAKE         ?= cmake

.PHONY: force_ssh_remote preflight ensure-branch ensure-clean commit push merge tag release \
        test changelog help build run clean rebuild preset \
        coverage publish-mods publish-mods-force

help:
	@echo "Targets:"
	@echo "  release VERSION=vX.Y.Z  Run full release flow (commit -> sync -> push/merge -> tag)"
	@echo "  commit                  Commit all changes on $(BRANCH_DEV)"
	@echo "  preflight               Sync branches with retries (fetch & rebase)"
	@echo "  push                    Push $(BRANCH_DEV) with retries"
	@echo "  merge                   Merge $(BRANCH_DEV) -> $(BRANCH_MAIN) and push with retries"
	@echo "  tag VERSION=vX.Y.Z      Create and push annotated tag"
	@echo "  test                    Run ctest if build/ exists, otherwise composer test"
	@echo "  changelog               Run scripts/update_changelog.sh if present"
	@echo "  build                   Configure + build (CMake preset: $(PRESET))"
	@echo "  run                     Build + run (uses run preset: $(RUN_PRESET))"
	@echo "  clean                   Remove build artifacts"
	@echo "  rebuild                 Full rebuild (clean + build)"
	@echo "  preset                  Placeholder target to override PRESET variable"

force_ssh_remote:
	@echo "🔐 Forcing SSH for GitHub remotes..."
	@git config --global url."git@github.com:".insteadOf https://github.com/
	@url="$$(git remote get-url $(REMOTE))"; \
	if [[ "$$url" =~ ^https://github.com/ ]]; then \
		new="$${url/https:\/\/github.com\//git@github.com:}"; \
		echo "🔁 Switching $(REMOTE) to $$new"; \
		git remote set-url $(REMOTE) "$$new"; \
	fi
	@echo "Remote $(REMOTE): $$(git remote get-url $(REMOTE))"
	@ssh -T git@github.com >/dev/null 2>&1 || true

ensure-branch:
	@if [ "$$(git rev-parse --abbrev-ref HEAD)" != "$(BRANCH_DEV)" ]; then \
		echo "❌ You must be on $(BRANCH_DEV) to run this target."; \
		exit 1; \
	fi

ensure-clean:
	@if [ -n "$$(git status --porcelain)" ]; then \
		echo "❌ Working tree not clean. Commit or stash first."; \
		git status --porcelain; \
		exit 1; \
	fi

preflight: force_ssh_remote
	@echo "🔎 Sync $(BRANCH_DEV) & $(BRANCH_MAIN) ..."
	@tries=0; until git fetch $(REMOTE); do \
		tries=$$((tries+1)); \
		if [ $$tries -ge 5 ]; then echo "❌ git fetch failed after $$tries tries"; exit 128; fi; \
		echo "⏳ Retry $$tries (fetch)..."; sleep 3; \
	done
	@git show-ref --verify --quiet refs/heads/$(BRANCH_DEV) || git branch $(BRANCH_DEV) $(REMOTE)/$(BRANCH_DEV) || true
	@git show-ref --verify --quiet refs/heads/$(BRANCH_MAIN) || git branch $(BRANCH_MAIN) $(REMOTE)/$(BRANCH_MAIN) || true

	@tries=0; until git checkout $(BRANCH_DEV) && git pull --rebase $(REMOTE) $(BRANCH_DEV); do \
		tries=$$((tries+1)); \
		if [ $$tries -ge 5 ]; then echo "❌ rebase $(BRANCH_DEV) failed after $$tries tries"; exit 128; fi; \
		echo "⏳ Retry $$tries (pull --rebase $(BRANCH_DEV))..."; sleep 3; \
	done

	@tries=0; until git checkout $(BRANCH_MAIN) && git pull --rebase $(REMOTE) $(BRANCH_MAIN); do \
		tries=$$((tries+1)); \
		if [ $$tries -ge 5 ]; then echo "❌ rebase $(BRANCH_MAIN) failed after $$tries tries"; exit 128; fi; \
		echo "⏳ Retry $$tries (pull --rebase $(BRANCH_MAIN))..."; sleep 3; \
	done

	@git checkout $(BRANCH_DEV)
	@echo "✅ Preflight sync OK"

commit: ensure-branch
	@if [ -n "$$(git status --porcelain)" ]; then \
		echo "📝 Committing changes..."; \
		git add -A; \
		git commit -m "chore(release): prepare $(VERSION)"; \
	else \
		echo "✅ Nothing to commit."; \
	fi

push: force_ssh_remote
	@tries=0; until git push $(REMOTE) $(BRANCH_DEV); do \
		tries=$$((tries+1)); \
		if [ $$tries -ge 5 ]; then echo "❌ push $(BRANCH_DEV) failed after $$tries tries"; exit 128; fi; \
		echo "⏳ Retry $$tries..."; sleep 3; \
	done

merge: force_ssh_remote
	git checkout $(BRANCH_MAIN)
	git merge --no-ff --no-edit $(BRANCH_DEV)
	@tries=0; until git push $(REMOTE) $(BRANCH_MAIN); do \
		tries=$$((tries+1)); \
		if [ $$tries -ge 5 ]; then echo "❌ push $(BRANCH_MAIN) failed after $$tries tries"; exit 128; fi; \
		echo "⏳ Retry $$tries..."; sleep 3; \
	done
	git checkout $(BRANCH_DEV)
	@echo "✅ Merge & push to $(BRANCH_MAIN) OK"

tag: force_ssh_remote
	@if ! [[ "$(VERSION)" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$$ ]]; then \
		echo "❌ VERSION must look like vX.Y.Z (got '$(VERSION)')"; exit 1; \
	fi
	@if git rev-parse -q --verify "refs/tags/$(VERSION)" >/dev/null; then \
		echo "❌ Tag $(VERSION) already exists."; exit 1; \
	fi
	@echo "🏷️  Creating annotated tag $(VERSION)..."
	git tag -a $(VERSION) -m "chore(release): $(VERSION)"
	@tries=0; until git push $(REMOTE) $(VERSION); do \
		tries=$$((tries+1)); \
		if [ $$tries -ge 5 ]; then echo "❌ push tag $(VERSION) failed after $$tries tries"; exit 128; fi; \
		echo "⏳ Retry $$tries..."; sleep 3; \
	done
	@echo "✅ Tag $(VERSION) pushed"

release: ensure-branch force_ssh_remote commit preflight ensure-clean push merge tag
	@echo "🎉 Release $(VERSION) done!"

test:
	@if [ -d "./build" ] || ls build-* 1>/dev/null 2>&1; then \
		echo "🔬 Running ctest..."; \
		ctest --test-dir ./build || exit $$?; \
	else \
		echo "🔬 No build folder found — running composer test fallback..."; \
		@composer test || true; \
	fi

coverage:
	@XDEBUG_MODE=coverage vendor/bin/phpunit || true

publish-mods:
	php bin/ivi modules:publish-assets || true

publish-mods-force:
	php bin/ivi modules:publish-assets --force || true

changelog:
	@bash scripts/update_changelog.sh || true

all: build

build:
	@echo "⚙️  Configuring with preset '$(PRESET)' and building '$(BUILD_PRESET)'..."
	@$(CMAKE) --preset $(PRESET)
	@$(CMAKE) --build --preset $(BUILD_PRESET)

run:
	@echo "▶ Building and running (preset: $(RUN_PRESET))..."
	@$(CMAKE) --preset $(PRESET)
	@$(CMAKE) --build --preset $(RUN_PRESET) --target run

clean:
	@echo "🧹 Cleaning build artifacts..."
	@rm -rf build-* CMakeFiles CMakeCache.txt || true

rebuild: clean build

preset:
	@:




