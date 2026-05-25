/**
 * @file VueTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/VueTemplates.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_readme_vue_app(const std::string &projectName)
  {
    std::string readme;
    readme.reserve(3000);

    readme += "# " + projectName + "\n\n";
    readme += "Vue frontend + Vix C++ backend.\n\n";

    readme += "## Quick start\n\n";
    readme += "Install frontend dependencies:\n\n";
    readme += "```bash\n";
    readme += "cd frontend\n";
    readme += "npm install\n";
    readme += "cd ..\n";
    readme += "```\n\n";

    readme += "Start the Vix backend:\n\n";
    readme += "```bash\n";
    readme += "vix run\n";
    readme += "```\n\n";

    readme += "Start the Vue frontend:\n\n";
    readme += "```bash\n";
    readme += "cd frontend\n";
    readme += "npm run dev\n";
    readme += "```\n\n";

    readme += "Then open the Vue dev server shown by Vite.\n\n";

    readme += "## Project layout\n\n";
    readme += "```txt\n";
    readme += "src/main.cpp        Vix C++ backend\n";
    readme += "frontend/           Vue frontend\n";
    readme += "frontend/src/       Vue source files\n";
    readme += "vix.app             Vix application manifest\n";
    readme += "vix.json            Vix project metadata and tasks\n";
    readme += "```\n\n";

    readme += "## API\n\n";
    readme += "The Vue frontend calls the Vix backend through `/api/*`.\n\n";
    readme += "During development, Vite proxies `/api` to:\n\n";
    readme += "```txt\n";
    readme += "http://localhost:8080\n";
    readme += "```\n\n";

    readme += "## Build\n\n";
    readme += "Build the Vix backend:\n\n";
    readme += "```bash\n";
    readme += "vix build\n";
    readme += "```\n\n";

    readme += "Build the Vue frontend:\n\n";
    readme += "```bash\n";
    readme += "cd frontend\n";
    readme += "npm run build\n";
    readme += "```\n\n";

    return readme;
  }

  std::string make_vix_json_vue_app(const std::string &name)
  {
    std::string s;
    s.reserve(5200);

    s += "{\n";
    s += "  \"name\": \"" + name + "\",\n";
    s += "  \"type\": \"app\",\n";
    s += "  \"template\": \"vue\",\n";
    s += "  \"deps\": [],\n";
    s += "  \"frontend\": {\n";
    s += "    \"framework\": \"vue\",\n";
    s += "    \"dir\": \"frontend\",\n";
    s += "    \"dev\": \"npm run dev\",\n";
    s += "    \"build\": \"npm run build\",\n";
    s += "    \"dist\": \"frontend/dist\"\n";
    s += "  },\n";
    s += "  \"vars\": {\n";
    s += "    \"preset\": \"dev-ninja\",\n";
    s += "    \"release_preset\": \"release\",\n";
    s += "    \"log_level\": \"info\"\n";
    s += "  },\n";
    s += "  \"tasks\": {\n";
    s += "    \"frontend:install\": {\n";
    s += "      \"description\": \"Install Vue dependencies\",\n";
    s += "      \"command\": \"npm install\",\n";
    s += "      \"cwd\": \"frontend\"\n";
    s += "    },\n";
    s += "    \"frontend:dev\": {\n";
    s += "      \"description\": \"Start Vue dev server\",\n";
    s += "      \"command\": \"npm run dev\",\n";
    s += "      \"cwd\": \"frontend\"\n";
    s += "    },\n";
    s += "    \"frontend:build\": {\n";
    s += "      \"description\": \"Build Vue frontend\",\n";
    s += "      \"command\": \"npm run build\",\n";
    s += "      \"cwd\": \"frontend\"\n";
    s += "    },\n";
    s += "    \"backend:dev\": {\n";
    s += "      \"description\": \"Start Vix backend\",\n";
    s += "      \"command\": \"vix run\"\n";
    s += "    },\n";
    s += "    \"backend:build\": {\n";
    s += "      \"description\": \"Build Vix backend\",\n";
    s += "      \"command\": \"vix build --preset ${preset}\"\n";
    s += "    },\n";
    s += "    \"fmt\": \"vix fmt\",\n";
    s += "    \"check\": {\n";
    s += "      \"description\": \"Validate backend project health\",\n";
    s += "      \"command\": \"vix check --preset ${preset} --tests\",\n";
    s += "      \"env\": {\n";
    s += "        \"VIX_LOG_LEVEL\": \"${log_level}\"\n";
    s += "      }\n";
    s += "    },\n";
    s += "    \"test\": {\n";
    s += "      \"description\": \"Run backend tests\",\n";
    s += "      \"command\": \"vix tests --preset ${preset} --fail-fast\"\n";
    s += "    },\n";
    s += "    \"ci\": {\n";
    s += "      \"description\": \"Local CI pipeline\",\n";
    s += "      \"commands\": [\n";
    s += "        \"vix check --preset ${preset} --tests\",\n";
    s += "        \"vix tests --preset ${preset} --fail-fast\",\n";
    s += "        \"cd frontend && npm install\",\n";
    s += "        \"cd frontend && npm run build\"\n";
    s += "      ]\n";
    s += "    }\n";
    s += "}\n";

    return s;
  }

  std::string make_vue_package_json(const std::string &projectName)
  {
    std::string s;
    s.reserve(800);

    s += "{\n";
    s += "  \"name\": \"" + projectName + "-frontend\",\n";
    s += "  \"private\": true,\n";
    s += "  \"version\": \"0.1.0\",\n";
    s += "  \"type\": \"module\",\n";
    s += "  \"scripts\": {\n";
    s += "    \"dev\": \"vite\",\n";
    s += "    \"build\": \"vite build\",\n";
    s += "    \"preview\": \"vite preview\"\n";
    s += "  },\n";
    s += "  \"dependencies\": {\n";
    s += "    \"@vitejs/plugin-vue\": \"latest\",\n";
    s += "    \"vite\": \"latest\",\n";
    s += "    \"vue\": \"latest\"\n";
    s += "  },\n";
    s += "  \"devDependencies\": {}\n";
    s += "}\n";

    return s;
  }

  std::string make_vue_index_html(const std::string &projectName)
  {
    std::string s;
    s.reserve(600);

    s += "<!doctype html>\n";
    s += "<html lang=\"en\">\n";
    s += "  <head>\n";
    s += "    <meta charset=\"UTF-8\" />\n";
    s += "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\n";
    s += "    <title>" + projectName + "</title>\n";
    s += "  </head>\n";
    s += "  <body>\n";
    s += "    <div id=\"app\"></div>\n";
    s += "    <script type=\"module\" src=\"/src/main.js\"></script>\n";
    s += "  </body>\n";
    s += "</html>\n";

    return s;
  }

  std::string make_vue_vite_config()
  {
    return R"JS(import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";

export default defineConfig({
  clearScreen: false,
  plugins: [vue()],
  server: {
    host: "0.0.0.0",
    proxy: {
      "/api": "http://localhost:8080"
    }
  }
});
)JS";
  }

  std::string make_vue_main_js()
  {
    return R"JS(import { createApp } from "vue";
import App from "./App.vue";

createApp(App).mount("#app");
)JS";
  }

  std::string make_vue_app_vue()
  {
    return R"VUE(<script setup>
import { ref } from "vue";

const message = ref("Loading from Vix...");

async function loadMessage() {
  try {
    const response = await fetch("/api/hello");
    const data = await response.json();
    message.value = data.message || "Hello from Vix";
  } catch (error) {
    message.value = "Could not reach the Vix backend";
  }
}

loadMessage();
</script>

<template>
  <main class="page">
    <section class="card">
      <p class="eyebrow">Vue + Vix</p>
      <h1>Frontend powered by Vue</h1>
      <p class="message">{{ message }}</p>
    </section>
  </main>
</template>

<style scoped>
.page {
  min-height: 100vh;
  display: grid;
  place-items: center;
  background: #0f172a;
  color: #f8fafc;
  font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
}

.card {
  width: min(92vw, 560px);
  padding: 48px;
  border-radius: 28px;
  background: rgba(15, 23, 42, 0.9);
  border: 1px solid rgba(148, 163, 184, 0.24);
  box-shadow: 0 24px 80px rgba(0, 0, 0, 0.35);
}

.eyebrow {
  margin: 0 0 12px;
  color: #38bdf8;
  font-weight: 700;
  letter-spacing: 0.12em;
  text-transform: uppercase;
}

h1 {
  margin: 0;
  font-size: clamp(2rem, 6vw, 4rem);
  line-height: 1;
}

.message {
  margin-top: 24px;
  color: #cbd5e1;
  font-size: 1.1rem;
}
</style>
)VUE";
  }

} // namespace vix::commands::new_cmd::templates
