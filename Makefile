CC=gcc
CFLAGS=-ggdb
LDFLAGS=

bot: bot.o jsmn/libjsmn.a
	#$^ is all deps, $@ is rule name
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	#$< is first dep, so the c file, and $@ is rule name
	$(CC) -c $(CFLAGS) $< -o $@

jsmn/libjsmn.a:
	$(MAKE) -C jsmn

# https://superuser.com/a/181543/270114
watch: bot
	inotifywait -q -m -e create . | while read -r directory events filename; do if [ "$$filename" = "bot.c" ]; then echo "change"; pkill --signal SIGINT bot; $(MAKE) bot && ./bot & fi done

clean:
	rm bot bot.o
	$(MAKE) -C jsmn clean

.PHONY: watch clean
