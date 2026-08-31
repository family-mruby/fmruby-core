# About /home

/home is yours. Everything you write -- programs, slide decks, your service
list, the settings the machine remembers for you -- lives there, and nothing
under it is ever replaced by a firmware update. It starts empty.

Everything the machine ships with lives outside /home, and an update replaces
it wholesale:

- /usr/share/samples/ruby/       small Ruby program samples
- /usr/share/samples/slides/     PicoRabbit slide decks
- /usr/share/samples/services/   a service list and two service bodies to copy
- /usr/share/services/           the bodies of the services the machine runs
- /usr/share/doc/                this file
- /app/                          the installed apps (tests under /app/test)

To play with a sample, copy it into /home first and edit your copy. That way
the original is still there when you want to start again, and your copy
survives the next update.
