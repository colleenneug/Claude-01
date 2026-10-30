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

It can also be put on any free static host (GitHub Pages, Netlify, Cloudflare Pages).

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
