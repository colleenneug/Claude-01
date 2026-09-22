"""Inline every stylesheet and script into one self-contained page.

The Artifact host supplies <!doctype>/<html>/<head>/<body>, so this emits only
the page content: a <title>, the Google Fonts link, one <style>, the markup,
and one <script>.
"""
import re, pathlib, sys, json

# usage: python3 tools/bundle.py [repo-root]   (defaults to this script's parent)
root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent)
html = (root / 'index.html').read_text()

css_files = re.findall(r'<link rel="stylesheet" href="(src/[^"]+)"', html)
js_files  = re.findall(r'<script src="((?:vendor|src)/[^"]+)"></script>', html)

css = '\n\n'.join(f'/* ==== {f} ==== */\n' + (root / f).read_text() for f in css_files)
js  = '\n\n'.join(f'/* ==== {f} ==== */\n' + (root / f).read_text() for f in js_files)

body = html.split('<body class="booting">', 1)[1].split('<script src=', 1)[0]
# a vendored bundle must not contain a literal </script> or it would close the tag early
for f in js_files:
    if '</script' in (root / f).read_text().lower():
        raise SystemExit(f'{f} contains a literal </script>; cannot inline safely')
body = body.rsplit('\n', 1)[0].rstrip()

fonts = re.search(r'<link href="https://fonts\.googleapis[^>]+>', html).group(0)

# Take the title from index.html rather than repeating it here: hardcoding it once
# already let the single-file build keep an old name after the game was renamed.
title_match = re.search(r'<title>(.*?)</title>', html, re.S)
if not title_match:
    raise SystemExit('index.html has no <title>; cannot name the bundle')
title = title_match.group(1).strip()

# Inline anything under assets/ as a data URI. The runtime looks its paths up in
# window.__ASSETS first, so the same code works unbundled and bundled.
import base64, mimetypes
asset_dir = root / 'assets'
assets = {}
if asset_dir.is_dir():
    for f in sorted(asset_dir.iterdir()):
        if not f.is_file():
            continue
        mime = mimetypes.guess_type(f.name)[0] or 'application/octet-stream'
        if f.name.endswith('.webp'):
            mime = 'image/webp'
        b64 = base64.b64encode(f.read_bytes()).decode('ascii')
        assets[f'assets/{f.name}'] = f'data:{mime};base64,{b64}'
asset_js = 'window.__ASSETS = ' + json.dumps(assets) + ';'
print(f'  inlined {len(assets)} asset(s)')

out = f"""<title>{title}</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
{fonts}
<style>
{css}
</style>

{body}

<script>
/* The host owns <body>, so the boot-screen state class is applied here
   rather than in markup. */
document.body.classList.add('booting');

{asset_js}

{js}
</script>
"""
dest = root / 'dist' / 'the-deep-choir.html'
dest.parent.mkdir(exist_ok=True)
dest.write_text(out)
print('wrote', dest, f'({len(out) // 1024} KB)')
