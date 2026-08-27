"""Inline every stylesheet and script into one self-contained page.

The Artifact host supplies <!doctype>/<html>/<head>/<body>, so this emits only
the page content: a <title>, the Google Fonts link, one <style>, the markup,
and one <script>.
"""
import re, pathlib, sys

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

out = f"""<title>Erebus Cradle</title>
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

{js}
</script>
"""
dest = root / 'dist' / 'erebus-cradle.html'
dest.parent.mkdir(exist_ok=True)
dest.write_text(out)
print('wrote', dest, f'({len(out) // 1024} KB)')
