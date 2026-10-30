# Free AI App Builder

Describe an app in plain words, watch an AI write it, see it running instantly, and keep
asking for changes. Download the result as a single HTML file and host it anywhere.

It costs nothing: no account, no subscription, no server. It is one HTML file.

## Running it

```
open index.html                  # or double-click it
# or
python3 -m http.server 8000      # then visit http://localhost:8000
```

## Using it in a web browser

Once GitHub Pages is on, the builder lives at
**https://colleenneug.github.io/Claude-01/** and works in any browser, on a computer or a phone.

To turn it on (one time): on GitHub open the repository's **Settings → Pages**, and under
**Build and deployment → Source** choose **GitHub Actions**. Then open the **Actions** tab, pick
**Deploy app builder to GitHub Pages**, and press **Run workflow**. The address appears in
the finished run after about a minute.

If the run says the branch is not allowed to deploy, open **Settings → Environments →
github-pages** and add this branch under **Deployment branches**, then run it again.

It can also be dropped onto any other free static host (Netlify Drop, Cloudflare Pages):
the whole site is the one `index.html` file.

## Free AI providers

Pick one under ⚙ in the top-left corner.

| Provider | Cost | Setup |
|---|---|---|
| **Pollinations** (default) | Free | None — works straight away, rate limited |
| **In your browser (WebLLM)** | Free, private, offline after first download | Recent Chrome/Edge with WebGPU; downloads a 1–5 GB model once |
| **Ollama** | Free, runs on your computer | `ollama pull qwen2.5-coder:7b`, then `OLLAMA_ORIGINS=* ollama serve` |
| **OpenRouter** `:free` models | Free key | [openrouter.ai/keys](https://openrouter.ai/keys) |
| **Google Gemini** free tier | Free key | [aistudio.google.com/apikey](https://aistudio.google.com/apikey) |
| **Groq** free tier | Free key | [console.groq.com/keys](https://console.groq.com/keys) |
| **Custom** | — | Any OpenAI-compatible `/v1/chat/completions` (LM Studio, llama.cpp, vLLM…) |

Keys are stored only in your browser's local storage and sent only to the provider you choose.

## Features

- Chat to build and then refine the app; each answer is a version you can restore or undo
- Live preview with desktop / tablet / phone widths
- Editable code view — changes show up in the preview
- Runtime errors in the app are caught and shown with an **Ask AI to fix** button
- Projects auto-save in the browser; export/import as JSON, or import an existing HTML file
- Download, copy, or open the app in a new tab

Generated apps run in a sandboxed iframe with no access to the builder's storage, so they
cannot read your API keys.
