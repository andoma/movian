sqlite3: sqlite3.c shell.c
	gcc -O2 shell.c sqlite3.c -ldl -lpthread -o sqlite3 -DHAVE_READLINE -lreadline

clean:
	rm -f sqlite3

