/**
 * @file WebViewTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/web/WebViewTemplates.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_web_view_base_html(const std::string &projectName)
  {
    (void)projectName;

    return R"HTML(<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>{{ title }} - {{ app_name }}</title>
    <link rel="stylesheet" href="/app.css" />
  </head>
  <body>
    {% include "header.html" %}

    <main class="page">
      {% block content %}{% endblock %}
    </main>

    <script src="/app.js"></script>
  </body>
</html>
)HTML";
  }

  std::string make_web_view_header_html(const std::string &projectName)
  {
    (void)projectName;

    return R"HTML(<header class="site-header">
  <a class="brand" href="/">{{ app_name }}</a>

  <nav class="nav">
    <a href="/">Home</a>
    <a href="/dashboard">Dashboard</a>
    <a href="/health">Health</a>
  </nav>
</header>
)HTML";
  }

  std::string make_web_view_index_html(const std::string &projectName)
  {
    (void)projectName;

    return R"HTML({% extends "base.html" %}

{% block content %}
  <section class="hero">
    <p class="eyebrow">Vix Web</p>
    <h1>{{ title }}</h1>
    <p class="lead">
      Hello {{ user }}. Your Vix web backend is rendering HTML with layouts,
      includes, variables, and static assets.
    </p>

    <div class="actions">
      <a class="button primary" href="/dashboard">Open dashboard</a>
      <a class="button secondary" href="/health">Check health</a>
    </div>
  </section>
{% endblock %}
)HTML";
  }

  std::string make_web_view_dashboard_html(const std::string &projectName)
  {
    (void)projectName;

    return R"HTML({% extends "base.html" %}

{% block content %}
  <section class="card">
    <p class="eyebrow">Dashboard</p>
    <h1>{{ title }}</h1>
    <p class="lead">Welcome back, {{ user }}.</p>

    <div class="stat">
      <span>Total orders</span>
      <strong>{{ total_orders }}</strong>
    </div>

    <h2>Enabled features</h2>

    <ul class="feature-list">
      {% for feature in features %}
        <li>{{ feature }}</li>
      {% endfor %}
    </ul>
  </section>
{% endblock %}
)HTML";
  }

  std::string make_web_public_app_css(const std::string &projectName)
  {
    (void)projectName;

    return R"CSS(:root {
  color-scheme: light dark;
  font-family:
    Inter,
    ui-sans-serif,
    system-ui,
    -apple-system,
    BlinkMacSystemFont,
    "Segoe UI",
    sans-serif;
  background: #0b0e14;
  color: #f7f7f8;
}

* {
  box-sizing: border-box;
}

body {
  min-height: 100vh;
  margin: 0;
  background:
    radial-gradient(circle at top left, rgba(255, 153, 0, 0.18), transparent 30rem),
    #0b0e14;
}

.site-header {
  position: sticky;
  top: 0;
  z-index: 10;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 18px 28px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(18px);
  background: rgba(11, 14, 20, 0.74);
}

.brand {
  color: #ffffff;
  font-size: 1.05rem;
  font-weight: 800;
  text-decoration: none;
}

.nav {
  display: flex;
  align-items: center;
  gap: 14px;
}

.nav a {
  color: rgba(255, 255, 255, 0.72);
  font-size: 0.95rem;
  text-decoration: none;
}

.nav a:hover {
  color: #ff9900;
}

.page {
  width: min(1040px, 100%);
  margin: 0 auto;
  padding: 72px 24px;
}

.hero,
.card {
  padding: clamp(28px, 6vw, 56px);
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 28px;
  background: rgba(255, 255, 255, 0.06);
  box-shadow: 0 24px 90px rgba(0, 0, 0, 0.35);
}

.eyebrow {
  margin: 0 0 14px;
  color: #ff9900;
  font-size: 0.82rem;
  font-weight: 800;
  letter-spacing: 0.12em;
  text-transform: uppercase;
}

h1 {
  max-width: 780px;
  margin: 0;
  font-size: clamp(2.4rem, 8vw, 5.8rem);
  line-height: 0.95;
  letter-spacing: -0.06em;
}

h2 {
  margin: 36px 0 16px;
  font-size: 1.1rem;
}

.lead {
  max-width: 680px;
  margin: 22px 0 0;
  color: rgba(255, 255, 255, 0.74);
  font-size: 1.08rem;
  line-height: 1.75;
}

.actions {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  margin-top: 30px;
}

.button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-height: 44px;
  padding: 0 18px;
  border-radius: 999px;
  font-weight: 800;
  text-decoration: none;
}

.button.primary {
  color: #0b0e14;
  background: #ff9900;
}

.button.secondary {
  color: #ffffff;
  border: 1px solid rgba(255, 255, 255, 0.18);
  background: rgba(255, 255, 255, 0.06);
}

.stat {
  display: inline-flex;
  flex-direction: column;
  gap: 8px;
  margin-top: 28px;
  padding: 18px 22px;
  border-radius: 20px;
  background: rgba(255, 255, 255, 0.08);
}

.stat span {
  color: rgba(255, 255, 255, 0.68);
  font-size: 0.9rem;
}

.stat strong {
  color: #ff9900;
  font-size: 2rem;
}

.feature-list {
  display: grid;
  gap: 10px;
  margin: 0;
  padding: 0;
  list-style: none;
}

.feature-list li {
  padding: 14px 16px;
  border-radius: 16px;
  background: rgba(255, 255, 255, 0.07);
  color: rgba(255, 255, 255, 0.82);
}

@media (max-width: 640px) {
  .site-header {
    align-items: flex-start;
    flex-direction: column;
  }

  .nav {
    flex-wrap: wrap;
  }
}
)CSS";
  }

  std::string make_web_public_app_js(const std::string &projectName)
  {
    std::string s;
    s.reserve(200);

    s += "console.log(\"";
    s += projectName;
    s += " web app is running\");\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
