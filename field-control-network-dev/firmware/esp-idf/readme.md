
some commands to remember.

  One important thing: export.sh only affects the terminal where you run it. If you close the terminal and open another one later, you'll need to run:

. ~/esp-idf/export.sh

again before idf.py will be available.

I actually prefer that initially rather than automatically modifying .bashrc, because it keeps the ESP-IDF development environment explicit while we're setting everything up.
