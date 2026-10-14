#!/bin/bash
# Usage: po/update-pot.sh   - regenerate po/superstrike-studio.pot from the files in po/POTFILES.
# Translators: copy the .pot to po/<lang>.po (msginit -l <lang>) or merge with: msgmerge -U po/<lang>.po po/superstrike-studio.pot
set -e
cd "$(dirname "$0")/.."
xgettext --language=C++ --from-code=UTF-8 \
    --keyword=_ --keyword=N_ --keyword=C_:1c,2 --keyword=NC_:1c,2 --keyword=ngettext:1,2 \
    --add-comments=TRANSLATORS --package-name="SUPERSTRIKE Studio" \
    --msgid-bugs-address=none -o po/superstrike-studio.pot $(cat po/POTFILES)
echo "po/superstrike-studio.pot: $(grep -c '^msgid ' po/superstrike-studio.pot) msgids (incl. header)"
